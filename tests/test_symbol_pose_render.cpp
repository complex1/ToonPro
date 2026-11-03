#include "animation_test_fixture.h"
#include "document.h"
#include "effects_preview_renderer.h"
#include "sheet.h"
#include "symbol_poses.h"
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = project.height = 128;
    SymbolDefinition source{animation_fixture::id(), "Mouth poses", animation_fixture::project().shots[0]};
    project.symbols.append(source);
    auto layer = RasterLayer::create("Mouth"); layer.kind = LayerKind::Symbol;
    layer.symbol = SymbolInstance{source.id}; layer.symbol->playback = SymbolPlayback::Poses;
    layer.symbol->poseKeys = {{0,0}, {4,4}, {8,0}};
    project.shots[0].durationFrames = 24; project.shots[0].layers = {layer};
    return project;
}
EffectsPreviewSnapshot snapshot(const Project &project, int frame, int divisor) {
    EffectsPreviewSnapshot s; s.context = 1; s.revision = 1; s.project = project; s.shot = project.shots[0];
    s.frame = frame; s.playback = true; s.playbackDivisor = divisor; s.pixelSize = {800,700};
    s.guides = false; s.drawPaperShadow = false;
    prepareEffectsPreviewBackground(s); return s;
}
QColor center(const EffectsPreviewSnapshot &s, const QImage &image) {
    const auto point = (sheetGeometry(s.project,s.shot).output.topLeft() + QPointF(64,64)).toPoint();
    return image.pixelColor(point);
}
}
class SymbolPoseRenderTests final : public QObject {
    Q_OBJECT
private slots:
    void heldPosesStaySynchronizedAcrossPlaybackQualities() {
        const auto project = fixture(); QVERIFY2(project.validate().isEmpty(),qPrintable(project.validate()));
        const auto keys = project.shots[0].layers[0].symbol->poseKeys;
        for (int divisor : {1,2,4,8}) for (int frame : {0,3,4,7,8,23,4}) {
            auto s = snapshot(project,frame,divisor);
            const auto result = renderEffectsPreview(s,std::make_shared<std::atomic_bool>(false));
            QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QVERIFY(!result.cancelled);
            QCOMPARE(result.sourceScale,1. / divisor); QVERIFY(result.playbackFallbackReason.isEmpty());
            QCOMPARE(center(s,result.image),QColor(frame >= 4 && frame < 8 ? Qt::blue : Qt::red));
            QCOMPARE(renderMovieFrame(project,frame).pixelColor(64,64),center(s,result.image));
        }
        QCOMPARE(project.shots[0].layers[0].symbol->poseKeys,keys);
    }
    void sameFramePoseEditsInvalidateReducedSourceAndUndoRestoresIt() {
        Document document; document.load(fixture());
        const auto layerId = document.activeShot().layers[0].id;
        auto first = snapshot(document.project(),5,4);
        const auto before = renderEffectsPreview(first,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(before.error.isEmpty(),qPrintable(before.error)); QCOMPARE(center(first,before.image),QColor(Qt::blue));
        QVERIFY(setSymbolPose(&document,layerId,5,0)); QCOMPARE(document.history()->count(),1);
        auto changed = snapshot(document.project(),5,4);
        const auto after = renderEffectsPreview(changed,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(after.error.isEmpty(),qPrintable(after.error)); QCOMPARE(center(changed,after.image),QColor(Qt::red));
        QVERIFY(after.image != before.image);
        document.history()->undo(); auto undo = snapshot(document.project(),5,4);
        const auto restored = renderEffectsPreview(undo,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(restored.error.isEmpty(),qPrintable(restored.error)); QCOMPARE(restored.image,before.image);
        changed.playback = false;
        const auto exact = renderEffectsPreview(changed,std::make_shared<std::atomic_bool>(false));
        QVERIFY2(exact.error.isEmpty(),qPrintable(exact.error)); QCOMPARE(exact.sourceScale,1.);
        QCOMPARE(center(changed,exact.image),QColor(Qt::red));
    }
};
QTEST_MAIN(SymbolPoseRenderTests)
#include "test_symbol_pose_render.moc"
