#include "timeline_test_geometry.h"
#include "animation_test_fixture.h"
#include "canvas.h"
#include "cel_edit.h"
#include "document.h"
#include "drawing_reuse_dialog.h"
#include "timeline_controller.h"
#include "timeline_panel.h"
#include "ui_theme.h"
#include "value_slider.h"

#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = 160; project.height = 120;
    auto &shot = project.shots[0]; shot.durationFrames = 60;
    for (const QString name : {QString("Character"), QString("Effects")}) {
        auto root = RasterLayer::create(name); root.group = true; shot.layers.append(root);
        AnimationTrack track; track.id = animation_fixture::id(); track.rootGroupLayerId = root.id;
        track.drawingTemplate = {{"ink", "Ink", LayerKind::Raster}};
        for (int i = 0; i < 3; ++i) {
            auto folder = RasterLayer::create(name + QString(" %1").arg(i + 1)); folder.group = true; folder.parentId = root.id;
            auto ink = RasterLayer::create("Ink"); ink.parentId = folder.id;
            QImage pixels(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); pixels.fill(Qt::transparent);
            { QPainter painter(&pixels); painter.fillRect(QRect(15 + i * 10, 15, 50, 70), QColor::fromHsv(40 + i * 90, 160, 220)); }
            ink.tiles.insert({0, 0}, pixels);
            AnimationDrawing drawing{animation_fixture::id(), folder.id, {{"ink", ink.id}}};
            track.drawings.append(drawing); track.exposures.append({animation_fixture::id(), drawing.id, 2 + 8 * i, 3});
            shot.layers.append(folder); shot.layers.append(ink);
        }
        shot.animationTracks.append(track);
    }
    return project;
}
QAbstractScrollArea *grid(QWidget &panel) { return panel.findChild<QAbstractScrollArea *>("timelineGrid"); }
int label(QAbstractScrollArea *view) { return timeline_test::labelWidth(view); }
QPoint cell(QAbstractScrollArea *view, int frame, int row) {
    return {label(view) + frame * 20 + 10 - view->horizontalScrollBar()->value(), timeline_test::rowCenter(view,row)};
}
void settle() { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest); }
void show(TimelinePanel &panel) {
    settle(); panel.resize(900, 360); panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel)); panel.raise(); panel.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&panel));
    settle(); grid(panel)->horizontalScrollBar()->setValue(0); grid(panel)->verticalScrollBar()->setValue(0);
}
void click(QAbstractScrollArea *view, int frame, int row, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers, cell(view, frame, row));
}
void move(QAbstractScrollArea *view, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(QEvent::MouseMove, point, view->viewport()->mapToGlobal(point), Qt::NoButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(view->viewport(), &event);
}
void drag(QAbstractScrollArea *view, QPoint from, QPoint to) {
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from); move(view, to);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
}
void seek(QAbstractScrollArea *view, int frame) {
    const QPoint point(cell(view, frame, 0).x(), 12);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
}
TimelineCelRef ref(const Project &project, int track, int exposure) {
    const auto &source = project.shots[0].animationTracks[track]; return {source.id, source.exposures[exposure].id};
}
QVector<int> starts(const Canvas &canvas, int track) {
    QVector<int> result; for (const auto &cel : canvas.shot().animationTracks[track].exposures) result.append(cel.startFrame); return result;
}
void contextAction(QAbstractScrollArea *view, QPoint point, const QString &actionName) {
    bool found = false; QString error;
    QTimer::singleShot(0, view, [&, actionName] {
        auto *menu = view->findChild<QMenu *>("timelineTrackMenu");
        if (!menu || !menu->isVisible()) { error = "Track context menu did not open."; if (auto *popup = QApplication::activePopupWidget()) popup->close(); return; }
        auto *action = menu->findChild<QAction *>(actionName);
        if (!action || !action->isEnabled()) error = "Missing or disabled context action: " + actionName;
        else { found = true; action->trigger(); }
        menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->viewport()->mapToGlobal(point));
    QApplication::sendEvent(view->viewport(), &event); QVERIFY2(found, qPrintable(error));
}
}

class CelTimelinePanelTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void clickToggleAndRangeSelectAcrossTracksWithoutEditing() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel);
        click(view, 3, 0); QCOMPARE(controller.selectedCels(), (QVector<TimelineCelRef>{ref(project, 1, 0)}));
        click(view, 11, 0, Qt::ControlModifier); QCOMPARE(controller.selectedCels().size(), 2);
        click(view, 11, 0, Qt::ControlModifier); QCOMPARE(controller.selectedCels().size(), 1);
        click(view, 3, 0); click(view, 11, 1, Qt::ShiftModifier);
        QCOMPARE(controller.selectedCels().size(), 4);
        for (int track : {0, 1}) for (int cel : {0, 1}) QVERIFY(controller.selectedCels().contains(ref(project, track, cel)));
        QTest::keySequence(view, QKeySequence::SelectAll); QCOMPARE(controller.selectedCels().size(), 6);
        QTest::keyClick(view, Qt::Key_Escape); QVERIFY(controller.selectedCels().isEmpty());
        QCOMPARE(canvas.project().toJson(), project.toJson()); QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void bulkDragPreservesGapsPreviewsWithoutMutationAndRejectsCollision() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel);
        click(view, 3, 0); click(view, 11, 0, Qt::ControlModifier);
        const auto from = cell(view, 3, 0), to = cell(view, 7, 0);
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from); move(view, to);
        QVERIFY(controller.hasPendingEdits()); QCOMPARE(starts(canvas, 1), (QVector<int>{2, 10, 18}));
        QCOMPARE(canvas.document()->history()->count(), 0);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(starts(canvas, 1), (QVector<int>{6, 14, 18})); QCOMPARE(starts(canvas, 0), (QVector<int>{2, 10, 18}));
        QCOMPARE(canvas.document()->history()->count(), 1); QCOMPARE(controller.selectedCels().size(), 2); QVERIFY(!controller.hasPendingEdits());
        QCOMPARE(canvas.shot().layerAnimations, project.shots[0].layerAnimations);
        for (int i = 0; i < canvas.shot().layers.size(); ++i) QCOMPARE(canvas.shot().layers[i].tiles, project.shots[0].layers[i].tiles);
        canvas.document()->history()->undo(); QCOMPARE(canvas.project().toJson(), project.toJson());
        const int history = canvas.document()->history()->count(); QSignalSpy warning(canvas.document(), &Document::warning);
        drag(view, cell(view, 3, 0), cell(view, 11, 0)); // The second selected cel would collide with the third.
        QCOMPARE(canvas.project().toJson(), project.toJson()); QCOMPARE(canvas.document()->history()->count(), history);
        QVERIFY(!warning.isEmpty()); QVERIFY(!controller.hasPendingEdits());
    }
    void marqueeEscapeRestoresPriorSelectionAndCommittedMarqueeIsTransient() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel); click(view, 3, 0);
        const auto original = controller.selectedCels(); const auto from = cell(view, 7, 0), to = cell(view, 20, 1);
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from); move(view, to);
        QCOMPARE(controller.selectedCels().size(), 4); QTest::keyClick(view, Qt::Key_Escape);
        QCOMPARE(controller.selectedCels(), original);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        drag(view, from, to); QCOMPARE(controller.selectedCels().size(), 4);
        for (int track : {0, 1}) for (int cel : {1, 2}) QVERIFY(controller.selectedCels().contains(ref(project, track, cel)));
        QCOMPARE(canvas.project().toJson(), project.toJson()); QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void celAndKeyClipboardShortcutsStayIndependent() {
        auto project = fixture(); auto &track = project.shots[0].animationTracks[1];
        const auto ink = track.drawings[0].templateLayers.value("ink");
        project.shots[0].layerAnimations = {{ink, {}, {{KeyframeProperty::PositionX, {{2, 0}, {4, 20}}}}}};
        Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        QVERIFY(controller.selectTrack(track.id)); QVERIFY(controller.setFrame(2));
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel);
        click(view, 3, 0); QTest::keySequence(view, QKeySequence::Copy); QVERIFY(controller.canPasteCels());
        click(view, 4, 1); QCOMPARE(controller.selectedKeys().size(), 1); QVERIFY(controller.selectedCels().isEmpty());
        QTest::keySequence(view, QKeySequence::Copy); QVERIFY(controller.canPasteKeys());
        seek(view, 3); QTest::keySequence(view, QKeySequence::Paste);
        const auto *channel = propertyChannel(canvas.shot(), ink, KeyframeProperty::PositionX); QVERIFY(channel); QCOMPARE(channel->keys.size(), 3);
        QCOMPARE(channel->keys[1].frame, 3); QCOMPARE(canvas.shot().animationTracks[1].exposures.size(), 3);
        const auto keys = canvas.shot().layerAnimations;
        click(view, 11, 0); seek(view, 30); QTest::keySequence(view, QKeySequence::Paste);
        QCOMPARE(canvas.shot().animationTracks[1].exposures.size(), 4);
        QCOMPARE(exposureAtFrame(canvas.shot().animationTracks[1], 30)->drawingId, track.drawings[0].id);
        QCOMPARE(canvas.shot().animationTracks[1].drawings.size(), 3); QCOMPARE(canvas.shot().layerAnimations, keys);
    }
    void contextHoldReverseAndRepeatKeepSharedDrawingIdentity() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel);
        click(view, 3, 0); click(view, 11, 0, Qt::ControlModifier);
        contextAction(view, cell(view, 3, 0), "timelineHold2"); QCOMPARE(starts(canvas, 1), (QVector<int>{2, 4, 18}));
        QCOMPARE(canvas.shot().animationTracks[1].exposures[0].durationFrames, 2);
        contextAction(view, cell(view, 3, 0), "timelineReverseCels");
        QCOMPARE(exposureAtFrame(canvas.shot().animationTracks[1], 2)->drawingId, project.shots[0].animationTracks[1].drawings[1].id);
        contextAction(view, cell(view, 3, 0), "timelineRepeatCels");
        QCOMPARE(starts(canvas, 1), (QVector<int>{2, 4, 6, 8, 18})); QCOMPARE(controller.selectedCels().size(), 2);
        QCOMPARE(exposureAtFrame(canvas.shot().animationTracks[1], 6)->drawingId, project.shots[0].animationTracks[1].drawings[1].id);
        QCOMPARE(exposureAtFrame(canvas.shot().animationTracks[1], 8)->drawingId, project.shots[0].animationTracks[1].drawings[0].id);
        QCOMPARE(canvas.shot().animationTracks[1].drawings.size(), 3); QCOMPARE(canvas.shot().layers.size(), project.shots[0].layers.size());
        QCOMPARE(canvas.document()->history()->count(), 3);
        for (int i = 0; i < 3; ++i) canvas.document()->history()->undo();
        QCOMPARE(canvas.project().toJson(), project.toJson());
    }
    void reuseToolbarOpensPickerAndCancelLeavesArtworkUntouched() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        QVERIFY(controller.selectTrack(project.shots[0].animationTracks[1].id)); QVERIFY(controller.setFrame(30));
        TimelinePanel panel(&controller); show(panel); auto *button = panel.findChild<QToolButton *>("timelineReuseDrawing");
        QVERIFY(button); QVERIFY(button->isEnabled()); bool opened = false;
        QTimer::singleShot(0, &panel, [&] {
            auto *dialog = panel.findChild<DrawingReuseDialog *>("drawingReuseDialog");
            if (!dialog) { if (auto *modal = QApplication::activeModalWidget()) modal->close(); return; }
            auto *buttons = dialog->findChild<QDialogButtonBox *>();
            opened = dialog->isVisible() && buttons;
            if (buttons) buttons->button(QDialogButtonBox::Cancel)->click(); else dialog->reject();
        });
        QTest::mouseClick(button, Qt::LeftButton); QVERIFY(opened);
        QCOMPARE(canvas.project().toJson(), project.toJson()); QCOMPARE(canvas.document()->history()->count(), 0);
        controller.setTransitionGuard([] { return false; });
        QTest::mouseClick(button, Qt::LeftButton); QVERIFY(!panel.findChild<DrawingReuseDialog *>());
        QCOMPARE(canvas.document()->history()->count(), 0);
    }
    void bulkInspectorCancelsDraftAndCommitsMixedHoldsAndStartOnce() {
        auto project = fixture(); project.shots[0].animationTracks[1].exposures[1].durationFrames = 5;
        Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        QVERIFY(controller.selectTrack(project.shots[0].animationTracks[1].id)); QVERIFY(controller.setFrame(3));
        controller.setSelectedCels({ref(project, 1, 0), ref(project, 1, 1)});
        QWidget workspace; auto *layout = new QHBoxLayout(&workspace);
        TimelinePanel panel(&controller, &workspace); FramePropertiesPanel properties(&controller, &workspace);
        layout->addWidget(&panel, 3); layout->addWidget(&properties, 1);
        settle(); workspace.resize(1180, 420); workspace.show(); QVERIFY(QTest::qWaitForWindowExposed(&workspace));
        workspace.raise(); workspace.activateWindow(); QVERIFY(QTest::qWaitForWindowActive(&workspace)); settle();
        auto *context = properties.findChild<QLabel *>("frameDrawingContext"); QVERIFY(context); QVERIFY(context->text().contains("Mixed holds"));
        QVERIFY(!properties.findChild<QLineEdit *>("frameDrawingName")->isEnabled());
        auto *hold = properties.findChild<ValueSlider *>("frameExposureHold")->findChild<QDoubleSpinBox *>();
        hold->setFocus(); QTRY_VERIFY(hold->hasFocus()); hold->selectAll(); QTest::keyClicks(hold, "3");
        QVERIFY(controller.hasPendingEdits()); QTest::keyClick(hold, Qt::Key_Escape);
        QVERIFY(!controller.hasPendingEdits()); QCOMPARE(canvas.project().toJson(), project.toJson());
        QCOMPARE(canvas.document()->history()->count(), 0); QVERIFY(context->text().contains("Mixed holds"));
        hold->selectAll(); QTest::keyClicks(hold, "3"); QTest::keyClick(hold, Qt::Key_Return);
        QCOMPARE(starts(canvas, 1), (QVector<int>{2, 5, 18}));
        QCOMPARE(canvas.shot().animationTracks[1].exposures[1].durationFrames, 3);
        QCOMPARE(canvas.document()->history()->count(), 1); QVERIFY(!context->text().contains("Mixed holds"));
        auto *start = properties.findChild<ValueSlider *>("frameExposureStart")->findChild<QDoubleSpinBox *>();
        start->setFocus(); QTRY_VERIFY(start->hasFocus()); start->selectAll(); QTest::keyClicks(start, "5"); QTest::keyClick(start, Qt::Key_Return);
        QCOMPARE(starts(canvas, 1), (QVector<int>{4, 7, 18})); QCOMPARE(starts(canvas, 0), (QVector<int>{2, 10, 18}));
        QCOMPARE(canvas.document()->history()->count(), 2); QVERIFY(!controller.hasPendingEdits());
        canvas.document()->history()->undo(); canvas.document()->history()->undo(); QCOMPARE(canvas.project().toJson(), project.toJson());
    }
    void renderPreview() {
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); if (output.isEmpty()) QSKIP("Opt-in multi-cel previews.");
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        QVERIFY(controller.selectTrack(project.shots[0].animationTracks[1].id)); QVERIFY(controller.setFrame(3));
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel);
        click(view, 3, 0); click(view, 11, 1, Qt::ShiftModifier); QCOMPARE(controller.selectedCels().size(), 4);
        FramePropertiesPanel properties(&controller); properties.resize(280, 380); properties.show(); settle(); QDir().mkpath(output);
        QVERIFY(panel.grab().save(output + "/multiple-cels-timeline.png")); QVERIFY(properties.grab().save(output + "/multiple-cels-properties.png"));
        properties.resize(240, 380); settle(); QVERIFY(properties.grab().save(output + "/multiple-cels-properties-240.png"));
    }
};
QTEST_MAIN(CelTimelinePanelTests)
#include "test_cel_timeline_panel.moc"
