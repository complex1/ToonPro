#include "pose3d_panel.h"
#include "scene3d_controller.h"
#include "icons.h"
#include "property_row.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QButtonGroup>
#include <QStackedLayout>
#include <QLabel>
#include <QPainter>
#include <QScopedValueRollback>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>
#include <limits>

namespace toon {
namespace {
class PoseStatus final:public QLabel {
public:
    PoseStatus(){
        setTextFormat(Qt::PlainText);setFixedHeight(qMax(20,fontMetrics().height()+2));
        setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    }
protected:
    void paintEvent(QPaintEvent *)override{
        QPainter painter(this);painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(),Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(text(),Qt::ElideRight,width()));
    }
};
QToolButton *poseButton(const char *name,const char *icon,const QString &help,bool checkable=false){
    auto *button=new QToolButton;button->setObjectName(name);button->setIcon(appIcon(icon));button->setIconSize({18,18});
    button->setFixedSize(28,28);button->setCheckable(checkable);button->setToolTip(help);button->setAccessibleName(help);return button;
}
QTabBar *poseTabs(const char *name,const QString &help,const QList<QPair<QString,QString>> &choices){
    auto *tabs=new QTabBar;tabs->setObjectName(name);tabs->setAccessibleName(help);tabs->setDrawBase(false);tabs->setExpanding(true);
    tabs->setIconSize({18,18});tabs->setUsesScrollButtons(false);
    tabs->setStyleSheet("QTabBar::tab { min-width: 24px; min-height: 24px; padding: 1px 4px; }");
    tabs->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
    for(const auto &choice:choices){const int index=tabs->addTab(appIcon(choice.first),{});tabs->setTabToolTip(index,choice.second);tabs->setAccessibleTabName(index,choice.second);}return tabs;
}
// FK root controls must not reserve space while an IK endpoint is being edited.
class PosePageLayout final:public QStackedLayout {
public:
    explicit PosePageLayout(QWidget *parent):QStackedLayout(parent){setSizeConstraint(QLayout::SetNoConstraint);}
    QSize minimumSize()const override{
        return currentWidget()?currentWidget()->minimumSizeHint().expandedTo(currentWidget()->minimumSize()):QSize();
    }
    QSize sizeHint()const override{return currentWidget()?currentWidget()->sizeHint():QSize();}
    bool hasHeightForWidth()const override{return true;}
    int heightForWidth(int width)const override{
        return currentWidget()?(currentWidget()->hasHeightForWidth()?currentWidget()->heightForWidth(width):currentWidget()->sizeHint().height()):0;
    }
};
PropertyRow *axisRow(const QString &axis,ValueSlider *field){
    auto *row=new PropertyRow(axis,field);row->setLabelWidth(28);return row;
}
void syncPoseValue(ValueSlider *field,double value,double &last,double tolerance){
    if(last!=value||std::abs(field->value()-value)>tolerance)field->setValueSilently(value);last=value;
}
}
class PosePropertyPages final:public QWidget {
public:
    PosePropertyPages():m_layout(new PosePageLayout(this)){
        auto policy=QSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);
        connect(m_layout,&QStackedLayout::currentChanged,this,[this]{updateGeometry();});
    }
    void addWidget(QWidget *widget){m_layout->addWidget(widget);}
    void setCurrentIndex(int index){m_layout->setCurrentIndex(index);}
