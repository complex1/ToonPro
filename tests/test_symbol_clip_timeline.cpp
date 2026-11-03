#include "timeline_test_geometry.h"
#include "animation_test_fixture.h"
#include "canvas.h"
#include "document.h"
#include "symbol_clip_panel.h"
#include "symbol_clips.h"
#include "symbol_frame_picker_dialog.h"
#include "symbols.h"
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
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtTest>

using namespace toon;
namespace {
Project fixture() {
    auto project = Project::create(); project.width = 160; project.height = 120;
    project.shots[0].durationFrames = 60;
    auto content = animation_fixture::project().shots[0];
    SymbolDefinition definition{animation_fixture::id(), "Walk cycle", content};
    project.symbols = {definition};
    auto instance = RasterLayer::create("Character Symbol"); instance.kind = LayerKind::Symbol;
    instance.symbol = SymbolInstance{definition.id, SymbolPlayback::PingPong, 5, 2, 1.5, 12, 1};
    project.shots[0].layers = {instance};
    return project;
}
QAbstractScrollArea *grid(QWidget &panel) { return panel.findChild<QAbstractScrollArea *>("timelineGrid"); }
int label(QAbstractScrollArea *view) { return timeline_test::labelWidth(view); }
QPoint cell(QAbstractScrollArea *view, int frame, int row = 0) {
    return {label(view) + frame * 20 + 10 - view->horizontalScrollBar()->value(),
        timeline_test::rowCenter(view,row)};
}
QPoint edge(QAbstractScrollArea *view, int frame, int row = 0) { return cell(view, frame, row) - QPoint(10, 0); }
void settle() { QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest); }
void show(TimelinePanel &panel) {
    settle(); panel.resize(900, 360); panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel)); panel.raise(); panel.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&panel)); settle();
    grid(panel)->horizontalScrollBar()->setValue(0); grid(panel)->verticalScrollBar()->setValue(0);
}
void move(QAbstractScrollArea *view, QPoint point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(QEvent::MouseMove, point, view->viewport()->mapToGlobal(point), Qt::NoButton, Qt::LeftButton, modifiers);
    QApplication::sendEvent(view->viewport(), &event);
}
void drag(QAbstractScrollArea *view, QPoint from, QPoint to, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::mousePress(view->viewport(), Qt::LeftButton, modifiers, from); move(view, to, modifiers);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, modifiers, to);
}
void click(QAbstractScrollArea *view, int frame, int row = 0) {
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, cell(view, frame, row));
}
const SymbolInstance &clip(const Canvas &canvas, const QString &id = {}) {
    for (const auto &layer : canvas.shot().layers) if (layer.symbol && (id.isEmpty() || layer.id == id)) return *layer.symbol;
    Q_UNREACHABLE_RETURN(*canvas.shot().layers[0].symbol);
}
bool contains(QWidget *parent, QWidget *child) { return child && parent->rect().contains(QRect(child->mapTo(parent, QPoint()), child->size())); }
void contextAction(QAbstractScrollArea *view, QPoint point, const QString &name) {
    bool found = false; QString error;
    QTimer::singleShot(0, view, [&, name] {
        auto *menu = view->findChild<QMenu *>("timelineSymbolMenu");
        if (!menu || !menu->isVisible()) { error = "Symbol context menu did not open."; if (auto *popup = QApplication::activePopupWidget()) popup->close(); return; }
        auto *action = menu->findChild<QAction *>(name);
        if (!action || !action->isEnabled()) error = "Missing or disabled Symbol context action: " + name;
        else { found = true; action->trigger(); }
        menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->viewport()->mapToGlobal(point));
    QApplication::sendEvent(view->viewport(), &event); QVERIFY2(found, qPrintable(error));
}
} // namespace

class SymbolClipTimelineTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void bodyDragPreviewsOnlyThenCommitsOneUndo() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel);
        const auto original = clip(canvas); const auto from = cell(view, 8), to = cell(view, 12);
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from); move(view, to);
        QCOMPARE(controller.selectedSymbolClipId(), project.shots[0].layers[0].id);
        QVERIFY(controller.hasPendingEdits()); QCOMPARE(clip(canvas), original); QCOMPARE(canvas.document()->history()->count(), 0);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(clip(canvas).startFrame, 9); QCOMPARE(clip(canvas).durationFrames, 12);
        QCOMPARE(clip(canvas).playbackOffset, original.playbackOffset); QCOMPARE(clip(canvas).firstFrame, original.firstFrame);
        QCOMPARE(clip(canvas).speed, original.speed); QCOMPARE(clip(canvas).playback, original.playback);
        QCOMPARE(canvas.document()->history()->count(), 1); QVERIFY(!controller.hasPendingEdits());
        QCOMPARE(canvas.project().toJson().value("symbols"), project.toJson().value("symbols"));
        QCOMPARE(canvas.project().symbols[0].content.layers[3].tiles, project.symbols[0].content.layers[3].tiles);
        canvas.document()->history()->undo(); QCOMPARE(canvas.project().toJson(), project.toJson());
    }
    void edgeTrimsPreserveFractionalPlaybackPhase() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel); const auto original = clip(canvas);
        drag(view, edge(view, 5), edge(view, 7));
        QCOMPARE(clip(canvas).startFrame, 7); QCOMPARE(clip(canvas).durationFrames, 10);
        QCOMPARE(clip(canvas).playbackOffset, original.playbackOffset + 2);
        for (int frame = 7; frame < 17; ++frame)
            QCOMPARE(symbolLocalFrame(clip(canvas), frame, 20), symbolLocalFrame(original, frame, 20));
        QCOMPARE(canvas.document()->history()->count(), 1);
        canvas.document()->history()->undo(); QCOMPARE(clip(canvas), original);
        drag(view, edge(view, 17), edge(view, 14));
        QCOMPARE(clip(canvas).startFrame, 5); QCOMPARE(clip(canvas).durationFrames, 9);
        QCOMPARE(clip(canvas).playbackOffset, original.playbackOffset);
        for (int frame = 5; frame < 14; ++frame)
            QCOMPARE(symbolLocalFrame(clip(canvas), frame, 20), symbolLocalFrame(original, frame, 20));
        QCOMPARE(canvas.document()->history()->count(), 1); canvas.document()->history()->undo();
        QCOMPARE(canvas.project().toJson(), project.toJson());
    }
    void altDragAndKeyboardSlipChangeContentWithoutMovingClip() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel); const auto original = clip(canvas);
        drag(view, cell(view, 8), cell(view, 11), Qt::AltModifier);
        QCOMPARE(clip(canvas).startFrame, original.startFrame); QCOMPARE(clip(canvas).durationFrames, original.durationFrames);
        QCOMPARE(clip(canvas).playbackOffset, original.playbackOffset + 3); QCOMPARE(clip(canvas).firstFrame, original.firstFrame);
        QCOMPARE(symbolLocalFrame(clip(canvas), 8, 20), symbolLocalFrame(original, 11, 20));
        QTest::keyClick(view, Qt::Key_Right, Qt::AltModifier | Qt::ShiftModifier);
        QCOMPARE(clip(canvas).playbackOffset, original.playbackOffset + 4); QCOMPARE(clip(canvas).startFrame, 5);
        QTest::keyClick(view, Qt::Key_Left, Qt::AltModifier);
        QCOMPARE(clip(canvas).startFrame, 4); QCOMPARE(canvas.document()->history()->count(), 3);
        for (int i = 0; i < 3; ++i) canvas.document()->history()->undo();
        QCOMPARE(canvas.project().toJson(), project.toJson());
    }
    void escapeAndInvalidRangeDiscardDragAndLockedClipCannotMove() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel);
        const auto from = cell(view, 8), to = cell(view, 12);
        QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, from); move(view, to);
        QTest::keyClick(view, Qt::Key_Escape); QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QCOMPARE(canvas.project().toJson(), project.toJson()); QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(!controller.hasPendingEdits());
        QSignalSpy warning(canvas.document(), &Document::warning);
        drag(view, cell(view, 8), cell(view, 1)); // Moving back seven frames would put the start before the shot.
        QCOMPARE(canvas.project().toJson(), project.toJson()); QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(!warning.isEmpty()); QVERIFY(!controller.hasPendingEdits());
        canvas.document()->setLayersLocked({project.shots[0].layers[0].id}, true);
        const auto locked = canvas.project().toJson(); const int history = canvas.document()->history()->count();
        drag(view, cell(view, 8), cell(view, 12)); QTest::keyClick(view, Qt::Key_Delete);
        QCOMPARE(canvas.project().toJson(), locked); QCOMPARE(canvas.document()->history()->count(), history);
    }
    void folderAndUnusedDrawingSymbolsHaveIndependentRows() {
        auto project = fixture(); const auto id = project.shots[0].layers[0].id;
        auto folder = RasterLayer::create("Character folder"); folder.group = true;
        auto inner = RasterLayer::create("Parts"); inner.group = true; inner.parentId = folder.id;
        project.shots[0].layers[0].parentId = inner.id; project.shots[0].layers.prepend(inner); project.shots[0].layers.prepend(folder);
        Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas); TimelinePanel panel(&controller); show(panel);
        auto *view = grid(panel);
        const auto symbolRow = [view](const QString &layerId) { return view->property("timelineRowIds").toStringList().indexOf("symbol:" + layerId); };
        const auto initialRows = view->property("timelineRowIds").toStringList();
        QVERIFY(initialRows.contains(folder.id)); QVERIFY(initialRows.contains(inner.id));
        QVERIFY(initialRows.indexOf(folder.id) < initialRows.indexOf(inner.id)); QVERIFY(symbolRow(id) > initialRows.indexOf(inner.id));
        click(view, 8, symbolRow(id)); QCOMPARE(controller.selectedSymbolClipId(), id);
        drag(view, cell(view, 8, symbolRow(id)), cell(view, 9, symbolRow(id))); QCOMPARE(clip(canvas, id).startFrame, 6);
        canvas.document()->history()->undo();

        auto celProject = animation_fixture::project(); celProject.symbols = project.symbols;
        auto &source = celProject.shots[0]; source.animationTracks[0].exposures.removeFirst();
        auto &layer = source.layers[3]; const auto celId = layer.id;
        layer.tiles.clear(); layer.kind = LayerKind::Symbol; layer.symbol = SymbolInstance{project.symbols[0].id, SymbolPlayback::Loop, 0, 0, 1, 15, 0};
        canvas.setProject(celProject); QVERIFY(controller.setFrame(10)); settle();
        view->horizontalScrollBar()->setValue(0); view->verticalScrollBar()->setValue(0);
        const auto exposures = canvas.shot().animationTracks[0].exposures;
        QVERIFY(symbolRow(celId) >= 0);
        click(view, 8, symbolRow(celId)); QCOMPARE(controller.selectedSymbolClipId(), celId); QCOMPARE(controller.currentFrame(), 10);
        drag(view, cell(view, 8, symbolRow(celId)), cell(view, 9, symbolRow(celId))); QCOMPARE(clip(canvas, celId).startFrame, 1);
        QCOMPARE(canvas.shot().animationTracks[0].exposures, exposures);
        QVERIFY(symbolClipIntervals(canvas.shot(), celId).isEmpty()); QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void clearExposureByMenuAndDeleteRetainsSymbolAndCanBeRestored() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel); const auto original = clip(canvas);
        contextAction(view, cell(view, 8), "timelineClearSymbolClip");
        QCOMPARE(clip(canvas).durationFrames, 0); QCOMPARE(clip(canvas).startFrame, original.startFrame);
        QCOMPARE(canvas.shot().layers.size(), 1);
        QCOMPARE(canvas.project().toJson().value("symbols"), project.toJson().value("symbols"));
        QCOMPARE(canvas.project().symbols[0].content.layers[3].tiles, project.symbols[0].content.layers[3].tiles);
        QCOMPARE(canvas.document()->history()->count(), 1); canvas.document()->history()->undo(); QCOMPARE(clip(canvas), original);
        click(view, 8); QTest::keyClick(view, Qt::Key_Delete);
        QCOMPARE(clip(canvas).durationFrames, 0); QCOMPARE(canvas.shot().layers.size(), 1);
        FramePropertiesPanel properties(&controller); properties.resize(240, 420); properties.show(); settle();
        auto *tabs = properties.findChild<QTabBar *>("framePropertiesTabs"); QCOMPARE(tabs->currentIndex(), 4);
        auto *duration = properties.findChild<ValueSlider *>("symbolClipDuration"); QVERIFY(duration); QVERIFY(duration->isEnabled());
        duration->setValue(12); QMetaObject::invokeMethod(duration, "editingFinished");
        QCOMPARE(clip(canvas).durationFrames, 12); QCOMPARE(clip(canvas), original);
        QCOMPARE(canvas.document()->history()->count(), 2);
    }
    void symbolSelectionDoesNotSelectCopyOrPasteDrawingCels() {
        auto project = animation_fixture::project(); const auto symbols = fixture();
        project.symbols = symbols.symbols; auto symbol = symbols.shots[0].layers[0];
        symbol.symbol->startFrame = 2; symbol.symbol->durationFrames = 12; project.shots[0].layers.append(symbol);
        Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        const auto &track = project.shots[0].animationTracks[0];
        controller.setSelectedCels({{track.id, track.exposures[0].id}}); QVERIFY(controller.copySelectedCels());
        QVERIFY(controller.canPasteCels()); QVERIFY(controller.setFrame(10));
        TimelinePanel panel(&controller); show(panel); auto *view = grid(panel); click(view, 8);
        QCOMPARE(controller.selectedSymbolClipId(), symbol.id); QVERIFY(controller.selectedCels().isEmpty());
        for (const auto shortcut : {QKeySequence::SelectAll, QKeySequence::Copy, QKeySequence::Paste}) {
            QTest::keySequence(view, QKeySequence(shortcut)); QVERIFY(controller.selectedCels().isEmpty());
            QCOMPARE(controller.selectedSymbolClipId(), symbol.id);
        }
        QCOMPARE(canvas.project().toJson(), project.toJson()); QCOMPARE(canvas.document()->history()->count(), 0);
        QCOMPARE(canvas.shot().animationTracks[0].exposures, track.exposures);
        for (const char *id : {"timelineNewDrawing", "timelineDuplicateDrawing", "timelineReuseDrawing"}) {
            auto *button = panel.findChild<QToolButton *>(id); QVERIFY(button); QVERIFY(!button->isEnabled());
        }
    }
    void doubleClickPickerCancelsWithoutChangesAndReturnsDrawingFocus() {
        const auto project = fixture(); QWidget workspace; auto *layout = new QVBoxLayout(&workspace);
        auto *canvas = new Canvas; canvas->setProject(project); TimelineController controller(canvas);
        // Destroy the panel before its stack controller, including early QVERIFY
        // returns; it holds controller connections during widget teardown.
        TimelinePanel panel(&controller, &workspace); layout->addWidget(canvas, 1); layout->addWidget(&panel, 1);
        workspace.resize(900, 650); workspace.show(); workspace.raise(); workspace.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&workspace)); settle(); auto *view = grid(panel);
        view->horizontalScrollBar()->setValue(0); bool opened = false;
        QTimer poll; poll.setInterval(10); QTimer deadline; deadline.setSingleShot(true);
        connect(&poll, &QTimer::timeout, &workspace, [&] {
            auto *picker = qobject_cast<SymbolFramePickerDialog *>(QApplication::activeModalWidget());
            if (!picker) return;
            poll.stop(); opened = true;
            picker->findChild<ValueSlider *>("symbolFramePickerFrame")->setValue(8);
            picker->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Cancel)->click();
        });
        connect(&deadline, &QTimer::timeout, &workspace, [] { if (auto *modal = QApplication::activeModalWidget()) modal->close(); });
        poll.start(); deadline.start(10000);
        QTest::mouseDClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, cell(view, 8));
        poll.stop(); deadline.stop(); QVERIFY(opened); QTRY_VERIFY(canvas->hasFocus());
        QCOMPARE(canvas->project().toJson(), project.toJson()); QCOMPARE(canvas->document()->history()->count(), 0);
        QVERIFY(!controller.hasPendingEdits());
    }
    void fifthTabAndNarrowInspectorKeepControlsAvailable() {
        const auto project = fixture(); Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        FramePropertiesPanel properties(&controller); properties.resize(240, 430); properties.show(); settle();
        auto *tabs = properties.findChild<QTabBar *>("framePropertiesTabs"); QVERIFY(tabs); QCOMPARE(tabs->count(), 6);
        tabs->setCurrentIndex(0); QVERIFY(controller.selectSymbolClip(project.shots[0].layers[0].id)); QCOMPARE(tabs->currentIndex(), 4);
        QCOMPARE(tabs->accessibleTabName(4), QString("Symbol clip properties")); QVERIFY(!tabs->tabToolTip(4).isEmpty());
        auto *clipPanel = properties.findChild<SymbolClipPanel *>(); QVERIFY(clipPanel); QVERIFY(clipPanel->isVisibleTo(&properties));
        auto font = properties.font(); font.setPointSizeF(font.pointSizeF() + 3); properties.setFont(font); settle();
        for (auto *control : clipPanel->findChildren<QToolButton *>()) if (control->isVisibleTo(&properties)) {
            QVERIFY(!control->accessibleName().isEmpty()); QVERIFY(!control->toolTip().isEmpty());
            QCOMPARE(control->iconSize(), QSize(16, 16)); QVERIFY(contains(&properties, control));
            QCOMPARE(control->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, control), 600);
        }
        for (const char *id : {"symbolClipStart", "symbolClipDuration", "symbolClipFirstFrame", "symbolClipSpeed", "symbolClipSlip"}) {
            auto *field = properties.findChild<ValueSlider *>(id); QVERIFY(field); QVERIFY(contains(&properties, field));
            auto *number = field->findChild<QDoubleSpinBox *>(); QCOMPARE(number->width(), 48); QVERIFY(number->suffix().isEmpty());
        }
        auto *playback = properties.findChild<QTabBar *>("symbolClipPlayback"); QVERIFY(playback);
        for (int i = 0; i < playback->count(); ++i) {
            QVERIFY(playback->rect().contains(playback->tabRect(i))); QVERIFY(!playback->accessibleTabName(i).isEmpty());
            QVERIFY(!playback->tabToolTip(i).isEmpty());
        }
    }
    void renderPreview() {
        const auto output = qEnvironmentVariable("TOON_UI_PREVIEW"); if (output.isEmpty()) QSKIP("Opt-in Symbol timeline previews.");
        auto project = fixture(); auto once = project.shots[0].layers[0]; once.id = animation_fixture::id(); once.name = "Blink once";
        once.symbol->playback = SymbolPlayback::Once; once.symbol->startFrame = 18; once.symbol->durationFrames = 12;
        auto still = once; still.id = animation_fixture::id(); still.name = "Held pose"; still.symbol->playback = SymbolPlayback::SingleFrame;
        still.symbol->startFrame = 8; still.symbol->durationFrames = 16;
        auto loop = once; loop.id = animation_fixture::id(); loop.name = "Looping walk"; loop.symbol->playback = SymbolPlayback::Loop;
        loop.symbol->startFrame = 2; loop.symbol->durationFrames = 20;
        project.shots[0].layers.append(once); project.shots[0].layers.append(still); project.shots[0].layers.append(loop);
        Canvas canvas; canvas.setProject(project); TimelineController controller(&canvas);
        TimelinePanel panel(&controller); show(panel); click(grid(panel), 8); QDir().mkpath(output);
        QVERIFY(panel.grab().save(output + "/symbol-clips-timeline.png"));
        FramePropertiesPanel properties(&controller); properties.resize(280, 430); properties.show(); settle();
        QVERIFY(properties.grab().save(output + "/symbol-clips-properties.png"));
        properties.resize(240, 430); auto font = properties.font(); font.setPointSizeF(font.pointSizeF() + 3); properties.setFont(font); settle();
        QVERIFY(properties.grab().save(output + "/symbol-clips-properties-240.png"));
    }
};
QTEST_MAIN(SymbolClipTimelineTests)
#include "test_symbol_clip_timeline.moc"
