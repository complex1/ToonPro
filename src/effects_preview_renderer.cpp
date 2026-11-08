#include "effects_preview_renderer.h"
#include "animation_render.h"
#include "camera_animation.h"
#include "effect_render.h"
#include "keyframe_render.h"
#include "scene3d_cache.h"
#include "reference3d_render.h"
#include "sheet.h"
#include "brush_engine.h"
#include "symbols.h"
#include <QFutureWatcher>
#include <QPainter>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <QtMath>
#include <QtConcurrentRun>
#include <cmath>
#include <cstring>
#include <exception>

namespace toon {
namespace {
bool stopped(const EffectsPreviewCancellation &token) { return token && token->load(std::memory_order_relaxed); }
QThreadPool &previewPool() {
    static QThreadPool pool;
    static const bool configured = [] { pool.setMaxThreadCount(2); return true; }();
    Q_UNUSED(configured);
    return pool;
}
bool draftEligible(const EffectsPreviewSnapshot &s) {
    if(s.transformPreview || s.onionEnabled || !s.shot.animationTracks.isEmpty() || !s.shot.layerAnimations.isEmpty())return false;
    for(const auto &layer:s.shot.layers)
        if(layer.scene3d || !layer.objects.isEmpty() || layer.kind!=LayerKind::Raster)return false;
    return true;
}
int draftDivisor(const EffectsPreviewSnapshot &s) {
    qreal extent=qMax(s.project.width,s.project.height);
    for(const auto &layer:s.shot.layers) {
        QRectF bounds;
        for(auto it=layer.tiles.cbegin();it!=layer.tiles.cend();++it)
            bounds|=QRectF(it.key().first*tileSize,it.key().second*tileSize,it->width(),it->height());
        extent=qMax(extent,qMax(bounds.width(),bounds.height()));
    }
    return extent<=1024?1:extent<=2048?2:8;
}
bool reduceTiles(const QMap<TileKey,QImage> &source,QMap<TileKey,QImage> &output,bool mask,int defaultValue,int divisor,
                 const EffectsPreviewCancellation &cancel) {
    const int part=tileSize/divisor;
    for(auto it=source.cbegin();it!=source.cend();++it) {
        if(stopped(cancel))return false;
        const int tx=int(std::floor(it.key().first/double(divisor))),ty=int(std::floor(it.key().second/double(divisor)));
        const int x=(it.key().first-tx*divisor)*part,y=(it.key().second-ty*divisor)*part;
        auto &target=output[{tx,ty}];
        const auto format=mask?QImage::Format_Grayscale8:QImage::Format_ARGB32_Premultiplied;
        if(target.isNull()) {target=QImage(tileSize,tileSize,format);if(target.isNull())return false;target.fill(mask?uint(defaultValue):0U);}
        auto reduced=it.value().scaled(QSize(qCeil(it->width()/qreal(divisor)),qCeil(it->height()/qreal(divisor))),Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(format);
        if(reduced.isNull())return false;
        const int bytes=mask?1:4;
        for(int row=0;row<reduced.height()&&y+row<tileSize;++row)
            std::memcpy(target.scanLine(y+row)+x*bytes,reduced.constScanLine(row),size_t(qMin(reduced.width(),tileSize-x)*bytes));
    }
    return true;
}
bool makeDraft(Shot &shot,int divisor,const EffectsPreviewCancellation &cancel) {
    const qreal scale=1./divisor;
    for(auto &layer:shot.layers) {
        if(stopped(cancel))return false;
        QMap<TileKey,QImage> pixels;
        if(!reduceTiles(layer.tiles,pixels,false,0,divisor,cancel))return false;
        layer.tiles=std::move(pixels);
        if(layer.mask) {
            QMap<TileKey,QImage> mask;
            if(!reduceTiles(layer.mask->tiles,mask,true,layer.mask->defaultValue,divisor,cancel))return false;
            layer.mask->tiles=std::move(mask);
            layer.mask->position*=scale;layer.mask->pivot*=scale;layer.mask->feather*=scale;
        }
        auto &fx=layer.effects;fx.blurRadius*=scale;fx.shadowRadius*=scale;fx.glowRadius*=scale;fx.shadowOffset*=scale;fx.outlineWidth*=scale;
        const auto t=layer.renderTransform;
        // q = scale*p. Children operate in scaled parent coordinates (S*T*S^-1).
        // Root layers map directly back into the original sheet (T*S^-1),
        // avoiding an extra folder level or any change to the camera/paper.
        const qreal output=layer.parentId.isEmpty()?1:scale;
        layer.renderTransform=QTransform(t.m11()*output/scale,t.m12()*output/scale,t.m13()/scale,
            t.m21()*output/scale,t.m22()*output/scale,t.m23()/scale,t.dx()*output,t.dy()*output,t.m33());
    }
    return true;
}
// Playback uses a private, uniformly scaled artwork coordinate system. The
// paper, viewport and camera remain in original coordinates; scaling back at
// the final paint boundary also keeps odd canvas sizes and overscan aligned.
QTransform scaledCoordinates(const QTransform &t,qreal scale) {
    return QTransform(t.m11(),t.m12(),t.m13()/scale,t.m21(),t.m22(),t.m23()/scale,
                      t.dx()*scale,t.dy()*scale,t.m33());
}
Shot artworkOnly(const Shot &source) {
    Shot result;result.id=source.id;result.durationFrames=source.durationFrames;
    result.layers=source.layers;result.animationTracks=source.animationTracks;
    result.layerAnimations=source.layerAnimations;result.camera2D=source.camera2D;
    result.camera2DKeys=source.camera2DKeys;return result;
}
bool sameArtwork(const Shot &a,const Shot &b) {
    return a.id==b.id&&a.durationFrames==b.durationFrames&&a.layers.constData()==b.layers.constData()
        &&a.animationTracks.constData()==b.animationTracks.constData()
        &&a.layerAnimations.constData()==b.layerAnimations.constData()
        &&a.camera2D==b.camera2D&&a.camera2DKeys.constData()==b.camera2DKeys.constData();
}
bool reduceArtwork(Shot &shot,int divisor,const EffectsPreviewCancellation &cancel) {
    const qreal scale=1./divisor;const auto objectScale=QTransform::fromScale(scale,scale);
    for(auto &layer:shot.layers) {
        if(stopped(cancel))return false;
        QMap<TileKey,QImage> pixels;
        if(!reduceTiles(layer.tiles,pixels,false,0,divisor,cancel))return false;
        layer.tiles=std::move(pixels);
        if(layer.mask) {
            QMap<TileKey,QImage> mask;
            if(!reduceTiles(layer.mask->tiles,mask,true,layer.mask->defaultValue,divisor,cancel))return false;
            layer.mask->tiles=std::move(mask);
            layer.mask->position*=scale;layer.mask->pivot*=scale;layer.mask->feather*=scale;
        }
        auto &fx=layer.effects;fx.blurRadius*=scale;fx.shadowRadius*=scale;fx.glowRadius*=scale;fx.shadowOffset*=scale;fx.outlineWidth*=scale;
        layer.renderTransform=scaledCoordinates(layer.renderTransform,scale);
        for(auto &object:layer.objects)object.transform*=objectScale;
    }
    for(auto &animation:shot.layerAnimations) {
        animation.pivot*=scale;
        for(auto &channel:animation.channels)for(auto &key:channel.keys) {
            switch(channel.property) {
            case KeyframeProperty::PositionX:case KeyframeProperty::PositionY:
            case KeyframeProperty::BlurRadius:case KeyframeProperty::ShadowRadius:
            case KeyframeProperty::ShadowOffsetX:case KeyframeProperty::ShadowOffsetY:
            case KeyframeProperty::GlowRadius:case KeyframeProperty::MaskPositionX:
            case KeyframeProperty::MaskPositionY:case KeyframeProperty::MaskFeather:key.value*=scale;break;
            default:break;
            }
            if(key.spatial){key.spatial->incoming*=scale;key.spatial->outgoing*=scale;}
        }
        for(auto &path:animation.paths)for(auto &key:path.keys)key.pose.transform*=objectScale;
    }
    shot.camera2D.position*=scale;
    for(auto &key:shot.camera2DKeys)key.pose.position*=scale;
    return true;
}
qint64 artworkBytes(const Shot &shot) {
    qint64 bytes=sizeof(Shot);
    for(const auto &layer:shot.layers) {
        bytes+=sizeof(RasterLayer)+(layer.id.size()+layer.name.size()+layer.parentId.size())*sizeof(QChar);
        bytes+=(layer.tiles.size()+(layer.mask?layer.mask->tiles.size():0))*64; // Conservative map nodes.
        for(const auto &tile:layer.tiles)bytes+=tile.sizeInBytes();
        if(layer.mask)for(const auto &tile:layer.mask->tiles)bytes+=tile.sizeInBytes();
        for(const auto &object:layer.objects) {
            bytes+=sizeof(EditableObject)+object.nodes.size()*sizeof(BezierNode)+object.widthFactors.size()*sizeof(qreal)
                +(object.text.size()+object.fontFamily.size())*sizeof(QChar);
            if(object.brushStroke) {
                const auto &brush=*object.brushStroke;
                bytes+=sizeof(VectorBrushStroke)+brush.samples.size()*sizeof(VectorBrushSample)+brush.sourceGeometry.size()
                    +brush.settings.tipFrames.size()*sizeof(BrushTipFrame)
                    +brush.selection.elementCount()*sizeof(QPainterPath::Element)
                    +brush.settings.tipMask.sizeInBytes()+brush.settings.colorTip.sizeInBytes()
                    +brush.settings.texture.pattern.sizeInBytes()+brush.settings.dual.tipMask.sizeInBytes();
            }
        }
    }
    for(const auto &animation:shot.layerAnimations) {
        for(const auto &channel:animation.channels)bytes+=channel.keys.size()*sizeof(ScalarKeyframe);
        for(const auto &path:animation.paths)for(const auto &key:path.keys)
            bytes+=sizeof(PathKeyframe)+key.pose.nodes.size()*sizeof(BezierNode)+key.pose.widthFactors.size()*sizeof(qreal);
    }
    for(const auto &track:shot.animationTracks)bytes+=track.drawings.size()*sizeof(AnimationDrawing)+track.exposures.size()*sizeof(CelExposure);
    bytes+=shot.camera2DKeys.size()*sizeof(Camera2DKeyframe);
    return bytes;
}
struct PlaybackSourceCache {
    int divisor=1;
    Shot source, reduced;
    QVector<SymbolDefinition> sourceLibrary,reducedLibrary;
};
PlaybackSourceCache &playbackSources(){static thread_local PlaybackSourceCache value;return value;}
QString playbackReductionReason(const EffectsPreviewSnapshot &s,QSet<QString> &reachable) {
    if(s.playbackDivisor!=2&&s.playbackDivisor!=4&&s.playbackDivisor!=8)return "This playback resolution is not supported.";
    if(s.transformPreview)return "Full quality is used while an artwork transform is unfinished.";
    if(s.onionEnabled)return "Full quality is used while onion-skin overlays are included.";
    const auto objectScale=QTransform::fromScale(1./s.playbackDivisor,1./s.playbackDivisor);
    const auto transformReason=QStringLiteral("Full quality is used to preserve very small vector or text transforms.");
    const auto inspect=[&](const Shot &shot,auto &&inspect)->QString {
        for(const auto &layer:shot.layers) {
            if(layer.scene3d||layer.kind==LayerKind::Scene3D)return "Full quality is used for shots containing 3D reference layers.";
            // Qt treats near-zero determinants as singular. A valid source
            // transform can cross that tolerance when preview pixels shrink.
            for(const auto &object:layer.objects)
                if(!(object.transform*objectScale).isInvertible())return transformReason;
            if(layer.symbol&&!reachable.contains(layer.symbol->symbolId)) {
                reachable.insert(layer.symbol->symbolId);
                const auto *definition=findSymbol(s.project,layer.symbol->symbolId);
                if(!definition)return "Full quality is used because a Symbol link is unavailable.";
                if(auto reason=inspect(definition->content,inspect);!reason.isEmpty())return reason;
            }
        }
        for(const auto &animation:shot.layerAnimations)for(const auto &path:animation.paths)for(const auto &key:path.keys)
            if(!(key.pose.transform*objectScale).isInvertible())return transformReason;
        return {};
    };
    return inspect(s.shot,inspect);
}
bool playbackSource(const EffectsPreviewSnapshot &s,const QSet<QString> &reachable,Project &project,Shot &shot,
                    const EffectsPreviewCancellation &cancel) {
    auto &cache=playbackSources();
    if(cache.divisor==s.playbackDivisor&&sameArtwork(cache.source,s.shot)
        &&cache.sourceLibrary.constData()==s.project.symbols.constData()) {
        shot=cache.reduced;project.symbols=cache.reducedLibrary;return true;
    }
    shot=artworkOnly(s.shot);
    if(!reduceArtwork(shot,s.playbackDivisor,cancel))return false;
    QVector<SymbolDefinition> library;
    for(const auto &definition:s.project.symbols)if(reachable.contains(definition.id)) {
        SymbolDefinition reduced{definition.id,definition.name,artworkOnly(definition.content)};
        if(!reduceArtwork(reduced.content,s.playbackDivisor,cancel))return false;
        library.append(std::move(reduced));
    }
    project.symbols=library;
    qint64 bytes=artworkBytes(s.shot)+artworkBytes(shot);
    for(const auto &definition:s.project.symbols)bytes+=artworkBytes(definition.content);
    for(const auto &definition:library)bytes+=artworkBytes(definition.content);
    // Retain source arrays as well as reduced arrays: their implicit-sharing
    // identities cannot be recycled after project replacement. This cache is
    // bounded per worker, independently of effects/Symbol render caches.
    cache={};
    if(bytes<=64*1024*1024)
        cache={s.playbackDivisor,artworkOnly(s.shot),shot,s.project.symbols,std::move(library)};
    return true;
}
void paintPlaybackArtwork(QPainter &painter,const EffectsPreviewSnapshot &s,const Project &project,const Shot &shot,
                          const SheetGeometry &geometry,int divisor) {
    painter.save();painter.setRenderHint(QPainter::Antialiasing);
    painter.setClipRect(geometry.paper.adjusted(0,geometry.header.height(),0,0),Qt::IntersectClip);
    painter.translate(geometry.output.topLeft());
    const auto camera=s.applyCamera?shotCameraTransform(s.shot,s.frame,QSizeF(s.project.width,s.project.height)):QTransform();
    painter.setWorldTransform(camera,true);painter.scale(divisor,divisor);
    const auto visible=geometry.paper.adjusted(0,geometry.header.height(),0,0).translated(-geometry.output.topLeft());
    const auto sourceBounds=QTransform::fromScale(1./divisor,1./divisor).mapRect(camera.inverted().mapRect(visible).adjusted(-2,-2,2,2));
    paintLayers(painter,evaluateLayers(project,shot,s.frame,false,sourceBounds),true,nullptr,s.smoothDisplay);
    painter.restore();
    if(s.guides && s.shot.sheet.showGuides) {
        painter.save();painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor("#bdc8d6"),1,Qt::DashLine));painter.drawRect(geometry.drawing);
        painter.setPen(QPen(QColor("#526d91"),2));painter.drawRect(geometry.output);
        const auto center=geometry.output.center();
        for(auto point:{QPointF(center.x(),geometry.output.top()),QPointF(center.x(),geometry.output.bottom())})painter.drawLine(point-QPointF(0,10),point+QPointF(0,10));
        for(auto point:{QPointF(geometry.output.left(),center.y()),QPointF(geometry.output.right(),center.y())})painter.drawLine(point-QPointF(10,0),point+QPointF(10,0));
        if(s.shot.sheet.safeArea){painter.setPen(QPen(QColor("#99a9bc"),1,Qt::DashLine));painter.drawRect(geometry.output.adjusted(s.project.width*.05,s.project.height*.05,-s.project.width*.05,-s.project.height*.05));}
        painter.restore();
    }
}
QImage viewportImage(const EffectsPreviewSnapshot &s) {
    QImage image(s.pixelSize,QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(s.devicePixelRatio);
    image.setDotsPerMeterX(s.dotsPerMeterX);image.setDotsPerMeterY(s.dotsPerMeterY);
    return image;
}
void paintOnions(QPainter &target,const EffectsPreviewSnapshot &s,const SheetGeometry &geometry,const QTransform &camera,
                 const EffectsPreviewCancellation &cancel,QString &error) {
    if(!s.onionEnabled||s.onionSettings.opacity<=0||s.onionTrackId.isEmpty())return;
    auto combined=viewportImage(s);if(combined.isNull()){error="There is not enough memory to render onion skins.";return;}combined.fill(Qt::transparent);
    const auto bounds=camera.inverted().mapRect(geometry.paper.adjusted(0,geometry.header.height(),0,0).translated(-geometry.output.topLeft()));
    QPainter compositor(&combined);
    for(int direction:{-1,1}) {
        const auto samples=onionSkinSamples(s.shot,s.frame,s.onionTrackId,direction,
            direction<0?s.onionSettings.previousCount:s.onionSettings.nextCount);
        for(auto sample=samples.crbegin();sample!=samples.crend();++sample) {
            if(stopped(cancel))return;
            const auto layers=onionSkinLayersForSample(s.project,s.shot,s.frame,s.onionTrackId,*sample,bounds);
            if(!keyframeRenderError().isEmpty()){error=keyframeRenderError();return;}
            if(layers.isEmpty())continue;
            auto ghost=viewportImage(s);if(ghost.isNull()){error="There is not enough memory to render onion skins.";return;}ghost.fill(Qt::transparent);
            QPainter painter(&ghost);painter.setTransform(s.view);
            painter.setClipRect(geometry.paper.adjusted(0,geometry.header.height(),0,0));
            painter.translate(geometry.output.topLeft());painter.setTransform(camera,true);
            paintLayers(painter,layers,false,nullptr,s.smoothDisplay);
            if(!keyframeRenderError().isEmpty()){error=keyframeRenderError();return;}
            painter.resetTransform();painter.setClipping(false);painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(QRectF(QPointF(),QSizeF(s.pixelSize)/s.devicePixelRatio),direction<0?s.onionSettings.previousColor:s.onionSettings.nextColor);
            painter.end();compositor.setOpacity(s.onionSettings.opacity);compositor.drawImage(QPointF(),ghost);
        }
    }
    compositor.end();target.save();target.resetTransform();target.drawImage(QPointF(),combined);target.restore();
}
}

bool prepareEffectsPreviewBackground(EffectsPreviewSnapshot &s) {
    if(s.pixelSize.isEmpty()||qint64(s.pixelSize.width())*s.pixelSize.height()>64000000||
        !std::isfinite(s.devicePixelRatio)||s.devicePixelRatio<=0)return false;
    auto image=viewportImage(s);if(image.isNull())return false;
    const auto geometry=sheetGeometry(s.project,s.shot);
    image.fill(s.surround);QPainter painter(&image);
    if(!s.viewportClip.isEmpty())painter.setClipRegion(s.viewportClip);
    painter.setTransform(s.view);
    if(s.drawPaperShadow)painter.fillRect(geometry.paper.translated(8,10),QColor(0,0,0,65));
    paintSheetBackground(painter,s.project,s.shot,geometry);painter.end();
    s.preparedGeometry=geometry;s.preparedBackground=std::move(image);return true;
}

EffectsPreviewResult renderEffectsPreview(const EffectsPreviewSnapshot &s,const EffectsPreviewCancellation &cancel) {
    EffectsPreviewResult result;result.context=s.context;result.revision=s.revision;result.quality=s.quality;result.playback=s.playback;result.frame=s.frame;
    if(stopped(cancel)){result.cancelled=true;return result;}
    if(s.pixelSize.isEmpty()||qint64(s.pixelSize.width())*s.pixelSize.height()>64000000||
        !std::isfinite(s.devicePixelRatio)||s.devicePixelRatio<=0) {
        result.error="The effects preview viewport exceeds the supported size.";return result;
    }
    EffectRenderCancellationScope cancellation(cancel.get());
    Scene3DLayerPreviewScope scenes(s.scenePreviews);
    std::optional<ReferenceLayerPreviewScope> referenceScenes;
    if(!s.referencePreviews.isEmpty())referenceScenes.emplace(s.referencePreviews);
    try {
        Shot shot=s.shot;Project reducedProject;auto reduction=s;
        QSet<QString> reachable;int reducedDivisor=1;
        if(s.playback&&s.playbackDivisor!=1) {
            result.playbackFallbackReason=playbackReductionReason(s,reachable);
            if(result.playbackFallbackReason.isEmpty())reducedDivisor=s.playbackDivisor;
        }else if(!s.playback&&s.quality==EffectsPreviewQuality::Draft&&!draftEligible(s)) {
            // Animated effects/masks and nested Symbols need their source
            // coordinates and scalar keys reduced together. Reuse the checked
            // playback pipeline, while Exact idle refinements remain full size.
            reduction.playbackDivisor=draftDivisor(s);
            if(reduction.playbackDivisor>1&&playbackReductionReason(reduction,reachable).isEmpty())
                reducedDivisor=reduction.playbackDivisor;
        }
        const int divisor=!s.playback&&s.quality==EffectsPreviewQuality::Draft&&draftEligible(s)?draftDivisor(s):1;
        if(reducedDivisor>1) {
            if(!playbackSource(reduction,reachable,reducedProject,shot,cancel)) {
                result.cancelled=stopped(cancel);
                if(!result.cancelled)result.error="There is not enough memory to prepare the reduced-resolution preview.";
                return result;
            }
            result.sourceScale=1./reducedDivisor;
        }else if(divisor>1) {
            if(!makeDraft(shot,divisor,cancel)) {
                result.cancelled=stopped(cancel);
                if(!result.cancelled)result.error="There is not enough memory to prepare the effects preview.";
                return result;
            }
            result.sourceScale=1./divisor;
        }
        if(s.preparedBackground.isNull()||!s.preparedGeometry||s.preparedBackground.size()!=s.pixelSize||
            s.preparedBackground.devicePixelRatio()!=s.devicePixelRatio||s.preparedBackground.dotsPerMeterX()!=s.dotsPerMeterX||
            s.preparedBackground.dotsPerMeterY()!=s.dotsPerMeterY) {
            result.error="The effects preview background is not prepared for this view.";return result;
        }
        auto image=s.preparedBackground.copy();
        if(image.isNull()){result.error="There is not enough memory to render the effects preview.";return result;}
        const auto geometry=*s.preparedGeometry;
        const auto camera=s.applyCamera?shotCameraTransform(s.shot,s.frame,QSizeF(s.project.width,s.project.height)):QTransform();
        QPainter painter(&image);
        if(!s.viewportClip.isEmpty())painter.setClipRegion(s.viewportClip);
        painter.save();painter.setTransform(s.view);
        if(reducedDivisor>1)paintPlaybackArtwork(painter,s,reducedProject,shot,geometry,reducedDivisor);
        else paintSheetArtwork(painter,s.project,shot,geometry,s.guides,s.transformPreview?&*s.transformPreview:nullptr,s.smoothDisplay,true,s.frame,s.applyCamera);
        result.error=keyframeRenderError();
        if(!stopped(cancel)&&result.error.isEmpty()) {
            paintOnions(painter,s,geometry,camera,cancel,result.error);
            if(s.symmetry.mode!=SymmetryMode::Off) {
                auto transform=s.view;transform.translate(geometry.output.x(),geometry.output.y());painter.setTransform(camera*transform);
                auto color=s.symmetryColor;color.setAlpha(170);QPen guide(color,1,Qt::DashLine);guide.setCosmetic(true);
                painter.setPen(guide);painter.setBrush(Qt::NoBrush);
                painter.drawPath(brushSymmetryGuide(s.symmetry,QSizeF(s.project.width,s.project.height)));
            }
        }
        painter.restore();painter.end();
        result.cancelled=stopped(cancel)||effectRenderCancelled();
        if(result.cancelled)result.error.clear();
        else if(result.error.isEmpty())result.image=std::move(image);
    }catch(const std::exception &error){result.error=QString("Could not render the effects preview: %1").arg(QString::fromUtf8(error.what()));}
    catch(...){result.error="Could not render the effects preview.";}
    return result;
}

struct EffectsPreviewRenderer::Job {
    quint64 generation=0,context=0,revision=0;
    EffectsPreviewQuality quality=EffectsPreviewQuality::Exact;
    bool playback=false;
    int frame=0;
    EffectsPreviewCancellation cancellation=std::make_shared<std::atomic_bool>(false);
};
EffectsPreviewRenderer::EffectsPreviewRenderer(QObject *parent):EffectsPreviewRenderer(renderEffectsPreview,parent){}
EffectsPreviewRenderer::EffectsPreviewRenderer(EffectsPreviewRenderFunction render,QObject *parent)
    :QObject(parent),m_render(std::move(render)),m_watcher(new QFutureWatcher<EffectsPreviewResult>(this)),m_refineTimer(new QTimer(this)) {
    qRegisterMetaType<EffectsPreviewResult>();
    connect(m_watcher,&QFutureWatcher<EffectsPreviewResult>::finished,this,&EffectsPreviewRenderer::finished);
    m_refineTimer->setSingleShot(true);m_refineTimer->setInterval(150);
    connect(m_refineTimer,&QTimer::timeout,this,[this] {
        if(!m_refinement)return;
        auto snapshot=std::move(*m_refinement);m_refinement.reset();snapshot.quality=EffectsPreviewQuality::Exact;
        request(std::move(snapshot));
    });
}
EffectsPreviewRenderer::~EffectsPreviewRenderer(){invalidate();disconnect(m_watcher,nullptr,this,nullptr);}
void EffectsPreviewRenderer::invalidate() {
    Q_ASSERT(thread()==QThread::currentThread());
    ++m_generation;m_pending.reset();m_refinement.reset();m_refineTimer->stop();m_hasContext=false;m_hasPublished=false;
    if(m_running)m_running->cancellation->store(true,std::memory_order_relaxed);
}
void EffectsPreviewRenderer::request(EffectsPreviewSnapshot snapshot) {
    Q_ASSERT(thread()==QThread::currentThread());
    if(!m_hasContext||snapshot.context!=m_context||snapshot.playback!=m_playback) {
        invalidate();m_context=snapshot.context;m_latestRevision=snapshot.revision;m_hasContext=true;m_playback=snapshot.playback;
    }else if(snapshot.revision<m_latestRevision)return;
    m_latestRevision=snapshot.revision;
    if(snapshot.preparedBackground.isNull()||!snapshot.preparedGeometry)prepareEffectsPreviewBackground(snapshot);
    if(!snapshot.playback&&snapshot.quality==EffectsPreviewQuality::Draft) {
        if((m_hasPublished&&m_publishedRevision==snapshot.revision&&m_publishedQuality==EffectsPreviewQuality::Exact)||
            (m_running&&m_running->generation==m_generation&&m_running->revision==snapshot.revision&&m_running->quality==EffectsPreviewQuality::Exact)||
            (m_pending&&m_pending->revision==snapshot.revision&&m_pending->quality==EffectsPreviewQuality::Exact))return;
        m_refinement=snapshot;m_refineTimer->start();
    }else {m_refinement.reset();m_refineTimer->stop();}
    if(m_running) {
        // Let drafts and playback frames finish so continuous input makes
        // visible progress. Exact edit refinements can yield to newer values.
        if(!m_running->playback&&m_running->quality==EffectsPreviewQuality::Exact&&snapshot.revision>m_running->revision)
            m_running->cancellation->store(true,std::memory_order_relaxed);
        if(m_pending&&m_pending->revision==snapshot.revision&&m_pending->quality==EffectsPreviewQuality::Exact&&snapshot.quality==EffectsPreviewQuality::Draft)return;
        m_pending=std::move(snapshot);return;
    }
    start(std::move(snapshot));
}
void EffectsPreviewRenderer::start(EffectsPreviewSnapshot snapshot) {
    auto job=std::make_shared<Job>();job->generation=m_generation;job->context=snapshot.context;job->revision=snapshot.revision;job->quality=snapshot.quality;job->playback=snapshot.playback;job->frame=snapshot.frame;
    m_running=job;
    auto render=m_render;
    m_watcher->setFuture(QtConcurrent::run(&previewPool(),[job,snapshot=std::move(snapshot),render=std::move(render)] {
        auto result=render(snapshot,job->cancellation);
        result.context=job->context;result.revision=job->revision;result.quality=job->quality;result.playback=job->playback;result.frame=job->frame;
        result.cancelled|=job->cancellation->load(std::memory_order_relaxed);
        return result;
    }));
}
void EffectsPreviewRenderer::finished() {
    auto job=std::move(m_running);if(!job)return;
    EffectsPreviewResult result;result.context=job->context;result.revision=job->revision;result.quality=job->quality;result.playback=job->playback;result.frame=job->frame;
    try {result=m_watcher->result();}
    catch(const std::exception &error){result.error=QString("Could not render the effects preview: %1").arg(QString::fromUtf8(error.what()));}
    catch(...){result.error="Could not render the effects preview.";}
    result.cancelled|=job->cancellation->load(std::memory_order_relaxed);
    const bool context=job->generation==m_generation&&m_hasContext&&result.context==m_context;
    const bool newer=!m_hasPublished||result.revision>m_publishedRevision||
        (result.revision==m_publishedRevision&&result.quality==EffectsPreviewQuality::Exact&&m_publishedQuality==EffectsPreviewQuality::Draft);
    const bool current=result.playback||result.quality==EffectsPreviewQuality::Draft||result.revision==m_latestRevision;
    const bool publish=context&&!result.cancelled&&newer&&current&&
        (result.error.isEmpty()||result.revision==m_latestRevision);
    if(publish){m_publishedRevision=result.revision;m_publishedQuality=result.quality;m_hasPublished=true;}
    if(m_pending){auto next=std::move(*m_pending);m_pending.reset();start(std::move(next));}
    // Emit last: a consumer may invalidate or destroy its renderer immediately.
    if(publish)emit completed(result);
}
}
