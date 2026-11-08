#include "drawing_tool_controller.h"
#include "raster.h"
#include "layer_panel.h"
#include "ui_theme.h"
#include "vector_edit_geometry.h"
#include "property_animation.h"
#include <QPainterPathStroker>
#include <QPainter>
#include <QLineF>
#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QTimer>
#include <cmath>
#include <limits>

namespace toon {
using Tool=Canvas::Tool;
namespace {
SelectionCombine combine(Qt::KeyboardModifiers mods,SelectionCombine fallback) {
    if ((mods&Qt::ShiftModifier)&&(mods&Qt::AltModifier)) return SelectionCombine::Intersect;
    if (mods&Qt::ShiftModifier) return SelectionCombine::Add;
    if (mods&Qt::AltModifier) return SelectionCombine::Subtract;
    return fallback;
}
qreal localPickRadius(const QTransform &view,qreal pixels) {
    const auto inverse=view.inverted();const auto origin=inverse.map(QPointF());
    return qMax(QLineF(origin,inverse.map(QPointF(pixels,0))).length(),
                QLineF(origin,inverse.map(QPointF(0,pixels))).length());
}

}
DrawingToolController::DrawingToolController(Canvas *canvas):QObject(canvas),m_canvas(canvas),m_raster(canvas->document()),m_objects(canvas->document()),m_transform(this) {
    textStyle=EditableObject::create(ObjectKind::Text); textStyle.text="Text"; textStyle.fill=Qt::black;
    canvas->installEventFilter(this);
    connect(canvas,&Canvas::viewChanged,this,&DrawingToolController::positionTextEditor);
    connect(&m_transform,&AdvancedTransform::changed,this,[this]{if(!m_transform.puppet())m_selectedPuppetPins.clear();m_canvas->invalidateArtworkCache();m_canvas->updateSelectionUi();emit changed();});
    connect(this,&DrawingToolController::changed,canvas,[this]{m_canvas->update();m_canvas->updateSelectionUi();});
    connect(doc(),&Document::changed,this,[this] {
        if(m_textEdit&&!doc()->externalEditActive()){finishTextEdit(true);return;}
        // Brush samples change pixels, not the selected path or tool settings.
        // Canvas already receives those samples directly; broadcasting settings
        // again repaints hidden property pages and repeats its cursor work.
        // The commit/cancel notification below still synchronizes object edits.
        if(m_drag||m_styleEdit||m_textEdit||doc()->strokeActive())return;
        synchronizeObjectStyle();
        emit changed();
    });
    connect(doc(),&Document::frameChanged,this,[this]{if(m_textEdit&&!doc()->externalEditActive())finishTextEdit(true);synchronizeObjectStyle();emit changed();});
    connect(doc(),&Document::selectionChanged,this,[this]{
        const auto layerId=doc()->isOpen()?doc()->activeShot().layers[doc()->activeLayer()].id:QString();
        if(layerId!=m_contextLayerId){m_contextLayerId=layerId;cancel();}
        if(!m_objectId.isEmpty()&&!selectedObject()){m_objectId.clear();m_node=-1;emit changed();}
    });
}
bool DrawingToolController::handlesTool() const {
    const auto t=m_canvas->pointerTool();
    return t==Tool::Arc || t==Tool::VectorNudge || (t>=Tool::Eyedropper && t<=Tool::VectorWidth) || (t==Tool::Eraser && doc()->isOpen() && doc()->activeShot().layers[doc()->activeLayer()].kind==LayerKind::Vector);
}
bool DrawingToolController::active() const { return m_drag||m_arcPending||!m_points.isEmpty()||!m_nodes.isEmpty()||m_transform.active()||m_objects.editActive()||m_raster.retouchActive(); }
void DrawingToolController::notifySettings(){ emit changed(); }
bool DrawingToolController::beginAdvanced(const QStringList &ids,bool boundary) {
    if (!m_canvas->resolvePendingTransform()) { emit changed(); return false; }
    cancel(); m_canvas->setTool(Tool::AdvancedTransform);
    if (m_canvas->tool() != Tool::AdvancedTransform) return false;
    selectionBoundary=boundary; m_transform.cancel();
    m_canvas->notifyToolSettingsEdited();
    auto selected=ids;
    if(selected.isEmpty()&&!boundary)if(auto *panel=m_canvas->window()->findChild<LayerPanel*>())selected=panel->selectedLayerIds();
    const bool ok=m_canvas->prepareToolTransform(selected); emit changed(); return ok;
}
bool DrawingToolController::setPuppetMode(bool enabled){
    if(enabled==puppetMode&&warpMode){
        if(!m_transform.active()&&doc()->isOpen()&&m_canvas->tool()==Tool::AdvancedTransform){const bool ready=m_canvas->prepareToolTransform();emit changed();return ready;}
        return true;
    }
    if(!m_canvas->resolvePendingTransform()){emit changed();return false;}
    m_transform.cancel();m_selectedPuppetPins.clear();puppetMode=enabled;warpMode=true;
    if(enabled)selectionBoundary=false;
    m_canvas->refreshLayerSelectionContext();m_canvas->notifyToolSettingsEdited();
    const bool ok=!doc()->isOpen()||m_canvas->tool()!=Tool::AdvancedTransform||m_canvas->prepareToolTransform();emit changed();return ok;
}
bool DrawingToolController::setPuppetDensity(int density){
    if(density<minPuppetDensity||density>maxPuppetDensity)return false;
    const int previous=puppetDensity;puppetDensity=density;
    if(m_transform.puppet()&&!m_transform.setPuppetDensity(density)){puppetDensity=previous;emit changed();return false;}
    m_canvas->notifyToolSettingsEdited();emit changed();return true;
}
bool DrawingToolController::removeSelectedPuppetPins(){
    if(!m_transform.puppet()||m_selectedPuppetPins.isEmpty())return false;
    if(!m_transform.removePuppetPins(m_selectedPuppetPins.values()))return false;
    m_selectedPuppetPins.clear();m_drag=false;emit changed();return true;
}
std::optional<EditableObject> DrawingToolController::selectedObject() const {
    if(!doc()->isOpen()) return {};
    const auto &shot=doc()->activeShot();const auto &layer=shot.layers[doc()->activeLayer()];
    if(const auto *o=ObjectTools::object(layer,m_objectId))
        return doc()->keyframeSourceEditing()?*o:evaluateAnimatedObject(shot,layer.id,*o,doc()->currentFrame());
    return {};
}
bool DrawingToolController::selectedPathAnimated()const {
    if(!doc()->isOpen()||m_objectId.isEmpty())return false;
    const auto &shot=doc()->activeShot();
    return pathAnimation(shot,shot.layers[doc()->activeLayer()].id,m_objectId)!=nullptr;
}
bool DrawingToolController::selectAnimatedPath(const QString &id) {
    if(!doc()->isOpen())return false;
    const auto *object=ObjectTools::object(doc()->activeShot().layers[doc()->activeLayer()],id);
    if(!object||object->kind!=ObjectKind::Path)return false;
    if(!m_canvas->resolvePendingTransform())return false;
    finishGesture();m_canvas->setTool(Tool::VectorNode);
    if(m_canvas->tool()!=Tool::VectorNode)return false;
    selectObject(id);return true;
}
QTransform DrawingToolController::objectLayerTransform()const {
    if(!doc()->isOpen()||doc()->keyframeSourceEditing())return {};
    const auto &shot=doc()->activeShot();
    return worldLayerTransform(shot,shot.layers[doc()->activeLayer()].id,doc()->currentFrame());
}
QTransform DrawingToolController::objectViewTransform()const {
    return objectLayerTransform()*m_canvas->artworkViewTransform();
}
QVector<EditableObject> DrawingToolController::editableObjectsAtFrame()const {
    if(!doc()->isOpen())return {};
    const auto &shot=doc()->activeShot();const auto &layer=shot.layers[doc()->activeLayer()];
    auto result=layer.objects;
    if(!doc()->keyframeSourceEditing())for(auto &object:result)object=evaluateAnimatedObject(shot,layer.id,object,doc()->currentFrame());
    return result;
}
void DrawingToolController::synchronizeObjectStyle() {
    if(m_canvas->tool()!=Tool::VectorNode&&m_canvas->tool()!=Tool::Text)return;
    if(auto object=selectedObject()) {
        if(m_contextNodeCount!=object->nodes.size()||m_node>=object->nodes.size())m_node=-1;
        m_contextNodeCount=object->nodes.size();
        strokeWidth=object->strokeWidth;objectOpacity=object->opacity;closedPath=object->closed;
        shapeStroke=object->stroke.isValid();shapeFill=object->fill.isValid();fillColor=object->fill;
        if(object->kind==ObjectKind::Text){textStyle=*object;if(object->fill.isValid())m_canvas->setBrushColor(object->fill);}
        else if(object->stroke.isValid())m_canvas->setBrushColor(object->stroke);
    }
}
void DrawingToolController::selectObject(const QString&id) {
    m_objectId=id; m_node=-1;
    if(auto o=selectedObject()) { m_contextNodeCount=o->nodes.size();strokeWidth=o->strokeWidth;objectOpacity=o->opacity;closedPath=o->closed;shapeStroke=o->stroke.isValid();shapeFill=o->fill.isValid();fillColor=o->fill;
        if(o->stroke.isValid()) m_canvas->setBrushColor(o->stroke);
        if(o->kind==ObjectKind::Text){textStyle=*o;if(o->fill.isValid())m_canvas->setBrushColor(o->fill);}
    }
    emit changed();
}
EditableObject DrawingToolController::styledObject(EditableObject o) const {
    if(o.kind==ObjectKind::Text){auto value=textStyle;value.id=o.id;value.position=o.position;value.transform=o.transform;return value;}
    o.closed=closedPath;o.stroke=shapeStroke?m_canvas->brush.color:QColor();o.fill=shapeFill?fillColor:QColor();o.strokeWidth=strokeWidth;o.opacity=objectOpacity;return o;
}
void DrawingToolController::applyObjectStyle(){
    if(m_canvas->tool()!=Tool::VectorNode&&m_canvas->tool()!=Tool::Text){emit changed();return;}
    if(m_textEdit){previewTextStyle();finishTextEdit();return;}
    if(auto o=selectedObject())ObjectTools::updateObject(doc(),styledObject(*o),o->kind==ObjectKind::Text?"Edit text":"Style vector path");
    emit changed();
}
bool DrawingToolController::beginTextEdit(bool inlineEditor){
    if(m_textResolving||m_canvas->tool()!=Tool::Text||!doc()->isOpen())return false;
    // The properties panel owns its pending values until they are committed.
    // Start a fresh inline transaction so its old text cannot replay over the
    // next keystroke when both editors synchronize with the selected object.
    if(inlineEditor&&m_textEdit&&(!m_textEditor||!m_textEditor->isVisible()))finishTextEdit();
    const auto selected=selectedObject();if(!selected||selected->kind!=ObjectKind::Text)return false;
    if(!m_textEdit){if(m_objects.editActive())m_objects.finishEdit();m_drag=false;if(!m_objects.beginEdit(selected->id))return false;m_textEdit=true;textStyle=*selected;}
    if(inlineEditor){
        if(!m_textEditor){
            m_textEditor=new QPlainTextEdit(m_canvas);m_textEditor->setObjectName("artworkInlineText");m_textEditor->setAccessibleName("Edit artwork text");m_textEditor->setToolTip("Edit artwork text. Ctrl/Cmd+Enter applies; Escape cancels. Enter adds a line.");
            m_textEditor->setStyleSheet(QString("QPlainTextEdit { background: %1; color: %2; border: 1px solid %3; }").arg(ui::base,ui::text,ui::accent));m_textEditor->setTabChangesFocus(true);m_textEditor->setAttribute(Qt::WA_NoMousePropagation);m_textEditor->installEventFilter(this);
            connect(m_textEditor,&QPlainTextEdit::textChanged,this,[this]{if(!m_textEdit||m_textResolving)return;textStyle.text=m_textEditor->toPlainText();previewTextStyle();});
        }
        {const QSignalBlocker block(m_textEditor);m_textEditor->setPlainText(textStyle.text);}positionTextEditor();
        // Canvas focus-out finishes pointer gestures. This intentional transfer
        // must not finish the text draft that is receiving keyboard focus.
        const QScopedValueRollback<bool> focusing(m_focusingTextEditor,true);
        m_textEditor->show();m_textEditor->raise();m_textEditor->setFocus(Qt::OtherFocusReason);m_textEditor->selectAll();
    }
    emit changed();return true;
}
bool DrawingToolController::previewTextStyle(){
    if(!m_textEdit||m_textResolving)return false;const auto selected=selectedObject();if(!selected)return false;
    auto object=styledObject(*selected);if(object.text.isEmpty()||object.text.size()>16384)return false;
    if(object==*selected)return true;
    const bool result=m_objects.previewEdit(object);if(result){if(m_textEditor&&m_textEditor->isVisible()&&m_textEditor->toPlainText()!=object.text){const QSignalBlocker block(m_textEditor);m_textEditor->setPlainText(object.text);}emit changed();}return result;
}
void DrawingToolController::finishTextEdit(bool cancelEdit){
    if(!m_textEdit||m_textResolving||m_focusingTextEditor)return;if(!cancelEdit)previewTextStyle();const QScopedValueRollback<bool> guard(m_textResolving,true);const auto id=m_objectId;const bool empty=textStyle.text.isEmpty();m_textEdit=false;
    if(m_textEditor)m_textEditor->hide();
    if(cancelEdit||empty)m_objects.cancelEdit();else m_objects.finishEdit();
    if(empty&&!cancelEdit)ObjectTools::removeObject(doc(),id);
    synchronizeObjectStyle();emit changed();
}
void DrawingToolController::positionTextEditor(){
    if(!m_textEdit||!m_textEditor)return;const auto selected=selectedObject();if(!selected)return;
    const auto bounds=objectViewTransform().mapRect(editableObjectBounds(*selected));
    const int width=qBound(160,qRound(bounds.width())+28,qMax(160,m_canvas->width()-16));const int height=qBound(80,qRound(bounds.height())+24,qMax(80,m_canvas->height()/2));
    m_textEditor->setGeometry(qBound(8,qRound(bounds.left()),qMax(8,m_canvas->width()-width-8)),qBound(8,qRound(bounds.top()),qMax(8,m_canvas->height()-height-8)),width,height);
    QFont font(selected->fontFamily);font.setPixelSize(qBound(10,qRound(selected->fontSize*m_canvas->artworkZoom()),72));font.setBold(selected->bold);font.setItalic(selected->italic);m_textEditor->setFont(font);
}
bool DrawingToolController::eventFilter(QObject *watched,QEvent *event){
    if(watched==m_textEditor){
        if(event->type()==QEvent::ShortcutOverride||event->type()==QEvent::KeyPress){auto *key=static_cast<QKeyEvent *>(event);const bool cancel=key->key()==Qt::Key_Escape;const bool apply=(key->key()==Qt::Key_Return||key->key()==Qt::Key_Enter)&&(key->modifiers()&(Qt::ControlModifier|Qt::MetaModifier));
            if(cancel||apply){if(event->type()==QEvent::KeyPress){finishTextEdit(cancel);m_canvas->setFocus();}event->accept();return true;}
            // QPlainTextEdit accepts its native typing/Undo shortcuts. Leave
            // application shortcuts such as Save available to the window.
        }
        if(event->type()==QEvent::FocusOut&&m_textEdit&&!m_textResolving&&static_cast<QFocusEvent *>(event)->reason()!=Qt::PopupFocusReason)finishTextEdit();
    }
    if(watched==m_canvas&&event->type()==QEvent::MouseButtonDblClick&&m_canvas->tool()==Tool::Text&&m_canvas->toolAvailable()&&m_canvas->toolBlockReason().isEmpty()){
        auto *mouse=static_cast<QMouseEvent *>(event);if(mouse->button()==Qt::LeftButton){finishGesture();auto layer=doc()->activeShot().layers[doc()->activeLayer()];layer.objects=editableObjectsAtFrame();const auto point=objectLayerTransform().inverted().map(m_canvas->artworkPoint(mouse->position()));const auto hit=ObjectTools::hitTest(layer,point,localPickRadius(objectViewTransform(),8));if(!hit.isEmpty()){selectObject(hit);beginTextEdit();event->accept();return true;}}
    }
    if(watched==m_canvas&&event->type()==QEvent::Resize)positionTextEditor();
    return QObject::eventFilter(watched,event);
}
bool DrawingToolController::beginStyleEdit(){
    if(m_drag||m_styleEdit||m_objectId.isEmpty())return false;
    const auto selected=selectedObject();if(!selected)return false;
    strokeWidth=selected->strokeWidth;objectOpacity=selected->opacity;
    m_styleEdit=m_objects.beginEdit(m_objectId);return m_styleEdit;
}
void DrawingToolController::previewObjectStyle(){
    if(m_styleEdit)if(auto o=selectedObject()){o->strokeWidth=strokeWidth;o->opacity=objectOpacity;m_objects.previewEdit(*o);}
    emit changed();
}
void DrawingToolController::finishStyleEdit(){if(m_styleEdit)m_objects.finishEdit();m_styleEdit=false;emit changed();}
void DrawingToolController::cancelStyleEdit(){if(m_styleEdit)m_objects.cancelEdit();m_styleEdit=false;if(auto o=selectedObject()){strokeWidth=o->strokeWidth;objectOpacity=o->opacity;}emit changed();}
void DrawingToolController::setPathEditMode(PathEditMode mode){if(pathEditMode==mode)return;cancel();pathEditMode=mode;m_node=-1;emit changed();}
bool DrawingToolController::simplifySelected(){if(m_objectId.isEmpty())return false;const bool changed=ObjectTools::simplify(doc(),simplifyTolerance,m_objectId);emit this->changed();return changed;}
bool DrawingToolController::simplifyLayer(){const bool changed=ObjectTools::simplify(doc(),simplifyTolerance);emit this->changed();return changed;}
bool DrawingToolController::resetWidths(){if(auto o=selectedObject()){o->widthFactors.clear();const bool changed=ObjectTools::updateObject(doc(),*o,"Reset path width");emit this->changed();return changed;}return false;}
QPointF DrawingToolController::rotationHandle(const EditableObject &o)const {
    const auto handles=vectorTransformHandles(o);const auto view=objectViewTransform();
    const auto top=view.map(handles[1]),bottom=view.map(handles[5]);auto direction=top-bottom;const qreal length=QLineF({},direction).length();
    return length>1e-6?top+direction*(26/length):top+QPointF(0,-26);
}
QVector<int> DrawingToolController::visibleNodeIndices(const EditableObject &o)const {
    QVector<int> indices;const auto view=objectViewTransform();QPointF last(-1e9,-1e9);
    for(int i=0;i<o.nodes.size();++i){const auto point=view.map(o.transform.map(o.nodes[i].anchor));
        if(i==0||i==o.nodes.size()-1||i==m_node||QLineF(point,last).length()>=18){indices.append(i);last=point;}}
    return indices;
}
void DrawingToolController::applyVectorBrush(QPointF point,qreal pressure) {
    const bool simplify=m_canvas->tool()==Tool::VectorSimplify;
    const auto from=m_lastBrushPoint;const qreal distance=QLineF(from,point).length();
    if(distance<qMax(.5,2/m_canvas->artworkZoom())&&!m_simplifyRegion.isEmpty())return;
    QPainterPath sweep(from);sweep.lineTo(point+QPointF(.001,0));QPainterPathStroker stroker;stroker.setWidth(widthBrushRadius*2);stroker.setCapStyle(Qt::RoundCap);stroker.setJoinStyle(Qt::RoundJoin);
    const auto region=stroker.createStroke(sweep);m_simplifyRegion=m_simplifyRegion.united(region);
    auto values=m_editObjects;bool any=false;
    if(simplify)for(auto &o:values){auto result=simplifiedVectorPath(o,simplifyTolerance,m_simplifyRegion);any|=result.nodes.size()!=o.nodes.size();o=result;}
    else {
        values=editableObjectsAtFrame();
        for(auto &o:values) {
            if(!vectorCorrectionIntersects(o,region))continue;
            auto candidate=o;
            const auto &shot=doc()->activeShot();
            if(!pathAnimation(shot,shot.layers[doc()->activeLayer()].id,o.id))
                resampleVectorWidthNodes(candidate,qMax(2.,qMin(12.,widthBrushRadius/3)));
            if(brushVectorWidths(candidate,from,point,widthBrushRadius,widthBrushStrength*qBound(.05,pressure,1.),widthThicken)){o=candidate;any=true;}
        }
    }
    if(any)m_objects.previewObjects(values);m_lastBrushPoint=point;
}
void DrawingToolController::applyVectorNudge(QPointF point,qreal pressure){
    auto values=m_editObjects;QString error;
    for(auto &object:values){
        const auto nudged=nudgedVectorPath(object,m_start,point-m_start,nudgeRadius,nudgeStrength*qBound(.05,pressure,1.),m_nudgeLayerTransform,&error);
        if(!nudged){cancel();emit doc()->warning(error);return;}object=*nudged;
    }
    // Always publish from the captured source, including returning to the start.
    // Neither event count nor previous previews accumulates deformation.
    if(!m_objects.previewObjects(values))cancel();
}
bool DrawingToolController::press(QPointF view,Qt::KeyboardModifiers mods,qreal pressure) {
    if(m_textEdit)finishTextEdit();
    auto t=m_canvas->pointerTool(); auto p=m_canvas->artworkPoint(view);
    if(t==Tool::Eyedropper || ((mods&Qt::AltModifier)&& (t==Tool::Brush||t==Tool::Eraser||t==Tool::Retouch))) {
        const auto displayed=m_canvas->displayShot();
        auto color=RasterTools::sample(doc(),p,fillSettings.source,fillSettings.referenceLayerId,&displayed);if(color.isValid())m_canvas->setBrushColor(color); return true;
    }
    if(!handlesTool())return false;
    if(t==Tool::Arc&&m_arcPending){
        m_current=p;
        finishPath();
        return true;
    }
    if(t==Tool::Text||t==Tool::VectorNode||t==Tool::VectorWidth||t==Tool::VectorSimplify||t==Tool::VectorEraser||t==Tool::Eraser) {
        bool invertible=false;const auto inverse=objectLayerTransform().inverted(&invertible);
        if(!invertible){emit doc()->warning("Restore a nonzero layer scale before editing its paths, or use Source mode.");return true;}
        p=inverse.map(p);
    }
    m_start=m_current=p;if(m_points.isEmpty()&&m_nodes.isEmpty())m_combine=combine(mods,m_canvas->selectionCombine());m_handle=-1;
    if(t==Tool::Bucket){
        if(!encloseFillMode){RasterTools::bucket(doc(),p,m_canvas->brush.color,fillSettings);return true;}
        m_enclosePath=QPainterPath(p);m_drag=true;emit changed();return true;
    }
    if(t==Tool::MagicWand||t==Tool::ColorRange){RasterTools::selectColor(doc(),p,fillSettings,t==Tool::MagicWand,m_combine);return true;}
    if(t==Tool::AdvancedTransform){
        if(!m_transform.active()&&!m_canvas->prepareToolTransform())return true;
        if(warpMode&&puppetMode){
            if(!m_transform.puppet())return true;
            const auto &pins=m_transform.pins();int hit=-1;qreal distance=12;
            for(int i=0;i<pins.size();++i){const auto d=QLineF(view,m_canvas->artworkViewTransform().map(pins[i].target)).length();if(d<distance){distance=d;hit=i;}}
            if((mods&Qt::AltModifier)&&hit>=0){m_selectedPuppetPins={hit};removeSelectedPuppetPins();return true;}
            if(hit<0){hit=m_transform.addPuppetPin(p);if(hit<0)return true;m_selectedPuppetPins={hit};}
            else if(mods&Qt::ShiftModifier){if(m_selectedPuppetPins.contains(hit))m_selectedPuppetPins.remove(hit);else m_selectedPuppetPins.insert(hit);}
            else if(!m_selectedPuppetPins.contains(hit))m_selectedPuppetPins={hit};
            m_transformPoints.clear();for(const auto &pin:m_transform.pins())m_transformPoints.append(pin.target);
            m_drag=m_selectedPuppetPins.contains(hit);emit changed();return true;
        }
        m_transformPoints=warpMode?m_transform.controlPoints():QVector<QPointF>(m_transform.quad());
        for(int i=0;i<m_transformPoints.size();++i)if(QLineF(view,m_canvas->artworkViewTransform().map(m_transformPoints[i])).length()<12){m_handle=i;break;}
        m_drag=true;emit changed();return true;
    }
    if(t==Tool::VectorNudge){
        const auto &shot=doc()->activeShot();const auto &layer=shot.layers[doc()->activeLayer()];
        if(layer.kind!=LayerKind::Vector)return true;
        if(const auto*animation=layerAnimation(shot,layer.id);animation&&!animation->paths.isEmpty()){
            emit doc()->warning("Disable path animation on this layer before nudging its source geometry.");return true;
        }
        m_editObjects=editableObjectsAtFrame();m_nudgeLayerTransform=objectLayerTransform();
        m_drag=m_objects.beginLayerEdit("Nudge vector strokes");emit changed();return true;
    }
    if(t==Tool::VectorSimplify||t==Tool::VectorWidth){
        const auto &layer=doc()->activeShot().layers[doc()->activeLayer()];
        if(layer.kind!=LayerKind::Vector)return true;
        m_editObjects=editableObjectsAtFrame();m_lastBrushPoint=p;m_simplifyRegion={};
        m_drag=m_objects.beginLayerEdit(t==Tool::VectorSimplify?"Simplify paths with brush":"Brush path width");
        if(m_drag)applyVectorBrush(p,pressure);emit changed();return true;
    }
    if(t==Tool::Text||t==Tool::VectorNode){
        auto layer=doc()->activeShot().layers[doc()->activeLayer()];layer.objects=editableObjectsAtFrame();
        const auto objectView=objectViewTransform();
        const auto hit=((t==Tool::Text&&layer.kind!=LayerKind::Text)||(t==Tool::VectorNode&&layer.kind!=LayerKind::Vector))?QString():ObjectTools::hitTest(layer,p,localPickRadius(objectView,8));
        if(t==Tool::VectorNode)if(auto o=selectedObject()) {
            if(pathEditMode==PathEditMode::Nodes){
                for(const int i:visibleNodeIndices(*o)){const auto &n=o->nodes[i];const QPointF handles[]={n.anchor,n.inControl,n.outControl};
                    for(int j=0;j<3;++j)if(QLineF(view,objectView.map(o->transform.map(handles[j]))).length()<10){
                        m_node=i;m_control=(j==0&&(mods&Qt::AltModifier))?2:j;m_editObject=*o;m_drag=m_objects.beginEdit(o->id);emit changed();return true;
                    }
                }
                if((mods&Qt::ControlModifier)&&hit==o->id){auto value=*o;const int index=insertVectorNode(value,p,localPickRadius(objectView,12));if(index>=0&&value.nodes.size()!=o->nodes.size()&&ObjectTools::updateObject(doc(),value,"Add path node"))m_node=index;emit changed();return true;}
            } else if(pathEditMode==PathEditMode::Transform){
                const auto handles=vectorTransformHandles(*o);
                for(int i=0;i<handles.size();++i)if(QLineF(view,objectView.map(handles[i])).length()<9){m_handle=i;break;}
                if(QLineF(view,rotationHandle(*o)).length()<10)m_handle=8;
                if(m_handle>=0){m_node=-1;m_editObject=*o;m_drag=m_objects.beginEdit(o->id);emit changed();return true;}
            } else {
                for(const int i:visibleNodeIndices(*o))if(QLineF(view,objectView.map(vectorWidthHandle(*o,i))).length()<10){
                    m_node=i;m_control=3;m_editObject=*o;m_drag=m_objects.beginEdit(o->id);emit changed();return true;
                }
                if(hit==o->id){
                    auto value=*o;int index=-1;
                    if(selectedPathAnimated()) {
                        qreal best=std::numeric_limits<qreal>::max();
                        for(int i=0;i<value.nodes.size();++i){const qreal distance=QLineF(view,objectView.map(value.transform.map(value.nodes[i].anchor))).length();if(distance<best){best=distance;index=i;}}
                    }else index=insertVectorNode(value,p,localPickRadius(objectView,12));
                    if(index>=0){m_node=index;m_control=4;m_editObject=value;m_drag=m_objects.beginEdit(o->id);if(m_drag)m_objects.previewEdit(value);emit changed();return true;}
                }
            }
        }
        if(!hit.isEmpty()){selectObject(hit);if(auto o=selectedObject()){m_editObject=*o;m_drag=m_objects.beginEdit(hit);}return true;}
        if(t==Tool::Text){auto o=textStyle;o.id=EditableObject::create(ObjectKind::Text).id;o.position=p;o.transform={};o.fill=m_canvas->brush.color;
            if(layer.kind!=LayerKind::Text&&!ObjectTools::addLayer(doc(),LayerKind::Text))return true;
            selectObject(ObjectTools::addText(doc(),o));
            m_openTextOnRelease=!m_objectId.isEmpty();
        }else selectObject({});
        return true;
    }
    if(t==Tool::Polygon||t==Tool::PolygonSelect||t==Tool::Curve){
        if(m_points.size()>2&&QLineF(p,m_points.first()).length()*m_canvas->artworkZoom()<10){finishPath(true);return true;}
        m_points.append(p); if(t==Tool::Curve&&m_points.size()==4)finishPath();emit changed();return true;
    }
    if(t==Tool::VectorPen){
        if(m_nodes.size()>2&&QLineF(p,m_nodes.first().anchor).length()*m_canvas->artworkZoom()<10){finishPath(true);return true;}
        m_nodes.append(BezierNode::corner(p));m_drag=true;emit changed();return true;
    }
    if(t==Tool::Retouch){retouchSettings.color=m_canvas->brush.color;m_drag=m_raster.beginRetouch(p,pressure,retouchSettings);return true;}
    m_drag=true;
    if(t==Tool::VectorEraser||t==Tool::Eraser){m_eraseSweep=QPainterPath(p);m_eraseSweep.lineTo(p+QPointF(.001,0));}
    emit changed();return true;
}
bool DrawingToolController::move(QPointF view,Qt::KeyboardModifiers mods,qreal pressure) {
    if(!handlesTool())return false;
    auto t=m_canvas->pointerTool();m_current=m_canvas->artworkPoint(view);
    if(t==Tool::Text||t==Tool::VectorNode||t==Tool::VectorWidth||t==Tool::VectorSimplify||t==Tool::VectorEraser||t==Tool::Eraser)
        m_current=objectLayerTransform().inverted().map(m_current);
    if(!m_drag && !m_arcPending && m_points.isEmpty() && m_nodes.isEmpty())return true;
    if(t==Tool::Arc){m_canvas->update();return true;}
    if(mods&Qt::ShiftModifier){auto d=m_current-m_start;if(t==Tool::Rectangle||t==Tool::Ellipse||t==Tool::EllipseSelect){const qreal a=qMax(qAbs(d.x()),qAbs(d.y()));m_current=m_start+QPointF(std::copysign(a,d.x()),std::copysign(a,d.y()));}
        if(t==Tool::Line){const qreal length=std::hypot(d.x(),d.y()),a=std::round(std::atan2(d.y(),d.x())/(M_PI/4))*(M_PI/4);m_current=m_start+QPointF(std::cos(a)*length,std::sin(a)*length);}}
    if(m_drag){
        if(t==Tool::AdvancedTransform){auto points=m_transformPoints;auto delta=m_current-m_start;
            if(m_transform.puppet()){for(int i:m_selectedPuppetPins)if(i<points.size())points[i]+=delta;m_transform.setPuppetTargets(points);}
            else{if(m_handle>=0)points[m_handle]+=delta;else for(auto &point:points)point+=delta;if(warpMode)m_transform.setWarp(points);else m_transform.setQuad(QPolygonF(points));}}
        else if(t==Tool::Bucket&&encloseFillMode){
            if(QLineF(m_canvas->artworkViewTransform().map(m_enclosePath.currentPosition()),view).length()>=.75){
                if(m_enclosePath.elementCount()>=8192){cancel();emit doc()->warning("The enclosure is too detailed. Draw a shorter loop around the regions to fill.");return true;}
                m_enclosePath.lineTo(m_current);
            }
        }
        else if(t==Tool::VectorNudge)applyVectorNudge(m_current,pressure);
        else if(t==Tool::VectorSimplify||t==Tool::VectorWidth)applyVectorBrush(m_current,pressure);
        else if(t==Tool::VectorNode||t==Tool::Text){auto o=m_editObject;const auto delta=m_current-m_start;
            if(m_node>=0&&m_node<o.nodes.size()){auto inv=o.transform.inverted();const auto local=inv.map(m_current);auto &n=o.nodes[m_node];
                if(m_control==3||m_control==4){
                    if(o.widthFactors.isEmpty())o.widthFactors=QVector<qreal>(o.nodes.size(),1);
                    const auto normal=vectorNodeNormal(o,m_node);const auto localStart=inv.map(m_start);const qreal initial=o.strokeWidth*vectorNodeWidthFactor(o,m_node)/2;
                    const qreal halfWidth=m_control==3?initial+QPointF::dotProduct(local-localStart,normal):QPointF::dotProduct(local-n.anchor,normal);
                    if(m_control==3||QLineF(m_current,m_start).length()>.01)o.widthFactors[m_node]=qBound(.02,2*qAbs(halfWidth)/qMax(.1,o.strokeWidth),16.);
                }
                else if(m_control==0){auto d=local-n.anchor;n.anchor=local;n.inControl+=d;n.outControl+=d;}
                else if(m_control==1){n.inControl=local;if(mods&Qt::ShiftModifier)n.outControl=n.anchor*2-local;}
                else {n.outControl=local;if(mods&Qt::ShiftModifier)n.inControl=n.anchor*2-local;}}
            else if(t==Tool::VectorNode&&pathEditMode==PathEditMode::Transform)o.transform=vectorHandleTransform(m_editObject,m_handle,m_start,m_current,mods&Qt::ShiftModifier);
            else o.transform *= QTransform::fromTranslate(delta.x(),delta.y());m_objects.previewEdit(o);
        }else if(t==Tool::VectorPen&&!m_nodes.isEmpty()){auto &n=m_nodes.last();n.outControl=m_current;n.inControl=n.anchor*2-m_current;}
        else if(t==Tool::Retouch)m_raster.continueRetouch(m_current,pressure);
        else if(t==Tool::VectorEraser||t==Tool::Eraser)m_eraseSweep.lineTo(m_current);
    }
    emit changed();return true;
}
bool DrawingToolController::release(QPointF view,Qt::KeyboardModifiers mods,qreal pressure){
    if(!handlesTool())return false;
    if(m_openTextOnRelease){m_openTextOnRelease=false;const auto id=m_objectId;QTimer::singleShot(0,this,[this,id]{if(m_objectId==id&&m_canvas->tool()==Tool::Text)beginTextEdit();});return true;}
    if(!m_drag)return true;
    move(view,mods,pressure);auto t=m_canvas->pointerTool();m_drag=false;
    if(t==Tool::Arc){
        if(QLineF(m_start,m_current).length()*m_canvas->artworkZoom()>=2){
            m_arcStart=m_start;m_arcEnd=m_current;m_arcPending=true;
            m_current=(m_arcStart+m_arcEnd)/2;
        }
        emit changed();return true;
    }
    if(t==Tool::AdvancedTransform){emit changed();return true;}
    if(t==Tool::VectorPen){emit changed();return true;}
    if(t==Tool::Text||t==Tool::VectorNode||t==Tool::VectorSimplify||t==Tool::VectorWidth||t==Tool::VectorNudge)m_objects.finishEdit();
    else if(t==Tool::Retouch)m_raster.endRetouch();
    else if(t==Tool::VectorEraser||t==Tool::Eraser){ObjectTools::erasePath(doc(),m_eraseSweep,(t==Tool::Eraser?m_canvas->brush.size:retouchSettings.size)/2,vectorEraseMode);m_eraseSweep={};}
    else if(t==Tool::Bucket&&encloseFillMode){
        auto enclosure=m_enclosePath;m_enclosePath={};
        if(enclosure.elementCount()>=3){enclosure.closeSubpath();RasterTools::encloseFill(doc(),enclosure,m_canvas->brush.color,fillSettings);}
    }
    else if(t==Tool::Gradient)RasterTools::gradient(doc(),m_start,m_current,gradientStops,radialGradient);
    else if(t==Tool::EllipseSelect)doc()->combineRasterSelection(gesturePath(),m_combine);
    else if(t==Tool::Line||t==Tool::Rectangle||t==Tool::Ellipse)commitPath(gesturePath());
    emit changed();return true;
}
QPainterPath DrawingToolController::gesturePath()const{
    QPainterPath path;auto t=m_canvas->tool();
    if(t==Tool::Ellipse||t==Tool::EllipseSelect)path.addEllipse(QRectF(m_start,m_current).normalized());
    else if(t==Tool::Rectangle)path.addRect(QRectF(m_start,m_current).normalized());
    else if(t==Tool::Line){path.moveTo(m_start);path.lineTo(m_current);}
    else if(t==Tool::Arc){
        if(m_arcPending){path.moveTo(m_arcStart);path.quadTo(2*m_current-(m_arcStart+m_arcEnd)/2,m_arcEnd);}
        else if(m_drag){path.moveTo(m_start);path.lineTo(m_current);}
    }
    else if(t==Tool::VectorPen&&!m_nodes.isEmpty()){auto o=EditableObject::create();o.nodes=m_nodes;o.closed=closedPath;path=editableObjectPath(o);}
    else if(!m_points.isEmpty()){path.moveTo(m_points.first());if(t==Tool::Curve&&m_points.size()==4)path.cubicTo(m_points[1],m_points[2],m_points[3]);else{for(int i=1;i<m_points.size();++i)path.lineTo(m_points[i]);if(t==Tool::Polygon||t==Tool::PolygonSelect)path.lineTo(m_current);}if(t==Tool::Polygon||t==Tool::PolygonSelect)path.closeSubpath();}
    return path;
}
void DrawingToolController::commitPath(const QPainterPath&path,const QVector<BezierNode>&provided,bool closed){
    if(path.isEmpty())return;
    auto nodes=provided; // Copy before a new layer emits selectionChanged and cancels pending canvas points.
    const bool vector=m_canvas->tool()==Tool::VectorPen||doc()->activeShot().layers[doc()->activeLayer()].kind==LayerKind::Vector;
    if(!vector){RasterTools::drawPath(doc(),path,shapeFill?fillColor:QColor(),shapeStroke?m_canvas->brush.color:QColor(),strokeWidth);return;}
    if(doc()->activeShot().layers[doc()->activeLayer()].kind!=LayerKind::Vector&&!ObjectTools::addLayer(doc(),LayerKind::Vector))return;
    if(nodes.isEmpty())for(int i=0;i<path.elementCount();++i){auto e=path.elementAt(i);if(e.isMoveTo()||e.isLineTo())nodes.append(BezierNode::corner({e.x,e.y}));else if(e.type==QPainterPath::CurveToElement&&i+2<path.elementCount()){auto c2=path.elementAt(++i),end=path.elementAt(++i);if(!nodes.isEmpty())nodes.last().outControl={e.x,e.y};auto n=BezierNode::corner({end.x,end.y});n.inControl={c2.x,c2.y};nodes.append(n);}}
    const auto t=m_canvas->tool();closed=closed||t==Tool::Rectangle||t==Tool::Ellipse||t==Tool::Polygon;
    if (closed && nodes.size()>2 && QLineF(nodes.first().anchor,nodes.last().anchor).length()<1e-6) {
        nodes.first().inControl=nodes.last().inControl; nodes.removeLast();
    }
    selectObject(ObjectTools::addPath(doc(),nodes,closed,shapeStroke?m_canvas->brush.color:QColor(),strokeWidth,shapeFill?fillColor:QColor()));
}
void DrawingToolController::finishPath(bool close){
    const auto t=m_canvas->tool();
    if(t==Tool::Arc){
        if(m_arcPending){const auto path=gesturePath();m_arcPending=false;commitPath(path);}
        m_drag=false;emit changed();return;
    }
    if(!m_points.isEmpty())m_current=m_points.last();
    if(t==Tool::PolygonSelect&&m_points.size()>=3)doc()->combineRasterSelection(gesturePath(),m_combine);
    else if(t==Tool::VectorPen&&m_nodes.size()>=2)commitPath(gesturePath(),m_nodes,close||closedPath);
    else if((t==Tool::Polygon&&m_points.size()>=3)||(t==Tool::Curve&&m_points.size()==4))commitPath(gesturePath(),{},close);
    m_points.clear();m_nodes.clear();m_drag=false;emit changed();
}
void DrawingToolController::finishGesture(){
    if(m_focusingTextEditor)return;
    finishTextEdit();
    if(m_raster.retouchActive())m_raster.endRetouch();
    if(m_objects.editActive()){if(m_canvas->tool()==Tool::VectorNudge&&m_drag)m_objects.cancelEdit();else m_objects.finishEdit();}
    m_enclosePath={};m_drag=false;m_styleEdit=false;emit changed();
}
void DrawingToolController::interruptGesture(){
    // Retouch changes painted pixels as contact moves. A missing release keeps
    // that work, like an ordinary brush; incomplete shape/node previews still
    // cancel. Escape remains an explicit cancel for either kind of gesture.
    if(m_raster.retouchActive())finishGesture();
    else cancel();
}
void DrawingToolController::cancel(){
    finishTextEdit(true);m_openTextOnRelease=false;
    m_raster.cancelRetouch();m_objects.cancelEdit();m_transform.cancel();m_selectedPuppetPins.clear();m_drag=false;m_arcPending=false;m_styleEdit=false;m_editObjects.clear();m_simplifyRegion={};m_points.clear();m_nodes.clear();m_eraseSweep={};m_enclosePath={};
    if(auto o=selectedObject()){strokeWidth=o->strokeWidth;objectOpacity=o->opacity;if(m_node>=o->nodes.size())m_node=-1;}
    emit changed();
}
bool DrawingToolController::key(int key,Qt::KeyboardModifiers mods){
    if(key==Qt::Key_Escape&&active()){cancel();return true;}
    if(key==Qt::Key_Return||key==Qt::Key_Enter){if(m_transform.active())m_transform.apply();else if(m_arcPending||!m_points.isEmpty()||!m_nodes.isEmpty())finishPath(mods&Qt::ShiftModifier);else return false;return true;}
    if(key==Qt::Key_Delete||key==Qt::Key_Backspace){
        if(m_transform.puppet()){removeSelectedPuppetPins();return true;}
        if(m_arcPending){cancel();return true;}
        if(!m_nodes.isEmpty()){m_nodes.removeLast();emit changed();return true;}if(!m_points.isEmpty()){m_points.removeLast();emit changed();return true;}
        if((m_canvas->tool()==Tool::VectorNode||m_canvas->tool()==Tool::Text)&&!m_objectId.isEmpty()){
            if(auto o=selectedObject();o&&pathEditMode==PathEditMode::Nodes&&m_node>=0&&m_node<o->nodes.size()&&o->nodes.size()>(o->closed?3:2)){o->nodes.removeAt(m_node);if(!o->widthFactors.isEmpty())o->widthFactors.removeAt(m_node);m_node=-1;ObjectTools::updateObject(doc(),*o,"Delete node");}else ObjectTools::removeObject(doc(),m_objectId);emit changed();return true;}
    }return false;
}
std::optional<QPainterPath> DrawingToolController::selectionPreview()const{
    const auto tool=m_canvas->tool();
    if(!doc()->isOpen() || !((tool==Tool::EllipseSelect&&m_drag)||(tool==Tool::PolygonSelect&&m_points.size()>=2)))return {};
    auto path=gesturePath();
    if(doc()->rasterSelection().active){const auto &previous=doc()->rasterSelection().path;
        // Preview the incoming gesture for complex masks; the document combines
        // it once on release without repeating costly geometry on every move.
        if(previous.elementCount()+path.elementCount()<=4096){
        switch(m_combine){case SelectionCombine::Add:path=previous.united(path);break;
        case SelectionCombine::Subtract:path=previous.subtracted(path);break;
        case SelectionCombine::Intersect:path=previous.intersected(path);break;
        case SelectionCombine::Replace:break;}
        }
    }
    if(doc()->drawingBounds().contains(path.controlPointRect()))return path;
    QPainterPath bounds;bounds.addRect(doc()->drawingBounds());return path.intersected(bounds);
}
QPainterPath DrawingToolController::cursorPath()const{
    QPainterPath p;if(!m_canvas->m_pointerInside||m_canvas->m_space||m_canvas->m_dragging)return p;
    const auto tool=m_canvas->pointerTool();const bool vectorEraser=doc()->isOpen() && doc()->activeShot().layers[doc()->activeLayer()].kind==LayerKind::Vector && tool==Tool::Eraser;
    if(vectorEraser||tool==Tool::Retouch||tool==Tool::VectorEraser||tool==Tool::VectorSimplify||tool==Tool::VectorWidth||tool==Tool::VectorNudge){const auto center=m_canvas->m_pointerPosition;
        const qreal size=tool==Tool::VectorNudge?nudgeRadius*2:(tool==Tool::VectorSimplify||tool==Tool::VectorWidth)?widthBrushRadius*2:vectorEraser?m_canvas->brush.size:retouchSettings.size;
        const qreal radius=qMax(2.,size*m_canvas->artworkZoom()/2);p.addEllipse(center,radius,radius);}return p;
}
void DrawingToolController::paint(QPainter &p)const{
    if(!doc()->isOpen())return; p.save();p.setRenderHint(QPainter::Antialiasing);const auto view=m_canvas->artworkViewTransform();
    QPen pen(QColor(ui::accent),1.5);pen.setCosmetic(true);p.setPen(pen);p.setBrush(Qt::NoBrush);
    const auto handle=[&](QPointF point,bool square=false){auto v=view.map(point);p.setBrush(QColor(ui::panel));if(square)p.drawRect(QRectF(v-QPointF(4,4),QSizeF(8,8)));else p.drawEllipse(v,4,4);p.setBrush(Qt::NoBrush);};
    if(m_transform.active()){
        if(m_transform.puppet()){
            const auto &mesh=m_transform.puppetMesh();
            if(showPuppetMesh){
                QPen meshPen(QColor(ui::muted),.7);meshPen.setCosmetic(true);p.setPen(meshPen);
                QSet<QPair<int,int>> edges;
                for(const auto &triangle:mesh.triangles)for(int i=0;i<3;++i){int a=triangle[i],b=triangle[(i+1)%3];if(a>b)std::swap(a,b);if(edges.contains({a,b}))continue;edges.insert({a,b});p.drawLine(view.map(mesh.target[a]),view.map(mesh.target[b]));}
            }
            for(int i=0;i<m_transform.pins().size();++i){
                const auto point=view.map(m_transform.pins()[i].target);const bool selected=m_selectedPuppetPins.contains(i);
                QPen pinPen(QColor(ui::text),1.5);pinPen.setCosmetic(true);p.setPen(pinPen);p.setBrush(QColor(selected?ui::accent:ui::panel));p.drawEllipse(point,6,6);
                p.setBrush(QColor(selected?ui::canvas:ui::text));p.drawEllipse(point,1.5,1.5);
            }
            p.setBrush(Qt::NoBrush);p.setPen(pen);
        }else{
        const auto points=warpMode?m_transform.controlPoints():QVector<QPointF>(m_transform.quad());
        if(warpMode){for(int i=0;i<3;++i){QPolygonF row,col;for(int j=0;j<3;++j){row<<view.map(points[i*3+j]);col<<view.map(points[j*3+i]);}p.drawPolyline(row);p.drawPolyline(col);}}
        else p.drawPolygon(view.map(QPolygonF(points)));for(auto point:points)handle(point,true);
        }
    }
    if(m_drag||m_arcPending||!m_points.isEmpty()||!m_nodes.isEmpty()){
        if(m_canvas->tool()==Tool::VectorNudge){p.drawEllipse(view.map(m_start),nudgeRadius*m_canvas->artworkZoom(),nudgeRadius*m_canvas->artworkZoom());p.drawLine(view.map(m_start),view.map(m_current));}
        else if(m_canvas->tool()==Tool::Bucket&&encloseFillMode){
            auto enclosure=m_enclosePath;enclosure.closeSubpath();
            auto enclosurePen=pen;enclosurePen.setStyle(Qt::DashLine);p.setPen(enclosurePen);
            auto tint=QColor(ui::accent);tint.setAlpha(32);p.setBrush(tint);p.drawPath(view.map(enclosure));p.setBrush(Qt::NoBrush);p.setPen(pen);
        }
        else if(m_canvas->tool()==Tool::Gradient){p.drawLine(view.map(m_start),view.map(m_current));handle(m_start);handle(m_current);}
        else if(m_canvas->tool()==Tool::VectorEraser||m_canvas->tool()==Tool::Eraser)p.drawPath(view.map(m_eraseSweep));
        else if(m_canvas->tool()!=Tool::EllipseSelect&&m_canvas->tool()!=Tool::PolygonSelect)p.drawPath(view.map(gesturePath()));
        for(auto point:m_points)handle(point);
        if(m_canvas->tool()==Tool::Arc){
            if(m_arcPending){handle(m_arcStart);handle(m_arcEnd);handle(m_current,true);}
            else {handle(m_start);handle(m_current);}
        }
        for(auto n:m_nodes){p.drawLine(view.map(n.inControl),view.map(n.outControl));handle(n.anchor,true);handle(n.inControl);handle(n.outControl);}
    }
    if(m_canvas->tool()==Tool::VectorNode||m_canvas->tool()==Tool::Text)if(auto o=selectedObject()){
        const auto objectView=objectViewTransform();
        const auto objectHandle=[&](QPointF point,bool square=false){auto v=objectView.map(point);p.setBrush(QColor(ui::panel));if(square)p.drawRect(QRectF(v-QPointF(4,4),QSizeF(8,8)));else p.drawEllipse(v,4,4);p.setBrush(Qt::NoBrush);};
        if(m_canvas->tool()==Tool::Text||pathEditMode==PathEditMode::Transform){
            const auto handles=vectorTransformHandles(*o);QPolygonF polygon;for(int i:{0,2,4,6})polygon<<objectView.map(handles[i]);p.drawPolygon(polygon);
            for(auto point:handles)objectHandle(point,true);const auto rotation=rotationHandle(*o);p.drawLine(objectView.map(handles[1]),rotation);p.setBrush(QColor(ui::panel));p.drawEllipse(rotation,5,5);p.setBrush(Qt::NoBrush);
        }else {
            p.drawPath(objectView.map(editableObjectPath(*o)));
            for(const int i:visibleNodeIndices(*o)){const auto n=o->nodes[i];auto map=[&](QPointF pt){return o->transform.map(pt);};
                if(pathEditMode==PathEditMode::Width){const auto widthPoint=vectorWidthHandle(*o,i);p.drawLine(objectView.map(map(n.anchor)),objectView.map(widthPoint));objectHandle(widthPoint);}
                else {objectHandle(map(n.anchor),true);if(i==m_node||o->nodes.size()<=16){p.drawLine(objectView.map(map(n.inControl)),objectView.map(map(n.outControl)));objectHandle(map(n.inControl));objectHandle(map(n.outControl));}}
            }
        }
    }
    p.restore();
}
}
