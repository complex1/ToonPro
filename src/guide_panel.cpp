#include "guide_panel.h"
#include "canvas.h"
#include "icons.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"
#include "property_row.h"
#include <QColorDialog>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QEvent>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace toon {
namespace {
constexpr GuideType types[] = {GuideType::Perspective, GuideType::VanishingPoint, GuideType::TwoPoint,
                               GuideType::ParallelRuler, GuideType::ThreePoint, GuideType::Fisheye, GuideType::Ellipse};
constexpr int typeCount = sizeof(types) / sizeof(types[0]);
constexpr FeatureIcon typeIcons[] = {FeatureIcon::PerspectiveGrid, FeatureIcon::VanishingPoint, FeatureIcon::TwoPointPerspective, FeatureIcon::ParallelGuide, FeatureIcon::ThreePointPerspective, FeatureIcon::Fisheye};
QString typeLabel(GuideType type) {
    if (type == GuideType::TwoPoint) return "Two-point";
    if (type == GuideType::ThreePoint) return "Three-point";
    return guideTypeName(type);
}
QString placementHelp(GuideType type) {
    switch (type) {
    case GuideType::Perspective: return "Place 4 corners around the grid.";
    case GuideType::VanishingPoint: return "Place the vanishing point.";
    case GuideType::TwoPoint: return "Place 3 handles: two vanishing points, then the grid anchor.";
    case GuideType::ParallelRuler: return "Place 2 endpoints to set the ruler direction.";
    case GuideType::ThreePoint: return "Place 4 handles: three vanishing points, then the grid anchor.";
    case GuideType::Fisheye: return "Place 3 handles: two axis endpoints, then the curve height.";
    case GuideType::Ellipse: return "Place 3 handles: two axis endpoints, then the ellipse height.";
    }
    return {};
}
QString placementToolTip(GuideType type) {
    auto help = placementHelp(type);
    if (type == GuideType::ThreePoint)
        help += " Keep the three vanishing points off one straight line and the anchor away from their connecting lines.";
    if (type == GuideType::Fisheye || type == GuideType::Ellipse)
        help += type == GuideType::Ellipse
            ? " Start drawing within 12 screen pixels of the ellipse to follow its curve. Hold Shift at the height handle for a circle. Drag the endpoints to resize or rotate, or the center to move."
            : " The third handle sets the perpendicular height of the ellipse. Hold Shift while placing or dragging it for a circular grid. Drag the endpoints to resize or rotate the grid.";
    return help;
}
class GuideTypeTabs final : public QTabBar {
protected:
    QSize tabSizeHint(int) const override { return {ui::metrics::actionTarget, ui::metrics::tabHeight}; }
    QSize minimumTabSizeHint(int index) const override { return tabSizeHint(index); }
};
class GuideCaption final : public QLabel {
public:
    GuideCaption() { setTextFormat(Qt::PlainText); setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed); updateHeight(); }
protected:
    void changeEvent(QEvent *event) override {
        QLabel::changeEvent(event);
        if(event->type()==QEvent::FontChange)updateHeight();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(),Qt::AlignLeading|Qt::AlignVCenter,fontMetrics().elidedText(text(),Qt::ElideRight,width()));
    }