private:
    PosePageLayout *m_layout;
};
Pose3DPanel::Pose3DPanel(Scene3DController *controller,QWidget *parent):QWidget(parent),m_controller(controller){
    setObjectName("pose3DProperties");setMinimumWidth(0);m_displayed.fill(std::numeric_limits<double>::quiet_NaN());
    auto policy=QSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);
    auto *statusRow=new QHBoxLayout;statusRow->setSpacing(2);
    m_status=new PoseStatus;m_status->setObjectName("pose3DStatus");statusRow->addWidget(m_status,1);
    m_mirror=poseButton("pose3DMirror","arrow-left-right","Mirror the whole current pose across paired joints");
    m_resetSelected=poseButton("pose3DResetSelected","undo-2","Reset the selected joint rotation and IK controls touching it; resetting a root also resets body position");
    m_resetAll=poseButton("pose3DResetAll","rotate-ccw","Reset the whole current pose, keeping joint limits");
    statusRow->addWidget(m_mirror);statusRow->addWidget(m_resetSelected);statusRow->addWidget(m_resetAll);layout->addLayout(statusRow);
    m_modes=poseTabs("pose3DMode","Pose manipulation",{{"rotate-ccw","Rotate a joint (FK)"},{"move","Move a two-bone limb endpoint (IK)"}});
    m_modes->setTabText(0,"FK");m_modes->setTabText(1,"IK");layout->addWidget(m_modes);
    m_pages=new PosePropertyPages;m_pages->setObjectName("pose3DPages");
    auto *fkPage=new QWidget;fkPage->setObjectName("pose3DFKPage");auto *fkLayout=new QVBoxLayout(fkPage);fkLayout->setContentsMargins(0,0,0,0);fkLayout->setSpacing(4);
    auto *ringAxes=new QButtonGroup(this);ringAxes->setExclusive(true);
    const char *rotationNames[]={"pose3DRotationX","pose3DRotationY","pose3DRotationZ"};
    const char *bodyNames[]={"pose3DBodyX","pose3DBodyY","pose3DBodyZ"};
    const char *ikNames[]={"pose3DIKX","pose3DIKY","pose3DIKZ"};
    for(int axis=0;axis<3;++axis){
        const QString coordinate(QStringLiteral("XYZ")[axis]);auto *button=new QToolButton;m_rotationAxes[axis]=button;
        button->setObjectName("pose3DRotationAxis"+coordinate);button->setText(coordinate);button->setCheckable(true);button->setFixedSize(28,28);ringAxes->addButton(button,axis);
        button->setToolTip("Use the "+coordinate+" axis rotation ring on the canvas");button->setAccessibleName("Rotate around "+coordinate);
        auto *field=new ValueSlider("Local joint "+coordinate+" rotation in degrees",-180,180,0,1);m_rotation[axis]=field;field->setObjectName(rotationNames[axis]);field->setSingleStep(1);
        auto *row=axisRow({},field);row->setAccessibleName("Joint "+coordinate+" rotation");row->setAccessory(button);fkLayout->addWidget(row);wire(field,"Rotate posed joint");
        connect(field,&ValueSlider::valueChanged,this,[this,axis]{if(!m_refreshing)updateRotation(axis);});
    }
    m_body=new QWidget;m_body->setObjectName("pose3DBody");auto *body=new QVBoxLayout(m_body);body->setContentsMargins(0,0,0,0);body->setSpacing(4);
    auto *bodyLabel=new QLabel("Body position");bodyLabel->setToolTip("Translate all skeleton roots together in model-local metres");body->addWidget(bodyLabel);
    for(int axis=0;axis<3;++axis){const QString coordinate(QStringLiteral("XYZ")[axis]);
        auto *field=new ValueSlider("Body "+coordinate+" position in model-local metres",-maxRig3DCoordinate,maxRig3DCoordinate,0,3);m_translation[axis]=field;
        field->setObjectName(bodyNames[axis]);field->setSliderRange(-2,2);field->setSingleStep(.01);
        body->addWidget(axisRow(coordinate,field));wire(field,"Move posed body");
        connect(field,&ValueSlider::valueChanged,this,[this,axis]{if(!m_refreshing)updateTranslation(axis);});
    }
    fkLayout->addWidget(m_body);m_pages->addWidget(fkPage);
    auto *ikPage=new QWidget;ikPage->setObjectName("pose3DIKPage");auto *ikLayout=new QVBoxLayout(ikPage);ikLayout->setContentsMargins(0,0,0,0);ikLayout->setSpacing(4);
    auto *ikHeader=new QHBoxLayout;ikHeader->setSpacing(2);auto *ikLabel=new QLabel("Limb control");ikLabel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);ikHeader->addWidget(ikLabel,1);
    m_ikEnabled=poseButton("pose3DIKEnabled","link-2","Enable two-bone IK for this endpoint",true);
    m_pin=poseButton("pose3DIKPinned","pin","Keep this model-local endpoint fixed when the body or parent joints move; moving the whole 3D object also moves the pin",true);ikHeader->addWidget(m_ikEnabled);ikHeader->addWidget(m_pin);ikLayout->addLayout(ikHeader);
    m_handles=poseTabs("pose3DIKHandle","IK canvas handle",{{"move","Move the limb endpoint target"},{"split","Move the pole to choose the limb bend direction"}});
    m_handles->setTabText(0,"Target");m_handles->setTabText(1,"Pole");ikLayout->addWidget(m_handles);
    for(int axis=0;axis<3;++axis){const QString coordinate(QStringLiteral("XYZ")[axis]);
        auto *field=new ValueSlider("IK handle "+coordinate+" in model-local metres",-maxRig3DCoordinate,maxRig3DCoordinate,0,3);m_ik[axis]=field;
        field->setObjectName(ikNames[axis]);field->setSliderRange(-2,2);field->setSingleStep(.01);
        ikLayout->addWidget(axisRow(coordinate,field));wire(field,"Move IK handle");
        connect(field,&ValueSlider::valueChanged,this,[this,axis]{if(!m_refreshing)updateIK(axis);});
    }
    m_pages->addWidget(ikPage);layout->addWidget(m_pages);
    auto *limitsHeader=new QHBoxLayout;limitsHeader->setSpacing(4);auto *limitsLabel=new QLabel("Joint limits");
    limitsLabel->setToolTip("Local rotation limits for the selected joint");limitsHeader->addWidget(limitsLabel);
    m_limitAxes=poseTabs("pose3DLimitAxis","Joint limit axis",{});
    for(const auto &axis:QStringList{"X","Y","Z"}){int i=m_limitAxes->addTab(axis);m_limitAxes->setTabToolTip(i,"Edit "+axis+" rotation limits");m_limitAxes->setAccessibleTabName(i,axis+" limits");}limitsHeader->addWidget(m_limitAxes,1);
    m_limitsEnabled=poseButton("pose3DLimitsEnabled","lock-keyhole","Enable local rotation limits for the selected joint",true);limitsHeader->addWidget(m_limitsEnabled);layout->addLayout(limitsHeader);
    m_limitMin=new ValueSlider("Minimum local rotation in degrees",-180,0,-180,1);m_limitMin->setObjectName("pose3DLimitMin");m_limitMin->setSingleStep(1);
    m_limitMax=new ValueSlider("Maximum local rotation in degrees",0,180,180,1);m_limitMax->setObjectName("pose3DLimitMax");m_limitMax->setSingleStep(1);
    layout->addWidget(axisRow("Min",m_limitMin));layout->addWidget(axisRow("Max",m_limitMax));layout->addStretch();
    wire(m_limitMin,"Change joint limits");wire(m_limitMax,"Change joint limits");
    connect(m_limitMin,&ValueSlider::valueChanged,this,[this]{if(!m_refreshing)updateLimit(true);});
    connect(m_limitMax,&ValueSlider::valueChanged,this,[this]{if(!m_refreshing)updateLimit(false);});
    connect(m_modes,&QTabBar::currentChanged,this,[this](int mode){if(!m_refreshing)m_controller->setPoseMode(static_cast<Scene3DController::PoseMode>(mode));});
    connect(ringAxes,&QButtonGroup::idClicked,this,[this](int axis){if(!m_refreshing)m_controller->setPoseRotationAxis(axis);});
    connect(m_handles,&QTabBar::currentChanged,this,[this](int handle){if(!m_refreshing)m_controller->setIKHandle(static_cast<Scene3DController::IKHandle>(handle));});
    connect(m_ikEnabled,&QToolButton::toggled,this,[this](bool enabled){if(!m_refreshing){m_controller->endEdit();m_controller->enableSelectedIK(enabled);}});
    connect(m_pin,&QToolButton::toggled,this,[this](bool pinned){if(!m_refreshing){m_controller->endEdit();m_controller->setIKPinned(pinned);}});
    connect(m_mirror,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->mirrorPose();});
    connect(m_resetSelected,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->resetSelectedPose();});
    connect(m_resetAll,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->resetAllPose();});
    connect(m_limitsEnabled,&QToolButton::toggled,this,[this](bool enabled){if(!m_refreshing){m_controller->endEdit();auto limit=m_controller->selectedPoseLimit();limit.enabled=enabled;m_controller->updatePoseLimit(limit);}});
    connect(m_limitAxes,&QTabBar::currentChanged,this,[this](int axis){if(!m_refreshing){m_controller->endEdit();m_limitAxis=axis;m_displayed[9]=m_displayed[10]=std::numeric_limits<double>::quiet_NaN();refresh();}});
    connect(controller,&Scene3DController::editResolutionRequested,this,[this](bool commit){
        QList<ValueSlider *> fields;for(auto *field:m_rotation)fields.append(field);for(auto *field:m_translation)fields.append(field);for(auto *field:m_ik)fields.append(field);fields<<m_limitMin<<m_limitMax;
        if(commit){for(auto *field:fields)field->commitEditing();}
        else{const QScopedValueRollback<bool> guard(m_refreshing,true);for(auto *field:fields)field->cancelEditing();}
    });
    connect(controller,&Scene3DController::changed,this,&Pose3DPanel::refresh);refresh();
}
void Pose3DPanel::wire(ValueSlider *field,const QString &label){
    connect(field,&ValueSlider::editingStarted,this,[this,label]{if(!m_refreshing)m_controller->beginEdit(label);});
    connect(field,&ValueSlider::editingFinished,this,[this]{if(!m_refreshing)m_controller->endEdit();});
    connect(field,&ValueSlider::editingCancelled,this,[this]{if(!m_refreshing)m_controller->cancelEdit();});
}
void Pose3DPanel::updateRotation(int axis){auto rotation=m_controller->poseRotation();rotation[axis]=float(m_rotation[axis]->value());m_controller->updatePoseRotation(rotation);}
void Pose3DPanel::updateTranslation(int axis){auto translation=m_controller->poseRootTranslation();translation[axis]=float(m_translation[axis]->value());m_controller->updatePoseRootTranslation(translation);}
void Pose3DPanel::updateIK(int axis){
    const auto control=m_controller->selectedIKTarget();if(!control)return;
    if(m_controller->ikHandle()==Scene3DController::IKHandle::Target){auto target=control->target;target[axis]=float(m_ik[axis]->value());m_controller->updateIKTarget(target);}
    else{auto pole=control->pole;pole[axis]=float(m_ik[axis]->value());m_controller->updateIKPole(pole);}
}
void Pose3DPanel::updateLimit(bool minimum){auto limit=m_controller->selectedPoseLimit();if(minimum)limit.minimum[m_limitAxis]=float(m_limitMin->value());else limit.maximum[m_limitAxis]=float(m_limitMax->value());m_controller->updatePoseLimit(limit);}
void Pose3DPanel::refresh(){
    if(m_refreshing)return;const QScopedValueRollback<bool> guard(m_refreshing,true);
    const auto *joint=m_controller->selectedJoint();const bool editable=m_controller->canPose();const bool root=joint&&joint->parentId.isEmpty();
    const QString selected=joint?joint->id:QString();const bool selectionChanged=selected!=m_displayedJoint;m_displayedJoint=selected;
    auto status=m_controller->poseStatus();if(status.isEmpty())status="Current pose";m_status->setText(status);m_status->setToolTip(status);m_status->setAccessibleName(status);
    m_modes->setCurrentIndex(int(m_controller->poseMode()));m_pages->setCurrentIndex(int(m_controller->poseMode()));
    m_body->setVisible(root);const auto rotation=m_controller->poseRotation(),translation=m_controller->poseRootTranslation();const auto target=m_controller->selectedIKTarget();
    const QVector3D ik=target?(m_controller->ikHandle()==Scene3DController::IKHandle::Target?target->target:target->pole):QVector3D();
    m_handles->setCurrentIndex(int(m_controller->ikHandle()));m_handles->setEnabled(editable&&target.has_value());
    m_ikEnabled->setEnabled(editable&&(m_controller->canUseSelectedIK()||target.has_value()));m_ikEnabled->setChecked(target.has_value());
    m_pin->setEnabled(editable&&target.has_value());m_pin->setChecked(target&&target->pinned);
    for(int axis=0;axis<3;++axis){
        m_rotationAxes[axis]->setChecked(axis==m_controller->poseRotationAxis());m_rotationAxes[axis]->setEnabled(editable&&m_controller->canRotateSelectedPoseJoint());
        m_rotation[axis]->setEnabled(editable&&m_controller->canRotateSelectedPoseJoint());m_translation[axis]->setEnabled(editable&&root);m_ik[axis]->setEnabled(editable&&target.has_value());
        if(selectionChanged){m_rotation[axis]->cancelEditing();m_translation[axis]->cancelEditing();m_ik[axis]->cancelEditing();}
        syncPoseValue(m_rotation[axis],rotation[axis],m_displayed[axis],.05001);syncPoseValue(m_translation[axis],translation[axis],m_displayed[axis+3],.000501);syncPoseValue(m_ik[axis],ik[axis],m_displayed[axis+6],.000501);
    }
    m_mirror->setEnabled(editable);m_resetSelected->setEnabled(editable&&joint);m_resetAll->setEnabled(editable);
    const auto limit=m_controller->selectedPoseLimit();m_limitsEnabled->setEnabled(editable&&joint);m_limitsEnabled->setChecked(limit.enabled);
    m_limitAxes->setEnabled(editable&&joint);m_limitMin->setEnabled(editable&&joint&&limit.enabled);m_limitMax->setEnabled(editable&&joint&&limit.enabled);
    if(selectionChanged){m_limitMin->cancelEditing();m_limitMax->cancelEditing();}
    syncPoseValue(m_limitMin,limit.minimum[m_limitAxis],m_displayed[9],.05001);syncPoseValue(m_limitMax,limit.maximum[m_limitAxis],m_displayed[10],.05001);
}
} // namespace toon
