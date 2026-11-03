#include "timeline_test_geometry.h"
#include "animation_test_fixture.h"
#include "canvas.h"
#include "main_window.h"
#include "timeline_controller.h"
#include "ui_theme.h"
#include <QAbstractScrollArea>
#include <QAction>
#include <QDockWidget>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace toon;

namespace {
using Property = KeyframeProperty;
using Interpolation = KeyframeInterpolation;

Project fixture(bool keys = true) {
    auto project = Project::create();
    project.width = project.height = 64;
    project.fps = 20;
    auto &shot = project.shots[0];
    shot.durationFrames = 24;
    shot.layers.append(RasterLayer::create("Other layer"));
    if (keys) {
        shot.layerAnimations = {
            {shot.layers[0].id, {}, {
                {Property::PositionX, {{0, 0}, {2, 20, Interpolation::EaseIn},
                                      {4, 40, Interpolation::EaseOut}, {8, 80}}},
                {Property::Opacity, {{0, 1}, {4, .5, Interpolation::Hold}}}}, {}},
            {shot.layers[1].id, {}, {{Property::PositionX, {{0, 0}, {2, 200}}}}, {}}
        };
    }
    return project;
}

TimelineKeyRef ref(const QString &layerId, Property property, int frame) {
    return {layerId, property, {}, frame};
}

std::optional<ScalarKeyframe> scalarKey(const Canvas &canvas, const QString &id,
                                      Property property, int frame) {
    if (const auto *channel = propertyChannel(canvas.shot(), id, property))
        for (const auto &key : channel->keys) if (key.frame == frame) return key;
    return std::nullopt;
}

void movePointer(QWidget *viewport, QPoint point) {
    QMouseEvent event(QEvent::MouseMove, point, viewport->mapToGlobal(point),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(viewport, &event);
}
}

class TimelineEditControllerTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void selectionIsScopedToActiveLayerAndSurvivesScrubbing() {
        Canvas canvas;
        TimelineController controller(&canvas);
        canvas.setProject(fixture());
        const auto first = canvas.shot().layers[0].id;
        const auto other = canvas.shot().layers[1].id;
        const auto x = ref(first, Property::PositionX, 2);
        const auto opacity = ref(first, Property::Opacity, 4);
        const auto snapshot = canvas.project().toJson();

        controller.setSelectedKeys({x, opacity, x, ref(other, Property::PositionX, 2),
                                    ref(first, Property::PositionX, 3)});
        QCOMPARE(controller.selectedKeys().size(), 2);
        QVERIFY(controller.selectedKeys().contains(x));
        QVERIFY(controller.selectedKeys().contains(opacity));
        QVERIFY(controller.setFrame(10));
        QCOMPARE(controller.selectedKeys().size(), 2);
        QVERIFY(controller.selectPropertyLayer(other));
        QVERIFY(controller.selectedKeys().isEmpty());
        controller.setSelectedKeys({x, ref(other, Property::PositionX, 2)});
        QCOMPARE(controller.selectedKeys(), QVector<TimelineKeyRef>({ref(other, Property::PositionX, 2)}));
        canvas.document()->selectLayer(0);
        QVERIFY(controller.selectedKeys().isEmpty());
        QCOMPARE(canvas.project().toJson(), snapshot);
        QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void changingDrawingsClearsSelectionFromPreviousDrawing() {
        auto project = animation_fixture::project();
        auto &shot = project.shots[0];
        const auto track = shot.animationTracks[0];
        const auto first = track.drawings[0].templateLayers.value("ink");
        shot.layerAnimations = {{first, {}, {{Property::PositionX, {{0, 0}, {2, 20}}}}, {}}};
        Canvas canvas;
        TimelineController controller(&canvas);
        canvas.setProject(project);
        QVERIFY(controller.selectTrack(track.id));
        controller.setSelectedKeys({ref(first, Property::PositionX, 2)});
        QCOMPARE(controller.selectedKeys().size(), 1);
        QVERIFY(controller.setFrame(1));
        QCOMPARE(controller.selectedKeys().size(), 1);
        QVERIFY(controller.setFrame(4));
        QCOMPARE(controller.selectedPropertyLayerId(), track.drawings[1].templateLayers.value("ink"));
        QVERIFY(controller.selectedKeys().isEmpty());
    }