private:
    void updateHeight(){setFixedHeight(qMax(ui::metrics::actionTarget,fontMetrics().height()+2));}
};
class GuideList final : public QTreeWidget {
public:
    GuideList(){updateRows();}
protected:
    void resizeEvent(QResizeEvent *event) override {
        QTreeWidget::resizeEvent(event);
        // A dock can shrink after selection has already scrolled into view.
        // Follow the current guide only for that geometry change; a normal
        // refresh must leave an artist's deliberate list scrolling alone.
        if (event->oldSize().height() > event->size().height() && currentItem())
            scrollToItem(currentItem(), QAbstractItemView::EnsureVisible);
    }
    void changeEvent(QEvent *event) override {
        QTreeWidget::changeEvent(event);
        if(event->type()==QEvent::FontChange)updateRows();
    }
private:
    void updateRows(){
        const int row=qMax(ui::metrics::actionTarget,fontMetrics().height()+ui::metrics::sectionGap);
        setMinimumHeight(row+2*frameWidth());setMaximumHeight(row*5+2*frameWidth());
        for(int i=0;i<topLevelItemCount();++i)topLevelItem(i)->setSizeHint(0,{0,row});
    }
};
// Scroll each property page independently; hidden pages must not increase the
// dock minimum or push creation, list actions and category tabs off screen.
class GuidePropertyTabs final : public QTabWidget {
public:
    QSize minimumSizeHint() const override {return {0,tabBar()->sizeHint().height()+32};}
    QSize sizeHint() const override {return {240,210};}
};
QScrollArea *propertyScroll(QWidget *page,const char *name){
    auto *scroll=new QScrollArea;scroll->setObjectName(name);scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMinimumSize(0,0);scroll->setWidget(page);return scroll;
}
QToolButton *iconButton(const char *id, const char *icon, const QString &help, bool checkable = false) {
    auto *button = new QToolButton;
    button->setProperty("compactDrawingAction", true);
    button->setObjectName(id); button->setIcon(appIcon(icon)); button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget); button->setCheckable(checkable);
    button->setToolTip(help); button->setAccessibleName(help);
    return button;
}
void colorChip(QToolButton *button, QColor color) {
    if (button->property("guideChipColor").value<QColor>() == color) return;
    button->setProperty("guideChipColor", color);
    QPixmap pixmap(18, 18); pixmap.fill(color); button->setIcon(QIcon(pixmap));
}
QLineEdit *hexField(const char *id, const QString &help) {
    auto *field = new QLineEdit;
    field->setProperty("compactDrawingField",true); field->setObjectName(id); field->setMaxLength(7); field->setMaximumWidth(105);
    field->setToolTip(help); field->setAccessibleName(help); field->setPlaceholderText("#RRGGBB");
    return field;
}
ValueSlider *number(const char *id, const QString &help, double low, double high) {
    auto *field = new ValueSlider(help, low, high, low, 1);
    field->setObjectName(id); field->setToolTip(help); return field;
}
}

