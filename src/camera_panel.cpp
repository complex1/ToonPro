#include "camera_panel.h"
#include "camera_animation.h"
#include "canvas.h"
#include "document.h"
#include "icons.h"
#include "property_row.h"
#include "scene3d_controller.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include "ui_metrics.h"
#include "value_slider.h"
#include <QLabel>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace toon {
namespace {
const Shot *currentShot(TimelineController *controller) {
    const auto *document = controller->document();
    return document->isOpen() && !document->project().shots.isEmpty() ? &document->activeShot() : nullptr;
}
const RasterLayer *sceneLayer(const Shot *shot, const QString &id) {
    if (shot) for (const auto &layer : shot->layers) if (layer.id == id && layer.scene3d) return &layer;
    return nullptr;
}
class CameraContext final : public QLabel {
public: using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this); painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideRight, width()));
    }
};
QToolButton *button(QWidget *parent, const char *id, const char *icon, const QString &help, bool checked = false) {
    auto *result = new QToolButton(parent); result->setObjectName(id);
    if (QByteArray(icon)=="diamond") {
        QPixmap image(40,40); image.setDevicePixelRatio(2); image.fill(Qt::transparent);
        {
            QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setPen(QPen(QColor(ui::text),1.8));
            painter.setBrush(Qt::NoBrush); painter.drawPolygon(QPolygonF{QPointF(10,3),QPointF(17,10),QPointF(10,17),QPointF(3,10)});
        }
        result->setIcon(QIcon(image));
    } else result->setIcon(appIcon(icon));
    result->setIconSize({ui::metrics::actionIcon,ui::metrics::actionIcon}); result->setFixedSize(ui::metrics::actionTarget,ui::metrics::actionTarget); result->setCheckable(checked);
    result->setProperty("compactDrawingAction",true); result->setFocusPolicy(Qt::StrongFocus);
    result->setToolTip(help); result->setAccessibleName(help); return result;
}
struct Field { const char *id, *label, *unit; double minimum, maximum, initial, slideMin, slideMax; int precision; };
constexpr Field shotFields[] = {
    {"cameraX", "Position X", "px", -32768,32768,0,-1000,1000,2},
    {"cameraY", "Position Y", "px", -32768,32768,0,-1000,1000,2},
    {"cameraZoom", "Zoom", "%", 1,10000,100,10,400,2},
    {"cameraRotation", "Rotation", "°", -36000,36000,0,-180,180,2}
};
constexpr Field sceneFields[] = {
    {"cameraTargetX", "Target X", "m", -1000000,1000000,0,-10,10,3},
    {"cameraTargetY", "Target Y", "m", -1000000,1000000,.8,-10,10,3},
    {"cameraTargetZ", "Target Z", "m", -1000000,1000000,0,-10,10,3},
    {"cameraYaw", "Orbit", "°", -1000000,1000000,30,-180,180,2},
    {"cameraPitch", "Elevation", "°", -89.9,89.9,15,-89.9,89.9,2},
    {"cameraDistance", "Distance", "m", .001,100000,5,.1,30,3},
    {"cameraFov", "Lens", "°", 5,150,45,5,150,2},
    {"cameraOrthoScale", "Ortho height", "m", .001,100000,3,.1,30,3}
};
}

