#include "canvas.h"
#include "drawing_input.h"
#include "layer_panel.h"
#include "main_window.h"
#include <QAction>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QLineF>
#include <QPaintEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest>
#include <cmath>

using namespace toon;
namespace {
constexpr qreal pi = 3.14159265358979323846;
Project fixture(bool vector = false) {
    auto project = Project::create();
    project.width = 1920; project.height = 1080; project.transparent = true;
    project.shots[0].layers[0].name = "Ink";
    if (vector) project.shots[0].layers[0].kind = LayerKind::Vector;
    return project;
}
void prepareCanvas(MainWindow &window, int smoothing = 0, bool canvasSmoothing = true) {
    window.resize(1440, 940); window.show(); window.activateWindow();
    auto *tools = window.findChild<QDockWidget *>("toolsPanel");
    auto *properties = window.findChild<QDockWidget *>("propertiesPanel");
    auto *layers = window.findChild<QDockWidget *>("layersPanel");
    auto *timeline = window.findChild<QDockWidget *>("animationPanel");
    auto *frameProperties = window.findChild<QDockWidget *>("framePropertiesPanel");
    QVERIFY(tools && properties && layers && timeline && frameProperties);
    // The default drawing workspace hides Timeline and tabs Frame Properties.
    // Match the reported workspace: both animation panels and Layers are visible.
    window.findChild<QDockWidget *>("colorsPanel")->hide();
    window.removeDockWidget(properties);
    window.addDockWidget(Qt::LeftDockWidgetArea, properties);
    window.splitDockWidget(tools, properties, Qt::Vertical);
    window.removeDockWidget(frameProperties);
    // Layers also belongs to a tab group containing hidden reference docks.
    // Remove that grouping before splitDockWidget, which otherwise adds a tab.
    window.removeDockWidget(layers);
    window.addDockWidget(Qt::RightDockWidgetArea, layers);
    window.addDockWidget(Qt::RightDockWidgetArea, frameProperties);
    window.splitDockWidget(layers, frameProperties, Qt::Vertical);
    for (auto *dock : {tools, properties, layers, timeline, frameProperties}) { dock->show(); dock->raise(); }
    window.resizeDocks({tools, layers}, {260, 300}, Qt::Horizontal);
    window.resizeDocks({tools, properties}, {300, 300}, Qt::Vertical);
    window.resizeDocks({layers, frameProperties}, {300, 300}, Qt::Vertical);
    window.resizeDocks({timeline}, {230}, Qt::Vertical);
    QCoreApplication::processEvents();
    auto *canvas = window.canvas(); canvas->setTool(Canvas::Tool::Brush);
    BrushSettings brush; brush.size = 12; brush.color = Qt::black; brush.pressureSize = false;
    brush.smoothingEnabled = smoothing > 0;
    canvas->setBrushSettings(brush); canvas->setStabilization(smoothing);
    canvas->fitSheet(); canvas->setZoom(.4); canvas->setCanvasSmoothing(canvasSmoothing); canvas->setFocus();
    QTest::qWait(80); // Settle ordinary initial layout and thumbnail work.
    auto *layerTree = layers->findChild<QTreeWidget *>("layerTree"); QVERIFY(layerTree);
    for (auto *dock : {layers, timeline, frameProperties}) {
        QVERIFY2(dock->isVisible(), qPrintable(dock->objectName()));
        // QScrollArea itself can have an empty visibleRegion when its opaque
        // children cover it completely; check the actual layer-row viewport.
        auto *surface = dock == layers ? layerTree->viewport() : dock->widget();
        QVERIFY2(surface->isVisibleTo(&window) && !surface->visibleRegion().isEmpty(), qPrintable(dock->objectName()));
    }
}
void tablet(Canvas *canvas, const QPointingDevice &pen, QEvent::Type type,
            QPointF artwork, bool queued, int timestamp) {
    const auto point = canvas->viewTransform().map(
        sheetGeometry(canvas->project(), canvas->shot()).output.topLeft() + artwork);
    auto *event = new QTabletEvent(type, &pen, point, canvas->mapToGlobal(point),
        type == QEvent::TabletRelease ? 0 : .8, 0, 0, 0, 0, 0, Qt::NoModifier,
        type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
    event->setTimestamp(timestamp);
    if (queued) QCoreApplication::postEvent(canvas, event);
    else { QApplication::sendEvent(canvas, event); delete event; }
}
QImage thumbnail(QTreeWidget *tree) {
    auto *item = tree->topLevelItem(0);
    if (!item) return {};
    // Discover the image payload without binding the test to private role numbers.
    for (int role = Qt::UserRole; role < Qt::UserRole + 24; ++role) {
        const auto value = item->data(0, role);
        if (value.metaType() == QMetaType::fromType<QImage>()) return value.value<QImage>();
    }
    return {};
}
class InteractionObserver final : public QObject {
public:
    explicit InteractionObserver(Canvas *canvas) : m_canvas(canvas) {}
    int enabledChanges = 0, canvasPaints = 0, fullCanvasPaints = 0;
    bool eventFilter(QObject *object, QEvent *event) override {
        if (m_canvas->document()->strokeActive()) {
            if (event->type() == QEvent::EnabledChange) ++enabledChanges;
            if (object == m_canvas && event->type() == QEvent::Paint) {
                ++canvasPaints;
                if (static_cast<QPaintEvent *>(event)->region().contains(m_canvas->rect())) ++fullCanvasPaints;
            }
        }
        return false;
    }
private:
    Canvas *m_canvas;
};
}

class StrokeWorkspaceTests final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { configureDrawingInput(); applyTheme(); }
    void penBurstKeepsWorkspaceQuiet_data() {
        QTest::addColumn<bool>("vector");
        QTest::addColumn<int>("smoothing");
        QTest::addColumn<bool>("canvasSmoothing");
        QTest::newRow("raster") << false << 0 << true;
        QTest::newRow("vector") << true << 0 << true;
        QTest::newRow("raster-stabilized") << false << 40 << true;
        QTest::newRow("vector-stabilized") << true << 40 << true;
        QTest::newRow("raster-display-aa-off") << false << 0 << false;
        QTest::newRow("vector-display-aa-off") << true << 0 << false;
    }
    void penBurstKeepsWorkspaceQuiet() {
        QFETCH(bool, vector); QFETCH(int, smoothing); QFETCH(bool, canvasSmoothing);
        QTemporaryDir directory; QVERIFY(directory.isValid()); QString error;
        const auto path = directory.filePath("drawing.toonproj");
        QVERIFY2(saveProject(fixture(vector), path, &error), qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        prepareCanvas(window, smoothing, canvasSmoothing);
        auto *canvas = window.canvas(); auto *document = canvas->document();
        auto *save = window.findChild<QAction *>("saveProjectAction");
        auto *add = window.findChild<QToolButton *>("addLayer");
        auto *tree = window.findChild<QTreeWidget *>("layerTree");
        auto *addTrack = window.findChild<QToolButton *>("timelineAddTrack");
        QVERIFY(save && add && tree && addTrack);
        QVERIFY(save->isEnabled() && add->isEnabled() && tree->isEnabled() && addTrack->isEnabled());
        QSignalSpy changed(document, &Document::changed), forwarded(canvas, &Canvas::documentChanged);
        QSignalSpy saveEnabledChanges(save, &QAction::enabledChanged);
        QSignalSpy rowChanges(tree->model(), &QAbstractItemModel::dataChanged);
        InteractionObserver observer(canvas);
        for (auto *widget : QList<QWidget *>{canvas, add, tree, addTrack}) widget->installEventFilter(&observer);
        QPointingDevice pen("Workspace burst pen", 91, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        const QPointF center(960, 540); constexpr qreal radius = 220;
        const auto point = [&](int sample) {
            const qreal angle = sample * 2 * pi / 240;
            return center + QPointF(radius * std::cos(angle), radius * std::sin(angle));
        };
        for (int i = 0; i < 240; i += 60) {
            const auto local = canvas->viewTransform().map(
                sheetGeometry(canvas->project(), canvas->shot()).output.topLeft() + point(i));
            QVERIFY2(canvas->rect().contains(local.toPoint()), "The complete test circle must remain on the visible canvas.");
        }
        bool remainedActive = true, controlsStable = true;
        const auto initialRenderCount = canvas->artworkRenderCount();
        const auto initialPaintedPixelCount = canvas->artworkPaintedPixelCount();
        QElapsedTimer elapsed; elapsed.start();
        tablet(canvas, pen, QEvent::TabletPress, point(0), false, 1000);
        for (int first = 1; first <= 240; first += 24) {
            for (int i = first; i < first + 24; ++i)
                tablet(canvas, pen, QEvent::TabletMove, point(i), true, 1000 + i);
            // Include real event-loop/paint opportunities, unlike a Canvas-only
            // sendEvent loop that bypasses the workspace's expensive subscribers.
            QCoreApplication::processEvents();
            if (first == 121) QTest::qWait(40);
            remainedActive &= document->strokeActive();
            controlsStable &= save->isEnabled() && add->isEnabled() && tree->isEnabled() && addTrack->isEnabled();
        }
        const auto drawingMs = elapsed.elapsed();
        const auto liveChanged = changed.count(), liveForwarded = forwarded.count(), liveRows = rowChanges.count();
        const auto enabledChanges = observer.enabledChanges;
        const auto saveChanges = saveEnabledChanges.count();
        const auto artworkRenders = canvas->artworkRenderCount() - initialRenderCount;
        const auto artworkPixels = canvas->artworkPaintedPixelCount() - initialPaintedPixelCount;
        tablet(canvas, pen, QEvent::TabletRelease, point(240), false, 1241);
        const auto totalMs = elapsed.elapsed();
        QCoreApplication::processEvents();
        qInfo().nospace() << "Workspace pen burst " << (vector ? "vector" : "raster")
            << ", smoothing=" << smoothing << ", display AA=" << canvasSmoothing
            << ": drawing=" << drawingMs << "ms, with release=" << totalMs
            << "ms, live Document::changed=" << liveChanged << ", Canvas::documentChanged=" << liveForwarded
            << ", layer row refreshes=" << liveRows << ", enabled transitions=" << enabledChanges
            << ", save enabled transitions=" << saveChanges
            << ", canvas paints=" << observer.canvasPaints << ", full canvas paints=" << observer.fullCanvasPaints
            << ", artwork compositions=" << artworkRenders << ", artwork pixels painted=" << artworkPixels;
        // Capture all metrics before asserting so an old build produces a useful baseline.
        QVERIFY(remainedActive); QVERIFY(!document->strokeActive());
        QCOMPARE(document->history()->count(), 1);
        const auto image = renderMovieFrame(canvas->project()); QVERIFY(!image.isNull());
        for (int i = 0; i < 240; i += 10) {
            const auto expected = point(i).toPoint();
            QVERIFY2(image.pixelColor(expected).alpha() > 0, "A delivered circle sample is missing from the painted path.");
        }
        for (int y = 340; y <= 740; y += 10) for (int x = 760; x <= 1160; x += 10)
            if (QLineF(center, QPointF(x, y)).length() < radius - 14)
                QCOMPARE(image.pixelColor(x, y).alpha(), 0);
        auto *undo = window.findChild<QAction *>("undoAction"); QVERIFY(undo && undo->isEnabled());
        undo->trigger(); QVERIFY(document->history()->isClean());
        document->history()->redo(); QCOMPARE(renderMovieFrame(canvas->project()), image);
        document->history()->setClean(); QVERIFY(window.close());
        QVERIFY(controlsStable); QCOMPARE(enabledChanges, 0); QCOMPARE(saveChanges, 0);
        QCOMPARE(liveChanged, 0); QCOMPARE(liveForwarded, 0); QCOMPARE(liveRows, 0);
        QVERIFY(changed.count() > 0); QVERIFY(forwarded.count() > 0);
    }
    void tileDiscoveryWaitsForDisplayInsteadOfEachPenSample_data() {
        QTest::addColumn<bool>("vector");
        QTest::newRow("stabilized-raster") << false;
        QTest::newRow("stabilized-vector") << true;
    }
    void tileDiscoveryWaitsForDisplayInsteadOfEachPenSample() {
        QFETCH(bool, vector);
        QTemporaryDir directory; QString error; const auto path = directory.filePath("deferred-tiles.toonproj");
        QVERIFY2(saveProject(fixture(vector), path, &error), qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        prepareCanvas(window, 65);
        auto *canvas = window.canvas(); auto *document = canvas->document();
        QSignalSpy changed(document, &Document::changed), forwarded(canvas, &Canvas::documentChanged);
        QSignalSpy strokeUpdates(document, &Document::strokeUpdated);
        auto *tree = window.findChild<QTreeWidget *>("layerTree"); QVERIFY(tree);
        QSignalSpy rows(tree->model(), &QAbstractItemModel::dataChanged);
        QPointingDevice pen("Deferred tile pen", 94, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        const auto point = [](int sample) {
            const qreal angle = sample * 2 * pi / 96;
            return QPointF(960, 540) + QPointF(220 * std::cos(angle), 220 * std::sin(angle));
        };
        const auto initialScans = canvas->strokeTileScanCount();
        tablet(canvas, pen, QEvent::TabletPress, point(0), false, 4000);
        for (int batch = 0; batch < 2; ++batch) {
            const auto before = canvas->strokeTileScanCount();
            for (int sample = batch * 48 + 1; sample <= (batch + 1) * 48; ++sample)
                tablet(canvas, pen, QEvent::TabletMove, point(sample), false, 4000 + sample);
            // Stabilization emits many drawing-space samples. They must not
            // repeatedly walk every painted tile before anything is displayed.
            QCOMPARE(canvas->strokeTileScanCount(), before);
            QVERIFY(document->strokeActive());
            QVERIFY(!canvas->grab().isNull());
            QCOMPARE(canvas->strokeTileScanCount(), before + 1);
            QCoreApplication::processEvents();
            QCOMPARE(canvas->strokeTileScanCount(), before + 1);
            QCOMPARE(changed.count(), 0); QCOMPARE(forwarded.count(), 0); QCOMPARE(rows.count(), 0);
        }
        QVERIFY(strokeUpdates.count() > 96);
        QCOMPARE(canvas->strokeTileScanCount(), initialScans + 2);
        tablet(canvas, pen, QEvent::TabletRelease, point(96), false, 4097);
        QCoreApplication::processEvents();
        QVERIFY(!document->strokeActive()); QCOMPARE(document->history()->count(), 1);
        const auto image = renderMovieFrame(canvas->project());
        QVERIFY(image.pixelColor(point(0).toPoint()).alpha() > 0);
        QCOMPARE(image.pixelColor(960, 540).alpha(), 0);
        document->history()->undo(); QVERIFY(document->history()->isClean());
        document->history()->redo(); QCOMPARE(renderMovieFrame(canvas->project()), image);
        document->history()->setClean(); QVERIFY(window.close());
    }
    void pendingLayerThumbnailWaitsUntilPenRelease() {
        QTemporaryDir directory; QString error; const auto path = directory.filePath("thumbnail.toonproj");
        QVERIFY2(saveProject(fixture(), path, &error), qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        prepareCanvas(window);
        auto *canvas = window.canvas(); auto *panel = window.findChild<LayerPanel *>(); QVERIFY(panel);
        auto *tree = panel->findChild<QTreeWidget *>("layerTree"); QVERIFY(tree);
        const auto before = thumbnail(tree); QVERIFY(!before.isNull());
        const auto timers = panel->findChildren<QTimer *>(QString(), Qt::FindDirectChildrenOnly);
        QCOMPARE(timers.size(), 1);
        // Simulate a thumbnail already queued by an earlier committed edit.
        // A quiet stroke signal alone does not prevent this callback running.
        timers.first()->start(0);
        QSignalSpy rowChanges(tree->model(), &QAbstractItemModel::dataChanged);
        QPointingDevice pen("Thumbnail pen", 92, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        canvas->setBrushSize(100);
        tablet(canvas, pen, QEvent::TabletPress, {750, 540}, false, 2000);
        tablet(canvas, pen, QEvent::TabletMove, {1150, 540}, false, 2001);
        QCoreApplication::processEvents();
        QVERIFY(canvas->document()->strokeActive()); QCOMPARE(thumbnail(tree), before);
        QCOMPARE(rowChanges.count(), 0); QVERIFY(tree->isEnabled());
        tablet(canvas, pen, QEvent::TabletRelease, {1150, 540}, false, 2002);
        QTRY_VERIFY(thumbnail(tree) != before);
        QVERIFY(rowChanges.count() > 0); QVERIFY(tree->isEnabled());
        canvas->document()->history()->setClean(); QVERIFY(window.close());
    }
    void cancelledAndInvisibleStrokesRefreshWithoutHistory() {
        QTemporaryDir directory; QString error; const auto path = directory.filePath("cancel.toonproj");
        QVERIFY2(saveProject(fixture(), path, &error), qPrintable(error));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(path));
        prepareCanvas(window);
        auto *canvas = window.canvas(); auto *document = canvas->document();
        const auto original = renderMovieFrame(canvas->project());
        const auto originalProject = canvas->project().toJson();
        QSignalSpy changed(document, &Document::changed), forwarded(canvas, &Canvas::documentChanged);
        QPointingDevice pen("Cancel pen", 93, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        tablet(canvas, pen, QEvent::TabletPress, {800, 540}, false, 3000);
        tablet(canvas, pen, QEvent::TabletMove, {1100, 560}, false, 3001);
        QCoreApplication::processEvents();
        QVERIFY(document->strokeActive()); QCOMPARE(changed.count(), 0); QCOMPARE(forwarded.count(), 0);
        QTest::keyClick(canvas, Qt::Key_Escape);
        QVERIFY(!document->strokeActive()); QVERIFY(changed.count() > 0); QVERIFY(forwarded.count() > 0);
        tablet(canvas, pen, QEvent::TabletRelease, {1100, 560}, false, 3002);
        QCOMPARE(document->history()->count(), 0); QCOMPARE(canvas->project().toJson(), originalProject);
        QCOMPARE(renderMovieFrame(canvas->project()), original);

        auto brush = canvas->brush; brush.strokeOpacity = 0; canvas->setBrushSettings(brush);
        QCoreApplication::processEvents(); changed.clear(); forwarded.clear();
        tablet(canvas, pen, QEvent::TabletPress, {800, 540}, false, 3100);
        tablet(canvas, pen, QEvent::TabletMove, {1100, 560}, true, 3101);
        QCoreApplication::processEvents();
        QVERIFY(document->strokeActive()); QCOMPARE(changed.count(), 0); QCOMPARE(forwarded.count(), 0);
        tablet(canvas, pen, QEvent::TabletRelease, {1100, 560}, false, 3102);
        QVERIFY(!document->strokeActive()); QCOMPARE(document->history()->count(), 0);
        QVERIFY(changed.count() > 0); QVERIFY(forwarded.count() > 0);
        QCOMPARE(canvas->project().toJson(), originalProject); QCOMPARE(renderMovieFrame(canvas->project()), original);
        QVERIFY(window.findChild<QAction *>("saveProjectAction")->isEnabled());
        QVERIFY(window.findChild<QTreeWidget *>("layerTree")->isEnabled());
        QVERIFY(window.close());
    }
};
QTEST_MAIN(StrokeWorkspaceTests)
#include "stroke_workspace_tests.moc"
