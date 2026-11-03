#include "skin3d_panel.h"
#include "scene3d_controller.h"
#include "skin3d_view.h"
#include "icons.h"
#include "property_row.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QLabel>
#include <QPainter>
#include <QScopedValueRollback>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace toon {
namespace {
class ElidedLabel final : public QLabel {
public:
    explicit ElidedLabel(QWidget *parent=nullptr):QLabel(parent){
        setTextFormat(Qt::PlainText);setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);
        setFixedHeight(fontMetrics().height());
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(),Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(text(),Qt::ElideRight,width()));
    }
};
class WeightLegend final : public QWidget {
public:
    WeightLegend(){setObjectName("skin3DWeightLegend");setFixedHeight(8+fontMetrics().height());setMinimumWidth(0);
        setToolTip("Selected-joint influence: blue is zero, red is full influence. Paint only on the front visible surface.");
        setAccessibleName("Selected joint weight: blue zero percent to red one hundred percent");}
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);QLinearGradient gradient(0,0,width(),0);
        for(int i=0;i<=24;++i)gradient.setColorAt(i/24.,skinWeightColor(i/24.f));
        painter.fillRect(QRect(0,0,width(),6),gradient);
        painter.setPen(QColor(ui::muted));painter.drawText(QRect(0,8,width(),fontMetrics().height()),Qt::AlignLeft|Qt::AlignVCenter,"0");
        painter.drawText(QRect(0,8,width(),fontMetrics().height()),Qt::AlignRight|Qt::AlignVCenter,"100");
    }
};
QToolButton *skinButton(const char *id,const char *icon,const QString &help){
    auto *button=new QToolButton;button->setObjectName(id);button->setIcon(appIcon(icon));button->setIconSize({18,18});
    button->setFixedSize(28,28);button->setToolTip(help);button->setAccessibleName(help);return button;
}
void compactTabs(QTabBar *tabs){
    tabs->setExpanding(true);tabs->setDrawBase(false);tabs->setIconSize({18,18});
    tabs->setStyleSheet("QTabBar::tab { min-width: 24px; min-height: 24px; padding: 1px 2px; }");
    tabs->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Fixed);
}
void syncScalar(ValueSlider *field,double value,double &last,double tolerance){
    if(value!=last||std::abs(field->value()-value)>tolerance)field->setValueSilently(value);
    last=value;
}
}
Skin3DPanel::Skin3DPanel(Scene3DController *controller,QWidget *parent):QWidget(parent),m_controller(controller){
    setObjectName("skin3DProperties");setMinimumWidth(0);
    auto policy=QSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);policy.setHeightForWidth(true);setSizePolicy(policy);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,4,0,0);layout->setSpacing(4);
    auto *binding=new QHBoxLayout;binding->setSpacing(2);
    m_status=new ElidedLabel;m_status->setObjectName("skin3DStatus");m_status->setFixedHeight(qMax(28,fontMetrics().height()));
    m_status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);binding->addWidget(m_status,1);
    m_bind=skinButton("skin3DBind","link-2","Bind the model to its rest skeleton and generate initial weights");
    m_unbind=skinButton("skin3DUnbind","scissors","Remove weights and pose to edit the rest skeleton (undoable)");
    binding->addWidget(m_bind);binding->addWidget(m_unbind);layout->addLayout(binding);
    auto *context=new QHBoxLayout;context->setSpacing(6);
    m_joint=new ElidedLabel;m_joint->setObjectName("skin3DSelectedJoint");context->addWidget(m_joint,1);
    auto *surface=new QLabel("Front surface");surface->setObjectName("skin3DSurfaceLabel");surface->setToolTip("Weight painting changes the front visible surface under the brush; hidden and rear vertices are protected.");
    surface->setStyleSheet(QString("color: %1;").arg(ui::muted));context->addWidget(surface);layout->addLayout(context);
    layout->addWidget(new WeightLegend);
    m_modes=new QTabBar;m_modes->setObjectName("skin3DBrushMode");m_modes->setAccessibleName("Weight brush operation");
    compactTabs(m_modes);
    const char *icons[]={"plus","minus","blend"};const char *help[]={"Add selected-joint weight","Subtract selected-joint weight","Smooth weights on the front visible surface"};
    for(int i=0;i<3;++i){m_modes->addTab(appIcon(icons[i]),{});m_modes->setTabToolTip(i,help[i]);m_modes->setAccessibleTabName(i,help[i]);}
    layout->addWidget(m_modes);
    m_radius=new ValueSlider("Weight brush radius in screen pixels",1,250,40,0);m_radius->setObjectName("skin3DBrushRadius");
    m_strength=new ValueSlider("Weight brush strength in percent",0,100,25,0);m_strength->setObjectName("skin3DBrushStrength");
    layout->addWidget(new PropertyRow("Radius",m_radius));
    layout->addWidget(new PropertyRow("Strength",m_strength));
    auto *preview=new QHBoxLayout;preview->setSpacing(2);
    auto *label=new QLabel("Bend check");label->setToolTip("Temporary deformation of the selected joint. This is not a saved pose; reset to paint weights.");
    label->setObjectName("skin3DBendLabel");
    label->setAccessibleName("Temporary bend check, not saved as a pose");preview->addWidget(label,1);
    m_axes=new QTabBar;m_axes->setObjectName("skin3DPreviewAxis");m_axes->setAccessibleName("Bend check axis");compactTabs(m_axes);
    for(const auto &axis:QStringList{"X","Y","Z"}){const int i=m_axes->addTab(axis);m_axes->setTabToolTip(i,"Temporary bend around the "+axis+" axis");m_axes->setAccessibleTabName(i,"Bend around "+axis);}
    preview->addWidget(m_axes);
    m_reset=skinButton("skin3DResetPreview","rotate-ccw","Reset the temporary bend check to paint weights");preview->addWidget(m_reset);layout->addLayout(preview);
    m_bend=new ValueSlider("Temporary bend angle in degrees",-90,90,0,1);m_bend->setObjectName("skin3DPreviewAngle");m_bend->setSingleStep(1);
    layout->addWidget(new PropertyRow("Angle",m_bend));layout->addStretch();
    connect(m_bind,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->bindSkin();});
    connect(m_unbind,&QToolButton::clicked,this,[this]{m_controller->endEdit();m_controller->unbindSkin();});
    connect(m_modes,&QTabBar::currentChanged,this,[this](int index){if(!m_refreshing)m_controller->setWeightPaintMode(static_cast<Scene3DController::WeightPaintMode>(index));});
    connect(m_radius,&ValueSlider::valueChanged,this,[this](double value){if(!m_refreshing)m_controller->setWeightBrushRadius(value);});
    connect(m_strength,&ValueSlider::valueChanged,this,[this](double value){if(!m_refreshing)m_controller->setWeightBrushStrength(value/100.);});
    connect(m_axes,&QTabBar::currentChanged,this,[this](int axis){
        if(m_refreshing)return;m_bend->commitEditing();m_axis=axis;m_displayed[2]=std::numeric_limits<double>::quiet_NaN();refresh();
    });
    connect(m_bend,&ValueSlider::valueChanged,this,[this](double value){
        if(m_refreshing)return;auto rotation=m_controller->skinPreviewRotation();rotation[m_axis]=float(value);m_controller->setSkinPreviewRotation(rotation);
    });
    connect(m_reset,&QToolButton::clicked,this,[this]{m_bend->cancelEditing();m_controller->setSkinPreviewRotation({});});
    connect(controller,&Scene3DController::editResolutionRequested,this,[this](bool commit){
        for(auto *field:{m_radius,m_strength,m_bend}){if(commit)field->commitEditing();else field->cancelEditing();}
    });
    connect(controller,&Scene3DController::changed,this,&Skin3DPanel::refresh);refresh();
}
void Skin3DPanel::refresh(){
    if(m_refreshing)return;const QScopedValueRollback<bool> guard(m_refreshing,true);
    const auto *object=m_controller->selectedObject();const auto *rig=m_controller->selectedRig();const auto *joint=m_controller->selectedJoint();
    const bool bound=m_controller->selectedSkin();const bool busy=m_controller->skinBindingBusy();
    const bool editable=m_controller->canEdit()&&m_controller->scene()&&m_controller->scene()->visible&&object&&object->visible&&!object->locked;
    const bool preview=!m_controller->skinPreviewRotation().isNull();const bool paint=m_controller->canPaintWeights();
    m_bind->setEnabled(m_controller->canEditRig()&&rig&&!rig->joints.isEmpty()&&!bound&&!busy);
    m_unbind->setEnabled(editable&&bound&&!busy);
    const QString status=busy?"Binding…":!object?"Select a model":!rig||rig->joints.isEmpty()?"Create rest joints":!bound?"Ready to bind":preview?"Reset to paint":!editable?"Show and unlock":m_controller->weightProjectionBusy()?"Preparing surface…":"Bound";
    m_status->setText(status);m_status->setToolTip(status);m_status->setAccessibleName(status);
    m_joint->setText(joint?joint->name:"Select a joint");m_joint->setToolTip(joint?joint->name+" — painted influence":"Select a joint in the hierarchy to paint its influence");
    m_joint->setAccessibleName(joint?joint->name:"Select a joint");
    m_modes->setEnabled(paint);m_modes->setCurrentIndex(int(m_controller->weightPaintMode()));
    m_radius->setEnabled(paint);m_strength->setEnabled(paint);
    const bool canPreview=editable&&bound&&joint&&!busy;
    m_axes->setEnabled(canPreview);m_bend->setEnabled(canPreview);m_reset->setEnabled(preview);
    syncScalar(m_radius,m_controller->weightBrushRadius(),m_displayed[0],.50001);
    syncScalar(m_strength,m_controller->weightBrushStrength()*100,m_displayed[1],.50001);
    syncScalar(m_bend,m_controller->skinPreviewRotation()[m_axis],m_displayed[2],.05001);
}
} // namespace toon