CameraPanel::CameraPanel(TimelineController *controller, QWidget *parent) : QWidget(parent), m_controller(controller) {
    setObjectName("cameraProperties"); setMinimumWidth(0);
    auto *outer = new QVBoxLayout(this); outer->setContentsMargins(0,0,0,0);
    auto *scroll = new QScrollArea; scroll->setObjectName("cameraPropertiesScroll"); scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); outer->addWidget(scroll);
    auto *content = new QWidget; auto *layout = new QVBoxLayout(content); layout->setContentsMargins(4,4,4,4); layout->setSpacing(6); scroll->setWidget(content);
    m_mode = new QTabBar; m_mode->setObjectName("cameraMode"); m_mode->setExpanding(true); m_mode->setDrawBase(false);
    m_mode->setProperty("compactDrawingTabs",true); m_mode->setIconSize({ui::metrics::tabIcon,ui::metrics::tabIcon}); m_mode->setFocusPolicy(Qt::StrongFocus);
    m_mode->setAccessibleName("Camera type");
    m_mode->addTab(appIcon("rectangle-horizontal"),"2D"); m_mode->addTab(appIcon("box"),"3D");
    m_mode->setTabToolTip(0,"Shot camera: pan, zoom and rotate the artwork frame"); m_mode->setTabToolTip(1,"Animate a 3D reference camera");
    m_mode->setAccessibleTabName(0,"2D shot camera"); m_mode->setAccessibleTabName(1,"3D reference camera"); layout->addWidget(m_mode);
    m_context = new CameraContext; m_context->setObjectName("cameraTargetName"); m_context->setMinimumHeight(24); m_context->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed); layout->addWidget(m_context);
    auto *actions = new QHBoxLayout; actions->setSpacing(4);
    m_enable = button(this,"cameraEnable","diamond","Enable camera animation",true);
    auto *keysLabel=new CameraContext("Keys");keysLabel->setObjectName("cameraKeysLabel");
    keysLabel->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);keysLabel->setMinimumWidth(0);
    keysLabel->setToolTip("Camera animation keys");keysLabel->setAccessibleName("Camera animation keys");
    actions->addWidget(m_enable); actions->addWidget(keysLabel,1);
    m_framing = button(this,"cameraFraming","move","Edit camera framing on the canvas",true);
    m_view = button(this,"cameraView","video","Preview the evaluated movie camera",true);
    actions->addWidget(m_framing); actions->addWidget(m_view); layout->addLayout(actions);
    const auto page = [this,layout](const Field *fields, int count, ValueSlider **values) {
        auto *widget = new QWidget; auto *rows = new QVBoxLayout(widget); rows->setContentsMargins(0,0,0,0); rows->setSpacing(4);
        for (int i = 0; i < count; ++i) {
            const auto &field = fields[i];
            auto *value = new ValueSlider(field.label,field.minimum,field.maximum,field.initial,field.precision,field.unit);
            value->setObjectName(field.id); value->setSliderRange(field.slideMin,field.slideMax); value->setSingleStep(field.precision == 3 ? .1 : 1);
            value->setToolTip(QString("%1 (%2)").arg(field.label,field.unit));
            auto *row = new PropertyRow(field.label,value); row->setObjectName(QString::fromLatin1(field.id)+"Row");
            row->labelWidget()->setToolTip(value->toolTip()); rows->addWidget(row); values[i] = value;
            connect(value,&ValueSlider::editingStarted,this,&CameraPanel::beginValue);
            connect(value,&ValueSlider::valueChanged,this,[this,i](double value){previewValue(i,value);});
            connect(value,&ValueSlider::editingFinished,this,&CameraPanel::finishValue);
            connect(value,&ValueSlider::editingCancelled,this,&CameraPanel::cancelEditors);
        }
        layout->addWidget(widget); return widget;
    };
    m_shotPage = page(shotFields,4,m_shotValues.data()); m_scenePage = page(sceneFields,8,m_sceneValues.data());
    auto *keys = new QHBoxLayout; keys->setSpacing(3);
    m_previous = button(this,"cameraPrevious","skip-back","Previous camera keyframe");
    m_add = button(this,"cameraAdd","diamond","Add a keyframe for the complete camera pose");
    m_remove = button(this,"cameraRemove","trash","Remove this camera keyframe");
    m_next = button(this,"cameraNext","skip-forward","Next camera keyframe");
    for (auto *action : {m_previous,m_add,m_remove,m_next}) keys->addWidget(action); keys->addStretch(); layout->addLayout(keys);
    m_keyTime = new ValueSlider("Camera keyframe time",1,1,1); m_keyTime->setObjectName("cameraKeyTime"); layout->addWidget(new PropertyRow("Key frame",m_keyTime));
    m_interpolation = new QTabBar; m_interpolation->setObjectName("cameraInterpolation"); m_interpolation->setExpanding(true); m_interpolation->setDrawBase(false); m_interpolation->setUsesScrollButtons(true);
    m_interpolation->setProperty("compactDrawingTabs",true); m_interpolation->setFocusPolicy(Qt::StrongFocus);
    m_interpolation->setAccessibleName("Camera interpolation");
    const QStringList labels{"Hold","Line","In","Out","In/Out"};
    for (int i = 0; i < labels.size(); ++i) {
        const auto name = keyframeInterpolationName(static_cast<KeyframeInterpolation>(i)); m_interpolation->addTab(labels[i]);
        m_interpolation->setTabToolTip(i,name + " interpolation to the next camera key"); m_interpolation->setAccessibleTabName(i,name + " interpolation");
    }
    layout->addWidget(m_interpolation);
    m_status = new CameraContext; m_status->setObjectName("cameraStatus"); m_status->setMinimumHeight(24); m_status->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed); layout->addWidget(m_status); layout->addStretch();
    connect(m_mode,&QTabBar::currentChanged,this,&CameraPanel::changeMode);
    connect(m_enable,&QToolButton::toggled,this,[this](bool enabled){if(!m_refreshing){commitEditors();if(selectTarget())m_controller->setCameraEnabled(enabled);refresh();}});
    connect(m_framing,&QToolButton::clicked,this,[this]{commitEditors();if(selectTarget())m_controller->editCameraFraming();refresh();});
    connect(m_view,&QToolButton::toggled,this,[this](bool enabled){if(!m_refreshing){commitEditors();m_controller->setCameraView(enabled);refresh();}});
    connect(m_previous,&QToolButton::clicked,this,[this]{stepKey(-1);}); connect(m_next,&QToolButton::clicked,this,[this]{stepKey(1);});
    connect(m_add,&QToolButton::clicked,this,[this]{commitEditors();if(selectTarget())m_controller->setCameraKey();refresh();});
    connect(m_remove,&QToolButton::clicked,this,[this]{commitEditors();if(selectTarget())m_controller->removeCameraKey(m_controller->currentFrame());refresh();});
    connect(m_keyTime,&ValueSlider::editingStarted,this,[this]{if(!m_refreshing)m_controller->setPendingEdits(this,true);});
    connect(m_keyTime,&ValueSlider::editingFinished,this,&CameraPanel::moveKey); connect(m_keyTime,&ValueSlider::editingCancelled,this,&CameraPanel::cancelEditors);
    connect(m_interpolation,&QTabBar::currentChanged,this,[this](int index){if(!m_refreshing&&index>=0){commitEditors();if(selectTarget())m_controller->setCameraInterpolation(m_controller->currentFrame(),static_cast<KeyframeInterpolation>(index));refresh();}});
    connect(controller,&TimelineController::changed,this,&CameraPanel::refresh);
    connect(controller,&TimelineController::editResolutionRequested,this,&CameraPanel::commitEditors);
    connect(controller,&TimelineController::editCancellationRequested,this,&CameraPanel::cancelEditors);
    refresh();
}

