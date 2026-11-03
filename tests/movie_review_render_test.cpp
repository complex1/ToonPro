#include "movie_review_renderer.h"
#include "animation_test_fixture.h"
#include "sheet.h"
#include "scene3d_cache.h"
#include "symbol_poses.h"
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create();
    project.width = project.height = 128;
    project.transparent = false;
    project.shots[0].durationFrames = 12;
    project.shots[0].layers = {RasterLayer::create("Red")};
    QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    { QPainter painter(&tile); painter.fillRect(QRect(32, 32, 64, 64), Qt::red); }
    project.shots[0].layers[0].tiles.insert({0, 0}, tile);
    auto second = project.shots[0]; second.id = "second";
    second.layers = {RasterLayer::create("Blue")};
    tile.fill(Qt::transparent);
    { QPainter painter(&tile); painter.fillRect(QRect(32, 32, 64, 64), Qt::blue); }
    second.layers[0].tiles.insert({0, 0}, tile);
    project.shots.append(second);
    return project;
}
struct Gate {
    QSemaphore started, release;
    QMutex mutex;
    QVector<int> frames;
    QStringList shots;
    QVector<EffectsPreviewCancellation> tokens;
    std::atomic_int finished{0};
    EffectsPreviewResult render(const EffectsPreviewSnapshot &snapshot, const EffectsPreviewCancellation &token) {
        { QMutexLocker lock(&mutex); frames.append(snapshot.frame); shots.append(snapshot.shot.id); tokens.append(token); }
        started.release(); release.acquire(); ++finished;
        EffectsPreviewResult result;
        result.image = QImage(8, 8, QImage::Format_ARGB32_Premultiplied);
        result.image.fill(QColor(snapshot.frame, 0, 0));
        return result;
    }
};
}
class MovieReviewRenderTests final : public QObject {
    Q_OBJECT
private slots:
    void croppedFramesMatchMovieCameraEffectsAndKeepSourceUnchanged() {
        auto project = fixture();
        auto &shot = project.shots[0];
        shot.camera2DKeys = {{0, {}, KeyframeInterpolation::Linear},
                            {10, {{20, 0}, 1, 0}, KeyframeInterpolation::Linear}};
        shot.layers[0].effects.blurEnabled = true;
        shot.layers[0].effects.blurRadius = 3;
        shot.layers[0].mask = LayerMask{};
        shot.layers[0].mask->defaultValue = 190;
        auto notes = RasterLayer::create("Notes"); notes.annotation = true;
        QImage blue(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); blue.fill(Qt::blue);
        notes.tiles.insert({0, 0}, blue); shot.layers.append(notes);
        const auto before = project.toJson();
        MovieReviewRenderer renderer;
        QSignalSpy frames(&renderer, &MovieReviewRenderer::frameReady);
        QSignalSpy errors(&renderer, &MovieReviewRenderer::failed);
        renderer.setProject(project);
        for (const auto pair : {QPair(0, 0), QPair(0, 5), QPair(1, 0), QPair(0, 10)}) {
            renderer.requestFrame(pair.first, pair.second, 8, false, {128, 128});
            QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 1, 10000);
            QCOMPARE(frames[0][0].toInt(), pair.first);
            QCOMPARE(frames[0][1].toInt(), pair.second);
            const auto image = qvariant_cast<QImage>(frames[0][2]);
            QCOMPARE(image.size(), QSize(128, 128));
            QCOMPARE(image, renderMovieFrame(project, pair.second, pair.first));
            frames.clear();
        }
        QCOMPARE(errors.size(), 0);
        QCOMPARE(project.toJson(), before);
    }
    void timedSymbolsRenderAtLocalShotFrameAtEveryQuality() {
        auto project = fixture();
        SymbolDefinition source{animation_fixture::id(), "Mouth", animation_fixture::project().shots[0]};
        project.symbols.append(source);
        auto layer = RasterLayer::create("Mouth"); layer.kind = LayerKind::Symbol;
        layer.symbol = SymbolInstance{source.id}; layer.symbol->playback = SymbolPlayback::Poses;
        layer.symbol->poseKeys = {{0, 0}, {4, 4}, {8, 0}};
        project.shots[0].layers = {layer};
        MovieReviewRenderer renderer;
        QSignalSpy frames(&renderer, &MovieReviewRenderer::frameReady);
        renderer.setProject(project);
        for (int divisor : {1, 2, 4, 8}) for (int frame : {0, 4, 8}) {
            renderer.requestFrame(0, frame, divisor, true, {128, 128});
            QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 1, 10000);
            const auto image = qvariant_cast<QImage>(frames[0][2]);
            QCOMPARE(image.pixelColor(64, 64), QColor(frame == 4 ? Qt::blue : Qt::red));
            frames.clear();
        }
    }
    void nestedSymbolNotesAnd3DReferencesStayOutOfMovieReview() {
        auto project = fixture();
        auto inner = project.shots[0]; inner.id = animation_fixture::id();
        auto notes = RasterLayer::create("Notes folder"); notes.group = true; notes.annotation = true;
        auto noteDrawing = RasterLayer::create("Nested notes"); noteDrawing.parentId = notes.id;
        QImage blue(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); blue.fill(Qt::blue);
        noteDrawing.tiles.insert({0, 0}, blue);
        auto reference = RasterLayer::create("3D reference"); reference.kind = LayerKind::Scene3D; reference.scene3d = Scene3D{};
        inner.layers.append(notes); inner.layers.append(noteDrawing); inner.layers.append(reference);
        const auto innerId = animation_fixture::id();
        auto innerInstance = RasterLayer::create("Inner Symbol"); innerInstance.kind = LayerKind::Symbol;
        innerInstance.symbol = SymbolInstance{innerId};
        auto outer = inner; outer.id = animation_fixture::id(); outer.layers = {innerInstance};
        const auto outerId = animation_fixture::id();
        project.symbols = {{innerId, "Inner", inner}, {outerId, "Outer", outer}};
        auto outerInstance = RasterLayer::create("Outer Symbol"); outerInstance.kind = LayerKind::Symbol;
        outerInstance.symbol = SymbolInstance{outerId};
        project.shots[0].layers = {outerInstance, reference};
        // A ready editor reference must still never enter movie review, even
        // when the reference image already exists in the main-thread cache.
        QImage referenceImage(128, 128, QImage::Format_ARGB32_Premultiplied); referenceImage.fill(Qt::green);
        cacheScene3DPreview(scene3DCacheKey(*reference.scene3d, {128, 128}), referenceImage);
        const auto before = project.toJson();
        const auto exported = renderMovieFrame(project, 2, 0);
        QCOMPARE(exported.pixelColor(64, 64), QColor(Qt::red));
        MovieReviewRenderer renderer; renderer.setProject(project);
        QSignalSpy frames(&renderer, &MovieReviewRenderer::frameReady);
        QSignalSpy errors(&renderer, &MovieReviewRenderer::failed);
        for (const bool playing : {false, true}) {
            renderer.requestFrame(0, 2, 1, playing, {128, 128});
            QTRY_COMPARE_WITH_TIMEOUT(frames.size(), 1, 10000);
            QCOMPARE(qvariant_cast<QImage>(frames[0][2]), exported);
            frames.clear();
        }
        QCOMPARE(errors.size(), 0); QCOMPARE(project.toJson(), before);
    }
    void thumbnailsUseArtworkAndReturnCachedImagesWithoutRenderingAgain() {
        auto count = std::make_shared<std::atomic_int>(0);
        MovieReviewRenderer renderer([count](const auto &snapshot, const auto &) {
            ++*count;
            EffectsPreviewResult result;
            result.image = snapshot.preparedBackground;
            return result;
        });
        QSignalSpy thumbnails(&renderer, &MovieReviewRenderer::thumbnailReady);
        auto project = fixture(); project.width = 256;
        renderer.setProject(project);
        renderer.requestThumbnail(0); renderer.requestThumbnail(0); renderer.requestThumbnail(1);
        QTRY_COMPARE(thumbnails.size(), 2);
        QCOMPARE(count->load(), 2);
        QCOMPARE(qvariant_cast<QImage>(thumbnails[0][1]).size(), QSize(192, 96));
        QCOMPARE(renderer.cachedThumbnail(0).size(), QSize(192, 96));
        renderer.requestThumbnail(0);
        QTRY_COMPARE(thumbnails.size(), 3);
        QCOMPARE(count->load(), 2);
        renderer.setProject(project);
        QVERIFY(renderer.cachedThumbnail(0).isNull());
        renderer.requestThumbnail(0);
        QTRY_COMPARE(thumbnails.size(), 4);
        QCOMPARE(count->load(), 3);
    }
    void currentCutThumbnailMovesAheadOfQueuedStripWork() {
        auto gate = std::make_shared<Gate>();
        const auto cleanup = qScopeGuard([gate] { gate->release.release(20); });
        MovieReviewRenderer renderer([gate](const auto &snapshot, const auto &token) { return gate->render(snapshot, token); });
        auto project = fixture();
        auto third = project.shots[0]; third.id = "third"; project.shots.append(third);
        auto fourth = project.shots[0]; fourth.id = "fourth"; project.shots.append(fourth);
        renderer.setProject(project);
        QSignalSpy thumbnails(&renderer, &MovieReviewRenderer::thumbnailReady);
        renderer.requestThumbnail(0);
        QTRY_VERIFY(gate->started.available()); QVERIFY(gate->started.tryAcquire());
        renderer.requestThumbnail(1); renderer.requestThumbnail(2); renderer.requestThumbnail(3);
        renderer.requestThumbnail(3, true);
        gate->release.release();
        QTRY_VERIFY(gate->started.available()); QVERIFY(gate->started.tryAcquire());
        { QMutexLocker lock(&gate->mutex); QCOMPARE(gate->shots[1], QString("fourth")); }
        gate->release.release(3); QTRY_COMPARE(thumbnails.size(), 4);
        QCOMPARE(thumbnails[0][0].toInt(), 0); QCOMPARE(thumbnails[1][0].toInt(), 3);
        QCOMPARE(gate->finished.load(), 4);
    }
    void latestSeekAndCutsRejectOldFrames() {
        auto gate = std::make_shared<Gate>();
        const auto cleanup = qScopeGuard([gate] { gate->release.release(20); });
        MovieReviewRenderer renderer([gate](const auto &snapshot, const auto &token) { return gate->render(snapshot, token); });
        renderer.setProject(fixture());
        QSignalSpy frames(&renderer, &MovieReviewRenderer::frameReady);
        renderer.requestFrame(0, 1, 1, false, {128, 128});
        QTRY_VERIFY(gate->started.available()); QVERIFY(gate->started.tryAcquire());
        renderer.requestFrame(0, 3, 1, false, {128, 128});
        renderer.requestFrame(1, 4, 1, true, {128, 128});
        { QMutexLocker lock(&gate->mutex); QVERIFY(gate->tokens[0]->load()); }
        gate->release.release();
        QTRY_VERIFY(gate->started.available()); QVERIFY(gate->started.tryAcquire());
        QCOMPARE(frames.size(), 0);
        gate->release.release(); QTRY_COMPARE(frames.size(), 1);
        QCOMPARE(frames[0][0].toInt(), 1); QCOMPARE(frames[0][1].toInt(), 4);
    }
    void forwardPlaybackFinishesWorkAndKeepsOnlyLatestWaitingFrame() {
        auto gate = std::make_shared<Gate>();
        const auto cleanup = qScopeGuard([gate] { gate->release.release(20); });
        MovieReviewRenderer renderer([gate](const auto &snapshot, const auto &token) { return gate->render(snapshot, token); });
        renderer.setProject(fixture());
        QSignalSpy frames(&renderer, &MovieReviewRenderer::frameReady);
        renderer.requestFrame(0, 1, 4, true, {128, 128});
        QTRY_VERIFY(gate->started.available()); QVERIFY(gate->started.tryAcquire());
        for (int frame = 2; frame <= 8; ++frame) renderer.requestFrame(0, frame, 4, true, {128, 128});
        { QMutexLocker lock(&gate->mutex); QVERIFY(!gate->tokens[0]->load()); }
        gate->release.release(); QTRY_COMPARE(frames.size(), 1);
        QCOMPARE(frames[0][1].toInt(), 1);
        QTRY_VERIFY(gate->started.available()); QVERIFY(gate->started.tryAcquire());
        { QMutexLocker lock(&gate->mutex); QCOMPARE(gate->frames, QVector<int>({1, 8})); }
        gate->release.release(); QTRY_COMPARE(frames.size(), 2);
        QCOMPARE(frames[1][1].toInt(), 8);
    }
    void clearingAndDestructionDoNotWaitForWorkers() {
        auto gate = std::make_shared<Gate>();
        const auto cleanup = qScopeGuard([gate] { gate->release.release(20); });
        auto *renderer = new MovieReviewRenderer([gate](const auto &snapshot, const auto &token) { return gate->render(snapshot, token); });
        renderer->setProject(fixture());
        int completed = 0;
        connect(renderer, &MovieReviewRenderer::frameReady, this, [&] { ++completed; });
        renderer->requestFrame(0, 1, 1, false, {128, 128});
        QTRY_VERIFY(gate->started.available()); QVERIFY(gate->started.tryAcquire());
        QElapsedTimer timer; timer.start(); renderer->clear(); delete renderer;
        QVERIFY(timer.elapsed() < 100);
        { QMutexLocker lock(&gate->mutex); QVERIFY(gate->tokens[0]->load()); }
        gate->release.release(); QTRY_COMPARE(gate->finished.load(), 1);
        QTest::qWait(20); QCOMPARE(completed, 0);
    }
};
QTEST_MAIN(MovieReviewRenderTests)
#include "movie_review_render_test.moc"
