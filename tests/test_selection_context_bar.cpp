#include "canvas.h"
#include "main_window.h"
#include "selection_context_bar.h"
#include "value_slider.h"

#include <QAction>
#include <QDir>
#include <QMenu>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTabletEvent>
#include <QToolButton>
#include <QtTest>

using namespace toon;
namespace {
Project artwork() {
    auto project = Project::create(); project.width = 640; project.height = 480; project.transparent = true;
    project.movie = "City of Lanterns"; project.shots[0].number = "C012"; project.shots[0].name = "Character color study";
    QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter painter(&image); painter.fillRect(QRect(60, 60, 150, 100), QColor("#659a9b"));
    painter.fillRect(QRect(85, 75, 45, 70), QColor("#314f60")); painter.end();
    project.shots[0].layers[0].tiles[{0, 0}] = image; return project;
}
QPainterPath selection() { QPainterPath path; path.addRect(QRectF(60, 60, 150, 100)); return path; }
QToolButton *button(SelectionContextBar *bar, const char *name) { return bar->findChild<QToolButton *>(name); }
QAction *action(SelectionContextBar *bar, const char *name) { return bar->findChild<QAction *>(name); }
SelectionContextBar *prepare(Canvas &canvas, QSize size = {900, 700}) {
    canvas.resize(size); canvas.setProject(artwork()); canvas.show();
    canvas.document()->setRasterSelection(selection()); QCoreApplication::processEvents();
    return canvas.findChild<SelectionContextBar *>();
}
void dragGrip(QToolButton *grip, QPoint delta) {
    const auto local = QPointF(grip->rect().center()); const auto global = QPointF(grip->mapToGlobal(local.toPoint()));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(grip, &press);
    QMouseEvent move(QEvent::MouseMove, local + delta, global + delta, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(grip, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, local, global + delta, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(grip, &release);
}
}
class SelectionContextBarTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }
    void emptySelectionKeepsDeselectAvailable() {
        Canvas canvas; auto *bar = prepare(canvas); auto *document = canvas.document();
        document->setRasterSelection(QPainterPath());
        QVERIFY(document->rasterSelection().active);
        QVERIFY(bar->isVisible());
        QVERIFY(button(bar, "contextDeselect")->isVisible());
        QVERIFY(button(bar, "contextDeselect")->isEnabled());
        QVERIFY(!action(bar, "contextFill")->isEnabled());
        QVERIFY(!action(bar, "contextClear")->isEnabled());
        QVERIFY(!button(bar, "contextTransform")->isEnabled());
        button(bar, "contextDeselect")->click();
        QVERIFY(!document->rasterSelection().active);
        QVERIFY(bar->isHidden());
        document->setRasterSelection(QPainterPath());
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!document->rasterSelection().active);
        QVERIFY(canvas.toolBlockReason().isEmpty());
    }
    void deselectShortcutRecoversEmptyAndFragmentedSelections() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        MainWindow window(directory.filePath("workspace.ini"));
        const auto projectPath = directory.filePath("selection.toonproj");
        QVERIFY(saveProject(artwork(), projectPath, nullptr));
        QVERIFY(window.openProject(projectPath)); window.resize(1100, 800); window.show();
        window.activateWindow();
        auto *canvas = window.canvas(); canvas->setTool(Canvas::Tool::Brush); canvas->setFocus();
        QApplication::processEvents();
        auto *deselect = canvas->findChild<QAction *>("deselectAction"); QVERIFY(deselect);
        QPainterPath islands;
        for (int y = 40; y < 420; y += 8) for (int x = 40; x < 600; x += 8) islands.addRect(x, y, 3, 3);
        for (const auto &path : {QPainterPath(), islands}) {
            canvas->document()->setRasterSelection(path);
            QVERIFY(canvas->document()->rasterSelection().active); QVERIFY(deselect->isEnabled());
            canvas->setFocus(); QApplication::processEvents();
            QTest::keySequence(canvas, deselect->shortcut());
            QVERIFY(!canvas->document()->rasterSelection().active);
            QVERIFY(!deselect->isEnabled()); QVERIFY(canvas->toolBlockReason().isEmpty());
        }
        QVERIFY(canvas->document()->history()->isClean());
    }
    void visibilityModesAndSelectionActions() {
        Canvas canvas; auto *bar = canvas.findChild<SelectionContextBar *>(); QVERIFY(bar); QVERIFY(bar->isHidden());
        bar = prepare(canvas); QVERIFY(bar); QVERIFY(bar->isVisible());
        for (const auto *name : {"contextDragGrip", "contextSelectionReplace", "contextTransform", "contextInvert", "contextDeselect", "contextMore"}) {
            auto *control = button(bar, name); QVERIFY(control); QVERIFY(!control->icon().isNull());
            QCOMPARE(control->focusPolicy(), Qt::NoFocus); QVERIFY(!control->toolTip().isEmpty());
        }
        const auto original = canvas.document()->project().shots[0].layers[0].tiles;
        for (const auto &entry : {qMakePair("contextSelectionAdd", SelectionCombine::Add),
                                 qMakePair("contextSelectionSubtract", SelectionCombine::Subtract),
                                 qMakePair("contextSelectionIntersect", SelectionCombine::Intersect),
                                 qMakePair("contextSelectionReplace", SelectionCombine::Replace)}) {
            auto *item = action(bar, entry.first); QVERIFY(item);
            auto *control = button(bar, entry.first); QVERIFY(control); QVERIFY(control->isVisible()); control->click();
            QCOMPARE(canvas.selectionCombine(), entry.second); QVERIFY(item->isChecked());
        }
        button(bar, "contextInvert")->click();
        QVERIFY(!canvas.document()->rasterSelection().path.contains({100, 100}));
        QVERIFY(canvas.document()->rasterSelection().path.contains({400, 300}));
        button(bar, "contextDeselect")->click(); QVERIFY(bar->isHidden()); QVERIFY(!canvas.document()->rasterSelection().active);
        action(bar, "contextSelectAll")->trigger(); QVERIFY(bar->isVisible());
        QCOMPARE(canvas.document()->history()->count(), 0); QCOMPARE(canvas.document()->project().shots[0].layers[0].tiles, original);
        const bool focused = canvas.selectionFocusOverlay();
        action(bar, "contextFocus")->trigger(); QCOMPARE(canvas.selectionFocusOverlay(), !focused);
        action(bar, "contextHide")->trigger(); QVERIFY(bar->isHidden()); QVERIFY(!canvas.selectionBarVisible());
        canvas.setSelectionBarVisible(true); QVERIFY(bar->isVisible());
        canvas.document()->setRasterSelection({}); QVERIFY(bar->isVisible());
        QVERIFY(button(bar, "contextDeselect")->isEnabled());
    }
    void fillClearAndResizeAreFunctional() {
        Canvas canvas; auto *bar = prepare(canvas); QVERIFY(bar); auto *document = canvas.document();
        const auto original = document->project().shots[0].layers[0].tiles;
        canvas.setBrushColor(QColor("#d99469")); action(bar, "contextFill")->trigger();
        QCOMPARE(document->history()->count(), 1);
        QCOMPARE(renderMovieFrame(document->project()).pixelColor(100, 100), QColor("#d99469"));
        QCOMPARE(renderMovieFrame(document->project()).pixelColor(30, 30).alpha(), 0);
        document->history()->undo(); QCOMPARE(document->project().shots[0].layers[0].tiles, original);
        action(bar, "contextClear")->trigger(); QCOMPARE(renderMovieFrame(document->project()).pixelColor(100, 100).alpha(), 0);
        QCOMPARE(document->history()->count(), 1); document->history()->undo();
        QCOMPARE(document->project().shots[0].layers[0].tiles, original);
        auto *expand = bar->findChild<ValueSlider *>("contextExpandAmount"); QVERIFY(expand); expand->setValue(8);
        bar->findChild<QPushButton *>("contextExpandApply")->click();
        QVERIFY(document->rasterSelection().path.contains({55, 100}));
        QCOMPARE(document->history()->index(), 0);
        document->setRasterSelection(selection());
        auto *contract = bar->findChild<ValueSlider *>("contextContractAmount"); QVERIFY(contract); contract->setValue(8);
        bar->findChild<QPushButton *>("contextContractApply")->click();
        QVERIFY(!document->rasterSelection().path.contains({65, 100})); QVERIFY(document->rasterSelection().path.contains({100, 100}));
        document->setLayerAlphaLocked(true); QVERIFY(!action(bar, "contextClear")->isEnabled());
    }
    void transformApplyAndCancel() {
        Canvas canvas; auto *bar = prepare(canvas); QVERIFY(bar); auto *document = canvas.document();
        const auto original = document->project().shots[0].layers[0].tiles;
        button(bar, "contextTransform")->click(); QVERIFY(document->transformActive());
        QVERIFY(button(bar, "contextSelectionReplace")->isHidden()); QVERIFY(button(bar, "contextApply")->isVisible());
        QVERIFY(button(bar, "contextCancel")->isVisible()); QVERIFY(!action(bar, "contextFill")->isEnabled());
        QVERIFY(!action(bar, "contextExpand")->isEnabled());
        TransformParameters transform; transform.translation = {20, 10}; QVERIFY(document->previewTransform(transform));
        button(bar, "contextApply")->click(); QVERIFY(!document->transformActive()); QCOMPARE(document->history()->count(), 1);
        const auto committed = document->project().shots[0].layers[0].tiles; QVERIFY(committed != original);
        button(bar, "contextTransform")->click(); QVERIFY(document->transformActive());
        QVERIFY(document->previewTransform(transform)); button(bar, "contextCancel")->click();
        QVERIFY(!document->transformActive()); QCOMPARE(document->project().shots[0].layers[0].tiles, committed);
        QCOMPARE(document->history()->count(), 1); document->history()->undo();
        QCOMPARE(document->project().shots[0].layers[0].tiles, original);
    }
    void followsFlipsClampsAndPins() {
        Canvas canvas; auto *bar = prepare(canvas); QVERIFY(bar);
        auto bounds = canvas.selectionViewportBounds();
        QVERIFY(bar->y() >= bounds.bottom()); QVERIFY(!QRectF(bar->geometry()).intersects(bounds));
        const auto first = bar->pos(); canvas.centerOn(canvas.center() + QPointF(60, 30)); QVERIFY(bar->pos() != first);
        canvas.setRotation(20); bounds = canvas.selectionViewportBounds();
        QVERIFY(canvas.rect().adjusted(7, 7, -7, -7).contains(bar->geometry()));
        dragGrip(button(bar, "contextDragGrip"), {50, -40}); QVERIFY(bar->positionPinned());
        const auto pinned = bar->pos(); canvas.centerOn(canvas.center() + QPointF(40, 30)); QCOMPARE(bar->pos(), pinned);
        action(bar, "contextReset")->trigger(); QVERIFY(!bar->positionPinned()); QVERIFY(bar->pos() != pinned);
        canvas.setRotation(0); canvas.setZoom(1);
        bounds = canvas.selectionViewportBounds();
        canvas.centerOn(canvas.center() - QPointF(0, canvas.height() - 30 - bounds.bottom()));
        bounds = canvas.selectionViewportBounds(); QVERIFY(bar->geometry().bottom() < bounds.top());
        canvas.centerOn(canvas.center() + QPointF(5000, 5000));
        QVERIFY(canvas.rect().adjusted(7, 7, -7, -7).contains(bar->geometry()));
    }
    void narrowCanvasAndGestures() {
        Canvas canvas; auto *bar = prepare(canvas, {320, 260}); QVERIFY(bar);
        QVERIFY(bar->width() <= canvas.width() - 16); QVERIFY(canvas.rect().contains(bar->geometry()));
        canvas.setTool(Canvas::Tool::RectangleSelect);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, {100, 100});
        QVERIFY(canvas.selectionGestureActive()); QVERIFY(bar->isHidden());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, {130, 130});
        QVERIFY(!canvas.selectionGestureActive()); QVERIFY(bar->isVisible());
        canvas.document()->setRasterSelection(selection()); button(bar, "contextTransform")->click();
        QVERIFY(canvas.document()->transformActive()); QVERIFY(bar->width() <= canvas.width() - 16);
        QVERIFY(canvas.rect().contains(bar->geometry())); canvas.document()->cancelTransform();
    }
    void barBackgroundDoesNotReachCanvas_data() {
        QTest::addColumn<int>("tool");
        QTest::newRow("rectangle") << int(Canvas::Tool::RectangleSelect);
        QTest::newRow("brush") << int(Canvas::Tool::Brush);
    }
    void barBackgroundDoesNotReachCanvas() {
        QFETCH(int, tool);
        Canvas canvas; auto *bar = prepare(canvas); QVERIFY(bar);
        canvas.setTool(Canvas::Tool(tool)); QVERIFY(bar->testAttribute(Qt::WA_NoMousePropagation));
        const auto pixels = canvas.document()->project().shots[0].layers[0].tiles;
        const auto path = canvas.document()->rasterSelection().path;
        const QPoint padding(2, 2); QVERIFY(!bar->childAt(padding));
        QTest::mousePress(bar, Qt::LeftButton, Qt::NoModifier, padding);
        QVERIFY(!canvas.selectionGestureActive()); QVERIFY(!canvas.document()->strokeActive());
        QTest::mouseRelease(bar, Qt::LeftButton, Qt::NoModifier, padding);
        QPointingDevice pen("Toolbar pen", 24, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        for (QWidget *target : {static_cast<QWidget *>(bar), static_cast<QWidget *>(button(bar, "contextInvert"))}) {
            const auto position = target == bar ? padding : target->rect().center();
            for (const auto type : {QEvent::TabletPress, QEvent::TabletMove, QEvent::TabletRelease}) {
                const bool release = type == QEvent::TabletRelease;
                QTabletEvent event(type, &pen, position, target->mapToGlobal(position), release ? 0 : .5, 0, 0, 0, 0, 0,
                                   Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                                   release ? Qt::NoButton : Qt::LeftButton);
                event.setAccepted(false); QApplication::sendEvent(target, &event);
                QVERIFY(!event.isAccepted()); // Keep Qt's pen-to-mouse synthesis available for toolbar controls.
                QVERIFY(!canvas.selectionGestureActive()); QVERIFY(!canvas.document()->strokeActive());
            }
        }
        QCOMPARE(canvas.document()->rasterSelection().path, path);
        QCOMPARE(canvas.document()->project().shots[0].layers[0].tiles, pixels);
        QCOMPARE(canvas.document()->history()->count(), 0); QVERIFY(bar->isVisible());
    }
    void screenshot() {
        Canvas canvas; auto *bar = prepare(canvas, {960, 760}); QVERIFY(bar);
        canvas.setSelectionFocusOverlay(true); canvas.setZoom(1.4);
        const auto output = sheetGeometry(canvas.project(), canvas.shot()).output.topLeft();
        canvas.centerOn(output + QPointF(240, 150)); QCoreApplication::processEvents();
        QVERIFY(bar->isVisible()); QDir().mkpath("test-artifacts");
        QVERIFY(canvas.grab().save("test-artifacts/selection-context-bar.png"));
    }
};
QTEST_MAIN(SelectionContextBarTests)
#include "test_selection_context_bar.moc"
