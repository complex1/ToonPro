#include "mask_tool.h"
#include "canvas.h"
#include "camera_tool.h"
#include "keyframe_transform_tool.h"
#include "drawing_tool_controller.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLineF>
#include <QScopeGuard>
#include <QTabletEvent>
#include <cmath>
#include <new>

namespace toon {
MaskTool::MaskTool(Canvas *canvas):QObject(canvas),m_canvas(canvas) {
    canvas->installEventFilter(this);
    connect(canvas,&Canvas::toolChanged,this,[this]{setEnabled(false);});
    connect(canvas->document(),&Document::loaded,this,[this]{m_enabled=m_drag=m_tablet=false;m_mask={};m_workingTiles.clear();emit changed();});
    connect(canvas->document(),&Document::effectsEditResolutionRequested,this,&MaskTool::finish);
    connect(canvas->document(),&Document::selectionChanged,this,[this]{finish();if(m_enabled&&!unavailableReason().isEmpty())setEnabled(false);});
    connect(canvas->document(),&Document::changed,this,[this]{
        if(m_drag&&!m_canvas->document()->effectsEditActive()){m_drag=m_tablet=false;m_mask={};m_workingTiles.clear();emit changed();}
        if(m_enabled)m_canvas->refreshPointerCursor();
    });
}
QTransform MaskTool::worldTransform() const {
    auto *doc=m_canvas->document();
    if(!doc->isOpen())return {};
    const auto &shot=m_canvas->shot();const auto &layer=shot.layers[doc->activeLayer()];
    const auto mask=layer.mask?(doc->keyframeSourceEditing()?*layer.mask:
        evaluateAnimatedLayerMask(shot,layer.id,doc->currentFrame(),*layer.mask)):LayerMask{};
    const auto local=layerMaskTransform(mask);
    return doc->keyframeSourceEditing()?local:local*worldLayerTransform(shot,layer.id,doc->currentFrame());
}
QString MaskTool::unavailableReason() const {
    if(m_canvas->animationPlayback())return "Pause playback to paint a layer mask.";
    auto *doc=m_canvas->document();const auto reason=doc->effectsEditReason();if(!reason.isEmpty())return reason;
    const auto &layer=m_canvas->shot().layers[doc->activeLayer()];
    if(!layer.mask)return "Add a layer mask in Effects first.";
    const auto mask=doc->keyframeSourceEditing()?*layer.mask:
        evaluateAnimatedLayerMask(m_canvas->shot(),layer.id,doc->currentFrame(),*layer.mask);
    if(!mask.enabled)return "Enable the layer mask at this frame before painting it.";
    if(mask.opacity<=0)return "Increase mask strength at this frame before painting it.";
    if(!worldTransform().isInvertible())return "A zero-scale layer or mask cannot be mask-painted.";
    if(doc->editingGestureActive()&&!m_drag)return "Finish the current edit before painting a mask.";
    return {};
}
void MaskTool::setEnabled(bool value) {
    if(m_enabled==value)return;
    finish();
    if(value) {
        m_canvas->finishStroke();m_canvas->drawingTools()->finishGesture();
        emit m_canvas->document()->effectsEditResolutionRequested();m_canvas->document()->commitEffectsEdit();
        if(!m_canvas->resolvePendingTransform())return;
        const auto reason=unavailableReason();if(!reason.isEmpty()){emit m_canvas->document()->warning(reason);return;}
        m_canvas->cameraTool()->setEnabled(false);m_canvas->keyframeTransformTool()->setEnabled(false);
    }
    m_enabled=value;m_canvas->refreshPointerCursor();emit changed();
}
void MaskTool::setRevealing(bool value){finish();if(m_reveal!=value){m_reveal=value;emit changed();}}
void MaskTool::setSize(qreal value){if(!std::isfinite(value))return;finish();m_size=qBound(1.,value,512.);m_canvas->update();emit changed();}
void MaskTool::setStrength(qreal value){if(!std::isfinite(value))return;finish();m_strength=qBound(0.,value,1.);emit changed();}
void MaskTool::setHardness(qreal value){if(!std::isfinite(value))return;finish();m_hardness=qBound(0.,value,1.);emit changed();}
QPainterPath MaskTool::cursorPath() const {
    QPainterPath path;
    if(!m_enabled||!m_canvas->m_pointerInside||m_canvas->m_space||!unavailableReason().isEmpty())return path;
    const auto point=worldTransform().inverted().map(m_canvas->artworkPoint(m_canvas->m_pointerPosition));
    path.addEllipse(point,m_size/2,m_size/2);
    return (worldTransform()*m_canvas->artworkViewTransform()).map(path);
}
bool MaskTool::dab(LayerMask &mask,QPointF point,qreal pressure,QString *error) {
    const auto fail=[error](const QString &message){if(error)*error=message;return false;};
    if(!std::isfinite(point.x())||!std::isfinite(point.y())||!std::isfinite(pressure))return fail("The mask stroke has invalid coordinates or pressure.");
    const qreal radius=m_size/2.;const auto bounds=QRectF(point-QPointF(radius,radius),QSizeF(m_size,m_size))
        .intersected(m_paintBounds).toAlignedRect();
    if(bounds.isEmpty()||pressure<=0||m_strength<=0)return true;
    const int x0=int(std::floor(bounds.left()/qreal(tileSize))),x1=int(std::floor(bounds.right()/qreal(tileSize)));
    const int y0=int(std::floor(bounds.top()/qreal(tileSize))),y1=int(std::floor(bounds.bottom()/qreal(tileSize)));
    for(int tx=x0;tx<=x1;++tx)for(int ty=y0;ty<=y1;++ty) {
        const TileKey key{tx,ty};
        const auto coverage=m_selectionActive?m_selectionCache.tile(key):RasterSelectionTile{};
        if(m_selectionActive&&coverage.coverage==RasterSelectionTile::Coverage::Empty)continue;
        if(m_selectionActive&&coverage.coverage==RasterSelectionTile::Coverage::Invalid)return fail("There is not enough memory to paint the mask.");
        QImage image=mask.tiles.value(key);
        const int target=m_reveal!=m_inverted?255:0;
        if(image.isNull()&&mask.defaultValue==target)continue;
        if(image.isNull()&&m_otherMaskTiles+mask.tiles.size()>=maxProjectMaskTiles)
            return fail("The project mask tile limit was reached.");
        if(image.isNull()){image=QImage(tileSize,tileSize,QImage::Format_Grayscale8);image.fill(mask.defaultValue);}
        if(image.isNull())return fail("There is not enough memory to paint the mask.");
        auto working=m_workingTiles.find(key);
        if(working==m_workingTiles.end()) {
            // At most 1024 × 256² floats (256 MiB), independent of pointer
            // distance. Allocation failure cancels the complete stroke.
            if(m_workingTiles.size()>=maxProjectMaskTiles)return fail("The mask stroke exceeds its working-memory limit.");
            try {
                QVector<float> pixels(tileSize*tileSize);
                for(int y=0;y<tileSize;++y){const auto *row=image.constScanLine(y);for(int x=0;x<tileSize;++x)pixels[y*tileSize+x]=row[x];}
                working=m_workingTiles.insert(key,std::move(pixels));
            } catch(const std::bad_alloc &) {return fail("There is not enough memory to paint the mask.");}
        }
        const QPoint origin(tx*tileSize,ty*tileSize);const auto area=bounds.intersected(QRect(origin,QSize(tileSize,tileSize))).translated(-origin);
        bool changed=false;
        for(int y=area.top();y<=area.bottom();++y) {
            auto *row=image.scanLine(y);const auto selected=m_selectionActive&&!coverage.mask.isNull()?coverage.mask.constScanLine(y):nullptr;
            if(!row)return fail("There is not enough memory to paint the mask.");
            for(int x=area.left();x<=area.right();++x) {
                const qreal distance=std::hypot(origin.x()+x+.5-point.x(),origin.y()+y+.5-point.y());
                const qreal edge=qMax(1.,radius*(1-m_hardness));
                const qreal alpha=qBound(0.,(radius-distance)/edge,1.)*m_strength*qBound(0.,pressure,1.)*(selected?selected[x]/255.:1.);
                auto &precise=working.value()[y*tileSize+x];
                precise=float(precise+(target-precise)*alpha);
                const int value=qBound(0,qRound(precise),255);
                if(value!=row[x]){row[x]=uchar(value);changed=true;}
            }
        }
        if(changed)mask.tiles.insert(key,image);
    }
    return true;
}
bool MaskTool::press(QPointF position,qreal pressure) {
    auto *doc=m_canvas->document();
    m_canvas->setFocus();m_canvas->trackPointer(position,pressure);
    const auto reason=unavailableReason();if(!reason.isEmpty()){emit doc->warning(reason);return true;}
    if(!doc->beginEffectsEdit({},m_reveal?"Reveal layer mask":"Hide layer mask"))return true;
    const auto &layer=m_canvas->shot().layers[doc->activeLayer()];m_layerId=layer.id;m_mask=*layer.mask;
    m_inverted=(doc->keyframeSourceEditing()?m_mask:
        evaluateAnimatedLayerMask(m_canvas->shot(),layer.id,doc->currentFrame(),m_mask)).inverted;
    m_workingTiles.clear();m_otherMaskTiles=projectMaskTileCount(doc->project(),m_layerId);
    m_inverse=worldTransform().inverted();m_last=m_inverse.map(m_canvas->artworkPoint(position));m_pressure=pressure;m_spacing=0;m_drag=true;
    m_paintBounds=m_inverse.mapRect(doc->drawingBounds()).intersected(QRectF(-32768,-32768,65792,65792));
    const auto &selection=doc->rasterSelection();m_selectionActive=selection.active;
    m_selectionCache.reset(selection,worldTransform());
    QString error;
    if(!dab(m_mask,m_last,pressure,&error)){emit doc->warning(error);cancel();return true;}
    if(!doc->previewLayerMask(m_mask)){cancel();return true;}
    emit changed();return true;
}
void MaskTool::move(QPointF position,qreal pressure) {
    m_canvas->trackPointer(position,pressure);if(!m_drag)return;
    auto *doc=m_canvas->document();
    if(!doc->effectsEditActive()||m_canvas->shot().layers[doc->activeLayer()].id!=m_layerId){cancel();return;}
    const auto point=m_inverse.map(m_canvas->artworkPoint(position));const qreal length=QLineF(m_last,point).length();
    const qreal step=qMax(.5,m_size*.1);
    if(!std::isfinite(length)||length/step>10000){emit doc->warning("The mask stroke moved beyond supported drawing coordinates.");cancel();return;}
    auto candidate=m_mask;
    for(qreal distance=step-m_spacing;distance<=length;distance+=step) {
        const qreal t=length>0?distance/length:0;
        QString error;
        if(!dab(candidate,m_last+(point-m_last)*t,m_pressure+(pressure-m_pressure)*t,&error)){emit doc->warning(error);cancel();return;}
    }
    m_spacing=std::fmod(m_spacing+length,step);m_last=point;m_pressure=pressure;
    if(!doc->previewLayerMask(candidate)){cancel();return;}m_mask=std::move(candidate);
}
void MaskTool::finish(){m_tablet=false;if(!m_drag)return;m_drag=false;m_canvas->document()->commitEffectsEdit();m_mask={};m_workingTiles.clear();emit changed();}
void MaskTool::finishTabletContact(){if(m_tablet)finish();}
void MaskTool::cancel(){m_tablet=false;if(!m_drag)return;m_drag=false;m_canvas->document()->cancelEffectsEdit();m_mask={};m_workingTiles.clear();emit changed();}
bool MaskTool::eventFilter(QObject *watched,QEvent *event) {
    if(!m_enabled || watched != m_canvas)return false;
    if(event->type()==QEvent::FocusOut){finish();return false;}
    if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){if(m_drag)cancel();else setEnabled(false);return true;}
    // Match the ordinary brush: temporary panning begins between strokes,
    // never in the middle of a held mask stroke (including its release).
    if(m_drag&&(event->type()==QEvent::KeyPress||event->type()==QEvent::KeyRelease)
        &&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Space){event->accept();return true;}
    if(m_drag&&event->type()==QEvent::MouseButtonPress&&static_cast<QMouseEvent*>(event)->button()==Qt::MiddleButton){finish();return false;}
    if(m_canvas->m_space||m_canvas->m_dragging)return false;
    if(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseMove||event->type()==QEvent::MouseButtonRelease) {
        auto *e=static_cast<QMouseEvent*>(event);
        if(e->source()!=Qt::MouseEventNotSynthesized||m_tablet)return true;
        m_canvas->trackPointer(e->position());
        if(event->type()==QEvent::MouseMove){if(m_drag){if(e->buttons()&Qt::LeftButton)move(e->position(),1);else finish();}return true;}
        if(e->button()!=Qt::LeftButton)return false;
        if(event->type()==QEvent::MouseButtonPress)return press(e->position(),1);
        move(e->position(),1);finish();return true;
    }
    if(event->type()==QEvent::TabletPress||event->type()==QEvent::TabletMove||event->type()==QEvent::TabletRelease) {
        auto *e=static_cast<QTabletEvent*>(event);
        // This filter consumes the event before Canvas::tabletEvent runs.
        const auto syncCursor = qScopeGuard([this, e] { m_canvas->syncTabletCursor(e->position()); });
        m_canvas->trackPointer(e->position(),e->pressure());
        if(event->type()==QEvent::TabletPress&&e->button()==Qt::LeftButton){finishTabletContact();m_tablet=true;press(e->position(),e->pressure());}
        else if(event->type()==QEvent::TabletMove&&m_tablet){if(e->buttons()&Qt::LeftButton)move(e->position(),e->pressure());else finish();}
        else if(event->type()==QEvent::TabletRelease&&e->button()==Qt::LeftButton&&m_tablet){move(e->position(),m_pressure);finish();}
        event->accept();return true;
    }
    return false;
}
} // namespace toon
