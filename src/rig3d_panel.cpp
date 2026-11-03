#include "rig3d_panel.h"
#include "scene3d_controller.h"
#include "rig3d.h"
#include "skin3d_panel.h"
#include "pose3d_panel.h"
#include "icons.h"
#include "value_slider.h"
#include "property_row.h"
#include <QComboBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPointer>
#include <QScopedValueRollback>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QStyledItemDelegate>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
class RigStatusLabel final : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter,
                         fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};
class RigModeTabs final : public QTabWidget {
public:
    RigModeTabs() {
        setDocumentMode(true); setIconSize({18,18});
        tabBar()->setExpanding(true); tabBar()->setDrawBase(false);
        tabBar()->setStyleSheet("QTabBar::tab { min-width: 24px; min-height: 24px; padding: 1px 2px; }");
        setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    }
    QSize sizeHint() const override { return tabBar()->sizeHint(); }
    QSize minimumSizeHint() const override { return {0,tabBar()->sizeHint().height()}; }
};
class RigPropertyLayout final : public QStackedLayout {
public:
    explicit RigPropertyLayout(QWidget *parent) : QStackedLayout(parent) { setSizeConstraint(QLayout::SetNoConstraint); }
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
class RigJointDelegate final : public QStyledItemDelegate {
public:
    RigJointDelegate(Scene3DController *controller, QObject *parent)
        : QStyledItemDelegate(parent), m_controller(controller) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override {
        m_controller->beginEdit("Rename rest joint");
        m_editor = QStyledItemDelegate::createEditor(parent, option, index);
        return m_editor;
    }
    void destroyEditor(QWidget *editor, const QModelIndex &index) const override {
        if(m_editor==editor)m_editor.clear();
        QStyledItemDelegate::destroyEditor(editor,index);
    }
    void resolve(bool commit) {
        const auto editor=m_editor;m_editor.clear();
        if(!editor)return;
        if(commit)emit commitData(editor);
        if(editor)emit closeEditor(editor,commit?NoHint:RevertModelCache);
    }
private:
    Scene3DController *m_controller;
    mutable QPointer<QWidget> m_editor;
};
QToolButton *rigButton(const char *id, const char *icon, const QString &help, bool checkable = false) {
    auto *button = new QToolButton;
    button->setObjectName(id); button->setIcon(appIcon(icon)); button->setIconSize({18,18});
    button->setFixedSize(28,28); button->setCheckable(checkable);
    button->setToolTip(help); button->setAccessibleName(help);
    return button;
}
QHash<QString,QTreeWidgetItem *> treeItems(QTreeWidget *tree) {
    QHash<QString,QTreeWidgetItem *> items;
    QTreeWidgetItemIterator iterator(tree);
    while (*iterator) { auto *item = *iterator; items.insert(item->data(0,Qt::UserRole).toString(),item); ++iterator; }
    return items;
}
}
class RigPropertyPages final : public QWidget {
public:
    RigPropertyPages() : m_layout(new RigPropertyLayout(this)) {
        auto policy=QSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
        policy.setHeightForWidth(true); setSizePolicy(policy);
        connect(m_layout,&QStackedLayout::currentChanged,this,[this]{updateGeometry();});
    }
    void addWidget(QWidget *widget) { m_layout->addWidget(widget); }
    void setCurrentIndex(int index) { m_layout->setCurrentIndex(index); }
private:
    RigPropertyLayout *m_layout;
};
Rig3DPanel::Rig3DPanel(Scene3DController *controller, QWidget *parent)
    : QWidget(parent), m_controller(controller) {
    setObjectName("rig3DProperties"); setMinimumWidth(0);
    auto policy=QSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0); layout->setSpacing(4);
    auto *restPage=new QWidget;auto *restLayout=new QVBoxLayout(restPage);restLayout->setContentsMargins(0,0,0,0);restLayout->setSpacing(4);
    m_navigation=new QWidget; m_navigation->setObjectName("rig3DNavigation");
    auto *navigationLayout=new QHBoxLayout(m_navigation);navigationLayout->setContentsMargins(0,0,0,0);navigationLayout->setSpacing(4);
    m_modes=new RigModeTabs;m_modes->setObjectName("rig3DEditMode");
    navigationLayout->addWidget(m_modes,1);
    m_mirrorEdits = rigButton("rig3DMirrorEdits","link-2","Mirror edits to paired joints",true);
    m_visible = rigButton("rig3DVisible","eye","Show the skeleton",true);
    navigationLayout->addWidget(m_mirrorEdits);navigationLayout->addWidget(m_visible);layout->addWidget(m_navigation);
    auto *actions = new QHBoxLayout; actions->setSpacing(2);
    m_create = rigButton("rig3DCreateRoot","file-plus","Create a skeleton with one root joint");
    m_humanoid = rigButton("rig3DCreateHumanoid","sparkles","Create a humanoid rest-skeleton template");
    m_child = rigButton("rig3DAddChild","split","Add a child joint to the selected joint");
    m_root = rigButton("rig3DAddRoot","plus","Add another root joint");
    m_remove = rigButton("rig3DDeleteBranch","trash","Delete the selected joint and its descendants");
    m_mirror = rigButton("rig3DMirrorBranch","arrow-left-right","Create a mirrored copy of the selected branch");
    for(auto *button:{m_create,m_humanoid,m_child,m_root,m_remove,m_mirror}) actions->addWidget(button);
    actions->addStretch();
    m_restStatus=new RigStatusLabel("Rest joints");m_restStatus->setObjectName("rig3DRestLabel");m_restStatus->setTextFormat(Qt::PlainText);
    m_restStatus->setFixedHeight(qMax(20,fontMetrics().height()+2));m_restStatus->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    restLayout->addWidget(m_restStatus);restLayout->addLayout(actions);
    m_joints = new QTreeWidget; m_joints->setObjectName("rig3DJoints"); m_joints->setAccessibleName("Skeleton joint hierarchy");
    m_joints->setHeaderHidden(true); m_joints->setColumnCount(1); m_joints->setUniformRowHeights(true);
    m_joints->setIndentation(12); m_joints->setIconSize({16,16}); m_joints->setFixedHeight(116);
    m_joints->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); m_joints->setTextElideMode(Qt::ElideRight);
    m_joints->header()->setSectionResizeMode(0,QHeaderView::Stretch);
    m_joints->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);
    auto *jointDelegate=new RigJointDelegate(controller,m_joints);
    m_joints->setItemDelegate(jointDelegate);
    layout->addWidget(m_joints);
    m_parent = new QComboBox; m_parent->setObjectName("rig3DParent"); m_parent->setMinimumWidth(0);
    m_parent->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); m_parent->setMinimumContentsLength(3);
    m_parent->setAccessibleName("Selected joint parent"); m_parent->setToolTip("Change the parent joint without moving its resting position");
    auto *parentRow=new PropertyRow("Parent",m_parent);parentRow->setLabelWidth(48);restLayout->addWidget(parentRow);
    auto *axes = new QVBoxLayout; axes->setContentsMargins(0,0,0,0); axes->setSpacing(4);
    const char *names[]={"rig3DX","rig3DY","rig3DZ"};
    for(int axis=0;axis<3;++axis) {
        const QString coordinate(QStringLiteral("XYZ")[axis]);
        auto *field = new ValueSlider(QString("Rest joint %1 in model-local metres").arg(coordinate),
            -maxRig3DCoordinate,maxRig3DCoordinate,0,3);
        m_axes[axis]=field;field->setObjectName(names[axis]);field->setSliderRange(-2,2);field->setSingleStep(.01);
        field->setToolTip(QString("Rest joint %1 in model-local metres").arg(coordinate));
        auto *row=new PropertyRow(coordinate,field);row->setLabelWidth(24);axes->addWidget(row);
        connect(field,&ValueSlider::editingStarted,this,[this]{if(!m_refreshing)m_controller->beginEdit("Move rest joint");});
        connect(field,&ValueSlider::valueChanged,this,[this,axis]{if(!m_refreshing)updateAxis(axis);});
        connect(field,&ValueSlider::editingFinished,this,[this]{if(!m_refreshing)m_controller->endEdit();});
        connect(field,&ValueSlider::editingCancelled,this,[this]{if(!m_refreshing)m_controller->cancelEdit();});
    }
    restLayout->addLayout(axes);restLayout->addStretch();
    m_modes->addTab(new QWidget,appIcon("spline"),{});m_modes->addTab(new QWidget,appIcon("paintbrush"),{});
    m_modes->addTab(new QWidget,appIcon("rotate-ccw"),{});
    m_modes->setTabToolTip(0,"Edit rest joints");m_modes->setTabToolTip(1,"Bind and paint skin weights");
    m_modes->setTabToolTip(2,"Pose the bound model with FK and IK");
    m_modes->tabBar()->setAccessibleName("Skeleton editing mode");m_modes->tabBar()->setAccessibleTabName(0,"Rest joints");m_modes->tabBar()->setAccessibleTabName(1,"Skin weights");
    m_modes->tabBar()->setAccessibleTabName(2,"Current pose");
    m_modes->setFixedHeight(m_modes->tabBar()->sizeHint().height());
    m_pages=new RigPropertyPages;m_pages->setObjectName("rig3DPropertyPages");
    m_pages->addWidget(restPage);m_pages->addWidget(new Skin3DPanel(controller));m_pages->addWidget(new Pose3DPanel(controller));
    layout->addWidget(m_pages);
    m_viewActions=new QWidget;m_viewActions->setObjectName("rig3DViewActions");
    auto *views = new QHBoxLayout(m_viewActions);views->setContentsMargins(0,0,0,0);views->setSpacing(4);
    const char *viewNames[]={"Front","Side","Top"}; const char *viewIds[]={"rig3DFrontView","rig3DSideView","rig3DTopView"};
    for(int i=0;i<3;++i){
        auto *button = new QToolButton; m_views[i]=button;button->setObjectName(viewIds[i]);button->setText(viewNames[i]);
        button->setFixedHeight(28);button->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        const auto help=QString("Set the drawing camera to the %1 orthographic view").arg(QString(viewNames[i]).toLower());
        button->setToolTip(help);button->setAccessibleName(help);views->addWidget(button);
        connect(button,&QToolButton::clicked,this,[this,i]{m_controller->endEdit();m_controller->setRigView(static_cast<Scene3DController::RigView>(i));});
    }
    layout->addWidget(m_viewActions);layout->addStretch();
    connect(m_create,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->createRig(false);});
    connect(m_humanoid,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->createRig(true);});
    connect(m_child,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->addJoint(true);});
    connect(m_root,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->addJoint(false);});
    connect(m_remove,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->removeSelectedJoint();});
    connect(m_mirror,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->mirrorSelectedBranch();});
    connect(m_modes,&QTabWidget::currentChanged,this,[this](int index){
        if(m_refreshing){showEditingMode(index);return;}
        // Tool selection emits a refresh with the previous controller mode.
        // Only switch real pages once that mode has caught up with the tabs.
        m_controller->endEdit();m_controller->selectRigTool();
        m_controller->setRigEditMode(static_cast<Scene3DController::RigEditMode>(index));
        showEditingMode(int(m_controller->rigEditMode()));
    });
    connect(m_mirrorEdits,&QToolButton::toggled,this,[this](bool enabled){if(!m_refreshing)m_controller->setMirrorEdits(enabled);});
    connect(m_visible,&QToolButton::toggled,this,[this](bool visible){if(!m_refreshing)m_controller->setRigVisible(visible);});
    connect(m_joints,&QTreeWidget::currentItemChanged,this,[this](QTreeWidgetItem *item){
        if(!m_refreshing)m_controller->selectJoint(item?item->data(0,Qt::UserRole).toString():QString());
    });
    connect(m_joints,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem *item,int column){
        if(m_refreshing||column!=0||!m_controller->canEditRig()||!m_controller->selectedRig())return;
        const auto *joint=rigJoint(*m_controller->selectedRig(),item->data(0,Qt::UserRole).toString());
        if(!joint)return;
        auto changed=*joint;changed.name=item->text(0).trimmed();
        if(changed.name.isEmpty()||changed.name.size()>120){refresh();return;}
        if(changed.name==joint->name)return;
        m_controller->selectJoint(changed.id);m_controller->updateJoint(changed);
    });
    connect(m_parent,&QComboBox::activated,this,[this](int index){
        if(m_refreshing||!m_controller->canEditRig()||!m_controller->selectedJoint())return;
        auto joint=*m_controller->selectedJoint();joint.parentId=m_parent->itemData(index).toString();
        m_controller->updateJoint(joint);
    });
    connect(jointDelegate,&QAbstractItemDelegate::closeEditor,this,[this](QWidget *,QAbstractItemDelegate::EndEditHint hint){
        if(m_refreshing)return;
        if(hint==QAbstractItemDelegate::RevertModelCache)m_controller->cancelEdit();
        else m_controller->endEdit();
    });
    connect(controller,&Scene3DController::editResolutionRequested,this,[this,jointDelegate](bool commit){
        if(commit){
            for(auto *axis:m_axes)axis->commitEditing();
            jointDelegate->resolve(true);
        }else{
            // Cancel local editor state before restoring the document, so a
            // later FocusOut cannot submit abandoned text or a drag value.
            const QScopedValueRollback<bool> guard(m_refreshing,true);
            for(auto *axis:m_axes)axis->cancelEditing();
            jointDelegate->resolve(false);
        }
    });
    connect(controller,&Scene3DController::changed,this,&Rig3DPanel::refresh);
    refresh();
}
QWidget *Rig3DPanel::takeNavigation() { layout()->removeWidget(m_navigation);return m_navigation; }
QWidget *Rig3DPanel::takeViewActions() { layout()->removeWidget(m_viewActions);return m_viewActions; }
void Rig3DPanel::setViewportHeight(int height) {
    if(m_viewportHeight==height)return;
    m_viewportHeight=height;updateHierarchyHeight();
}
void Rig3DPanel::updateHierarchyHeight() {
    const int rowHeight=qMax(28,fontMetrics().height()+8);
    const auto *rig=m_controller->selectedRig();
    const int rows=qBound(1,rig?int(rig->joints.size()):0,m_viewportHeight>=340?4:m_viewportHeight>=200?3:2);
    const int height=rows*rowHeight+4;
    if(m_joints->height()==height)return;
    m_joints->setFixedHeight(height);
    if(m_joints->currentItem())m_joints->scrollToItem(m_joints->currentItem());
}
void Rig3DPanel::showEditingMode(int index) {
    if(index==m_activeMode)return;
    emit editingModeAboutToChange(m_activeMode,index);
    m_activeMode=index;m_pages->setCurrentIndex(index);updateGeometry();
    emit editingModeChanged(index);
}
void Rig3DPanel::updateAxis(int axis) {
    if(!m_controller->canEditRig()||!m_controller->selectedJoint())return;
    auto joint=*m_controller->selectedJoint();joint.position[axis]=float(m_axes[axis]->value());
    m_controller->updateJoint(joint);
}
void Rig3DPanel::refresh() {
    if(m_refreshing)return;
    m_refreshing=true;
    const auto *rig=m_controller->selectedRig();const auto *joint=m_controller->selectedJoint();
    const bool editable=m_controller->canEditRig();const bool empty=!rig||rig->joints.isEmpty();
    const bool bound=m_controller->selectedSkin();
    m_modes->setCurrentIndex(int(m_controller->rigEditMode()));
    const QString restStatus=bound?"Unbind to edit rest joints":m_controller->skinBindingBusy()?"Binding…":"Rest joints";
    m_restStatus->setText(restStatus);m_restStatus->setToolTip(restStatus);m_restStatus->setAccessibleName(restStatus);
    const QString restHelp=bound?"Unbind the model to edit rest joints.":m_controller->skinBindingBusy()?"Wait for binding to finish.":QString();
    const QList<QWidget *> restControls{m_create,m_humanoid,m_child,m_root,m_remove,m_mirror,m_mirrorEdits,m_parent,m_axes[0],m_axes[1],m_axes[2]};
    for(auto *control:restControls){
        if(!control->property("restEditingHelp").isValid())control->setProperty("restEditingHelp",control->toolTip());
        control->setToolTip(restHelp.isEmpty()?control->property("restEditingHelp").toString():restHelp);
    }
    const bool room=rig&&rig->joints.size()<maxRig3DJoints;
    m_create->setEnabled(editable&&empty);m_humanoid->setEnabled(editable&&empty);
    m_child->setEnabled(editable&&joint&&room);m_root->setEnabled(editable&&room);
    m_remove->setEnabled(editable&&joint);
    bool mirrorable=editable&&joint&&room;
    if(mirrorable)for(const auto &id:rigDescendants(*rig,joint->id))
        if(const auto *entry=rigJoint(*rig,id);entry&&!entry->mirrorId.isEmpty()){mirrorable=false;break;}
    m_mirror->setEnabled(mirrorable);m_mirrorEdits->setEnabled(editable&&!empty&&m_controller->rigEditMode()==Scene3DController::RigEditMode::Rest);
    m_mirrorEdits->setChecked(m_controller->mirrorEdits());m_visible->setEnabled(!empty);m_visible->setChecked(m_controller->rigVisible());
    m_visible->setIcon(appIcon(m_controller->rigVisible()?"eye":"eye-off"));
    const auto *scene=m_controller->scene();
    for(auto *view:m_views)view->setEnabled(m_controller->canEdit()&&scene&&!scene->camera.locked&&m_controller->selectedObject());
    QStringList signature{m_controller->selectedObjectId()};
    if(rig)for(const auto &entry:rig->joints){signature<<entry.id<<entry.parentId;}
    auto items=treeItems(m_joints);
    if(signature!=m_treeSignature){
        QSet<QString> expanded;
        for(auto it=items.cbegin();it!=items.cend();++it)if(it.value()->isExpanded())expanded.insert(it.key());
        const bool initial=items.isEmpty();m_joints->clear();items.clear();
        if(rig){
            for(const auto &entry:rig->joints){auto *item=new QTreeWidgetItem;item->setData(0,Qt::UserRole,entry.id);item->setSizeHint(0,{0,qMax(28,fontMetrics().height()+8)});items.insert(entry.id,item);}
            for(const auto &entry:rig->joints){auto *item=items.value(entry.id);if(auto *parent=items.value(entry.parentId))parent->addChild(item);else m_joints->addTopLevelItem(item);}
            for(auto it=items.cbegin();it!=items.cend();++it)it.value()->setExpanded(initial||expanded.contains(it.key()));
        }
        m_treeSignature=signature;
    }
    if(rig)for(const auto &entry:rig->joints){
        auto *item=items.value(entry.id);if(!item)continue;
        if(item->text(0)!=entry.name)item->setText(0,entry.name);
        const auto flags=Qt::ItemIsSelectable|Qt::ItemIsEnabled|(editable?Qt::ItemIsEditable:Qt::NoItemFlags);
        if(item->flags()!=flags)item->setFlags(flags);
        const auto icon=appIcon(entry.mirrorId.isEmpty()?"spline":"link-2");
        if(item->icon(0).cacheKey()!=icon.cacheKey())item->setIcon(0,icon);
        item->setToolTip(0,entry.name+(entry.mirrorId.isEmpty()?QString():" — paired for mirrored edits")+(restHelp.isEmpty()?QString():"\n"+restHelp));
        item->setData(0,Qt::AccessibleTextRole,entry.name);
    }
    auto *selected=joint?items.value(joint->id):nullptr;
    if(m_joints->currentItem()!=selected)m_joints->setCurrentItem(selected);
    const auto selectedId=joint?joint->id:QString();
    const bool selectionChanged=selectedId!=m_displayedSelection;
    if(selected&&selectionChanged){
        for(auto *parent=selected->parent();parent;parent=parent->parent())parent->setExpanded(true);
        m_joints->scrollToItem(selected,QAbstractItemView::EnsureVisible);
    }
    m_displayedSelection=selectedId;
    QStringList parentSignature{selectedId};QStringList excluded;
    if(joint&&rig)excluded=rigDescendants(*rig,joint->id);
    if(rig)for(const auto &entry:rig->joints)if(!excluded.contains(entry.id))parentSignature<<entry.id<<entry.name;
    if(parentSignature!=m_parentSignature){
        m_parent->clear();m_parent->addItem("No parent",QString());
        if(rig)for(const auto &entry:rig->joints)if(!excluded.contains(entry.id))m_parent->addItem(entry.name,entry.id);
        m_parentSignature=parentSignature;
    }
    m_parent->setEnabled(editable&&joint);m_parent->setCurrentIndex(joint?m_parent->findData(joint->parentId):0);
    const QVector3D position=joint?joint->position:QVector3D();
    for(int axis=0;axis<3;++axis){
        m_axes[axis]->setEnabled(editable&&joint);
        if(selectionChanged)m_axes[axis]->cancelEditing();
        // Rounded fields must not reapply the same higher-precision model echo:
        // that would replace partially typed text during an unrelated refresh.
        // A rejected edit leaves the model unchanged, but its submitted number
        // still needs to return to the accepted value. Pending text has not
        // changed ValueSlider::value(), so unrelated refreshes preserve it.
        if(selectionChanged||position[axis]!=m_displayedPosition[axis]
            ||std::abs(m_axes[axis]->value()-position[axis])>.000501)
            m_axes[axis]->setValueSilently(position[axis]);
    }
    m_displayedPosition=position;
    updateHierarchyHeight();
    m_refreshing=false;
}
} // namespace toon
