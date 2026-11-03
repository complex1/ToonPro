#include "scene3d_panel.h"
#include "scene3d_controller.h"
#include "rig3d_panel.h"
#include "icons.h"
#include "ui_theme.h"
#include "value_slider.h"
#include "property_row.h"
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace toon {
namespace {
class SceneStatusLabel final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};
// The category strip is pinned; its actual property pages live in the scroll
// area below. Retain the existing QTabWidget navigation and accessibility API.
class SceneCategoryTabs final : public QTabWidget {
public:
    SceneCategoryTabs() {
        setDocumentMode(true); setIconSize({18,18});
        tabBar()->setExpanding(true); tabBar()->setDrawBase(false);
        tabBar()->setStyleSheet("QTabBar::tab { min-width: 24px; min-height: 24px; padding: 1px 2px; }");
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    }
    QSize sizeHint() const override { return tabBar()->sizeHint(); }
    QSize minimumSizeHint() const override { return {0, tabBar()->sizeHint().height()}; }
};
class ScenePropertyLayout final : public QStackedLayout {
public:
    explicit ScenePropertyLayout(QWidget *parent) : QStackedLayout(parent) { setSizeConstraint(QLayout::SetNoConstraint); }
    QSize minimumSize() const override {
        return currentWidget() ? currentWidget()->minimumSizeHint().expandedTo(currentWidget()->minimumSize()) : QSize();
    }
    QSize sizeHint() const override { return currentWidget() ? currentWidget()->sizeHint() : QSize(); }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        if (!currentWidget()) return 0;
        return currentWidget()->hasHeightForWidth() ? currentWidget()->heightForWidth(width) : currentWidget()->sizeHint().height();
    }
};
QToolButton *button(const char *id, const char *icon, const QString &help, bool checkable = false) {
    auto *result = new QToolButton;
    result->setObjectName(id); result->setIcon(appIcon(icon)); result->setIconSize({18, 18});
    result->setFixedSize(28, 28); result->setCheckable(checkable);
    result->setToolTip(help); result->setAccessibleName(help); return result;
}
QTabBar *modes(const char *id, const QList<QPair<QString, QString>> &choices) {
    auto *tabs = new QTabBar;
    tabs->setObjectName(id); tabs->setAccessibleName(id); tabs->setExpanding(true); tabs->setDrawBase(false);
    tabs->setIconSize({18, 18});
    tabs->setStyleSheet("QTabBar::tab { min-width: 24px; min-height: 24px; padding: 1px 2px; }");
    tabs->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    for (const auto &choice : choices) {
        const auto i = tabs->addTab(appIcon(choice.first), {});
        tabs->setTabToolTip(i, choice.second); tabs->setAccessibleTabName(i, choice.second);
    }
    return tabs;
}
ValueSlider *scalar(const char *id, const QString &label, double low, double high, double value, int decimals = 1) {
    auto *result = new ValueSlider(label, low, high, value, decimals);
    result->setObjectName(id); result->setToolTip(label); return result;
}
void showValue(ValueSlider *field, double value) { field->setValueSilently(value); }
}
class ScenePropertyPages final : public QWidget {
public:
    ScenePropertyPages() : m_layout(new ScenePropertyLayout(this)) {
        auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true); setSizePolicy(policy);
        connect(m_layout, &QStackedLayout::currentChanged, this, [this] { updateGeometry(); });
    }
    void addWidget(QWidget *widget) { m_layout->addWidget(widget); }
    void setCurrentIndex(int index) { m_layout->setCurrentIndex(index); }