GuidePanel::GuidePanel(Canvas *canvas, QWidget *parent) : QWidget(parent), m_canvas(canvas), m_content(new QWidget(this)) {
    setObjectName("guideContent"); setProperty("compactDrawingPanel", true);
    setMinimumSize(220, 180);
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    m_content->setObjectName("guidePanelBody");outer->addWidget(m_content);
    auto *layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin, ui::metrics::panelMargin); layout->setSpacing(ui::metrics::controlGap);
    m_types = new GuideTypeTabs;
    m_types->setObjectName("guideTypes"); m_types->setAccessibleName("New guide type");
    m_types->setExpanding(true); m_types->setDrawBase(false); m_types->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
    // Seven direct targets fit the shared 220-pixel minimum without overflow.
    m_types->setUsesScrollButtons(false);
    m_types->setProperty("compactDrawingTabs", true);
    for (int i = 0; i < typeCount; ++i) {
        m_types->addTab(types[i] == GuideType::Ellipse ? appIcon("circle") : featureIcon(typeIcons[i]), {});
        m_types->setTabToolTip(i, guideTypeName(types[i]) + " — " + placementToolTip(types[i]));
        m_types->setAccessibleTabName(i, guideTypeName(types[i]));
    }
    layout->addWidget(m_types);
    auto *creation = new QHBoxLayout; creation->setSpacing(ui::metrics::controlGap);
    m_typeName = new GuideCaption; m_typeName->setObjectName("guideTypeName");
    
    creation->addWidget(m_typeName, 1);
    m_placement = new GuideCaption; m_placement->setObjectName("guidePlacementStatus");
    m_placement->setStyleSheet(QString("color: %1;").arg(ui::muted));creation->addWidget(m_placement,1);
    m_add = iconButton("addGuide", "plus", "Add a guide by placing its handles on the canvas");
    m_cancel = iconButton("cancelGuideCreation", "x", "Cancel guide placement (Esc)");
    creation->addWidget(m_add); creation->addWidget(m_cancel); layout->addLayout(creation);
    m_list = new GuideList;
    m_list->setObjectName("guideList"); m_list->setAccessibleName("Drawing guides");
    m_list->setColumnCount(4); m_list->setHeaderHidden(true); m_list->setRootIsDecorated(false);
    m_list->setUniformRowHeights(true); m_list->setIconSize({16, 16});
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setMinimumSectionSize(ui::metrics::actionTarget);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 4; ++i) {
        m_list->header()->setSectionResizeMode(i, QHeaderView::Fixed); m_list->setColumnWidth(i, ui::metrics::actionTarget);
    }
    layout->addWidget(m_list,1);
    auto *actions = new QHBoxLayout; actions->setSpacing(ui::metrics::controlGap);
    m_duplicate = iconButton("duplicateGuide", "square-plus", "Duplicate the selected guide");
    m_remove = iconButton("removeGuide", "trash", "Delete the selected guide (Delete)");
    m_load = iconButton("loadGuideSet", "folder-open", "Add guides from a saved guide set…");
    m_save = iconButton("saveGuideSet", "save", "Save all guides as a reusable guide set…");
    m_detect = iconButton("detectPerspective", "sparkles", "Detect editable perspective guides from a layer or image…");
    actions->addWidget(m_duplicate); actions->addWidget(m_remove); actions->addStretch();
    actions->addWidget(m_detect); actions->addWidget(m_load); actions->addWidget(m_save); layout->addLayout(actions);

    m_properties = new GuidePropertyTabs; m_properties->setObjectName("guidePropertyTabs");
    m_properties->setDocumentMode(true); m_properties->setIconSize({ui::metrics::tabIcon, ui::metrics::tabIcon});
    m_properties->tabBar()->setProperty("compactDrawingTabs", true);
    m_selected = new QWidget;
    auto *selectedLayout = new QVBoxLayout(m_selected); selectedLayout->setContentsMargins(0, 4, 0, 0); selectedLayout->setSpacing(ui::metrics::controlGap);
    m_name = new QLineEdit; m_name->setObjectName("guideName"); m_name->setMaxLength(100);
    m_name->setProperty("compactDrawingField",true); m_name->setAccessibleName("Selected guide name"); m_name->setPlaceholderText("Guide name"); m_name->setMinimumWidth(0);
    auto *nameRow = new QHBoxLayout; nameRow->setSpacing(ui::metrics::controlGap); nameRow->addWidget(m_name, 1);
    m_density = new ValueSlider("Guide density", 1, 64, 8);
    m_density->setObjectName("guideDensity"); m_density->setToolTip("Number of divisions in this guide");
    m_vertical = iconButton("guideVertical", "split", "Enable the vertical ruler for this two-point guide", true);
    m_limited = iconButton("guideLimited", "square-dashed", "Limit drawing assistance to a rectangular area", true);
    nameRow->addWidget(m_vertical); nameRow->addWidget(m_limited); selectedLayout->addLayout(nameRow);
    auto *densityRow = new PropertyRow("Density", m_density); densityRow->setObjectName("guideDensityRow"); densityRow->setLabelWidth(ui::metrics::labelWidth);
    selectedLayout->addWidget(densityRow);
    m_areaControls = new QWidget;
    auto *areaLayout = new QVBoxLayout(m_areaControls); areaLayout->setContentsMargins(0, 0, 0, 0); areaLayout->setSpacing(ui::metrics::controlGap);
    const char *areaIds[] = {"guideAreaX", "guideAreaY", "guideAreaWidth", "guideAreaHeight"};
    const char *areaLabels[] = {"X", "Y", "Width", "Height"};
    const char *areaHelp[] = {"Guide area left in artwork pixels", "Guide area top in artwork pixels", "Guide area width in pixels", "Guide area height in pixels"};
    for (int i = 0; i < 4; ++i) {
        m_area[i] = number(areaIds[i], areaHelp[i], i < 2 ? -1000000 : .1, 1000000);
        auto *row = new PropertyRow(areaLabels[i], m_area[i]);
        row->setObjectName(QString::fromLatin1(areaIds[i]) + "Row"); row->setLabelWidth(ui::metrics::labelWidth);
        areaLayout->addWidget(row);
        connect(m_area[i], &ValueSlider::editingFinished, this, [this] {
            editSelected([this](DrawingGuide &guide) {
                guide.area = {m_area[0]->value(), m_area[1]->value(), m_area[2]->value(), m_area[3]->value()};
            });
        });
    }
    selectedLayout->addWidget(m_areaControls);
    auto *colorLine = new QHBoxLayout; colorLine->setSpacing(ui::metrics::controlGap);
    m_customColor = iconButton("guideCustomColor", "palette", "Use a custom color and opacity for this guide", true);
    colorLine->addWidget(m_customColor);
    m_colorControls = new QWidget;
    auto *custom = new QHBoxLayout(m_colorControls); custom->setContentsMargins(0, 0, 0, 0); custom->setSpacing(ui::metrics::controlGap);
    m_color = iconButton("guideColor", "palette", "Choose this guide’s custom color…");
    m_colorHex = hexField("guideColorHex", "Custom guide color in hexadecimal");
    custom->addWidget(m_color); custom->addWidget(m_colorHex, 1); custom->addStretch();
    colorLine->addWidget(m_colorControls, 1); selectedLayout->addLayout(colorLine);
    m_opacity = new ValueSlider("Custom guide opacity", 0, 100, 60, 0, " %"); m_opacity->setObjectName("guideOpacity");
    auto *opacityRow = new PropertyRow("Opacity", m_opacity); opacityRow->setObjectName("guideOpacityRow"); opacityRow->setLabelWidth(ui::metrics::labelWidth);
    selectedLayout->addWidget(opacityRow); selectedLayout->addStretch();

    auto *global = new QWidget;
    auto *globalLayout = new QVBoxLayout(global); globalLayout->setContentsMargins(0, 4, 0, 0); globalLayout->setSpacing(ui::metrics::controlGap);
    auto *globalFlags = new QHBoxLayout; globalFlags->setSpacing(ui::metrics::controlGap);
    m_visible = iconButton("guidesVisible", "eye", "Show all drawing guides", true);
    m_snapping = iconButton("guidesSnapping", "link-2", "Snap brush and eraser strokes to enabled guides", true);
    m_handles = iconButton("guidesHandles", "move", "Show guide handles while editing guides", true);
    globalFlags->addWidget(m_visible); globalFlags->addWidget(m_snapping); globalFlags->addWidget(m_handles); globalFlags->addStretch();
    globalLayout->addLayout(globalFlags);
    auto *globalColorLine = new QHBoxLayout; globalColorLine->setSpacing(ui::metrics::controlGap);
    globalColorLine->addWidget(new QLabel("Color"));
    m_globalColor = iconButton("guidesColor", "palette", "Choose the default guide color…");
    m_globalHex = hexField("guidesColorHex", "Default guide color in hexadecimal");
    globalColorLine->addWidget(m_globalColor); globalColorLine->addWidget(m_globalHex, 1); globalColorLine->addStretch();
    globalLayout->addLayout(globalColorLine);
    m_globalOpacity = new ValueSlider("Default guide opacity", 0, 100, 60, 0, " %"); m_globalOpacity->setObjectName("guidesOpacity");
    auto *globalOpacityRow = new PropertyRow("Opacity", m_globalOpacity); globalOpacityRow->setObjectName("guidesOpacityRow"); globalOpacityRow->setLabelWidth(ui::metrics::labelWidth);
    globalLayout->addWidget(globalOpacityRow); globalLayout->addStretch();
    m_properties->addTab(propertyScroll(m_selected,"guideSelectedScroll"), appIcon("pen-tool"), {});
    m_properties->addTab(propertyScroll(global,"guideGlobalScroll"), appIcon("sliders-horizontal"), {});
    m_properties->setTabToolTip(0, "Selected guide"); m_properties->setTabToolTip(1, "All guides");
    auto *propertyTabs = m_properties->findChild<QTabBar *>();
    propertyTabs->setAccessibleName("Guide property categories"); propertyTabs->setAccessibleTabName(0, "Selected guide"); propertyTabs->setAccessibleTabName(1, "All guides");
    layout->addWidget(m_properties,3);

    connect(m_canvas, &Canvas::guidesChanged, this, &GuidePanel::refresh);
    connect(m_canvas, &Canvas::guideSelectionChanged, this, &GuidePanel::refresh);
    connect(m_canvas, &Canvas::guideCreationChanged, this, &GuidePanel::refresh);
    connect(m_canvas, &Canvas::toolStateApplied, this, &GuidePanel::refresh);
    connect(m_canvas, &Canvas::toolAvailabilityChanged, this, &GuidePanel::refresh);
    // Strokes emit documentChanged for each dab. Guide changes have their own
    // signal; ordinary artwork updates only need to gate editing controls.
    connect(m_canvas, &Canvas::documentChanged, this, &GuidePanel::refreshEnabled);
    connect(m_canvas->document(), &Document::transformChanged, this, &GuidePanel::refreshEnabled);
    connect(m_types, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_refreshing && index >= 0 && index < typeCount) m_canvas->setGuideCreationType(types[index]);
        refresh();
    });
    connect(m_add, &QToolButton::clicked, this, [this] {
        m_canvas->beginGuideCreation(m_canvas->guideCreationType()); m_canvas->setFocus();
    });
    connect(m_cancel, &QToolButton::clicked, m_canvas, &Canvas::cancelGuideCreation);
    connect(m_duplicate, &QToolButton::clicked, m_canvas, &Canvas::duplicateSelectedGuide);
    connect(m_remove, &QToolButton::clicked, m_canvas, &Canvas::removeSelectedGuide);
    connect(m_load, &QToolButton::clicked, this, &GuidePanel::loadSet);
    connect(m_save, &QToolButton::clicked, this, &GuidePanel::saveSet);
    connect(m_detect, &QToolButton::clicked, this, &GuidePanel::detectPerspectiveRequested);
    connect(m_list, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!m_refreshing && item) m_canvas->selectGuide(item->data(0, Qt::UserRole).toString());
    });
    connect(m_list, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int column) {
        if (m_refreshing || !item || column == 0) return;
        const auto id = item->data(0, Qt::UserRole).toString();
        for (auto guide : m_canvas->drawingGuides()) if (guide.id == id) {
            if (column == 1) guide.visible = !guide.visible;
            if (column == 2) guide.locked = !guide.locked;
            if (column == 3) guide.snapping = !guide.snapping;
            m_canvas->updateGuide(guide); break;
        }
    });
    connect(m_name, &QLineEdit::editingFinished, this, [this] { editSelected([this](DrawingGuide &g) { g.name = m_name->text().trimmed(); }); });
    connect(m_density, &ValueSlider::editingFinished, this, [this] { editSelected([this](DrawingGuide &g) { g.subdivisions = qRound(m_density->value()); }); });
    connect(m_vertical, &QToolButton::toggled, this, [this](bool value) { editSelected([value](DrawingGuide &g) { g.vertical = value; }); });
    connect(m_limited, &QToolButton::toggled, this, [this](bool value) { editSelected([value](DrawingGuide &g) { g.limited = value; }); });
    connect(m_customColor, &QToolButton::toggled, this, [this](bool value) { editSelected([value](DrawingGuide &g) { g.customColor = value; }); });
    connect(m_opacity, &ValueSlider::editingFinished, this, [this] { editSelected([this](DrawingGuide &g) { g.opacity = m_opacity->value() / 100.; }); });
    connect(m_colorHex, &QLineEdit::editingFinished, this, [this] {
        const QColor color(m_colorHex->text());
        if (color.isValid()) editSelected([color](DrawingGuide &g) { g.color = color; }); else refresh();
    });
    connect(m_color, &QToolButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(QColor(m_colorHex->text()), this, "Guide color");
        if (color.isValid()) editSelected([color](DrawingGuide &g) { g.color = color; });
    });
    connect(m_visible, &QToolButton::toggled, this, [this](bool value) { editSettings([value](GuideSettings &s) { s.visible = value; }); });
    connect(m_snapping, &QToolButton::toggled, this, [this](bool value) { editSettings([value](GuideSettings &s) { s.snapping = value; }); });
    connect(m_handles, &QToolButton::toggled, this, [this](bool value) { editSettings([value](GuideSettings &s) { s.handles = value; }); });
    connect(m_globalOpacity, &ValueSlider::editingFinished, this, [this] { editSettings([this](GuideSettings &s) { s.opacity = m_globalOpacity->value() / 100.; }); });
    connect(m_globalHex, &QLineEdit::editingFinished, this, [this] {
        const QColor color(m_globalHex->text());
        if (color.isValid()) editSettings([color](GuideSettings &s) { s.color = color; }); else refresh();
    });
    connect(m_globalColor, &QToolButton::clicked, this, [this] {
        const QColor color = QColorDialog::getColor(QColor(m_globalHex->text()), this, "Default guide color");
        if (color.isValid()) editSettings([color](GuideSettings &s) { s.color = color; });
    });
    refresh();
}

