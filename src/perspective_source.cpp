#include "perspective_source.h"
#include "animation.h"
#include "property_animation.h"
#include "effect_render.h"
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <cmath>

namespace toon {
namespace {
bool stopped(const std::atomic_bool *cancelled) { return cancelled && cancelled->load(); }
QSize analysisSize(QSize size) {
    const qreal scale=qMin(1.,qMin(1600./qMax(size.width(),size.height()),
                                  std::sqrt(2000000./(qreal(size.width())*size.height()))));
    return {qMax(1,qRound(size.width()*scale)),qMax(1,qRound(size.height()*scale))};
}
int indexOf(const QVector<RasterLayer> &layers,const QString &id) {
    for(int i=0;i<layers.size();++i)if(layers[i].id==id)return i;return -1;
}
}
PerspectiveSource capturePerspectiveSource(const Project &project,int shotIndex,const QString &id,
                                           int frame,bool sourceEditing,const std::atomic_bool *cancelled) {
    PerspectiveSource result;
    if(stopped(cancelled))return result;
    if(shotIndex<0||shotIndex>=project.shots.size()||project.width<=0||project.height<=0) {
        result.error="Open a drawing before detecting perspective.";return result;
    }
    auto shot=project.shots[shotIndex];
    const int source=indexOf(shot.layers,id);
    if(source<0||!layerPresentAtFrame(shot,id,frame)) {
        result.error="Select a layer exposed in the current drawing.";return result;
    }
    if(sourceEditing)shot.layerAnimations.clear();
    QString ancestor=id;
    for(int depth=0;!ancestor.isEmpty()&&depth<=8;++depth){
        const int at=indexOf(shot.layers,ancestor);if(at<0)break;
        shot.layers[at].visible=true;ancestor=shot.layers[at].parentId;
    }
    const QSize artworkSize(project.width,project.height),size=analysisSize(artworkSize);
    const QRectF bounds(QPointF(),artworkSize);
    EffectRenderCancellationScope scope(cancelled);clearEffectRenderError();
    QString error;const auto layers=evaluateLayers(project,shot,frame,false,bounds,&error);
    if(stopped(cancelled))return result;
    if(!error.isEmpty()){result.error=error;return result;}
    const int selected=indexOf(layers,id);
    if(selected<0){result.error="This layer has no image at the current frame.";return result;}
    // A clipping stack or adjustment needs its actual backdrop. Include only
    // those siblings, never unrelated folders or the rest of the drawing.
    const auto parent=layers[selected].parentId;
    QVector<int> roots;
    if(layers[selected].adjustment) {
        for(int i=0;i<=selected;++i)if(layers[i].parentId==parent)roots.append(i);
        result.description=layers[selected].name+" · adjustment and backdrop";
    } else if(layers[selected].clipped) {
        int base=selected;
        for(int i=selected-1;i>=0;--i)if(layers[i].parentId==parent) {
            base=i;if(!layers[i].clipped)break;
        }
        for(int i=base;i<=selected;++i)if(layers[i].parentId==parent)roots.append(i);
        result.description=layers[selected].name+" · clipping stack";
    } else {roots.append(selected);result.description=layers[selected].name;}
    QSet<QString> keep;
    for(int at:roots){keep.insert(layers[at].id);if(layers[at].group)for(int i=0;i<layers.size();++i)
        if(layerIsDescendant(layers,i,layers[at].id))keep.insert(layers[i].id);}
    // Retain the selected branch's ancestor wrappers: their masks, effects and
    // transforms matter even though unrelated siblings are excluded.
    QString wrapper=parent;
    for(int depth=0;!wrapper.isEmpty()&&depth<=8;++depth){
        const int at=indexOf(layers,wrapper);if(at<0)break;
        if(layers[at].clipped){result.error="Select the clipped parent folder to analyze its complete clipping stack.";return result;}
        keep.insert(wrapper);wrapper=layers[at].parentId;
    }
    QVector<RasterLayer> isolated;
    for(auto layer:layers)if(keep.contains(layer.id)){
        if(layer.id==id)layer.visible=true;
        isolated.append(std::move(layer));
    }
    QImage image(size,QImage::Format_ARGB32_Premultiplied);
    if(image.isNull()){result.error="There is not enough memory to prepare the source preview.";return result;}
    image.fill(Qt::transparent);
    {QPainter painter(&image);painter.scale(qreal(size.width())/artworkSize.width(),qreal(size.height())/artworkSize.height());
        paintLayers(painter,isolated,true,nullptr,true,artworkSize);}
    if(stopped(cancelled))return result;
    if(const auto reason=effectRenderError();!reason.isEmpty()){result.error=reason;return result;}
    result.image=std::move(image);
    result.imageToArtwork=QTransform::fromScale(qreal(artworkSize.width())/size.width(),qreal(artworkSize.height())/size.height());
    return result;
}
PerspectiveSource loadPerspectiveImage(const QString &path,QSize artworkSize,const std::atomic_bool *cancelled) {
    PerspectiveSource result;
    if(stopped(cancelled))return result;
    if(artworkSize.isEmpty()){result.error="Open a drawing before choosing an image.";return result;}
    if(QFileInfo(path).size()>32LL*1024*1024){result.error="Choose an image file no larger than 32 MiB.";return result;}
    QImageReader reader(path);reader.setAutoTransform(true);
    const auto raw=reader.size();
    if(raw.isEmpty()||raw.width()>16384||raw.height()>16384||qint64(raw.width())*raw.height()>16000000) {
        result.error="Choose a supported image up to 16 megapixels and 16,384 pixels per side.";return result;
    }
    reader.setScaledSize(analysisSize(raw));
    auto image=reader.read();
    if(stopped(cancelled))return result;
    if(image.isNull()){result.error="The image could not be decoded: "+reader.errorString();return result;}
    const qreal scale=qMin(qreal(artworkSize.width())/image.width(),qreal(artworkSize.height())/image.height());
    const QPointF offset((artworkSize.width()-image.width()*scale)/2,(artworkSize.height()-image.height()*scale)/2);
    result.imageToArtwork=QTransform(scale,0,0,scale,offset.x(),offset.y());
    result.image=image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    result.description=QFileInfo(path).fileName();
    return result;
}
}