QString CameraPanel::preferredScene() const {
    const auto *shot = currentShot(m_controller); if (!shot) return {};
    for (const auto &id : {m_controller->selectedCameraLayerId(),m_controller->selectedPropertyLayerId(),m_controller->canvas()->scene3D()->layerId(),m_layerId})
        if (sceneLayer(shot,id)) return id;
    for (const auto &layer : shot->layers) if (layer.scene3d) return layer.id;
    return {};
}
bool CameraPanel::selectTarget() {
    if (m_mode->currentIndex() == 1 && m_layerId.isEmpty()) return false;
    const auto target=m_mode->currentIndex() == 1 ? m_layerId : QString();
    if(m_controller->cameraSelected()&&m_controller->selectedCameraLayerId()==target)return true;
    return m_controller->selectCamera(target);
}
void CameraPanel::activate() { selectTarget(); refresh(); }
void CameraPanel::changeMode(int mode) {
    if (m_refreshing) return;
    {const QSignalBlocker block(m_mode);m_mode->setCurrentIndex(m_displayedMode);}
    commitEditors();
    {const QSignalBlocker block(m_mode);m_mode->setCurrentIndex(mode);}m_displayedMode=mode;
    if (mode == 1) m_layerId = preferredScene(); else m_layerId.clear();
    selectTarget(); refresh();
}
QVector<int> CameraPanel::keyFrames() const {
    QVector<int> frames; const auto *shot = currentShot(m_controller); if (!shot) return frames;
    if (m_mode->currentIndex() == 0) for (const auto &key : shot->camera2DKeys) frames.append(key.frame);
    else if (const auto *animation = sceneCameraAnimation(*shot,m_layerId)) for (const auto &key : animation->keys) frames.append(key.frame);
    return frames;
}
void CameraPanel::refresh() {
    if (m_refreshing) return; const QScopedValueRollback<bool> guard(m_refreshing,true);
    const auto *shot = currentShot(m_controller);
    if (m_controller->cameraSelected()) {
        m_layerId = m_controller->selectedCameraLayerId(); m_mode->setCurrentIndex(m_layerId.isEmpty()?0:1);
    } else if (m_mode->currentIndex() == 1 && !sceneLayer(shot,m_layerId)) m_layerId = preferredScene();
    m_displayedMode=m_mode->currentIndex();
    m_mode->setTabEnabled(1,!preferredScene().isEmpty());
    const bool three = m_mode->currentIndex() == 1; const auto *target = sceneLayer(shot,m_layerId);
    m_displayedMode=m_mode->currentIndex();
    m_shotPage->setVisible(!three); m_scenePage->setVisible(three);
    const QString name = three ? (target ? target->name : "No 3D reference") : "2D shot camera";
    m_context->setText(name); m_context->setToolTip(name); m_context->setAccessibleName(name);
    QString reason = !shot ? "Open a project" : three ? sceneCameraEditError(*shot,m_layerId) : QString();
    if (reason.isEmpty() && m_controller->playing()) reason = "Playback";
    const bool editable = reason.isEmpty(); const int frame = m_controller->currentFrame(); const auto frames = keyFrames();
    m_enable->setEnabled(editable); m_enable->setChecked(!frames.isEmpty());
    const QString help = frames.isEmpty() ? "Enable camera animation; each key stores the complete pose" : "Disable camera animation and keep this frame's pose; Undo restores the keys";
    m_enable->setToolTip(help); m_enable->setAccessibleName(help);
    m_framing->setEnabled(editable); m_framing->setChecked(m_controller->cameraSelected() && m_controller->cameraFramingEditing());
    m_framing->setToolTip(three ? "Move the selected 3D camera with the canvas camera tool" : "Move, resize or rotate the shot camera frame on the canvas"); m_framing->setAccessibleName(m_framing->toolTip());
    m_view->setEnabled(shot && !m_controller->playing()); m_view->setChecked(m_controller->cameraView());
    m_shotPage->setEnabled(editable); m_scenePage->setEnabled(editable);
    if (shot && !m_editing) {
        const auto camera = evaluateShotCamera(*shot,frame);
        const double values[]{camera.position.x(),camera.position.y(),camera.zoom*100,camera.rotation};
        for (int i = 0; i < 4; ++i) m_shotValues[i]->setValueSilently(values[i]);
        if (target) {
            const auto scene = evaluateSceneCamera(*shot,m_layerId,frame);
            const double values[]{scene.target.x(),scene.target.y(),scene.target.z(),scene.yaw,scene.pitch,scene.distance,scene.fieldOfView,scene.orthoScale};
            for (int i = 0; i < 8; ++i) m_sceneValues[i]->setValueSilently(values[i]);
            m_sceneValues[6]->setEnabled(editable&&!scene.orthographic); m_sceneValues[7]->setEnabled(editable&&scene.orthographic);
        }
    }
    const bool exact = frames.contains(frame);
    m_previous->setEnabled(!m_controller->playing()&&std::any_of(frames.cbegin(),frames.cend(),[frame](int f){return f<frame;}));
    m_next->setEnabled(!m_controller->playing()&&std::any_of(frames.cbegin(),frames.cend(),[frame](int f){return f>frame;}));
    m_add->setEnabled(editable&&!exact); m_remove->setEnabled(editable&&exact);
    const int count = qMax(1,m_controller->frameCount());
    if (m_keyTime->property("frameCount").toInt()!=count) {m_keyTime->setProperty("frameCount",count);m_keyTime->setRange(1,count);}
    if (m_displayedFrame!=frame) {m_keyTime->setValueSilently(frame+1);m_displayedFrame=frame;}
    m_keyTime->setEnabled(editable&&exact); m_interpolation->setEnabled(editable&&exact);
    auto interpolation = KeyframeInterpolation::Linear;
    if (shot&&exact) {
        if (!three) {for (const auto &key:shot->camera2DKeys)if(key.frame==frame)interpolation=key.interpolation;}
        else if (const auto *animation=sceneCameraAnimation(*shot,m_layerId))for(const auto &key:animation->keys)if(key.frame==frame)interpolation=key.interpolation;
    }
    m_interpolation->setCurrentIndex(int(interpolation)); m_status->setText(reason); m_status->setToolTip(reason); m_status->setAccessibleName(reason);
}
void CameraPanel::beginValue() {
    if (m_refreshing||m_resolving) return;
    {const QScopedValueRollback<bool> guard(m_resolving,true);m_editing=selectTarget()&&m_controller->beginCameraEdit();}
    m_controller->setPendingEdits(this,m_editing); refresh();
}
void CameraPanel::previewValue(int index,double value) {
    if (m_refreshing||!m_editing) return; const auto *shot=currentShot(m_controller);if(!shot)return;
    if (m_mode->currentIndex()==0) {
        auto pose=evaluateShotCamera(*shot,m_controller->currentFrame());
        if(index==0)pose.position.setX(value);else if(index==1)pose.position.setY(value);else if(index==2)pose.zoom=value/100;else pose.rotation=value;
        m_controller->previewShotCamera(pose);
    } else {
        auto pose=evaluateSceneCamera(*shot,m_layerId,m_controller->currentFrame());
        if(index<3)pose.target[index]=float(value);else if(index==3)pose.yaw=value;else if(index==4)pose.pitch=value;
        else if(index==5)pose.distance=value;else if(index==6)pose.fieldOfView=value;else pose.orthoScale=value;
        m_controller->previewSceneCamera(pose);
    }
}
void CameraPanel::finishValue() {
    if(m_refreshing)return;
    if(m_editing){m_editing=false;m_controller->finishCameraEdit();}
    m_controller->setPendingEdits(this,false);refresh();
}
void CameraPanel::commitEditors() {
    if(m_resolving)return;const QScopedValueRollback<bool> guard(m_resolving,true);
    for(auto *field:m_shotValues)field->commitEditing();for(auto *field:m_sceneValues)field->commitEditing();m_keyTime->commitEditing();finishValue();
}
void CameraPanel::cancelEditors() {
    if(m_resolving||m_refreshing)return;const QScopedValueRollback<bool> guard(m_resolving,true);
    {const QScopedValueRollback<bool> update(m_refreshing,true);for(auto *field:m_shotValues)field->cancelEditing();for(auto *field:m_sceneValues)field->cancelEditing();m_keyTime->cancelEditing();}
    if(m_editing)m_controller->cancelCameraEdit();m_editing=false;m_displayedFrame=-1;m_controller->setPendingEdits(this,false);refresh();
}
void CameraPanel::stepKey(int direction) {
    commitEditors();const int current=m_controller->currentFrame();int next=current;
    for(int frame:keyFrames())if((direction<0&&frame<current&&(next==current||frame>next))||(direction>0&&frame>current&&(next==current||frame<next)))next=frame;
    m_controller->setFrame(next);
}
void CameraPanel::moveKey() {
    if(m_refreshing)return;const int from=m_controller->currentFrame(),to=qRound(m_keyTime->value())-1;
    if(from!=to){const QScopedValueRollback<bool> guard(m_resolving,true);if(selectTarget()&&m_controller->moveCameraKey(from,to))m_controller->setFrame(to);}
    m_displayedFrame=-1;m_controller->setPendingEdits(this,false);refresh();
}
} // namespace toon
