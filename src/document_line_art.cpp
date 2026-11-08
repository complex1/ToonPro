#include "line_art_document.h"
#include "document.h"
#include "animation.h"
#include "effect_render.h"
#include <QPainter>

namespace toon {
namespace {
int indexOf(const QVector<RasterLayer> &layers,const QString &id){for(int i=0;i<layers.size();++i)if(layers[i].id==id)return i;return -1;}
RasterLayer fromImage(const QImage &image,const QString &name,const QString &parent) {
    auto layer=RasterLayer::create(name);layer.parentId=parent;
    if(image.isNull())return layer;
    const auto pixels=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for(int y=0;y<pixels.height();y+=tileSize)for(int x=0;x<pixels.width();x+=tileSize){
        const QRect rect=QRect(x,y,tileSize,tileSize).intersected(pixels.rect());bool used=false;
        for(int yy=rect.top();yy<=rect.bottom()&&!used;++yy){const auto *row=reinterpret_cast<const QRgb *>(pixels.constScanLine(yy));for(int xx=rect.left();xx<=rect.right();++xx)if(qAlpha(row[xx])){used=true;break;}}
        if(!used)continue;
        QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
        {QPainter painter(&tile);painter.drawImage(QPoint(),pixels,rect);}layer.tiles.insert({x/tileSize,y/tileSize},tile);
    }
    return layer;
}
}
QString lineArtSourceReason(const Project &project,int shotIndex,const QString &id,int frame) {
    if(shotIndex<0||shotIndex>=project.shots.size())return "Open a drawing first.";
    if(qint64(project.width)*project.height>maxLineArtPixels||project.width>16384||project.height>16384)return "Conversion supports canvases up to 9 megapixels and 16,384 pixels per side.";
    const auto &shot=project.shots[shotIndex];const int at=indexOf(shot.layers,id);
    if(at<0)return "Select an image or drawing layer.";
    const auto &source=shot.layers[at];
    if(source.scene3d||source.kind==LayerKind::Scene3D)return "Use a 2D image snapshot; direct 3D conversion is deferred.";
    if(source.group||source.adjustment||source.symbol)return "Select a raster, vector, or text layer.";
    if(source.clipped)return "Convert an unclipped source layer. Clipped stacks need a merged copy first.";
    for(int i=at+1;i<shot.layers.size();++i)if(shot.layers[i].parentId==source.parentId){
        if(shot.layers[i].clipped)return "Use a merged copy of this clipping stack before converting it.";
        break;
    }
    if(!source.visible||!layerPresentAtFrame(shot,id,frame))return "Select a visible drawing exposed at the current frame.";
    auto parent=source.parentId;
    for(int depth=0;!parent.isEmpty()&&depth<=8;++depth){
        const int ancestor=indexOf(shot.layers,parent);if(ancestor<0)return "The destination folder is unavailable.";
        const auto &layer=shot.layers[ancestor];if(layer.locked||layer.positionLocked||!layer.visible)return "Unlock and show the destination folder first.";
        parent=layer.parentId;
    }
    return {};
}
LineArtInput lineArtSource(const Project &project,int shot,const QString &id,int frame,QString *error) {
    LineArtInput input;input.size={project.width,project.height};
    QString reason=lineArtSourceReason(project,shot,id,frame);if(!reason.isEmpty()){if(error)*error=reason;return input;}
    const auto layers=evaluateLayers(project.shots[shot],frame,false,QRectF(QPointF(),input.size),&reason);
    const int at=indexOf(layers,id);if(at<0||!reason.isEmpty()){if(error)*error=reason.isEmpty()?"The source is unavailable at this frame.":reason;return input;}
    auto source=layers[at];source.parentId.clear();source.annotation=false;
    input.image=QImage(input.size,QImage::Format_ARGB32_Premultiplied);input.image.fill(Qt::transparent);
    clearEffectRenderError();
    {QPainter painter(&input.image);paintLayers(painter,{source},true,nullptr,false,input.size);}
    const auto renderError=effectRenderError();if(!renderError.isEmpty()){if(error)*error=renderError;input.image={};}
    return input;
}
bool Document::createLineArtLayers(const QString &sourceId,quint64 expectedRevision,int frame,const LineArtResult &result,bool hideSource) {
    const auto fail=[this](const QString &message){emit warning(message);return false;};
    if(!isOpen()||loading()||editingGestureActive()||layerPropertyEditActive())return fail("Finish the current edit before creating converted layers.");
    if(contentRevision()!=expectedRevision||currentFrame()!=frame||activeShot().layers[activeLayer()].id!=sourceId)
        return fail("The source changed. Reopen conversion to review the current drawing.");
    const auto reason=lineArtSourceReason(project(),activeShotIndex(),sourceId,frame);if(!reason.isEmpty())return fail(reason);
    if(result.cancelled||!result.error.isEmpty()||result.lines.isNull())return fail("Prepare a valid conversion before creating layers.");
    auto after=project();auto &layers=after.shots[activeShotIndex()].layers;const int at=indexOf(layers,sourceId);const auto source=layers[at];
    if(hideSource&&source.locked)return fail("Keep the locked source visible, or unlock it before hiding it.");
    for(const auto *image:{&result.lines,&result.shade,&result.deepShade,&result.base})if(!image->isNull()&&image->size()!=QSize(after.width,after.height))
        return fail("Create layers requires the full canvas resolution.");
    auto group=RasterLayer::create(source.name+" · Line art");group.group=true;group.parentId=source.parentId;group.annotation=source.annotation;
    QVector<RasterLayer> output;
    for(const auto &entry:{qMakePair(&result.base,QString("Base")),qMakePair(&result.shade,QString("Shadows")),qMakePair(&result.deepShade,QString("Deep shadows")),qMakePair(&result.lines,QString("Lines"))}){
        auto layer=fromImage(*entry.first,entry.second,group.id);if(!layer.tiles.isEmpty())output.append(std::move(layer));
    }
    if(output.isEmpty())return fail("No lines or tones were found. Increase Detail or enable shading.");
    if(hideSource)layers[at].visible=false;
    int insertion=at+1;layers.insert(insertion++,group);for(const auto &layer:output)layers.insert(insertion++,layer);
    const auto validation=after.validate();if(!validation.isEmpty())return fail(validation);
    return commit(m_project,after,"Convert to line art",output.last().id);
}
}