    void groupedMoveAndDeleteAreAtomicUndoCommands() {
        Canvas canvas;
        TimelineController controller(&canvas);
        canvas.setProject(fixture());
        const auto id = canvas.shot().layers[0].id;
        auto *history = canvas.document()->history();
        const auto baseline = canvas.project().toJson();
        const QVector<TimelineKeyRef> keys{ref(id, Property::PositionX, 2),
                                          ref(id, Property::PositionX, 4),
                                          ref(id, Property::Opacity, 4)};
        controller.setSelectedKeys(keys);
        // The destination at frame 4 is vacated by another selected key.
        QVERIFY(controller.moveSelectedKeys(2));
        QCOMPARE(history->count(), 1);
        const auto firstMoved = scalarKey(canvas, id, Property::PositionX, 4);
        const auto secondMoved = scalarKey(canvas, id, Property::PositionX, 6);
        const auto opacityMoved = scalarKey(canvas, id, Property::Opacity, 6);
        QVERIFY(firstMoved && secondMoved && opacityMoved);
        QCOMPARE(firstMoved->value, 20.);
        QCOMPARE(secondMoved->value, 40.);
        QCOMPARE(opacityMoved->value, .5);
        QVERIFY(controller.selectedKeys().contains(ref(id, Property::PositionX, 6)));
        const auto moved = canvas.project().toJson();
        QVERIFY(!controller.moveSelectedKeys(2)); // Frame 8 contains an unselected key.
        QCOMPARE(canvas.project().toJson(), moved);
        QCOMPARE(history->count(), 1);
        history->undo();
        QCOMPARE(canvas.project().toJson(), baseline);
        history->redo();
        QCOMPARE(canvas.project().toJson(), moved);
        history->undo();

        controller.setSelectedKeys(keys);
        QVERIFY(controller.deleteSelectedKeys());
        QCOMPARE(history->count(), 1);
        QCOMPARE(history->index(), 1);
        QVERIFY(!scalarKey(canvas, id, Property::PositionX, 2));
        QVERIFY(!scalarKey(canvas, id, Property::PositionX, 4));
        QVERIFY(!scalarKey(canvas, id, Property::Opacity, 4));
        QVERIFY(controller.selectedKeys().isEmpty());
        history->undo();
        QCOMPARE(canvas.project().toJson(), baseline);
    }

    void clipboardKeepsValuesAfterSourceChangesAndDeletion() {
        Canvas canvas;
        TimelineController controller(&canvas);
        canvas.setProject(fixture());
        const auto id = canvas.shot().layers[0].id;
        auto *document = canvas.document();
        auto *history = document->history();
        const auto original = canvas.project().toJson();
        controller.setSelectedKeys({ref(id, Property::PositionX, 2), ref(id, Property::Opacity, 4)});
        QVERIFY(controller.copySelectedKeys());
        QCOMPARE(canvas.project().toJson(), original);
        QCOMPARE(history->count(), 0);
        controller.clearKeySelection();
        QVERIFY(controller.canPasteKeys());

        QVERIFY(controller.setFrame(2));
        QVERIFY(document->setKeyframeValue(id, Property::PositionX, 900));
        QVERIFY(document->setKeyframeInterpolation(id, Property::PositionX, 2, Interpolation::Linear));
        QVERIFY(document->removeKeyframe(id, Property::Opacity, 4));
        QVERIFY(controller.setFrame(12));
        const auto beforePaste = canvas.project().toJson();
        const int beforeIndex = history->index();
        QVERIFY(controller.pasteKeys());
        QCOMPARE(history->index(), beforeIndex + 1);
        const auto position = scalarKey(canvas, id, Property::PositionX, 12);
        const auto opacity = scalarKey(canvas, id, Property::Opacity, 14);
        QVERIFY(position && opacity);
        QCOMPARE(position->value, 20.);
        QCOMPARE(position->interpolation, Interpolation::EaseIn);
        QCOMPARE(opacity->value, .5);
        QCOMPARE(opacity->interpolation, Interpolation::Hold);
        const auto pasted = canvas.project().toJson();
        QVERIFY(!controller.pasteKeys());
        QCOMPARE(canvas.project().toJson(), pasted);
        QCOMPARE(history->index(), beforeIndex + 1);
        history->undo();
        QCOMPARE(canvas.project().toJson(), beforePaste);
        history->redo();
        QCOMPARE(canvas.project().toJson(), pasted);
    }

