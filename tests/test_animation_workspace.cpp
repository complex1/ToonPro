#include "animation_render.h"
#include "canvas.h"
#include "main_window.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "ui_theme.h"
#include <QAction>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUuid>
#include <QtTest>

using namespace toon;
namespace {
QString id() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
RasterLayer paintLayer(const QString &name, QColor color, QRect area) {
    auto layer = RasterLayer::create(name);
    QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); painter.fillRect(area, color); painter.end();
    layer.tiles.insert({0, 0}, image); return layer;
}
Project fixture() {
    auto project = Project::create(); project.width = project.height = 64; project.fps = 50; project.transparent = true;
    auto &shot = project.shots[0]; shot.durationFrames = 12; shot.sheet.overscan = 0;
    auto background = paintLayer("Background", Qt::green, {0, 0, 64, 64});
    auto first = paintLayer("Ink", Qt::red, {4, 4, 24, 24});
    auto second = paintLayer("Ink", Qt::blue, {32, 4, 24, 24});
    auto root = RasterLayer::create("Character"); root.group = true;
    auto a = RasterLayer::create("A1"), b = RasterLayer::create("A2"); a.group = b.group = true;
    a.parentId = b.parentId = root.id; first.parentId = a.id; second.parentId = b.id;
    AnimationTrack track; track.id = id(); track.rootGroupLayerId = root.id; track.drawingTemplate = {{"ink", "Ink"}};
    track.drawings = {{id(), a.id, {{"ink", first.id}}}, {id(), b.id, {{"ink", second.id}}}};
    track.exposures = {{id(), track.drawings[0].id, 0, 4}, {id(), track.drawings[1].id, 4, 4}, {id(), track.drawings[0].id, 8, 2}};
    shot.layers = {background, root, a, first, b, second}; shot.animationTracks = {track}; return project;
}
void answerTransform(const char *name) {
    QTimer::singleShot(0, qApp, [name] {
        for (auto *widget : QApplication::topLevelWidgets()) if (auto *message = qobject_cast<QMessageBox *>(widget))
            if (auto *button = message->findChild<QPushButton *>(name)) button->click();
    });
}
}
class AnimationWorkspaceTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void currentFrameRenderingAndSequenceAgree() {
        const auto project = fixture(); QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        QCOMPARE(renderMovieFrame(project, 0).pixelColor(10, 10), QColor(Qt::red));
        QCOMPARE(renderMovieFrame(project, 4).pixelColor(10, 10), QColor(Qt::green));
        QCOMPARE(renderMovieFrame(project, 4).pixelColor(40, 10), QColor(Qt::blue));
        QCOMPARE(renderMovieFrame(project, 10).pixelColor(40, 10), QColor(Qt::green));
        QVERIFY(renderMovieFrame(project, 12).isNull());
        const auto sheet = renderSheet(project, project.shots[0], false, 4);
        const auto origin = sheetGeometry(project, project.shots[0]).output.topLeft().toPoint();
        QCOMPARE(sheet.pixelColor(origin + QPoint(40, 10)), QColor(Qt::blue));
        QTemporaryDir temporary; QString error; int progress = -1;
        const auto directory = temporary.filePath("sequence");
        QVERIFY2(exportPngSequence(project, 0, 0, 11, directory, [&progress](int done, int) { progress = done; return true; }, &error), qPrintable(error));
        QCOMPARE(progress, 12); QCOMPARE(QDir(directory).entryList({"*.png"}, QDir::Files).size(), 12);
        for (int frame = 0; frame < 12; ++frame) {
            const QImage output(QDir(directory).filePath(QString("frame-%1.png").arg(frame + 1, 4, 10, QLatin1Char('0'))));
            QCOMPARE(output.convertToFormat(QImage::Format_ARGB32_Premultiplied), renderMovieFrame(project, frame));
        }
        QVERIFY(!exportPngSequence(project, 0, 0, 11, directory, {}, &error));
        QVERIFY(error.contains("preserved"));
        const auto cancelled = temporary.filePath("cancelled");
        QVERIFY(!exportPngSequence(project, 0, 0, 11, cancelled, [](int done, int) { return done < 2; }, &error));
        QCOMPARE(QDir(cancelled).entryList({"*.png"}, QDir::Files).size(), 2);
    }
    void onionSkipsHoldsRepeatedDrawingsAndStaticBackground() {
        const auto project = fixture(); const auto &shot = project.shots[0]; const auto trackId = shot.animationTracks[0].id;
        QVERIFY(onionSkinLayers(shot, 0, trackId, -1).isEmpty());
        const auto next = onionSkinLayers(shot, 1, trackId, 1);
        QVERIFY(!next.isEmpty());
        for (const auto &layer : next) QVERIFY(layer.id != shot.layers[0].id && layer.id != shot.layers[3].id);
        QCOMPARE(next.last().id, shot.layers[5].id);
        QCOMPARE(onionSkinLayers(shot, 8, trackId, -1).last().id, shot.layers[5].id);
        QVERIFY(onionSkinLayers(shot, 8, trackId, 1).isEmpty());
        QCOMPARE(animationDisplayKey(shot, 0), animationDisplayKey(shot, 3));
        QVERIFY(animationDisplayKey(shot, 3) != animationDisplayKey(shot, 4));
    }
    void scrubbingIsTransientAndHeldFramesReuseCanvasCache() {
        Canvas canvas; TimelineController controller(&canvas); canvas.resize(800, 600); canvas.setProject(fixture()); canvas.show();
        QVERIFY(controller.selectTrack(canvas.shot().animationTracks[0].id));
        const auto original = canvas.project().toJson();
        canvas.grab(); const auto renders = canvas.artworkRenderCount();
        QSignalSpy dirty(canvas.document(), &Document::changed);
        QVERIFY(controller.setFrame(1)); canvas.grab();
        QCOMPARE(canvas.artworkRenderCount(), renders);
        QCOMPARE(canvas.document()->history()->count(), 0); QVERIFY(canvas.document()->history()->isClean()); QCOMPARE(dirty.size(), 0);
        QVERIFY(controller.setFrame(4)); canvas.grab(); QVERIFY(canvas.artworkRenderCount() > renders);
        QCOMPARE(canvas.document()->activeLayer(), 5);
        controller.setOnionSkin(true); const auto withOnion = canvas.grab().toImage();
        controller.setOnionSkin(false); const auto withoutOnion = canvas.grab().toImage();
        QVERIFY(withOnion != withoutOnion); QCOMPARE(canvas.project().toJson(), original);
        QCOMPARE(renderMovieFrame(canvas.project(), 4), renderMovieFrame(fixture(), 4));
        QVERIFY(controller.setFrame(10)); QVERIFY(canvas.toolBlockReason().contains("empty"));
    }
    void playbackStopsAtEndAndDoesNotDirtyDocument() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        QVERIFY(controller.selectTrack(canvas.shot().animationTracks[0].id));
        controller.setLooping(false); controller.setOnionSkin(true); controller.setPlaying(true);
        QVERIFY(controller.playing()); QVERIFY(canvas.animationPlayback()); QVERIFY(!canvas.onionSkin());
        QVERIFY(canvas.toolBlockReason().contains("Pause"));
        QTRY_VERIFY_WITH_TIMEOUT(controller.currentFrame() > 0, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.playing(), 2000);
        QCOMPARE(controller.currentFrame(), 11); QVERIFY(canvas.onionSkin());
        QCOMPARE(canvas.document()->history()->count(), 0); QVERIFY(canvas.document()->history()->isClean());
        controller.setLooping(true); QVERIFY(controller.setFrame(10)); controller.setPlaying(true);
        QTRY_VERIFY_WITH_TIMEOUT(controller.currentFrame() < 10, 1000); QVERIFY(controller.playing());
        controller.setPlaying(false);
    }
    void frameSwitchOffersApplyDiscardCancel() {
        Canvas canvas; TimelineController controller(&canvas); canvas.setProject(fixture());
        QVERIFY(controller.selectTrack(canvas.shot().animationTracks[0].id));
        auto *doc = canvas.document(); QVERIFY(doc->beginTransform());
        auto transform = doc->transformParameters(); transform.translation = {8, 0}; QVERIFY(doc->previewTransform(transform));
        answerTransform("cancelPendingTransform"); QVERIFY(!controller.setFrame(4));
        QCOMPARE(controller.currentFrame(), 0); QVERIFY(doc->transformActive());
        answerTransform("applyPendingTransform"); QVERIFY(controller.setFrame(4));
        QCOMPARE(controller.currentFrame(), 4); QVERIFY(!doc->transformActive()); QCOMPARE(doc->history()->count(), 1);
        QVERIFY(controller.setFrame(0)); QVERIFY(doc->beginTransform());
        transform = doc->transformParameters(); transform.translation = {0, 8}; QVERIFY(doc->previewTransform(transform));
        const auto image = renderMovieFrame(doc->project(), 0);
        answerTransform("discardPendingTransform"); QVERIFY(controller.setFrame(4));
        QCOMPARE(renderMovieFrame(doc->project(), 0), image); QCOMPARE(doc->history()->count(), 1);
    }
    void realWorkspaceProvidesTwoPanelsAndPreview() {
        QTemporaryDir temporary; QString error; const auto path = temporary.filePath("animation.toonproj");
        auto project = fixture(); project.width = 640; project.height = 360; project.movie = "Frame animation";
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        MainWindow window(temporary.filePath("workspace.ini")); QVERIFY(window.openProject(path)); window.resize(1320, 900); window.show();
        auto *timeline = window.findChild<QDockWidget *>("animationPanel");
        auto *properties = window.findChild<QDockWidget *>("framePropertiesPanel");
        QVERIFY(timeline && properties); QVERIFY(qobject_cast<TimelinePanel *>(timeline->widget())); QVERIFY(qobject_cast<FramePropertiesPanel *>(properties->widget()));
        auto *controller = window.findChild<TimelineController *>(); QVERIFY(controller);
        QVERIFY(controller->selectTrack(project.shots[0].animationTracks[0].id)); timeline->show(); properties->show(); properties->raise();
        window.resizeDocks({timeline}, {240}, Qt::Vertical); QTest::qWait(50);
        const auto preview = qEnvironmentVariable("TOON_UI_PREVIEW");
        if (!preview.isEmpty()) { QDir().mkpath(preview); QVERIFY(window.grab().save(QDir(preview).filePath("animation-workspace.png"))); }
        QVERIFY(window.findChild<QAction *>("sequenceExportAction")->isEnabled());
        QVERIFY(window.close());
    }
    void realWorkspaceUndoCancelsDraftAndSaveCommitsTiming() {
        QTemporaryDir temporary; QString error; const auto path = temporary.filePath("draft.toonproj");
        const auto project = fixture(); QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        MainWindow window(temporary.filePath("workspace.ini")); QVERIFY(window.openProject(path)); window.show();
        auto *controller = window.findChild<TimelineController *>(); QVERIFY(controller);
        QVERIFY(controller->selectTrack(project.shots[0].animationTracks[0].id));
        auto *properties = window.findChild<QDockWidget *>("framePropertiesPanel"); QVERIFY(properties);
        properties->show(); properties->raise(); QTest::qWait(50);
        auto *field = properties->findChild<QWidget *>("frameExposureHold"); QVERIFY(field);
        auto *hold = field->findChild<QDoubleSpinBox *>(); QVERIFY(hold);
        auto *history = window.canvas()->document()->history();
        auto *undo = window.findChild<QAction *>("undoAction");
        auto *save = window.findChild<QAction *>("saveProjectAction"); QVERIFY(undo && save);
        const auto enter = [&] { hold->setFocus(); hold->selectAll(); QTest::keyClicks(hold, "3"); };
        enter(); QVERIFY(controller->hasPendingEdits()); QVERIFY(undo->isEnabled());
        undo->trigger(); QVERIFY(!controller->hasPendingEdits());
        hold->clearFocus(); QCoreApplication::processEvents();
        QCOMPARE(history->count(), 0); QCOMPARE(window.canvas()->project().toJson(), project.toJson());
        enter(); QVERIFY(controller->hasPendingEdits()); save->trigger();
        QVERIFY(!controller->hasPendingEdits()); QCOMPARE(history->count(), 1); QVERIFY(history->isClean());
        hold->clearFocus(); QCoreApplication::processEvents(); QCOMPARE(history->count(), 1);
        const auto loaded = loadProject(path, &error); QVERIFY2(loaded.has_value(), qPrintable(error));
        QCOMPARE(loaded->shots[0].animationTracks[0].exposures[0].durationFrames, 3);
        QCOMPARE(loaded->toJson(), window.canvas()->project().toJson()); QVERIFY(window.close());
    }
};
QTEST_MAIN(AnimationWorkspaceTests)
#include "test_animation_workspace.moc"