void GuidePanel::editSelected(const std::function<void(DrawingGuide &)> &edit) {
    if (m_refreshing || !m_canvas->hasProject()) return;
    for (auto guide : m_canvas->drawingGuides()) if (guide.id == m_canvas->selectedGuideId()) {
        edit(guide); m_canvas->updateGuide(guide); break;
    }
    refresh();
}
void GuidePanel::editSettings(const std::function<void(GuideSettings &)> &edit) {
    if (m_refreshing || !m_canvas->hasProject()) return;
    auto settings = m_canvas->guideSettings(); edit(settings); m_canvas->setGuideSettings(settings); refresh();
}
void GuidePanel::refreshEnabled() {
    const bool enabled = m_canvas->hasProject() && m_canvas->toolAvailable() && !m_canvas->document()->editingGestureActive();
    if (m_content->isEnabled() != enabled) m_content->setEnabled(enabled);
}
void GuidePanel::refresh() {
    if (m_refreshing) return;
    m_refreshing = true;
    const auto guides = m_canvas->drawingGuides(); const auto settings = m_canvas->guideSettings();
    const bool creating = m_canvas->guideCreating();
    refreshEnabled();
    int typeIndex = 0; for (int i = 0; i < typeCount; ++i) if (types[i] == m_canvas->guideCreationType()) typeIndex = i;
    const auto type = types[typeIndex];
    m_types->setCurrentIndex(typeIndex); m_types->setEnabled(!creating);
    m_types->setVisible(!m_canvas->property("toolCatalogManaged").toBool());
    m_typeName->setText(typeLabel(type)); m_typeName->setToolTip(guideTypeName(type));m_typeName->setAccessibleName(guideTypeName(type));
    m_typeName->setVisible(!creating);
    m_add->setToolTip("Add " + guideTypeName(type).toLower() + ". " + placementToolTip(type));
    m_add->setAccessibleName("Add " + guideTypeName(type).toLower());
    m_add->setEnabled(!creating && guides.size() < maxDrawingGuides); m_cancel->setVisible(creating);
    m_placement->setVisible(creating);
    m_placement->setText(placementHelp(type)); m_placement->setToolTip(placementToolTip(type));m_placement->setAccessibleName(placementToolTip(type));
    m_list->setEnabled(!creating); m_load->setEnabled(!creating); m_save->setEnabled(!creating && !guides.isEmpty());
    const QSignalBlocker blockList(m_list);
    bool rebuild = guides.size() != m_list->topLevelItemCount();
    for (int i = 0; !rebuild && i < guides.size(); ++i)
        rebuild = m_list->topLevelItem(i)->data(0, Qt::UserRole).toString() != guides[i].id;
    if (rebuild) m_list->clear();
    const DrawingGuide *selected = nullptr;
    for (int i = 0; i < guides.size(); ++i) {
        const auto &guide = guides[i];
        auto *item = rebuild ? new QTreeWidgetItem(m_list) : m_list->topLevelItem(i);
        item->setData(0, Qt::UserRole, guide.id); item->setText(0, guide.name);
        item->setToolTip(0, guide.name + " · " + guideTypeName(guide.type));
        item->setData(0, Qt::AccessibleTextRole, item->toolTip(0));
        item->setIcon(1, appIcon(guide.visible ? "eye" : "eye-off"));
        item->setIcon(2, appIcon(guide.locked ? "lock-keyhole" : "lock-keyhole-open"));
        item->setIcon(3, appIcon(guide.snapping ? "link-2" : "x"));
        item->setToolTip(1, guide.visible ? "Hide guide" : "Show guide");
        item->setToolTip(2, guide.locked ? "Unlock guide handles" : "Lock guide handles");
        item->setToolTip(3, guide.snapping ? "Disable snapping to this guide" : "Enable snapping to this guide");
        for (int col = 1; col < 4; ++col) item->setData(col, Qt::AccessibleTextRole, item->toolTip(col));
        item->setSizeHint(0, {0, qMax(ui::metrics::actionTarget,m_list->fontMetrics().height()+ui::metrics::sectionGap)});
        if (guide.id == m_canvas->selectedGuideId()) {
            selected = &guide;
            if (m_list->currentItem() != item) m_list->setCurrentItem(item);
        }
    }
    if (!selected) m_list->setCurrentItem(nullptr);
    m_duplicate->setEnabled(selected && !creating && guides.size() < maxDrawingGuides);
    m_remove->setEnabled(selected && !creating);
    m_selected->setEnabled(selected && !creating);
    m_properties->setEnabled(!creating);
    if (selected) {
        m_name->setText(selected->name); m_density->setValueSilently(selected->subdivisions);
        m_density->parentWidget()->setVisible(selected->type != GuideType::Ellipse);
        m_vertical->setVisible(selected->type == GuideType::TwoPoint); m_vertical->setChecked(selected->vertical);
        m_limited->setChecked(selected->limited); m_areaControls->setVisible(selected->limited);
        const double values[] = {selected->area.x(), selected->area.y(), selected->area.width(), selected->area.height()};
        const double extents[] = {double(m_canvas->project().width), double(m_canvas->project().height)};
        for (int i = 0; i < 4; ++i) {
            // Keep the track useful at this shot's scale while exact entry still
            // supports guide bounds far beyond the artwork frame.
            const double extent = extents[i % 2];
            m_area[i]->setSliderRange(i < 2 ? qMin(-extent, values[i]) : .1,
                                     qMax(i < 2 ? extent : extent * 2, values[i]));
            m_area[i]->setValueSilently(values[i]);
        }
        m_customColor->setChecked(selected->customColor); m_colorControls->setVisible(selected->customColor);
        m_opacity->parentWidget()->setVisible(selected->customColor); m_colorHex->setText(selected->color.name().toUpper());
        colorChip(m_color, selected->color); m_opacity->setValueSilently(selected->opacity * 100);
    } else {
        m_name->clear(); m_vertical->hide(); m_areaControls->hide();
        m_density->parentWidget()->show();
        m_colorControls->hide();m_opacity->parentWidget()->hide();
    }
    m_visible->setChecked(settings.visible); m_snapping->setChecked(settings.snapping); m_handles->setChecked(settings.handles);
    m_visible->setIcon(appIcon(settings.visible ? "eye" : "eye-off"));
    m_globalHex->setText(settings.color.name().toUpper()); colorChip(m_globalColor, settings.color);
    m_globalOpacity->setValueSilently(settings.opacity * 100);
    m_refreshing = false;
}
void GuidePanel::loadSet() {
    const auto path = QFileDialog::getOpenFileName(this, "Add drawing guides", {}, "Toon Pro guide set (*.toonguides)");
    if (path.isEmpty()) return;
    QVector<DrawingGuide> guides; GuideSettings settings; QString error;
    if (!loadGuideSet(path, &guides, &settings, &error)) { QMessageBox::warning(this, "Cannot load guides", error); return; }
    if (m_canvas->drawingGuides().size() + guides.size() > maxDrawingGuides) {
        QMessageBox::warning(this, "Cannot load guides", QString("A shot supports up to %1 guides.").arg(maxDrawingGuides)); return;
    }
    m_canvas->replaceGuideSet(guides, settings, true);
}
void GuidePanel::saveSet() {
    auto path = QFileDialog::getSaveFileName(this, "Save drawing guides", {}, "Toon Pro guide set (*.toonguides)");
    if (path.isEmpty()) return;
    if (!path.endsWith(".toonguides", Qt::CaseInsensitive)) path += ".toonguides";
    QString error;
    if (!saveGuideSet(m_canvas->drawingGuides(), m_canvas->guideSettings(), path, &error))
        QMessageBox::warning(this, "Cannot save guides", error);
}
} // namespace toon