    void pastingAfterShorteningShotSelectsTranslatedSnapshotFrames() {
        auto project = fixture(false);
        auto &shot = project.shots[0];
        shot.durationFrames = 64;
        const auto id = shot.layers[0].id;
        shot.layerAnimations = {{id, {}, {{Property::PositionX,
            {{50, 20, Interpolation::EaseIn}, {54, 40, Interpolation::EaseOut}}}}, {}}};
        Canvas canvas;
        TimelineController controller(&canvas);
        canvas.setProject(project);
        controller.setSelectedKeys({ref(id, Property::PositionX, 50), ref(id, Property::PositionX, 54)});
        QVERIFY(controller.copySelectedKeys());

        auto shortened = canvas.project();
        shortened.shots[0].durationFrames = 40;
        shortened.shots[0].layerAnimations.clear();
        canvas.document()->replaceProject(shortened, "Shorten shot and remove source keys");
        QVERIFY(controller.selectedKeys().isEmpty());
        QVERIFY(controller.canPasteKeys());
        QVERIFY(controller.setFrame(10));
        QVERIFY(controller.pasteKeys());
        QCOMPARE(controller.selectedKeys(), QVector<TimelineKeyRef>({
            ref(id, Property::PositionX, 10), ref(id, Property::PositionX, 14)}));
        const auto first = scalarKey(canvas, id, Property::PositionX, 10);
        const auto last = scalarKey(canvas, id, Property::PositionX, 14);
        QVERIFY(first && last);
        QCOMPARE(first->value, 20.);
        QCOMPARE(first->interpolation, Interpolation::EaseIn);
        QCOMPARE(last->value, 40.);
        QCOMPARE(last->interpolation, Interpolation::EaseOut);
        canvas.document()->history()->undo();
        QCOMPARE(canvas.project().toJson(), shortened.toJson());
    }

    void playbackRangeIsInclusiveTransientAndPreservesDisabledEndpoints() {
        Canvas canvas;
        TimelineController controller(&canvas);
        canvas.setProject(fixture(false));
        const auto original = canvas.project().toJson();
        QVERIFY(controller.setPlaybackRange(3, 5));
        QVERIFY(controller.playbackRangeEnabled());
        QCOMPARE(controller.playbackFirstFrame(), 3);
        QCOMPARE(controller.playbackLastFrame(), 5);
        QVERIFY(!controller.setPlaybackRange(-1, 5));
        QVERIFY(!controller.setPlaybackRange(6, 5));
        QVERIFY(!controller.setPlaybackRange(3, 24));
        QCOMPARE(controller.playbackFirstFrame(), 3);
        QCOMPARE(controller.playbackLastFrame(), 5);
        controller.setPlaybackRangeEnabled(false);
        QCOMPARE(controller.playbackFirstFrame(), 0);
        QCOMPARE(controller.playbackLastFrame(), 23);
        controller.setPlaybackRangeEnabled(true);
        QCOMPARE(controller.playbackFirstFrame(), 3);
        QCOMPARE(controller.playbackLastFrame(), 5);

        QVERIFY(controller.setFrame(1)); // Scrubbing is not constrained to the playback range.
        QCOMPARE(controller.currentFrame(), 1);
        controller.setPlaying(true);
        QVERIFY(controller.playing());
        QCOMPARE(controller.currentFrame(), 3);
        QVERIFY(controller.setPlaybackRange(8, 10));
        QVERIFY(!controller.playing());
        QVERIFY(controller.setFrame(10));
        controller.setPlaying(true);
        QCOMPARE(controller.currentFrame(), 8);
        controller.setPlaying(false);
        controller.resetPlaybackRange();
        QVERIFY(!controller.playbackRangeEnabled());
        controller.setPlaybackRangeEnabled(true);
        QCOMPARE(controller.playbackFirstFrame(), 0);
        QCOMPARE(controller.playbackLastFrame(), 23);
        QCOMPARE(canvas.project().toJson(), original);
        QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(canvas.document()->history()->isClean());
    }