private:
    ScenePropertyLayout *m_layout;
};
Scene3DPanel::Scene3DPanel(Scene3DController *controller, QWidget *parent)
    : QScrollArea(parent), m_controller(controller) {
    setObjectName("scene3DContent"); setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
    setMinimumSize(240, 240); setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget; auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 4, 4, 4); layout->setSpacing(4);
    m_header = new QWidget(this); m_header->setObjectName("scene3DPinnedHeader");
    auto *headerLayout = new QVBoxLayout(m_header);
    headerLayout->setContentsMargins(4, 4, 4, 4); headerLayout->setSpacing(4);
    auto *header = new QHBoxLayout; header->setSpacing(4);
    m_create = button("create3DScene", "file-plus", "Create a 3D reference scene");
    m_duplicate = button("duplicate3DObject", "copy", "Duplicate the selected model");
    m_remove = button("remove3DObject", "trash", "Delete the selected model");
    m_textured = button("textured3DScene", "image", "Show model textures", true);
    header->addWidget(m_create); header->addWidget(m_duplicate); header->addWidget(m_remove);
    header->addStretch(); header->addWidget(m_textured); headerLayout->addLayout(header);
    m_objects = new QTreeWidget; m_objects->setObjectName("scene3DObjects"); m_objects->setAccessibleName("3D scene objects");
    m_objects->setColumnCount(3); m_objects->setHeaderHidden(true); m_objects->setRootIsDecorated(false);
    m_objects->setUniformRowHeights(true); m_objects->setIconSize({18, 18});
    m_objects->setFixedHeight(68);
    m_objects->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_objects->setTextElideMode(Qt::ElideRight);
    m_objects->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_objects->header()->setStretchLastSection(false);
    m_objects->header()->setSectionResizeMode(0, QHeaderView::Fixed); m_objects->setColumnWidth(0, 30);
    m_objects->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_objects->header()->setSectionResizeMode(2, QHeaderView::Fixed); m_objects->setColumnWidth(2, 30);
    headerLayout->addWidget(m_objects);
    m_status = new SceneStatusLabel; m_status->setObjectName("scene3DStatus"); m_status->setTextFormat(Qt::PlainText);
    // Preview completion must not move a slider under the pointer or change the scroll range.
    // Reserve one line even when idle; status text must not determine the dock's width.
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_status->setFixedHeight(qMax(20, m_status->fontMetrics().height()+2));
    m_status->setStyleSheet(QString("color: %1;").arg(ui::muted)); headerLayout->addWidget(m_status);
    m_tabs = new SceneCategoryTabs; m_tabs->setObjectName("scene3DPropertyTabs");
    headerLayout->addWidget(m_tabs);
    m_modePages = new QStackedWidget; m_modePages->setObjectName("scene3DModeNavigation");
    m_modePages->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    headerLayout->addWidget(m_modePages);
    m_footer = new QWidget(this); m_footer->setObjectName("scene3DPinnedActions");
    auto *footerLayout = new QVBoxLayout(m_footer); footerLayout->setContentsMargins(4,4,4,4); footerLayout->setSpacing(0);
    m_actionPages = new QStackedWidget; footerLayout->addWidget(m_actionPages);
    m_objectPage = new QWidget; auto *objectLayout = new QVBoxLayout(m_objectPage);
    objectLayout->setContentsMargins(0, 0, 0, 0); objectLayout->setSpacing(4);
    m_objectModes = modes("scene3DObjectMode", {{"move", "Move model"}, {"rotate-ccw", "Rotate model"}, {"scan", "Scale model"}});
    m_objectModes->setAccessibleName("Model transform mode");
    m_modePages->addWidget(m_objectModes);
    m_transformControls = new QWidget; auto *axes = new QVBoxLayout(m_transformControls);
    axes->setContentsMargins(0, 0, 0, 0); axes->setSpacing(4);
    const char *axisNames[] = {"X", "Y", "Z"}; const char *axisIds[] = {"scene3DX", "scene3DY", "scene3DZ"};
    for (int i = 0; i < 3; ++i) {
        m_axes[i] = scalar(axisIds[i], QString("Model %1 position in metres").arg(axisNames[i]), -1000000, 1000000, 0, 3);
        m_axes[i]->setSliderRange(-10, 10); m_axes[i]->setSingleStep(.01);
        auto *row = new PropertyRow(axisNames[i], m_axes[i]); row->setLabelWidth(24);
        axes->addWidget(row); wireEdit(m_axes[i], false);
    }
    objectLayout->addWidget(m_transformControls);
    auto *objectActions = new QWidget; auto *objectFooter = new QHBoxLayout(objectActions);
    objectFooter->setContentsMargins(0,0,0,0); objectFooter->setSpacing(4);
    m_reset = button("reset3DObject", "undo-2", "Reset the selected model’s position, rotation and scale");
    m_fitSelected = button("fit3DSelected", "focus", "Frame the selected model with the 3D camera");
    objectFooter->addWidget(m_reset); objectFooter->addWidget(m_fitSelected); objectFooter->addStretch();
    m_actionPages->addWidget(objectActions); objectLayout->addStretch();
    m_cameraPage = new QWidget; auto *cameraLayout = new QVBoxLayout(m_cameraPage);
    cameraLayout->setContentsMargins(0, 0, 0, 0); cameraLayout->setSpacing(4);
    m_cameraModes = modes("scene3DCameraMode", {{"rotate-ccw", "Orbit camera"}, {"hand", "Pan camera"}, {"zoom-in", "Dolly camera"}});
    m_cameraModes->setAccessibleName("Camera movement mode");
    m_modePages->addWidget(m_cameraModes);
    m_projection = modes("scene3DProjection", {{"triangle", "Perspective projection"}, {"rectangle-horizontal", "Orthographic projection"}});
    m_projection->setAccessibleName("Camera projection");
    cameraLayout->addWidget(m_projection);
    m_fov = scalar("scene3DFov", "Vertical camera field of view in degrees", 5, 150, 45);
    m_distance = scalar("scene3DDistance", "Camera distance in metres", .001, 100000, 5, 3); m_distance->setSliderRange(.1, 30); m_distance->setSingleStep(.1);
    m_yaw = scalar("scene3DYaw", "Camera orbit angle in degrees", -1000000, 1000000, 30); m_yaw->setSliderRange(-180, 180);
    m_pitch = scalar("scene3DPitch", "Camera elevation in degrees", -89.9, 89.9, 15);
    m_orthoScale = scalar("scene3DOrthoScale", "Orthographic height in metres", .001, 100000, 3, 3); m_orthoScale->setSliderRange(.1, 30); m_orthoScale->setSingleStep(.1);
    cameraLayout->addWidget(new PropertyRow("Lens", m_fov)); cameraLayout->addWidget(new PropertyRow("Height", m_orthoScale));
    cameraLayout->addWidget(new PropertyRow("Distance", m_distance)); cameraLayout->addWidget(new PropertyRow("Orbit", m_yaw));
    cameraLayout->addWidget(new PropertyRow("Elevation", m_pitch));
    for (auto *field : {m_fov, m_distance, m_yaw, m_pitch, m_orthoScale}) wireEdit(field, true);
    auto *cameraActions = new QWidget; auto *cameraFooter = new QHBoxLayout(cameraActions);
    cameraFooter->setContentsMargins(0,0,0,0); cameraFooter->setSpacing(4);
    m_fit = button("fit3DScene", "scan", "Frame all visible models with the 3D camera");
    m_cameraLock = button("lock3DCamera", "lock-keyhole", "Lock the drawing camera", true);
    cameraFooter->addWidget(m_fit); cameraFooter->addWidget(m_cameraLock); cameraFooter->addStretch();
    m_actionPages->addWidget(cameraActions); cameraLayout->addStretch();
    m_tabs->addTab(new QWidget, appIcon("box"), {}); m_tabs->addTab(new QWidget, appIcon("video"), {});
    m_rigPage = new Rig3DPanel(controller);
    m_modePages->addWidget(m_rigPage->takeNavigation());
    m_actionPages->addWidget(m_rigPage->takeViewActions());
    m_tabs->addTab(new QWidget, appIcon("spline"), {});
    m_tabs->setTabToolTip(0, "Model properties"); m_tabs->setTabToolTip(1, "Camera properties");
    m_tabs->setTabToolTip(2, "Rest skeleton properties");
    auto *tabBar = m_tabs->tabBar(); tabBar->setAccessibleName("3D property categories");
    tabBar->setAccessibleTabName(0, "Models"); tabBar->setAccessibleTabName(1, "Camera");
    tabBar->setAccessibleTabName(2, "Rig");
    m_pages = new ScenePropertyPages; m_pages->setObjectName("scene3DPropertyPages");
    m_pages->addWidget(m_objectPage); m_pages->addWidget(m_cameraPage); m_pages->addWidget(m_rigPage);
    layout->addWidget(m_pages); layout->addStretch(); setWidget(content);
    // Qt's viewport margins do not shorten the scrollbar itself. Reserve the
    // pinned regions in its own layout so its handle follows only the body.
    m_scrollHeaderSpacer = new QWidget; m_scrollFooterSpacer = new QWidget;
    addScrollBarWidget(m_scrollHeaderSpacer, Qt::AlignTop);
    addScrollBarWidget(m_scrollFooterSpacer, Qt::AlignBottom);
    connect(m_rigPage,&Rig3DPanel::editingModeAboutToChange,this,[this](int previous,int){
        if(m_currentPage==2)m_rigScrollPositions[previous]=verticalScrollBar()->value();
    });
    connect(m_rigPage,&Rig3DPanel::editingModeChanged,this,[this](int mode){
        if(m_currentPage==2)restoreRigScroll(mode);
    });
    connect(controller, &Scene3DController::changed, this, &Scene3DPanel::refresh);
    connect(controller, &Scene3DController::editResolutionRequested, this, [this](bool commit) {
        for (auto *field : {m_axes[0],m_axes[1],m_axes[2],m_fov,m_distance,m_yaw,m_pitch,m_orthoScale}) {
            if(commit)field->commitEditing();else field->cancelEditing();
        }
    });
    connect(m_create, &QToolButton::clicked, controller, &Scene3DController::createScene);
    connect(m_duplicate, &QToolButton::clicked, controller, &Scene3DController::duplicateSelected);
    connect(m_remove, &QToolButton::clicked, controller, &Scene3DController::removeSelected);
    connect(m_reset, &QToolButton::clicked, controller, &Scene3DController::resetSelected);
    connect(m_fit, &QToolButton::clicked, controller, &Scene3DController::fitScene);
    connect(m_fitSelected, &QToolButton::clicked, controller, &Scene3DController::fitSelected);
    connect(m_textured, &QToolButton::toggled, this, [this](bool value) { if (!m_refreshing) m_controller->setTextured(value); });
    connect(m_cameraLock, &QToolButton::toggled, this, [this](bool value) {
        if (m_refreshing || !m_controller->scene()) return;
        auto camera = m_controller->scene()->camera; camera.locked = value; m_controller->updateCamera(camera);
    });
    connect(m_objects, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!m_refreshing) m_controller->selectObject(item ? item->data(1, Qt::UserRole).toString() : QString());
    });
    connect(m_objects, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int column) {
        if (m_refreshing || column == 1 || !m_controller->canEdit() || !m_controller->scene()) return;
        const auto id = item->data(1, Qt::UserRole).toString();
        for (auto object : m_controller->scene()->objects) if (object.id == id) {
            if (column == 0) object.visible = !object.visible; else object.locked = !object.locked;
            m_controller->updateObject(object); break;
        }
    });
    connect(m_objects, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int column) {
        if (column == 1 && m_controller->canEdit() && m_controller->selectedObject() && !m_controller->selectedObject()->locked)
            m_objects->editItem(item, 1);
    });
    connect(m_objects, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (m_refreshing || column != 1 || !m_controller->selectedObject()) return;
        auto object = *m_controller->selectedObject(); object.name = item->text(1).trimmed().left(120);
        if (!object.name.isEmpty()) m_controller->updateObject(object); else refresh();
    });
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        showPage(index);
        if (m_refreshing) return;
        if (index == 2) { m_controller->endEdit(); m_controller->selectRigTool(); refresh(); return; }
        const int mode = index == 1 ? m_cameraModes->currentIndex() + 3 : m_objectModes->currentIndex();
        m_controller->endEdit(); m_controller->selectTool(index == 1);
        m_controller->setManipulation(static_cast<Scene3DController::Manipulation>(mode)); refresh();
    });
    connect(m_objectModes, &QTabBar::currentChanged, this, [this](int index) {
        if (m_refreshing) return;
        m_controller->endEdit(); m_controller->selectTool(false);
        m_controller->setManipulation(static_cast<Scene3DController::Manipulation>(index)); refresh();
    });
    connect(m_cameraModes, &QTabBar::currentChanged, this, [this](int index) {
        if (m_refreshing) return;
        m_controller->endEdit(); m_controller->selectTool(true);
        m_controller->setManipulation(static_cast<Scene3DController::Manipulation>(index + 3)); refresh();
    });
    connect(m_projection, &QTabBar::currentChanged, this, [this](int index) {
        if (m_refreshing || !m_controller->scene()) return;
        auto camera = m_controller->scene()->camera; camera.orthographic = index == 1; m_controller->updateCamera(camera);
    });
    refresh();
}
void Scene3DPanel::setCameraMode(bool camera) {
    const QSignalBlocker block(m_tabs); m_tabs->setCurrentIndex(camera ? 1 : 0); showPage(camera ? 1 : 0); refresh();
}
void Scene3DPanel::setRigMode() {
    const QSignalBlocker block(m_tabs); m_tabs->setCurrentIndex(2); showPage(2); refresh();
}
void Scene3DPanel::resizeEvent(QResizeEvent *event) {
    QScrollArea::resizeEvent(event); layoutPinnedControls();
}
bool Scene3DPanel::viewportEvent(QEvent *event) {
    const bool result = QScrollArea::viewportEvent(event);
    if (event->type() == QEvent::Resize) layoutPinnedControls();
    return result;
}
void Scene3DPanel::layoutPinnedControls() {
    if (!m_header || !m_pages || m_layingOutPinned) return;
    const QScopedValueRollback<bool> guard(m_layingOutPinned, true);
    const int rowHeight = qMax(32, m_objects->fontMetrics().height()+10);
    const int visibleRows = qBound(1, m_objects->topLevelItemCount(), height() < 420 ? 1 : 3);
    const int listHeight = visibleRows*rowHeight+4;
    if (m_objects->height() != listHeight) {
        m_objects->setFixedHeight(listHeight);
        if (m_objects->currentItem()) m_objects->scrollToItem(m_objects->currentItem());
    }
    m_tabs->setFixedHeight(m_tabs->tabBar()->sizeHint().height());
    m_modePages->setFixedHeight(qMax(28,m_modePages->currentWidget()->sizeHint().height()));
    const int headerHeight = m_header->sizeHint().height();
    const int footerHeight = 36;
    if (m_scrollHeaderSpacer) m_scrollHeaderSpacer->setFixedHeight(headerHeight);
    if (m_scrollFooterSpacer) m_scrollFooterSpacer->setFixedHeight(footerHeight);
    setViewportMargins(0, headerHeight, 0, footerHeight);
    const QRect body = viewport()->geometry();
    m_header->setGeometry(body.x(), 0, body.width(), headerHeight);
    m_footer->setGeometry(body.x(), height()-footerHeight, body.width(), footerHeight);
    if(m_currentPage==2)m_rigPage->setViewportHeight(body.height());
    m_header->raise(); m_footer->raise();
}
void Scene3DPanel::showPage(int index) {
    if (index == m_currentPage) return;
    m_scrollPositions[m_currentPage] = verticalScrollBar()->value();
    if(m_currentPage==2)m_rigScrollPositions[m_rigPage->editingMode()]=verticalScrollBar()->value();
    m_currentPage = index;
    m_pages->setCurrentIndex(index); m_modePages->setCurrentIndex(index); m_actionPages->setCurrentIndex(index);
    layoutPinnedControls();
    widget()->layout()->invalidate(); widget()->layout()->activate();
    const int position = index==2?m_rigScrollPositions[m_rigPage->editingMode()]:m_scrollPositions[index];
    verticalScrollBar()->setValue(position);
    QTimer::singleShot(0, this, [this, index, position] {
        if (m_currentPage == index) verticalScrollBar()->setValue(position);
    });
}
void Scene3DPanel::restoreRigScroll(int mode) {
    layoutPinnedControls();widget()->layout()->invalidate();widget()->layout()->activate();
    const int position=m_rigScrollPositions[mode];
    verticalScrollBar()->setValue(position);
    QTimer::singleShot(0,this,[this,mode,position]{
        if(m_currentPage==2&&m_rigPage->editingMode()==mode)verticalScrollBar()->setValue(position);
    });
}
void Scene3DPanel::wireEdit(ValueSlider *field, bool camera) {
    connect(field, &ValueSlider::editingStarted, this, [this, camera] {
        if (!m_refreshing) {
            if(camera)m_controller->beginCameraEdit("Adjust 3D camera");
            else m_controller->beginEdit("Transform 3D model");
        }
    });
    connect(field, &ValueSlider::valueChanged, this, [this, field, camera] {
        if (m_refreshing) return;
        if (camera) updateCameraValue(field); else updateObjectValue(field);
    });
    connect(field, &ValueSlider::editingFinished, this, [this] { if (!m_refreshing) m_controller->endEdit(); });
    connect(field, &ValueSlider::editingCancelled, this, [this] { if (!m_refreshing) m_controller->cancelEdit(); });
}
void Scene3DPanel::updateObjectValue(ValueSlider *field) {
    const auto *selected = m_controller->selectedObject();
    if (!selected || selected->locked || !m_controller->canEdit()) return;
    auto object = *selected;
    auto &value = m_objectModes->currentIndex() == 0 ? object.position : m_objectModes->currentIndex() == 1 ? object.rotation : object.scale;
    for (int i = 0; i < 3; ++i) if (m_axes[i] == field) value[i] = field->value();
    m_controller->updateObject(object);
}
void Scene3DPanel::updateCameraValue(ValueSlider *field) {
    if (!m_controller->scene() || !m_controller->canEdit() || m_controller->scene()->camera.locked) return;
    auto camera = m_controller->scene()->camera;
    if (field == m_fov) camera.fieldOfView = field->value();
    else if (field == m_distance) camera.distance = field->value();
    else if (field == m_yaw) camera.yaw = field->value();
    else if (field == m_pitch) camera.pitch = field->value();
    else if (field == m_orthoScale) camera.orthoScale = field->value();
    m_controller->updateCamera(camera);
}
void Scene3DPanel::refresh() {
    if (m_refreshing) return;
    m_refreshing = true;
    const auto *scene = m_controller->scene(); const auto *object = m_controller->selectedObject();
    const bool editable = scene && m_controller->canEdit();
    const bool objectEditable = editable && object && !object->locked;
    m_duplicate->setEnabled(editable && object); m_remove->setEnabled(objectEditable);
    m_reset->setEnabled(objectEditable); m_transformControls->setEnabled(objectEditable);
    m_textured->setEnabled(editable); m_textured->setChecked(scene && scene->textured);
    QStringList currentIds, modelIds;
    for (int i = 0; i < m_objects->topLevelItemCount(); ++i) currentIds << m_objects->topLevelItem(i)->data(1, Qt::UserRole).toString();
    if (scene) for (const auto &entry : scene->objects) modelIds << entry.id;
    if (currentIds != modelIds) {
        m_objects->clear();
        for (const auto &id : modelIds) {
            auto *item = new QTreeWidgetItem(m_objects); item->setData(1, Qt::UserRole, id);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            item->setSizeHint(1, {0, qMax(32, m_objects->fontMetrics().height()+10)});
        }
    }
    if (scene) for (int i = 0; i < scene->objects.size(); ++i) {
        const auto &entry = scene->objects[i]; auto *item = m_objects->topLevelItem(i);
        if (item->text(1) != entry.name) item->setText(1, entry.name);
        const auto visibleIcon = appIcon(entry.visible ? "eye" : "eye-off");
        const auto lockedIcon = appIcon(entry.locked ? "lock-keyhole" : "lock-keyhole-open");
        if (item->icon(0).cacheKey() != visibleIcon.cacheKey()) item->setIcon(0, visibleIcon);
        if (item->icon(2).cacheKey() != lockedIcon.cacheKey()) item->setIcon(2, lockedIcon);
        item->setToolTip(0, entry.visible ? "Hide model" : "Show model");
        item->setToolTip(1, entry.name + " — double-click to rename");
        item->setToolTip(2, entry.locked ? "Unlock model" : "Lock model");
        item->setData(0, Qt::AccessibleTextRole, entry.visible ? "Visible" : "Hidden");
        item->setData(1, Qt::AccessibleTextRole, entry.name);
        item->setData(2, Qt::AccessibleTextRole, entry.locked ? "Locked" : "Unlocked");
        if (entry.id == m_controller->selectedObjectId() && m_objects->currentItem() != item) m_objects->setCurrentItem(item);
    }
    if (!object) { m_objects->clearSelection(); m_objects->setCurrentItem(nullptr); }
    const int manipulation = int(m_controller->manipulation());
    if (manipulation < 3) m_objectModes->setCurrentIndex(manipulation);
    else m_cameraModes->setCurrentIndex(manipulation - 3);
    const int mode = m_objectModes->currentIndex();
    const QVector3D values = object ? (mode == 0 ? object->position : mode == 1 ? object->rotation : object->scale) : QVector3D();
    for (int i = 0; i < 3; ++i) {
        if (m_axes[i]->property("transformMode").toInt() != mode || !m_axes[i]->property("transformMode").isValid()) {
            m_axes[i]->setProperty("transformMode", mode);
            m_axes[i]->setRange(mode == 2 ? -10000 : -1000000, mode == 2 ? 10000 : 1000000);
            m_axes[i]->setSliderRange(mode == 0 ? -10 : mode == 1 ? -180 : .01, mode == 0 ? 10 : mode == 1 ? 180 : 5);
            m_axes[i]->setDecimals(mode == 1 ? 1 : 3); m_axes[i]->setSingleStep(mode == 1 ? 1 : .01);
            const auto help = QString("Model %1 %2%3").arg(QString("XYZ")[i]).arg(mode == 0 ? "position" : mode == 1 ? "rotation" : "scale")
                .arg(mode == 0 ? " in metres" : mode == 1 ? " in degrees" : "");
            m_axes[i]->setToolTip(help); m_axes[i]->setAccessibleName(help);
            m_axes[i]->findChild<QWidget *>("valueSliderNumber")->setAccessibleName(help);
            m_axes[i]->findChild<QWidget *>("valueSliderTrack")->setAccessibleName(help + " slider");
        }
        showValue(m_axes[i], values[i]);
    }
    const Camera3D camera = scene ? scene->camera : Camera3D();
    const bool cameraEditable = editable && !camera.locked;
    for (auto *field : {m_fov, m_distance, m_yaw, m_pitch, m_orthoScale}) field->setEnabled(cameraEditable);
    m_projection->setEnabled(cameraEditable); m_cameraModes->setEnabled(cameraEditable);
    m_cameraLock->setEnabled(editable); m_cameraLock->setChecked(camera.locked);
    m_fit->setEnabled(cameraEditable); m_fitSelected->setEnabled(cameraEditable && object);
    m_projection->setCurrentIndex(camera.orthographic ? 1 : 0);
    m_fov->setEnabled(cameraEditable && !camera.orthographic); m_orthoScale->setEnabled(cameraEditable && camera.orthographic);
    showValue(m_fov, camera.fieldOfView); showValue(m_distance, camera.distance); showValue(m_yaw, camera.yaw);
    showValue(m_pitch, camera.pitch); showValue(m_orthoScale, camera.orthoScale);
    m_status->setText(!scene ? "Add a model from 3D Library." : !editable ? "This 3D layer is locked."
        : m_controller->rendering() ? "Updating 3D reference…" : object && object->locked ? "Model locked." : QString());
    m_status->setToolTip(m_status->text());
    m_status->setAccessibleName(m_status->text());
    layoutPinnedControls();
    m_refreshing = false;
}
} // namespace toon
