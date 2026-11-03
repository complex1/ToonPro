#include "clipboard_tools.h"
#include "animation.h"
#include "keyframe_render.h"
#include "editable_object.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QMimeData>
#include <cmath>
#include <new>
#include <memory>

namespace toon {
namespace {
constexpr auto placementMime = "application/x-toon-pro-image-placement";
constexpr qint64 maxClipboardPixels = 64000000;
bool failure(Document *document, QString *error, const QString &message) {
    if(error)*error=message;if(document)emit document->warning(message);return false;
}
bool ready(Document *document,QString *error) {
    if(!document || !document->isOpen())return failure(document,error,"Open a project first.");
    const auto reason=keyframeRasterEditReason(document);if(!reason.isEmpty())return failure(document,error,reason);
    if(document->editingGestureActive() || document->layerPropertyEditActive())return failure(document,error,"Finish the current gesture before editing the clipboard.");
    const auto &shot=document->activeShot();
    if(!layerPresentAtFrame(shot,shot.layers[document->activeLayer()].id,document->currentFrame()))
        return failure(document,error,"Choose a drawing exposed at the current frame.");
    return true;
}
bool originalPixelsAvailable(Document *document, QString *error) {
    if (document->keyframeSourceEditing()) return true;
    const auto &shot = document->activeShot();
    const auto *animation = layerAnimation(shot, shot.layers[document->activeLayer()].id);
    if (animation && !animation->paths.isEmpty())
        return failure(document, error, "Enable Source artwork to copy the original pixels of an animated path layer.");
    return true;
}
QRgb multiply(QRgb color,int amount) {
    return qRgba((qRed(color)*amount+127)/255,(qGreen(color)*amount+127)/255,(qBlue(color)*amount+127)/255,(qAlpha(color)*amount+127)/255);
}
bool nonempty(const QImage &image) {
    for(int y=0;y<image.height();++y){const auto row=reinterpret_cast<const QRgb *>(image.constScanLine(y));for(int x=0;x<image.width();++x)if(qAlpha(row[x]))return true;}return false;
}
int coordinate(int value){return int(std::floor(value/qreal(tileSize)));}
QRectF boundsFor(const Project &project,int shotIndex,int index) {
    const auto &shot=project.shots[shotIndex];const auto &sheet=shot.sheet;const qreal extra=sheet.overscan;
    QRectF bounds(-extra,-extra,project.width+extra*2,project.height+extra*2);
    if(layerIsAnnotation(shot.layers,index))bounds.adjust(-sheet.left,-sheet.top,sheet.right,sheet.bottom);return bounds;
}
bool extract(const RasterLayer &layer,const RasterSelection &selection,QImage *result,QPoint *origin,RasterLayer *cut,QString *error) {
    const auto bounds=selectedPixelBounds(layer,selection.active?&selection.path:nullptr,selection.featherRadius).toAlignedRect();
    if(bounds.isEmpty()){*error="There are no painted pixels in this selection.";return false;}
    if(qint64(bounds.width())*bounds.height()>maxClipboardPixels){*error="The clipboard area exceeds 64 million pixels. Select a smaller area.";return false;}
    QImage image(bounds.size(),QImage::Format_ARGB32_Premultiplied);if(image.isNull()){*error="There is not enough memory for the clipboard image.";return false;}image.fill(Qt::transparent);
    RasterSelectionCache cache(selection.active?&selection.path:nullptr,selection.featherRadius);
    if(cut)*cut=layer;
    for(auto tile=layer.tiles.cbegin();tile!=layer.tiles.cend();++tile) {
        const QPoint tileOrigin(tile.key().first*tileSize,tile.key().second*tileSize);
        const auto part=bounds.intersected(QRect(tileOrigin,QSize(tileSize,tileSize)));if(part.isEmpty())continue;
        const auto coverage=cache.tile(tile.key());
        if(coverage.coverage==RasterSelectionTile::Coverage::Invalid){*error="There is not enough memory for the selection mask.";return false;}
        if(coverage.coverage==RasterSelectionTile::Coverage::Empty)continue;
        QImage remainder=cut?tile->copy():QImage();
        for(int y=part.top();y<=part.bottom();++y){
            const auto source=reinterpret_cast<const QRgb *>(tile->constScanLine(y-tileOrigin.y()));
            const auto mask=coverage.mask.isNull()?nullptr:coverage.mask.constScanLine(y-tileOrigin.y());
            auto target=reinterpret_cast<QRgb *>(image.scanLine(y-bounds.top()));
            auto remain=cut?reinterpret_cast<QRgb *>(remainder.scanLine(y-tileOrigin.y())):nullptr;
            for(int x=part.left();x<=part.right();++x){const int local=x-tileOrigin.x(),alpha=mask?mask[local]:255;
                target[x-bounds.left()]=multiply(source[local],alpha);if(remain)remain[local]=multiply(source[local],255-alpha);}
        }
        if(cut){if(nonempty(remainder))cut->tiles.insert(tile.key(),remainder);else cut->tiles.remove(tile.key());}
    }
    *result=image;*origin=bounds.topLeft();return true;
}
bool parentLocked(const QVector<RasterLayer> &layers,int active) {
    QString id=layers[active].group?layers[active].id:layers[active].parentId;
    for(int depth=0;!id.isEmpty() && depth<10;++depth){bool found=false;for(const auto &layer:layers)if(layer.id==id){if(layer.locked)return true;id=layer.parentId;found=true;break;}if(!found)return true;}
    return !id.isEmpty();
}
// Build a layer candidate; callers commit only after all budget/bounds checks pass.
bool placeImage(const Project &project,int shotIndex,int active,const QImage &source,QPointF topLeft,bool newLayer,const QString &name,
                const RasterSelection &selection,Project *result,QString *selected,QString *error) {
    if(source.isNull() || qint64(source.width())*source.height()>maxClipboardPixels){*error="Choose an image of up to 64 million pixels.";return false;}
    if(!std::isfinite(topLeft.x()) || !std::isfinite(topLeft.y()) || std::abs(topLeft.x())>32768 || std::abs(topLeft.y())>32768){*error="The image position is invalid.";return false;}
    const auto &layers=project.shots[shotIndex].layers;const auto &activeLayer=layers[active];
    if(!newLayer && activeLayer.kind==LayerKind::Symbol){*error="Paste into a new drawing layer above the symbol, or edit its contents first.";return false;}
    if(!newLayer && activeLayer.kind==LayerKind::Scene3D){*error="Paste into a new drawing layer above the 3D reference.";return false;}
    const auto &shot=project.shots[shotIndex];const auto owner=animationOwner(shot,activeLayer.id);
    if(newLayer&&!owner.trackId.isEmpty()&&owner.drawingId.isEmpty()){
        *error="Create a drawing at the current frame before adding an image to this track.";return false;
    }
    const int layerLimit=shot.animationTracks.isEmpty()?maxDrawingLayers:maxAnimationLayers;
    if(newLayer&&layers.size()>=layerLimit){*error=QString("This shot has reached its %1-layer limit.").arg(layerLimit);return false;}
    if(newLayer?parentLocked(layers,active):(!layerCanPaint(layers,active) || activeLayer.alphaLocked)){
        *error=newLayer?"Unlock the destination folder before pasting an image.":"Choose an unlocked raster layer and turn off alpha lock before pasting.";return false;}
    auto layer=newLayer?RasterLayer::create(name.trimmed().isEmpty()?"Pasted image":name):activeLayer;
    if(newLayer){layer.parentId=activeLayer.group?activeLayer.id:activeLayer.parentId;layer.annotation=activeLayer.annotation;}
    auto image=source.convertToFormat(QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(1);
    // Placement is on pixel boundaries, preserving transparent pixels exactly.
    const QPoint origin(qRound(topLeft.x()),qRound(topLeft.y()));const QRect area(origin,image.size());
    QRectF extent=boundsFor(project,shotIndex,active);
    if(!extent.contains(QRectF(area))){*error="The image extends beyond the drawing area. Resize it or choose a position inside the canvas and overscan.";return false;}
    const int used=projectTileCount(project);
    const int allowance=maxProjectTiles-used+(newLayer?0:layer.tiles.size());
    RasterSelectionCache cache(!newLayer && selection.active?&selection.path:nullptr,selection.featherRadius);
    for(int ty=coordinate(area.top());ty<=coordinate(area.bottom());++ty)for(int tx=coordinate(area.left());tx<=coordinate(area.right());++tx){
        const TileKey key(tx,ty);const QPoint tileOrigin(tx*tileSize,ty*tileSize);const auto part=area.intersected(QRect(tileOrigin,QSize(tileSize,tileSize)));
        const auto coverage=cache.tile(key);if(coverage.coverage==RasterSelectionTile::Coverage::Empty)continue;
        if(coverage.coverage==RasterSelectionTile::Coverage::Invalid){*error="There is not enough memory for the selection.";return false;}
        QImage patch(part.size(),QImage::Format_ARGB32_Premultiplied);if(patch.isNull()){*error="There is not enough memory to paste the image.";return false;}
        for(int y=0;y<part.height();++y){const auto row=reinterpret_cast<const QRgb *>(image.constScanLine(part.y()+y-origin.y()));
            const auto mask=coverage.mask.isNull()?nullptr:coverage.mask.constScanLine(part.y()+y-tileOrigin.y());auto out=reinterpret_cast<QRgb *>(patch.scanLine(y));
            for(int x=0;x<part.width();++x)out[x]=multiply(row[part.x()+x-origin.x()],mask?mask[part.x()+x-tileOrigin.x()]:255);}
        if(!nonempty(patch))continue;
        auto tile=layer.tiles.value(key);if(tile.isNull()){
            if(layer.tiles.size()>=allowance){*error="Pasting this image exceeds the 1024-tile project limit. No artwork was changed.";return false;}
            tile=QImage(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);if(tile.isNull()){*error="There is not enough memory to paste the image.";return false;}tile.fill(Qt::transparent);}
        {QPainter painter(&tile);painter.drawImage(part.topLeft()-tileOrigin,patch);}layer.tiles.insert(key,tile);
    }
    if(newLayer && layer.tiles.isEmpty()){*error="The image contains no visible pixels.";return false;}
    auto after=project;
    if(newLayer)after.shots[shotIndex].layers.insert(activeLayer.group?int(layers.size()):active+1,layer);else after.shots[shotIndex].layers[active]=layer;
    const auto validation=after.validate();if(!validation.isEmpty()){*error=validation;return false;}
    *result=std::move(after);*selected=layer.id;return true;
}
}
bool ClipboardTools::copy(Document *document,bool cut,QString *error) {
    if(!ready(document,error))return false;
    if(!originalPixelsAvailable(document,error))return false;
    const auto &layers=document->m_project.shots[document->activeShotIndex()].layers;const auto &layer=layers[document->m_active];
    if(layer.group)return failure(document,error,"Choose a drawing layer to copy pixels.");
    if(layer.kind==LayerKind::Symbol)return failure(document,error,"Duplicate the symbol layer, or edit its contents before copying pixels.");
    if(layer.kind==LayerKind::Scene3D)return failure(document,error,"3D references do not contain drawing pixels. Duplicate objects in the 3D Scene panel.");
    if(cut && (!layerCanPaint(layers,document->m_active) || layer.alphaLocked))return failure(document,error,"Cut needs an unlocked raster layer with alpha lock off. Copy remains available for vector and text layers.");
    try {
        QImage image;QPoint origin;RasterLayer remainder;QString message;
        if(!extract(layer,document->m_selection,&image,&origin,cut?&remainder:nullptr,&message))return failure(document,error,message);
        auto mime=std::make_unique<QMimeData>();mime->setImageData(image);
        mime->setData(placementMime,QJsonDocument(QJsonObject{{"version",1},{"x",origin.x()},{"y",origin.y()}}).toJson(QJsonDocument::Compact));
        if(cut){auto after=document->m_project;after.shots[document->activeShotIndex()].layers[document->m_active]=std::move(remainder);
            if(!document->commit(document->m_project,after,"Cut pixels")){if(error)*error="The selected pixels could not be cut.";return false;}}
        QGuiApplication::clipboard()->setMimeData(mime.release());
        if(error)error->clear();return true;
    } catch(const std::bad_alloc &){return failure(document,error,"There is not enough memory for the clipboard image.");}
}
bool ClipboardTools::paste(Document *document,bool newLayer,std::optional<QPointF> topLeft,QString *error) {
    if(!ready(document,error))return false;
    const auto *mime=QGuiApplication::clipboard()->mimeData();if(!mime || !mime->hasImage())return failure(document,error,"The clipboard does not contain an image.");
    const auto image=QGuiApplication::clipboard()->image();
    QPointF origin((document->m_project.width-image.width())/2.0,(document->m_project.height-image.height())/2.0);
    if(!topLeft && mime->hasFormat(placementMime)){
        const auto bytes=mime->data(placementMime);if(bytes.size()<512){const auto object=QJsonDocument::fromJson(bytes).object();
            if(object.value("version").toInt()==1 && object.value("x").isDouble() && object.value("y").isDouble())origin={object.value("x").toDouble(),object.value("y").toDouble()};}
    }
    if(topLeft)origin=*topLeft;
    try {
        Project after;QString selected,message;
        if(!placeImage(document->m_project,document->activeShotIndex(),document->m_active,image,origin,newLayer,"Pasted image",document->m_selection,&after,&selected,&message))return failure(document,error,message);
        if(!document->commit(document->m_project,after,newLayer?"Paste image layer":"Paste pixels",selected)){
            if(error)*error="The image could not be pasted into this drawing.";return false;}
        if(error)error->clear();return true;
    } catch(const std::bad_alloc &){return failure(document,error,"There is not enough memory to paste the image.");}
}
bool ClipboardTools::duplicate(Document *document,QString *error) {
    if(!ready(document,error))return false;
    if(!originalPixelsAvailable(document,error))return false;
    const auto &layer=document->m_project.shots[document->activeShotIndex()].layers[document->m_active];
    if(layer.group)return failure(document,error,"Choose a drawing layer to duplicate selected pixels.");
    if(layer.kind==LayerKind::Symbol)return failure(document,error,"Duplicate the symbol layer, or edit its contents before copying pixels.");
    if(layer.kind==LayerKind::Scene3D)return failure(document,error,"Duplicate 3D objects in the 3D Scene panel instead of duplicating pixels.");
    try {
        QImage image;QPoint origin;QString message,selected;Project after;
        if(!extract(layer,document->m_selection,&image,&origin,nullptr,&message)
            || !placeImage(document->m_project,document->activeShotIndex(),document->m_active,image,origin,true,layer.name+" selection",{},&after,&selected,&message))return failure(document,error,message);
        if(!document->commit(document->m_project,after,"Duplicate selected pixels",selected)){
            if(error)*error="The selected pixels could not be duplicated.";return false;}
        if(error)error->clear();return true;
    } catch(const std::bad_alloc &){return failure(document,error,"There is not enough memory to duplicate the pixels.");}
}
bool ClipboardTools::importImage(Document *document,const QImage &image,const QString &name,QPointF topLeft,QString *error) {
    if(!ready(document,error))return false;
    try {
        Project after;QString selected,message;
        if(!placeImage(document->m_project,document->activeShotIndex(),document->m_active,image,topLeft,true,name,{},&after,&selected,&message))return failure(document,error,message);
        if(!document->commit(document->m_project,after,"Import artwork image",selected)){
            if(error)*error="The image could not be added to this drawing.";return false;}
        if(error)error->clear();return true;
    } catch(const std::bad_alloc &){return failure(document,error,"There is not enough memory to import the image.");}
}
} // namespace toon