    void playbackLoopsAndStopsAtInclusiveLastFrame() {
        Canvas canvas;
        TimelineController controller(&canvas);
        canvas.setProject(fixture(false));
        QVERIFY(controller.setPlaybackRange(3, 5));
        QVector<int> visited;
        connect(canvas.document(), &Document::frameChanged, &controller,
                [&] { visited.append(controller.currentFrame()); });
        controller.setLooping(false);
        controller.setPlaying(true);
        QCOMPARE(controller.currentFrame(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.playing(), 1000);
        QCOMPARE(controller.currentFrame(), 5);
        QVERIFY(visited.contains(3));
        QVERIFY(visited.contains(5));
        for (int frame : visited) QVERIFY(frame >= 3 && frame <= 5);

        visited.clear();
        controller.setLooping(true);
        controller.setPlaying(true);
        QCOMPARE(controller.currentFrame(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(visited.count(3) >= 2, 1000);
        controller.setPlaying(false);
        for (int frame : visited) QVERIFY(frame >= 3 && frame <= 5);
    }

    void singleFrameRangeWorksForLoopAndOnce() {
        Canvas canvas;
        TimelineController controller(&canvas);
        canvas.setProject(fixture(false));
        QVERIFY(controller.setPlaybackRange(7, 7));
        controller.setLooping(true);
        controller.setPlaying(true);
        QVERIFY(controller.playing());
        QCOMPARE(controller.currentFrame(), 7);
        QTest::qWait(100);
        QVERIFY(controller.playing());
        QCOMPARE(controller.currentFrame(), 7);
        controller.setPlaying(false);
        controller.setLooping(false);
        controller.setPlaying(true);
        QTRY_VERIFY_WITH_TIMEOUT(!controller.playing(), 1000);
        QCOMPARE(controller.currentFrame(), 7);
    }

    void durationChangesClampEnabledAndStoredRanges() {
        Canvas canvas;
        TimelineController controller(&canvas);
        canvas.setProject(fixture(false));
        QVERIFY(controller.setPlaybackRange(10, 15));
        auto shorter = canvas.project();
        shorter.shots[0].durationFrames = 12;
        canvas.document()->replaceProject(shorter, "Shorten shot");
        QCOMPARE(controller.playbackFirstFrame(), 10);
        QCOMPARE(controller.playbackLastFrame(), 11);
        controller.setPlaybackRangeEnabled(false);
        shorter.shots[0].durationFrames = 4;
        canvas.document()->replaceProject(shorter, "Shorten shot again");
        QCOMPARE(controller.playbackFirstFrame(), 0);
        QCOMPARE(controller.playbackLastFrame(), 3);
        controller.setPlaybackRangeEnabled(true);
        QCOMPARE(controller.playbackFirstFrame(), 3);
        QCOMPARE(controller.playbackLastFrame(), 3);
    }

    void reloadingSameShotClearsTransientWorkspaceState() {
        Canvas canvas;
        TimelineController controller(&canvas);
        const auto project = fixture();
        canvas.setProject(project);
        const auto id = project.shots[0].layers[0].id;
        controller.setSelectedKeys({ref(id, Property::PositionX, 2)});
        QVERIFY(controller.copySelectedKeys());
        QVERIFY(controller.setPlaybackRange(3, 5));
        controller.setLooping(false);
        controller.setOnionSkin(true);
        controller.setPlaying(true);
        QObject editor;
        controller.setPendingEdits(&editor, true);
        QVERIFY(controller.hasPendingEdits());
        canvas.document()->load(project);
        QCOMPARE(canvas.shot().id, project.shots[0].id);
        QVERIFY(!controller.playing());
        QVERIFY(!controller.looping());
        QVERIFY(controller.onionSkin());
        QVERIFY(!controller.hasPendingEdits());
        QVERIFY(controller.selectedKeys().isEmpty());
        QVERIFY(!controller.canPasteKeys());
        QVERIFY(!controller.playbackRangeEnabled());
        QCOMPARE(controller.playbackFirstFrame(), 0);
        QCOMPARE(controller.playbackLastFrame(), 23);
        QCOMPARE(controller.currentFrame(), 0);
        QCOMPARE(canvas.project().toJson(), project.toJson());
        QCOMPARE(canvas.document()->history()->count(), 0);
    }

    void pendingPropertyEditsResolveOrCancelBeforeTimelineActions() {
        Canvas canvas;
        TimelineController controller(&canvas);
        canvas.setProject(fixture());
        const auto id = canvas.shot().layers[0].id;
        auto *history = canvas.document()->history();
        const auto baseline = canvas.project().toJson();
        QObject firstEditor, secondEditor;
        QSignalSpy resolutions(&controller, &TimelineController::editResolutionRequested);
        QSignalSpy cancellations(&controller, &TimelineController::editCancellationRequested);
        QVERIFY(controller.setFrame(2));
        QVERIFY(controller.beginPropertyEdit(Property::PositionX));
        QVERIFY(controller.previewPropertyValue(Property::PositionX, 75));
        controller.setPendingEdits(&firstEditor, true);
        controller.setPendingEdits(&secondEditor, true);
        controller.setPendingEdits(&firstEditor, false);
        QVERIFY(controller.hasPendingEdits());
        const int beforeCancel = cancellations.size();
        controller.cancelEditing();
        QCOMPARE(cancellations.size(), beforeCancel + 1);
        QVERIFY(!controller.hasPendingEdits());
        QVERIFY(!canvas.document()->keyframeEditActive());
        QCOMPARE(canvas.project().toJson(), baseline);
        QCOMPARE(history->count(), 0);

        QVERIFY(controller.beginPropertyEdit(Property::PositionX));
        QVERIFY(controller.previewPropertyValue(Property::PositionX, 95));
        controller.setPendingEdits(&firstEditor, true);
        const int beforeResolve = resolutions.size();
        QVERIFY(controller.prepareTimelineEdit());
        QCOMPARE(resolutions.size(), beforeResolve + 1);
        QVERIFY(!controller.hasPendingEdits());
        QVERIFY(!canvas.document()->keyframeEditActive());
        QCOMPARE(history->count(), 1);
        const auto committed = scalarKey(canvas, id, Property::PositionX, 2);
        QVERIFY(committed);
        QCOMPARE(committed->value, 95.);
        controller.finishEditing();
        QCOMPARE(history->count(), 1);
        history->undo();
        QCOMPARE(canvas.project().toJson(), baseline);
    }

    void nativeWindowUndoCancelsGridDragAndSaveCommitsOnce() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto project = fixture();
        project.shots[0].layers.resize(1);
        project.shots[0].layerAnimations.resize(1);
        const auto id = project.shots[0].layers[0].id;
        const auto path = directory.filePath("timeline.toonproj");
        QString error;
        QVERIFY2(saveProject(project, path, &error), qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini"));
        QVERIFY(window.openProject(path));
        window.resize(1320, 900);
        window.show();
        auto *controller = window.findChild<TimelineController *>();
        auto *dock = window.findChild<QDockWidget *>("animationPanel");
        auto *undo = window.findChild<QAction *>("undoAction");
        auto *save = window.findChild<QAction *>("saveProjectAction");
        QVERIFY(controller && dock && undo && save);
        dock->show();
        dock->raise();
        QTest::qWait(40);
        auto *grid = dock->findChild<QAbstractScrollArea *>("timelineGrid");
        QVERIFY(grid);
        auto *history = window.canvas()->document()->history();
        const auto baseline = window.canvas()->project().toJson();
        const int labels = timeline_test::labelWidth(grid);
        const QPoint start(labels + 4 * 20 + 10, 26 + 32 + 16);
        const QPoint end = start + QPoint(2 * 20, 0);
        const auto beginDrag = [&] {
            QTest::mousePress(grid->viewport(), Qt::LeftButton, Qt::NoModifier, start);
            movePointer(grid->viewport(), end);
        };
        beginDrag();
        QVERIFY(controller->hasPendingEdits());
        QVERIFY(undo->isEnabled());
        QCOMPARE(window.canvas()->project().toJson(), baseline);
        undo->trigger();
        QVERIFY(!controller->hasPendingEdits());
        QTest::mouseRelease(grid->viewport(), Qt::LeftButton, Qt::NoModifier, end);
        QCOMPARE(window.canvas()->project().toJson(), baseline);
        QCOMPARE(history->count(), 0);
        QVERIFY(history->isClean());

        beginDrag();
        QVERIFY(controller->hasPendingEdits());
        save->trigger();
        QVERIFY(!controller->hasPendingEdits());
        QCOMPARE(history->count(), 1);
        QVERIFY(history->isClean());
        const auto saved = loadProject(path, &error);
        QVERIFY2(saved.has_value(), qPrintable(error));
        const auto *channel = propertyChannel(saved->shots[0], id, Property::PositionX);
        QVERIFY(channel);
        QVERIFY(std::any_of(channel->keys.cbegin(), channel->keys.cend(),
                            [](const auto &key) { return key.frame == 6 && key.value == 40; }));
        QVERIFY(std::none_of(channel->keys.cbegin(), channel->keys.cend(),
                             [](const auto &key) { return key.frame == 4; }));
        QTest::mouseRelease(grid->viewport(), Qt::LeftButton, Qt::NoModifier, end);
        QCoreApplication::processEvents();
        QCOMPARE(history->count(), 1);
        QVERIFY(history->isClean());
        QVERIFY(window.close());
    }
};

QTEST_MAIN(TimelineEditControllerTests)
#include "timeline_edit_controller_tests.moc"
