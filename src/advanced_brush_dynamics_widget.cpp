#include "advanced_brush_dynamics_widget.h"
#include "value_slider.h"
#include "property_row.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include <QCheckBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

namespace toon {
namespace {
class ClusterPreview : public QWidget {
public:
    BrushClusterSpray settings;
    std::function<void(qreal, qreal)> edit;
    explicit ClusterPreview(QWidget *parent) : QWidget(parent) {
        setFixedHeight(120); setFocusPolicy(Qt::StrongFocus);
        setAccessibleName("Clustered spray distribution");
        setToolTip("Drag to adjust cluster radius and angle. Left/Right turns the spray; Up/Down changes its radius.");
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const QRectF field=rect().adjusted(1,1,-1,-1); p.fillRect(field,QColor(ui::base));
        p.setPen(QPen(QColor(hasFocus()?ui::focus:ui::border),1)); p.drawRoundedRect(field,3,3);
        const QPointF center(width()/2.,height()/2.); const qreal scale=qMin(width(),height())/11.;
        p.setPen(QPen(QColor(ui::border),1,Qt::DashLine)); p.drawEllipse(center,scale*settings.radius,scale*settings.radius);
        p.setPen(Qt::NoPen); p.setBrush(QColor(isEnabled()?ui::accent:ui::disabled));
        for(int i=0;i<48;++i) {
            const auto pt=center+brushClusterOffset(settings,scale,0,0,37,i);
            p.drawEllipse(pt,1.6,1.6);
        }
        p.setPen(QPen(QColor(ui::muted),1));
        p.drawLine(center+QPointF(-4,0),center+QPointF(4,0));p.drawLine(center+QPointF(0,-4),center+QPointF(0,4));
    }
    void changeAt(QPointF p) {
        const auto delta=p-QPointF(width()/2.,height()/2.);
        const qreal scale=qMin(width(),height())/11.;
        if(edit)edit(qBound(0.,std::hypot(delta.x(),delta.y())/scale,5.),std::atan2(delta.y(),delta.x())*180/3.141592653589793);
    }
    void mousePressEvent(QMouseEvent *e) override {
        if(e->button()==Qt::LeftButton){setFocus();changeAt(e->position());e->accept();}
        else QWidget::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if(e->buttons()&Qt::LeftButton){changeAt(e->position());e->accept();}else QWidget::mouseMoveEvent(e);
    }
    void keyPressEvent(QKeyEvent *e) override {
        if(e->key()==Qt::Key_Left||e->key()==Qt::Key_Right) {
            if(edit)edit(settings.radius,std::remainder(settings.angle+(e->key()==Qt::Key_Left?-5:5),360.)); e->accept();
        } else if(e->key()==Qt::Key_Up||e->key()==Qt::Key_Down) {
            if(edit)edit(qBound(0.,settings.radius+(e->key()==Qt::Key_Up?.1:-.1),5.),settings.angle);e->accept();
        } else QWidget::keyPressEvent(e);
    }
};
}
AdvancedBrushDynamicsWidget::AdvancedBrushDynamicsWidget(Section section,QWidget *parent)
    :QWidget(parent),m_section(section),m_enabled(new QCheckBox(section==Section::Spray?"Clustered spray":"Speed response",this)),m_controls(new QWidget(this)) {
    setObjectName(section==Section::Spray?"clusterSprayWidget":"speedDynamicsWidget");
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Maximum);
    auto *layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(ui::metrics::controlGap);layout->setAlignment(Qt::AlignTop);layout->addWidget(m_enabled);
    m_enabled->setMinimumHeight(ui::metrics::actionTarget);
    m_controls->setObjectName(section==Section::Spray?"clusterSprayControls":"speedDynamicsControls");
    m_enabled->setObjectName(section==Section::Spray?"clusterSprayEnabled":"speedDynamicsEnabled");
    m_enabled->setToolTip(section==Section::Spray?"Group Count copies around shared centers. Radius and angle distributions are independent.":"Change size and opacity using pen or mouse speed in artwork pixels per second.");
    auto *body=new QVBoxLayout(m_controls);body->setContentsMargins(0,0,0,0);body->setSpacing(ui::metrics::controlGap);body->setAlignment(Qt::AlignTop);layout->addWidget(m_controls);
    const auto row=[&](QString name,QString key,qreal low,qreal high,int decimals,QString help) {
        auto *value=new ValueSlider(name,low,high,low,decimals);value->setObjectName(key);value->setToolTip(help);
        auto *property=new PropertyRow(name,value);body->addWidget(property);m_values.append(value);
        connect(value,&ValueSlider::valueChanged,this,[this]{edited();});
    };
    if(section==Section::Spray) {
        auto *preview=new ClusterPreview(m_controls);m_preview=preview;body->addWidget(preview);
        preview->edit=[this](qreal radius,qreal angle){m_values[1]->setValueSilently(radius*100);m_values[5]->setValueSilently(angle);edited();};
        row("Clusters","sprayClusters",1,16,0,"Number of shared cluster centers. Increase Count to place several tips in each cluster.");
        row("Radius","sprayRadius",0,500,0,"Cluster-center radius as a percentage of brush size.");
        row("Spread","spraySpread",0,200,0,"Independent local spread around each cluster center, as a percentage of brush size.");
        row("Radial bias","sprayRadialBias",0,100,0,"0 favors the center, 50 spreads centers uniformly by area, 100 favors the outer ring.");
        row("Angle span","sprayAngularSpread",0,360,0,"Angular distribution of cluster centers, in degrees.");
        row("Direction","sprayAngle",-180,180,0,"Rotate the cluster distribution relative to stroke direction, in degrees.");
    } else {
        row("Speed","speedReference",1,1000000,0,"Speed in artwork pixels per second at which the configured change is fully reached.");m_values[0]->setSliderRange(10,5000);
        row("Size change","speedSizeAmount",-95,100,0,"Percentage change at reference speed. Negative makes faster strokes thinner.");
        row("Opacity change","speedOpacityAmount",-100,100,0,"Percentage change at reference speed. Negative makes faster strokes lighter.");
    }
    connect(m_enabled,&QCheckBox::toggled,this,[this]{edited();});refresh();
}
void AdvancedBrushDynamicsWidget::setSettings(const BrushSettings &settings){m_settings=settings;refresh();}
void AdvancedBrushDynamicsWidget::refresh(){
    const QSignalBlocker blocker(m_enabled);
    if(m_section==Section::Spray) {
        const auto &s=m_settings.clusterSpray;m_enabled->setChecked(s.enabled);m_controls->setEnabled(s.enabled);m_controls->setVisible(s.enabled);
        const QVector<qreal> values{qreal(s.clusters),s.radius*100,s.spread*100,s.radialBias*100,s.angularSpread,s.angle};
        for(int i=0;i<values.size();++i)m_values[i]->setValueSilently(values[i]);
        auto *preview=static_cast<ClusterPreview *>(m_preview);preview->settings=s;preview->update();
    } else {
        const auto &s=m_settings.speedDynamics;m_enabled->setChecked(s.enabled);m_controls->setEnabled(s.enabled);m_controls->setVisible(s.enabled);
        m_values[0]->setValueSilently(s.referenceSpeed);m_values[1]->setValueSilently(s.sizeAmount*100);m_values[2]->setValueSilently(s.opacityAmount*100);
    }
}
void AdvancedBrushDynamicsWidget::edited(){
    if(m_section==Section::Spray) {
        auto &s=m_settings.clusterSpray;s.enabled=m_enabled->isChecked();s.clusters=qRound(m_values[0]->value());
        s.radius=m_values[1]->value()/100;s.spread=m_values[2]->value()/100;s.radialBias=m_values[3]->value()/100;
        s.angularSpread=m_values[4]->value();s.angle=m_values[5]->value();refresh();emit clusterSprayEdited(s);
    } else {
        auto &s=m_settings.speedDynamics;s.enabled=m_enabled->isChecked();s.referenceSpeed=m_values[0]->value();
        s.sizeAmount=m_values[1]->value()/100;s.opacityAmount=m_values[2]->value()/100;refresh();emit speedDynamicsEdited(s);
    }
}
} // namespace toon
