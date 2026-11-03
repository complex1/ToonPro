#include "effects_preview_renderer.h"
#include "animation_render.h"
#include "animation_test_fixture.h"
#include "brush_engine.h"
#include "camera_animation.h"
#include "effect_render.h"
#include "scene3d_cache.h"
#include "sheet.h"
#include "symbols.h"
#include <QElapsedTimer>
#include <QFontMetricsF>
#include <QMutex>
#include <QMutexLocker>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QtTest>
#include <stdexcept>
#include <cstring>
using namespace toon;
namespace {
EffectsPreviewSnapshot fixture(int dimension=512) {
    EffectsPreviewSnapshot s;s.context=1;s.revision=1;s.project=Project::create();
    s.project.width=dimension;s.project.height=dimension;s.shot=s.project.shots.first();
    auto &layer=s.shot.layers.first();
    QImage tile(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);tile.fill(Qt::transparent);
    {QPainter p(&tile);p.fillRect(QRect(40,50,120,100),QColor(90,170,210,190));}
    layer.tiles.insert({0,0},tile);layer.tiles.insert({-1,0},tile);
    layer.effects.blurEnabled=true;layer.effects.blurRadius=7;layer.effects.shadowEnabled=true;
    layer.effects.shadowOffset={17.25,-8.5};layer.effects.shadowRadius=13;
    layer.mask=LayerMask{};layer.mask->defaultValue=193;
    s.pixelSize={700,500};s.view.translate(80,30);s.view.scale(.52,.52);s.view.rotate(4);
    return s;
}
QImage reference(const EffectsPreviewSnapshot &s) {
    Scene3DLayerPreviewScope sceneScope(s.scenePreviews);
    QImage image(s.pixelSize,QImage::Format_ARGB32_Premultiplied);image.setDevicePixelRatio(s.devicePixelRatio);
    image.setDotsPerMeterX(s.dotsPerMeterX);image.setDotsPerMeterY(s.dotsPerMeterY);image.fill(s.surround);
    const auto g=sheetGeometry(s.project,s.shot);
    const auto camera=s.applyCamera?shotCameraTransform(s.shot,s.frame,QSizeF(s.project.width,s.project.height)):QTransform();
    QPainter p(&image);if(!s.viewportClip.isEmpty())p.setClipRegion(s.viewportClip);p.setTransform(s.view);
    if(s.drawPaperShadow)p.fillRect(g.paper.translated(8,10),QColor(0,0,0,65));
    paintSheet(p,s.project,s.shot,s.guides,s.transformPreview?&*s.transformPreview:nullptr,s.smoothDisplay,true,s.frame,s.applyCamera);
    if(s.onionEnabled) {
        const auto sourceBounds=camera.inverted().mapRect(g.paper.adjusted(0,g.header.height(),0,0).translated(-g.output.topLeft()));
        QImage onions(s.pixelSize,QImage::Format_ARGB32_Premultiplied);onions.setDevicePixelRatio(s.devicePixelRatio);onions.fill(Qt::transparent);
        QPainter overlay(&onions);
        for(int direction:{-1,1}) {
            const auto samples=onionSkinSamples(s.shot,s.frame,s.onionTrackId,direction,direction<0?s.onionSettings.previousCount:s.onionSettings.nextCount);
            for(auto sample=samples.crbegin();sample!=samples.crend();++sample) {
                QImage ghost(s.pixelSize,QImage::Format_ARGB32_Premultiplied);ghost.setDevicePixelRatio(s.devicePixelRatio);ghost.fill(Qt::transparent);
                QPainter ink(&ghost);ink.setTransform(s.view);ink.setClipRect(g.paper.adjusted(0,g.header.height(),0,0));
                ink.translate(g.output.topLeft());ink.setTransform(camera,true);
                paintLayers(ink,onionSkinLayersForSample(s.shot,s.frame,s.onionTrackId,*sample,sourceBounds),false,nullptr,s.smoothDisplay);
                ink.resetTransform();ink.setClipping(false);ink.setCompositionMode(QPainter::CompositionMode_SourceIn);
                ink.fillRect(QRectF(QPointF(),QSizeF(s.pixelSize)/s.devicePixelRatio),direction<0?s.onionSettings.previousColor:s.onionSettings.nextColor);ink.end();
                overlay.setOpacity(s.onionSettings.opacity);overlay.drawImage(QPointF(),ghost);
            }
        }
        overlay.end();p.save();p.resetTransform();p.drawImage(QPointF(),onions);p.restore();
    }
    if(s.symmetry.mode!=SymmetryMode::Off) {
        auto transform=s.view;transform.translate(g.output.x(),g.output.y());p.setTransform(camera*transform);
        auto color=s.symmetryColor;color.setAlpha(170);QPen pen(color,1,Qt::DashLine);pen.setCosmetic(true);
        p.setPen(pen);p.setBrush(Qt::NoBrush);p.drawPath(brushSymmetryGuide(s.symmetry,QSizeF(s.project.width,s.project.height)));
    }
    p.end();return image;
}
// Frozen pre-split paintSheet sequence: validates the background/artwork split
// independently of the new helper delegation, especially tiny 4K sheet labels.
void legacySheet(QPainter &painter,const EffectsPreviewSnapshot &s) {
    const auto &p=s.project;const auto &shot=s.shot;const auto g=sheetGeometry(p,shot);
    QFont titleFont("Arial");titleFont.setPixelSize(30);titleFont.setBold(true);
    QFont bodyFont("Arial");bodyFont.setPixelSize(22);
    painter.save();painter.setRenderHint(QPainter::Antialiasing);painter.fillRect(g.paper,Qt::white);
    painter.save();painter.setClipRect(g.header,Qt::IntersectClip);painter.setPen(QColor("#222a35"));painter.setFont(titleFont);
    QRectF titleBounds;painter.drawText(g.header.adjusted(28,20,-28,-20),Qt::TextWrapAnywhere,p.movie+"  /  "+shot.number,&titleBounds);
    painter.setFont(bodyFont);painter.drawText(QRectF(28,titleBounds.bottom()+10,g.header.width()-56,g.header.height()-titleBounds.bottom()-52),Qt::TextWrapAnywhere,shot.name);
    painter.setPen(QColor("#596575"));
    const auto footer=QString("%1 × %2 px     %3 FPS     %4 frames / %5 s     LAYOUT")
        .arg(p.width).arg(p.height).arg(p.fps,0,'g',6).arg(shot.durationFrames).arg(shot.durationFrames/p.fps,0,'f',2);
    painter.drawText(QRectF(28,g.header.bottom()-36,g.header.width()-56,28),Qt::AlignLeft|Qt::AlignVCenter,
        QFontMetricsF(bodyFont).elidedText(footer,Qt::ElideRight,g.header.width()-56));
    painter.restore();painter.setPen(QPen(QColor("#b4bdc9"),1));painter.drawLine(g.header.bottomLeft(),g.header.bottomRight());
    painter.save();painter.setClipRect(g.paper.adjusted(0,g.header.height(),0,0),Qt::IntersectClip);painter.translate(g.output.topLeft());
    const auto camera=s.applyCamera?shotCameraTransform(shot,s.frame,QSizeF(p.width,p.height)):QTransform();painter.setWorldTransform(camera,true);
    const auto visible=g.paper.adjusted(0,g.header.height(),0,0).translated(-g.output.topLeft());
    const auto sourceBounds=camera.inverted().mapRect(visible).adjusted(-2,-2,2,2);
    paintLayers(painter,evaluateLayers(shot,s.frame,false,sourceBounds),true,nullptr,s.smoothDisplay,QSize(p.width,p.height));painter.restore();
    if(s.guides) {
        painter.setPen(QPen(QColor("#bdc8d6"),1,Qt::DashLine));painter.drawRect(g.drawing);
        painter.setPen(QPen(QColor("#526d91"),2));painter.drawRect(g.output);const auto center=g.output.center();
        for(auto point:{QPointF(center.x(),g.output.top()),QPointF(center.x(),g.output.bottom())})painter.drawLine(point-QPointF(0,10),point+QPointF(0,10));
        for(auto point:{QPointF(g.output.left(),center.y()),QPointF(g.output.right(),center.y())})painter.drawLine(point-QPointF(10,0),point+QPointF(10,0));
        if(shot.sheet.safeArea){painter.setPen(QPen(QColor("#99a9bc"),1,Qt::DashLine));painter.drawRect(g.output.adjusted(p.width*.05,p.height*.05,-p.width*.05,-p.height*.05));}
    }
    painter.restore();
}
EffectsPreviewResult renderPrepared(EffectsPreviewSnapshot s,const EffectsPreviewCancellation &token) {
    prepareEffectsPreviewBackground(s);
    return renderEffectsPreview(s,token);
}
EffectsPreviewResult resultFor(const EffectsPreviewSnapshot &s) {
    EffectsPreviewResult r;r.context=s.context;r.revision=s.revision;r.quality=s.quality;
    r.image=QImage(8,8,QImage::Format_ARGB32_Premultiplied);r.image.fill(QColor(int(s.revision)%255,0,0));return r;
}
struct Gate {
    QSemaphore started,release;
    QMutex mutex;
    QVector<QPair<quint64,quint64>> requests;
    QVector<EffectsPreviewCancellation> tokens;
    std::atomic_int finished{0};
    EffectsPreviewResult render(const EffectsPreviewSnapshot &s,const EffectsPreviewCancellation &token) {
        {QMutexLocker lock(&mutex);requests.append({s.context,s.revision});tokens.append(token);}
        started.release();release.acquire();++finished;return resultFor(s);
    }
};
}
class EffectsPreviewRendererTests final:public QObject {
    Q_OBJECT
private slots:
    void latestPendingReplacesWorkButCompletedDraftMakesProgress() {
        auto gate=std::make_shared<Gate>();const auto cleanup=qScopeGuard([gate]{gate->release.release(20);});
        EffectsPreviewRenderer renderer([gate](const auto &s,const auto &c){return gate->render(s,c);});
        QSignalSpy ready(&renderer,&EffectsPreviewRenderer::completed);
        auto s=fixture();s.quality=EffectsPreviewQuality::Draft;renderer.request(s);QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());
        s.revision=2;renderer.request(s);s.revision=3;s.quality=EffectsPreviewQuality::Exact;renderer.request(s);
        QVERIFY(renderer.busy());QVERIFY(renderer.hasPending());
        gate->release.release();QTRY_COMPARE(ready.count(),1);QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());
        const auto first=qvariant_cast<EffectsPreviewResult>(ready[0][0]);QCOMPARE(first.revision,quint64(1));QCOMPARE(first.quality,EffectsPreviewQuality::Draft);
        {QMutexLocker lock(&gate->mutex);QCOMPARE(gate->requests.size(),2);QCOMPARE(gate->requests[1].second,quint64(3));QVERIFY(!gate->tokens[0]->load());}
        gate->release.release();QTRY_COMPARE(ready.count(),2);QTRY_VERIFY(!renderer.busy());
        QCOMPARE(qvariant_cast<EffectsPreviewResult>(ready[1][0]).revision,quint64(3));QVERIFY(!renderer.hasPending());
    }
    void exactWorkYieldsAndOldContextsNeverPublish() {
        auto gate=std::make_shared<Gate>();const auto cleanup=qScopeGuard([gate]{gate->release.release(20);});
        EffectsPreviewRenderer renderer([gate](const auto &s,const auto &c){return gate->render(s,c);});QSignalSpy ready(&renderer,&EffectsPreviewRenderer::completed);
        auto s=fixture();renderer.request(s);QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());s.revision=2;renderer.request(s);
        {QMutexLocker lock(&gate->mutex);QVERIFY(gate->tokens[0]->load());}
        renderer.invalidate();s.context=9;s.revision=1;renderer.request(s);gate->release.release();
        QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());QCOMPARE(ready.count(),0);
        gate->release.release();QTRY_COMPARE(ready.count(),1);QCOMPARE(qvariant_cast<EffectsPreviewResult>(ready[0][0]).context,quint64(9));
    }
    void playbackFinishesFramesAndKeepsOnlyTheLatestWaitingFrame() {
        auto gate=std::make_shared<Gate>();const auto cleanup=qScopeGuard([gate]{gate->release.release(20);});
        EffectsPreviewRenderer renderer([gate](const auto &s,const auto &c){return gate->render(s,c);});
        QSignalSpy ready(&renderer,&EffectsPreviewRenderer::completed);
        auto s=fixture();s.playback=true;s.frame=1;renderer.request(s);
        QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());
        // Several timer ticks arrive while one expensive frame is blocked.
        // Exact edit work used to cancel here, indefinitely during playback.
        for(int frame=2;frame<=8;++frame){s.revision=quint64(frame);s.frame=frame;renderer.request(s);}
        {QMutexLocker lock(&gate->mutex);QCOMPARE(gate->requests.size(),1);QVERIFY(!gate->tokens[0]->load());}
        gate->release.release();QTRY_COMPARE(ready.count(),1);
        const auto first=qvariant_cast<EffectsPreviewResult>(ready[0][0]);QCOMPARE(first.revision,quint64(1));QCOMPARE(first.frame,1);QVERIFY(first.playback);
        QCOMPARE(first.quality,EffectsPreviewQuality::Exact);
        QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());
        {QMutexLocker lock(&gate->mutex);QCOMPARE(gate->requests.size(),2);QCOMPARE(gate->requests[1].second,quint64(8));}
        // Looping can move the frame number backwards; request revisions must
        // still advance so an earlier loop cannot replace a newer display.
        s.revision=9;s.frame=0;renderer.request(s);s.revision=10;s.frame=1;renderer.request(s);
        gate->release.release();QTRY_COMPARE(ready.count(),2);
        QCOMPARE(qvariant_cast<EffectsPreviewResult>(ready[1][0]).frame,8);
        QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());
        {QMutexLocker lock(&gate->mutex);QCOMPARE(gate->requests.size(),3);QCOMPARE(gate->requests[2].second,quint64(10));QVERIFY(!gate->tokens[1]->load());}
        gate->release.release();QTRY_COMPARE(ready.count(),3);QTRY_VERIFY(!renderer.busy());
        const auto last=qvariant_cast<EffectsPreviewResult>(ready[2][0]);QCOMPARE(last.revision,quint64(10));QCOMPARE(last.frame,1);
        QVERIFY(!renderer.hasPending());QTest::qWait(180);QCOMPARE(ready.count(),3);QVERIFY(!renderer.busy());
    }
    void stoppingPlaybackAndChangingViewRejectUnfinishedFrames() {
        auto gate=std::make_shared<Gate>();const auto cleanup=qScopeGuard([gate]{gate->release.release(20);});
        EffectsPreviewRenderer renderer([gate](const auto &s,const auto &c){return gate->render(s,c);});QSignalSpy ready(&renderer,&EffectsPreviewRenderer::completed);
        auto s=fixture();s.playback=true;s.frame=4;renderer.request(s);
        QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());
        s.revision=2;s.frame=7;renderer.request(s);
        // Even when a caller reuses its context ID, leaving playback rejects
        // that context's unfinished frame and pending animation request.
        s.playback=false;s.revision=3;s.frame=6;renderer.request(s);
        {QMutexLocker lock(&gate->mutex);QVERIFY(gate->tokens[0]->load());}
        gate->release.release();QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());QCOMPARE(ready.count(),0);
        gate->release.release();QTRY_COMPARE(ready.count(),1);
        const auto stopped=qvariant_cast<EffectsPreviewResult>(ready[0][0]);QCOMPARE(stopped.frame,6);QVERIFY(!stopped.playback);
        s.context=2;s.playback=true;s.revision=4;s.frame=8;renderer.request(s);
        QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());
        renderer.invalidate();s.context=3;s.revision=5;s.frame=9;renderer.request(s);
        gate->release.release();QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());QCOMPARE(ready.count(),1);
        gate->release.release();QTRY_COMPARE(ready.count(),2);
        QCOMPARE(qvariant_cast<EffectsPreviewResult>(ready[1][0]).context,quint64(3));
        QCOMPARE(qvariant_cast<EffectsPreviewResult>(ready[1][0]).frame,9);
    }
    void deletingAnOwnerDoesNotWaitForItsWorkerOrPublish() {
        auto gate=std::make_shared<Gate>();const auto cleanup=qScopeGuard([gate]{gate->release.release(20);});
        auto *renderer=new EffectsPreviewRenderer([gate](const auto &s,const auto &c){return gate->render(s,c);});
        int completions=0;connect(renderer,&EffectsPreviewRenderer::completed,this,[&]{++completions;});
        renderer->request(fixture());QTRY_VERIFY(gate->started.available()>0);QVERIFY(gate->started.tryAcquire());QElapsedTimer elapsed;elapsed.start();delete renderer;
        QVERIFY(elapsed.elapsed()<100);{QMutexLocker lock(&gate->mutex);QVERIFY(gate->tokens[0]->load());}
        gate->release.release();QTRY_COMPARE(gate->finished.load(),1);QTest::qWait(20);QCOMPARE(completions,0);
    }
    void idleDraftRefinesWithoutAnotherEditAndDeliversOnOwnerThread() {
        EffectsPreviewRenderer renderer([](const auto &s,const auto &){return resultFor(s);});QSignalSpy ready(&renderer,&EffectsPreviewRenderer::completed);
        bool ownerThread=true;connect(&renderer,&EffectsPreviewRenderer::completed,this,[&]{ownerThread&=QThread::currentThread()==renderer.thread();});
        auto s=fixture();s.quality=EffectsPreviewQuality::Draft;renderer.request(s);
        QTRY_COMPARE(ready.count(),1);QVERIFY(renderer.hasPending());
        QTRY_COMPARE(ready.count(),2);const auto full=qvariant_cast<EffectsPreviewResult>(ready[1][0]);
        QCOMPARE(full.quality,EffectsPreviewQuality::Exact);QCOMPARE(full.revision,s.revision);QVERIFY(ownerThread);QVERIFY(!renderer.hasPending());
    }
    void futureExceptionBecomesAnErrorAndDoesNotBreakNextRequest() {
        EffectsPreviewRenderer renderer([](const auto &s,const auto &)->EffectsPreviewResult {
            if(s.revision==1)throw std::runtime_error("test render failure");return resultFor(s);
        });QSignalSpy ready(&renderer,&EffectsPreviewRenderer::completed);auto s=fixture();renderer.request(s);
        QTRY_COMPARE(ready.count(),1);QVERIFY(!qvariant_cast<EffectsPreviewResult>(ready[0][0]).error.isEmpty());
        s.revision=2;renderer.request(s);QTRY_COMPARE(ready.count(),2);QVERIFY(!qvariant_cast<EffectsPreviewResult>(ready[1][0]).image.isNull());
    }
    void exactSnapshotMatchesOriginalSheetAtDprAndDpi_data() {
        QTest::addColumn<qreal>("ratio");QTest::addColumn<int>("dots");
        QTest::newRow("standard")<<1.<<3780;QTest::newRow("retina")<<2.<<2835;
    }
    void exactSnapshotMatchesOriginalSheetAtDprAndDpi() {
        QFETCH(qreal,ratio);QFETCH(int,dots);auto s=fixture();s.devicePixelRatio=ratio;s.dotsPerMeterX=s.dotsPerMeterY=dots;
        s.smoothDisplay=true;s.shot.camera2D={{9,-7},1.13,7};s.symmetry.mode=SymmetryMode::DualAxis;
        const auto original=s.shot.layers[0].tiles;const auto expected=reference(s);
        EffectsPreviewRenderer renderer;QSignalSpy ready(&renderer,&EffectsPreviewRenderer::completed);
        renderer.request(s);QTRY_COMPARE(ready.count(),1);
        const auto result=qvariant_cast<EffectsPreviewResult>(ready[0][0]);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.cancelled);QCOMPARE(result.sourceScale,1.);
        QCOMPARE(result.image,expected);QCOMPARE(result.image.devicePixelRatio(),ratio);QCOMPARE(s.shot.layers[0].tiles,original);
    }
    void fourKHeaderAndArtworkMatchPreSplitSheetSequence() {
        for(qreal ratio:{1.,2.}) {
            auto s=fixture(4096);s.devicePixelRatio=ratio;s.dotsPerMeterX=s.dotsPerMeterY=2835;
            s.pixelSize={1600,1000};s.project.movie="4K effects preview";s.shot.name="Original sheet text and artwork";s.shot.sheet.safeArea=true;
            const auto geometry=sheetGeometry(s.project,s.shot);s.view={};s.view.translate(23,17);s.view.scale(.19/ratio,.19/ratio);
            QImage expected(s.pixelSize,QImage::Format_ARGB32_Premultiplied);expected.setDevicePixelRatio(ratio);
            expected.setDotsPerMeterX(s.dotsPerMeterX);expected.setDotsPerMeterY(s.dotsPerMeterY);expected.fill(s.surround);
            {QPainter painter(&expected);painter.setTransform(s.view);painter.fillRect(geometry.paper.translated(8,10),QColor(0,0,0,65));legacySheet(painter,s);}
            QCOMPARE(reference(s),expected);
            EffectsPreviewRenderer renderer;QSignalSpy ready(&renderer,&EffectsPreviewRenderer::completed);renderer.request(s);
            QTRY_COMPARE(ready.count(),1);const auto result=qvariant_cast<EffectsPreviewResult>(ready[0][0]);
            QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.image,expected);
        }
    }

    void fourKCanvasViewportClipMatchesOriginalGuideRasterization_data() {
        QTest::addColumn<qreal>("ratio");
        QTest::newRow("canvas-standard")<<1.;QTest::newRow("canvas-fractional")<<1.25;QTest::newRow("canvas-retina")<<2.;
    }
    void fourKCanvasViewportClipMatchesOriginalGuideRasterization() {
        QFETCH(qreal,ratio);auto s=fixture(3840);s.project.height=2160;s.shot.sheet.overscan=0;
        const QSize viewport(1068,915);s.devicePixelRatio=ratio;s.dotsPerMeterX=s.dotsPerMeterY=2835;
        s.pixelSize={qCeil(viewport.width()*ratio),qCeil(viewport.height()*ratio)};
        // Capture the original logical QWidget rect, including when ceil(pixel
        // size)/DPR differs from it. This is the actual 4K benchmark fit view.
        s.viewportClip=QRegion(QRect(QPoint(),viewport));
        const auto geometry=sheetGeometry(s.project,s.shot);
        const qreal zoom=qMin((viewport.width()-80.)/geometry.paper.width(),(viewport.height()-80.)/geometry.paper.height());
        s.view={};s.view.translate(viewport.width()/2.,viewport.height()/2.);s.view.scale(zoom,zoom);
        s.view.translate(-geometry.paper.center().x(),-geometry.paper.center().y());
        QImage expected(s.pixelSize,QImage::Format_ARGB32_Premultiplied);expected.setDevicePixelRatio(ratio);
        expected.setDotsPerMeterX(s.dotsPerMeterX);expected.setDotsPerMeterY(s.dotsPerMeterY);expected.fill(s.surround);
        {QPainter painter(&expected);painter.setClipRegion(s.viewportClip);painter.setTransform(s.view);
            painter.fillRect(geometry.paper.translated(8,10),QColor(0,0,0,65));legacySheet(painter,s);}
        QCOMPARE(reference(s),expected);
        EffectsPreviewRenderer renderer;QSignalSpy ready(&renderer,&EffectsPreviewRenderer::completed);renderer.request(s);
        QTRY_COMPARE(ready.count(),1);const auto result=qvariant_cast<EffectsPreviewResult>(ready[0][0]);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.image,expected);
    }

    void authoredTextRebuiltOnWorkerMatchesSynchronousArtwork_data() {
        QTest::addColumn<qreal>("ratio");QTest::newRow("text-standard")<<1.;QTest::newRow("text-retina")<<2.;
    }
    void authoredTextRebuiltOnWorkerMatchesSynchronousArtwork() {
        QFETCH(qreal,ratio);auto s=fixture();s.devicePixelRatio=ratio;s.smoothDisplay=true;
        auto &layer=s.shot.layers[0];layer.kind=LayerKind::Text;layer.tiles.clear();
        auto text=EditableObject::create(ObjectKind::Text);text.text="Toon Pro fi\nPaint & motion";text.fontFamily="Arial";
        text.position={24,42};text.fontSize=31;text.textWidth=210;text.lineSpacing=1.15;text.bold=true;text.italic=true;
        text.fill=QColor(190,70,35,210);text.stroke=QColor(30,40,50);text.strokeWidth=1.25;layer.objects={text};
        layer.effects.colorEnabled=true;layer.effects.hue=41;layer.effects.contrast=13;
        QString error;QVERIFY2(rebuildObjectLayer(layer,{0,0,512,512},maxProjectTiles,&error),qPrintable(error));
        s.shot.camera2D={{7,-3},1.12,5};const auto tiles=layer.tiles;const auto objects=layer.objects;
        const auto expected=reference(s);
        auto blank=s;blank.shot.layers.clear();QVERIFY(expected!=reference(blank));
        EffectsPreviewRenderer renderer;QSignalSpy ready(&renderer,&EffectsPreviewRenderer::completed);
        renderer.request(s);QTRY_COMPARE(ready.count(),1);const auto result=qvariant_cast<EffectsPreviewResult>(ready[0][0]);
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.image,expected);
        QCOMPARE(s.shot.layers[0].objects,objects);QCOMPARE(s.shot.layers[0].tiles,tiles);
    }

    void transformedPreviewAndPreparedScenesRemainExact() {
        auto s=fixture();RasterLayerPreview preview;preview.layerId=s.shot.layers[0].id;preview.moving=s.shot.layers[0];preview.transform.translate(21,13);s.transformPreview=preview;
        auto scene=RasterLayer::create("Prepared scene");scene.kind=LayerKind::Scene3D;scene.scene3d=Scene3D{};s.shot.layers.prepend(scene);
        QImage pixels(64,64,QImage::Format_ARGB32_Premultiplied);pixels.fill(QColor(30,60,100,180));s.scenePreviews.insert(scene.id,pixels);
        auto result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.image,reference(s));
        s.quality=EffectsPreviewQuality::Draft;result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));QCOMPARE(result.sourceScale,1.);QCOMPARE(result.image,reference(s));
    }
    void onionSamplesAndSymmetryMatchExistingComposition() {
        auto s=fixture();s.project=animation_fixture::project();s.shot=s.project.shots[0];s.frame=4;
        s.onionEnabled=true;s.onionTrackId=s.shot.animationTracks[0].id;s.onionSettings.opacity=.37;s.symmetry.mode=SymmetryMode::Vertical;
        const auto result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.image,reference(s));
        s.onionEnabled=false;QVERIFY(result.image!=reference(s));
    }
    void draftResolutionAdaptsAndNeverChangesSource_data() {
        QTest::addColumn<int>("dimension");QTest::addColumn<qreal>("scale");
        QTest::newRow("small")<<512<<1.;QTest::newRow("medium")<<2048<<.5;QTest::newRow("four-k")<<4096<<.125;
    }
    void draftResolutionAdaptsAndNeverChangesSource() {
        QFETCH(int,dimension);QFETCH(qreal,scale);auto s=fixture(dimension);s.quality=EffectsPreviewQuality::Draft;
        auto folder=RasterLayer::create("Folder");folder.group=true;folder.renderTransform.translate(19,-11);folder.renderTransform.scale(1.1,.9);
        s.shot.layers[0].parentId=folder.id;s.shot.layers[0].renderTransform.translate(7,13);s.shot.layers.prepend(folder);
        const auto original=s.shot.layers;const auto result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QVERIFY(!result.image.isNull());QCOMPARE(result.sourceScale,scale);
        QCOMPARE(s.shot.layers[1].tiles,original[1].tiles);QCOMPARE(s.shot.layers[1].mask,original[1].mask);QCOMPARE(s.shot.layers[1].renderTransform,original[1].renderTransform);
        s.quality=EffectsPreviewQuality::Exact;const auto exact=renderPrepared(s,std::make_shared<std::atomic_bool>(false));QCOMPARE(exact.sourceScale,1.);QCOMPARE(exact.image,reference(s));
        if(scale==1)QCOMPARE(result.image,exact.image);
    }
    void draftKeepsNegativeTilesFolderGeometryAndAdjustmentMasksAligned() {
        for(int dimension:{2048,4096}) {
            auto s=fixture(dimension);s.pixelSize={1000,800};s.view={};s.guides=false;s.drawPaperShadow=false;
            auto folder=RasterLayer::create("Scaled folder");folder.group=true;folder.opacity=.8;
            folder.renderTransform.translate(100,50);folder.renderTransform.scale(1.5,.75);
            auto layer=RasterLayer::create("Negative source");layer.parentId=folder.id;layer.renderTransform.translate(160,0);
            QImage ink(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);ink.fill(QColor(70,110,170,192));layer.tiles.insert({-1,0},ink);
            QImage mask(tileSize,tileSize,QImage::Format_Grayscale8);mask.fill(255);
            for(int y=0;y<tileSize;++y)std::memset(mask.scanLine(y),0,128);
            layer.mask=LayerMask{};layer.mask->tiles.insert({-1,0},mask);
            auto adjustment=RasterLayer::create("Masked exposure");adjustment.parentId=folder.id;adjustment.adjustment=true;
            adjustment.renderTransform=layer.renderTransform;adjustment.effects.colorEnabled=true;adjustment.effects.exposure=1;
            mask.fill(0);for(int y=0;y<tileSize;++y)std::memset(mask.scanLine(y)+128,255,64);
            adjustment.mask=LayerMask{};adjustment.mask->defaultValue=0;adjustment.mask->tiles.insert({-1,0},mask);
            s.shot.layers={folder,layer,adjustment};const auto expected=reference(s);
            s.quality=EffectsPreviewQuality::Draft;const auto draft=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
            QVERIFY2(draft.error.isEmpty(),qPrintable(draft.error));QCOMPARE(draft.sourceScale,dimension==2048?.5:.125);
            const auto origin=sheetGeometry(s.project,s.shot).output.topLeft();
            for(const QPointF source:{QPointF(-100,100),QPointF(-32,100),QPointF(-180,100),QPointF(40,100)}) {
                const auto point=(origin+folder.renderTransform.map(layer.renderTransform.map(source))).toPoint();
                QVERIFY(draft.image.rect().contains(point));
                // Away from resampled edges, source color/coverage, masked-out
                // space and the adjustment's boundary must stay at their exact
                // original artwork positions, including negative tile origins.
                QCOMPARE(draft.image.pixel(point),expected.pixel(point));
            }
            const auto bright=(origin+folder.renderTransform.map(layer.renderTransform.map(QPointF(-100,100)))).toPoint();
            const auto base=(origin+folder.renderTransform.map(layer.renderTransform.map(QPointF(-32,100)))).toPoint();
            QVERIFY(expected.pixel(bright)!=expected.pixel(base));
        }
    }

    void cancelledSnapshotHasNoPartialImageOrErrorAndCanRenderAgain() {
        const auto s=fixture();auto cancel=std::make_shared<std::atomic_bool>(true);const auto cancelled=renderPrepared(s,cancel);
        QVERIFY(cancelled.cancelled);QVERIFY(cancelled.image.isNull());QVERIFY(cancelled.error.isEmpty());
        cancel->store(false);const auto result=renderPrepared(s,cancel);QVERIFY(!result.cancelled);QVERIFY(result.error.isEmpty());QCOMPARE(result.image,reference(s));
    }
    void explicitPlaybackResolutionKeepsCameraFolderAndMaskCoordinates_data(){
        QTest::addColumn<int>("divisor");for(int divisor:{2,4,8})QTest::newRow(qPrintable(QString::number(divisor)))<<divisor;
    }
    void explicitPlaybackResolutionKeepsCameraFolderAndMaskCoordinates(){
        QFETCH(int,divisor);auto s=fixture();s.playback=true;s.playbackDivisor=divisor;s.frame=6;
        s.pixelSize={1100,1000};s.view={};s.guides=false;s.drawPaperShadow=false;
        auto folder=RasterLayer::create("Animated folder");folder.group=true;folder.opacity=.8;
        auto layer=RasterLayer::create("Negative source");layer.parentId=folder.id;
        QImage ink(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);ink.fill(QColor(70,110,170,192));layer.tiles.insert({-1,0},ink);
        QImage mask(tileSize,tileSize,QImage::Format_Grayscale8);mask.fill(255);for(int y=0;y<tileSize;++y)std::memset(mask.scanLine(y),0,128);
        layer.mask=LayerMask{};layer.mask->tiles.insert({-1,0},mask);
        auto adjustment=RasterLayer::create("Masked correction");adjustment.parentId=folder.id;adjustment.adjustment=true;
        adjustment.effects.colorEnabled=true;adjustment.effects.exposure=1;
        mask.fill(0);for(int y=0;y<tileSize;++y)std::memset(mask.scanLine(y)+128,255,64);
        adjustment.mask=LayerMask{};adjustment.mask->defaultValue=0;adjustment.mask->tiles.insert({-1,0},mask);
        s.shot.layers={folder,layer,adjustment};
        s.shot.layerAnimations={{folder.id,{128,64},{{KeyframeProperty::PositionX,{{0,160},{12,240}}},
            {KeyframeProperty::PositionY,{{0,64},{12,96}}},{KeyframeProperty::ScaleX,{{0,1.5}}},{KeyframeProperty::ScaleY,{{0,.75}}}}},
            {layer.id,{},{{KeyframeProperty::PositionX,{{0,160}}}}},{adjustment.id,{},{{KeyframeProperty::PositionX,{{0,160}}}}}};
        s.shot.camera2DKeys={{0,{{-32,-16},1.1,4}},{12,{{32,16},1.2,-4}}};
        const auto original=s.shot.layers;const auto animations=s.shot.layerAnimations;const auto expected=reference(s);
        const auto result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.sourceScale,1./divisor);QVERIFY(result.playbackFallbackReason.isEmpty());
        const auto origin=sheetGeometry(s.project,s.shot).output.topLeft();
        const auto camera=shotCameraTransform(s.shot,s.frame,QSizeF(s.project.width,s.project.height));
        for(const QPointF point:{QPointF(-100,100),QPointF(-32,100),QPointF(-180,100),QPointF(40,100)}){
            const auto pixel=(origin+camera.map(worldLayerTransform(s.shot,layer.id,s.frame).map(point))).toPoint();
            QVERIFY(result.image.rect().contains(pixel));QCOMPARE(result.image.pixel(pixel),expected.pixel(pixel));
        }
        QCOMPARE(s.shot.layers[1].tiles,original[1].tiles);QCOMPARE(s.shot.layers[1].mask,original[1].mask);QCOMPARE(s.shot.layerAnimations,animations);
        s.playback=false;const auto stopped=renderPrepared(s,std::make_shared<std::atomic_bool>(false));QCOMPARE(stopped.sourceScale,1.);QCOMPARE(stopped.image,reference(s));
    }
    void playbackNestedSymbolsFollowCelClocksAndLibraryEdits(){
        auto s=fixture();s.playback=true;s.playbackDivisor=4;s.pixelSize={1100,900};s.view={};s.guides=false;s.drawPaperShadow=false;
        auto innerProject=animation_fixture::project();SymbolDefinition inner{animation_fixture::id(),"Cel source",innerProject.shots.first()};
        auto nested=RasterLayer::create("Nested");nested.kind=LayerKind::Symbol;nested.symbol=SymbolInstance{inner.id};
        SymbolDefinition outer{animation_fixture::id(),"Nested animation",Project::create().shots.first()};outer.content.layers={nested};outer.content.durationFrames=20;
        outer.content.layerAnimations={{nested.id,{},{{KeyframeProperty::PositionX,{{0,16},{10,80}}}}}};
        s.project.symbols={inner,outer};auto instance=RasterLayer::create("Animated instance");instance.kind=LayerKind::Symbol;instance.symbol=SymbolInstance{outer.id};
        s.shot.layers={instance};s.shot.layerAnimations={{instance.id,{},{{KeyframeProperty::PositionX,{{0,80},{10,160}}}}}};
        const auto original=s.project.symbols;QImage first;
        for(int frame:{0,4,7,0}){
            s.frame=frame;const auto result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
            QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.sourceScale,.25);QVERIFY(result.playbackFallbackReason.isEmpty());
            const auto expected=reference(s);const auto origin=sheetGeometry(s.project,s.shot).output.topLeft();
            const auto source=QPointF(64,64)+evaluateAnimatedPosition(s.shot.layerAnimations[0],frame)+evaluateAnimatedPosition(outer.content.layerAnimations[0],frame);
            QCOMPARE(result.image.pixel((origin+source).toPoint()),expected.pixel((origin+source).toPoint()));
            if(frame==0)first=result.image;
        }
        // Preserve identifiers while replacing pixels: no retained reduced
        // library or per-frame Symbol render may publish the old red artwork.
        for(auto &layer:s.project.symbols[0].content.layers)if(!layer.tiles.isEmpty())for(auto &tile:layer.tiles)tile.fill(Qt::green);
        s.frame=0;const auto edited=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(edited.error.isEmpty(),qPrintable(edited.error));QVERIFY(edited.image!=first);
        const auto point=(sheetGeometry(s.project,s.shot).output.topLeft()+QPointF(160,64)).toPoint();
        QCOMPARE(edited.image.pixel(point),reference(s).pixel(point));
        bool keptRed=false;for(const auto &layer:original[0].content.layers)for(const auto &tile:layer.tiles)keptRed|=tile.pixel(64,64)==qRgb(255,0,0);QVERIFY(keptRed);
    }
    void playbackVectorMorphKeepsArtworkRegistration(){
        auto s=fixture();s.playback=true;s.playbackDivisor=4;s.frame=5;s.pixelSize={1000,900};s.view={};s.guides=false;s.drawPaperShadow=false;
        auto layer=RasterLayer::create("Vector morph");layer.kind=LayerKind::Vector;
        auto object=EditableObject::create();object.closed=true;object.stroke=QColor();object.fill=QColor(50,140,190);
        for(auto point:{QPointF(20,20),QPointF(140,20),QPointF(140,120),QPointF(20,120)})object.nodes.append(BezierNode::corner(point));
        layer.objects={object};auto a=pathPose(object),b=a;b.transform.translate(80,40);
        s.shot.layers={layer};s.shot.layerAnimations={{layer.id,{80,70},{{KeyframeProperty::PositionX,{{0,96}}},{KeyframeProperty::ScaleX,{{0,1.5}}}},{{object.id,{{0,a},{10,b}}}}}};
        const auto source=layer.objects;const auto result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.sourceScale,.25);
        const auto point=(sheetGeometry(s.project,s.shot).output.topLeft()+worldLayerTransform(s.shot,layer.id,5).map(QPointF(100,80))).toPoint();
        QCOMPARE(result.image.pixel(point),reference(s).pixel(point));QCOMPARE(s.shot.layers[0].objects,source);
        // A same-id edit must invalidate cached scaled source arrays.
        s.shot.layers[0].objects[0].fill=Qt::yellow;s.shot.layerAnimations[0].paths[0].keys[0].pose.fill=Qt::yellow;s.shot.layerAnimations[0].paths[0].keys[1].pose.fill=Qt::yellow;
        const auto edited=renderPrepared(s,std::make_shared<std::atomic_bool>(false));QCOMPARE(edited.image.pixel(point),reference(s).pixel(point));QVERIFY(edited.image!=result.image);
    }
    void unsupportedPlaybackReductionFallsBackWithoutOmittingArtwork(){
        auto s=fixture();s.playback=true;s.playbackDivisor=3;
        auto result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));QCOMPARE(result.sourceScale,1.);QVERIFY(!result.playbackFallbackReason.isEmpty());QCOMPARE(result.image,reference(s));
        s.playbackDivisor=4;s.onionEnabled=true;
        result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));QCOMPARE(result.sourceScale,1.);QVERIFY(result.playbackFallbackReason.contains("onion"));QCOMPARE(result.image,reference(s));
        s.onionEnabled=false;s.transformPreview=RasterLayerPreview{};s.transformPreview->layerId=s.shot.layers[0].id;s.transformPreview->stationary=s.shot.layers[0];
        result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));QCOMPARE(result.sourceScale,1.);QVERIFY(result.playbackFallbackReason.contains("transform"));QCOMPARE(result.image,reference(s));
        s.transformPreview.reset();
        auto scene=RasterLayer::create("Prepared 3D reference");scene.kind=LayerKind::Scene3D;scene.scene3d=Scene3D{};s.shot.layers.prepend(scene);
        QImage pixels(64,64,QImage::Format_ARGB32_Premultiplied);pixels.fill(QColor(30,60,100,180));s.scenePreviews.insert(scene.id,pixels);
        result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.sourceScale,1.);QVERIFY(result.playbackFallbackReason.contains("3D"));QCOMPARE(result.image,reference(s));
        auto withoutScene=s;withoutScene.shot.layers.removeFirst();QVERIFY(result.image!=reference(withoutScene));
    }
    void playbackRespectsCanvasDisplaySmoothing(){
        auto s=fixture();s.playback=true;s.playbackDivisor=4;s.guides=false;s.drawPaperShadow=false;
        s.pixelSize={1000,900};s.view={};s.view.translate(40,10);s.view.scale(1.2,1.2);s.view.rotate(7);
        auto &layer=s.shot.layers[0];layer.effects={};layer.mask.reset();layer.tiles.clear();
        QImage pixels(tileSize,tileSize,QImage::Format_ARGB32_Premultiplied);pixels.fill(Qt::white);
        {QPainter painter(&pixels);for(int y=0;y<tileSize;y+=16)for(int x=0;x<tileSize;x+=16)
            if((x/16+y/16)%2)painter.fillRect(x,y,16,16,Qt::black);}
        layer.tiles.insert({0,0},pixels);const auto source=layer.tiles;
        s.smoothDisplay=false;const auto crisp=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
        s.smoothDisplay=true;const auto smooth=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(crisp.error.isEmpty(),qPrintable(crisp.error));QVERIFY2(smooth.error.isEmpty(),qPrintable(smooth.error));
        QCOMPARE(crisp.sourceScale,.25);QCOMPARE(smooth.sourceScale,.25);QVERIFY(crisp.image!=smooth.image);
        QCOMPARE(s.shot.layers[0].tiles,source);
    }
    void playbackTinyObjectTransformsFallBackWithoutRenderErrors_data(){
        QTest::addColumn<bool>("pathPose");QTest::newRow("source-object")<<false;QTest::newRow("morph-pose")<<true;
    }
    void playbackTinyObjectTransformsFallBackWithoutRenderErrors(){
        QFETCH(bool,pathPose);auto s=fixture();s.playback=true;s.playbackDivisor=8;s.frame=5;
        auto layer=RasterLayer::create("Tiny vector source");layer.kind=LayerKind::Vector;
        auto object=EditableObject::create();object.closed=true;object.stroke=QColor();object.fill=QColor(50,140,190);
        for(auto point:{QPointF(0,0),QPointF(30000,0),QPointF(30000,30000),QPointF(0,30000)})object.nodes.append(BezierNode::corner(point));
        const auto tiny=QTransform::fromScale(2e-6,2e-6);QVERIFY(tiny.isInvertible());
        QVERIFY(!(tiny*QTransform::fromScale(.125,.125)).isInvertible());
        if(!pathPose)object.transform=tiny;
        layer.objects={object};s.shot.layers={layer};
        s.shot.layerAnimations={{layer.id,{},{{KeyframeProperty::ScaleX,{{0,100}}},{KeyframeProperty::ScaleY,{{0,100}}}}}};
        if(pathPose){auto pose=toon::pathPose(object);pose.transform=tiny;s.shot.layerAnimations[0].paths={{object.id,{{0,pose},{10,pose}}}};}
        const auto result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.sourceScale,1.);
        QVERIFY(result.playbackFallbackReason.contains("small"));QCOMPARE(result.image,reference(s));
    }
    void playbackKeepsSheetFurnitureAtFullResolution(){
        auto s=fixture(4095);s.shot.layers.clear();s.playback=true;s.playbackDivisor=8;s.shot.sheet.safeArea=true;
        s.devicePixelRatio=2;s.pixelSize={1600,1200};s.view={};s.view.translate(8,12);s.view.scale(.18,.18);s.view.rotate(7);
        const auto result=renderPrepared(s,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(result.error.isEmpty(),qPrintable(result.error));QCOMPARE(result.sourceScale,.125);QCOMPARE(result.image,reference(s));
    }
};
QTEST_MAIN(EffectsPreviewRendererTests)
#include "test_effects_preview_renderer.moc"
