#include "canvas.h"
#include "main_window.h"
#include "mask_tool.h"
#include "object_tools.h"
#include "raster_tools.h"
#include "ui_theme.h"

#include <QAction>
#include <QDir>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QStyle>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>
#include <cmath>

using namespace toon;

namespace {
Project drawing(bool colored = false) {
    auto p = Project::create(); p.width = p.height = 256; p.transparent = true;
    if (colored) {
        QImage tile(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
        tile.fill(Qt::transparent);
        QPainter paint(&tile);
        paint.fillRect(QRect(30, 45, 30, 30), Qt::red);
        paint.fillRect(QRect(180, 140, 25, 35), Qt::blue);
        paint.end();
        p.shots.first().layers.first().tiles.insert({0, 0}, tile);
    }
    return p;
}
void prepare(Canvas &canvas, const Project &project) {
    canvas.resize(800, 640); canvas.setProject(project); canvas.setZoom(1);
    canvas.centerOn(sheetGeometry(canvas.project(), canvas.shot()).output.center());
    canvas.show(); canvas.setFocus(); QCoreApplication::processEvents();
}
QPoint viewport(const Canvas &canvas, QPointF artwork) {
    return canvas.artworkViewTransform().map(artwork).toPoint();
}
void hover(Canvas &canvas, QPointF point) {
    QMouseEvent event(QEvent::MouseMove, point, canvas.mapToGlobal(point),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}
}

class CanvasMirrorTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { applyTheme(); }

    void mirrorOnlyChangesViewAndPreservesCenterZoomAndExport() {
        Canvas canvas; prepare(canvas, drawing(true)); canvas.setTool(Canvas::Tool::Hand);
        const auto before = canvas.project().toJson();
        const auto exported = renderMovieFrame(canvas.project());
        const auto originalTransform = canvas.viewTransform();
        const auto originalCenter = canvas.center();
        const auto originalZoom = canvas.zoom();
        const auto red = viewport(canvas, {45, 60});
        const auto original = canvas.grab().toImage();
        QCOMPARE(original.pixelColor(red * canvas.devicePixelRatioF()), QColor(Qt::red));
        QSignalSpy changed(&canvas, &Canvas::canvasMirroredChanged);
        canvas.setCanvasMirrored(true);
        QVERIFY(canvas.canvasMirrored());
        const auto mirrored = canvas.grab().toImage();
        const auto now = viewport(canvas, {45, 60});
        QVERIFY(now.x() > red.x());
        QCOMPARE(mirrored.pixelColor(now * canvas.devicePixelRatioF()), QColor(Qt::red));
        QCOMPARE(canvas.center(), originalCenter); QCOMPARE(canvas.zoom(), originalZoom);
        QCOMPARE(canvas.project().toJson(), before);
        QCOMPARE(renderMovieFrame(canvas.project()), exported);
        QCOMPARE(canvas.document()->history()->count(), 0);
        QVERIFY(canvas.document()->history()->isClean());
        canvas.setCanvasMirrored(true); QCOMPARE(changed.count(), 1);
        canvas.setCanvasMirrored(false); QCOMPARE(changed.count(), 2);
        QCOMPARE(canvas.viewTransform(), originalTransform);
    }

    void mirroredInputSelectionAndSampling_data() {
        QTest::addColumn<double>("rotation"); QTest::addColumn<double>("cameraRotation");
        QTest::newRow("horizontal") << 0. << 0.;
        QTest::newRow("rotated") << 31. << 0.;
        QTest::newRow("rotated-camera") << -22. << 28.;
    }
    void mirroredInputSelectionAndSampling() {
        QFETCH(double, rotation); QFETCH(double, cameraRotation);
        auto p = drawing(true); p.shots.first().camera2D.rotation = cameraRotation;
        p.shots.first().camera2D.zoom = 1.2;
        Canvas canvas; prepare(canvas, p); canvas.setRotation(rotation); canvas.setCanvasMirrored(true);
        const QPointF target(110, 110);
        QVERIFY(QLineF(canvas.artworkPoint(canvas.artworkViewTransform().map(target)), target).length() < 1e-8);
        canvas.setTool(Canvas::Tool::Brush);
        BrushSettings brush; brush.size = 10; brush.color = Qt::green; brush.pressureSize = false;
        brush.smoothingEnabled = false; canvas.setBrushSettings(brush); canvas.setStabilization(0);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, viewport(canvas, target));
        // Inspect unprojected source pixels independently from the view camera.
        QCOMPARE(RasterTools::sample(canvas.document(), target, RasterSampleSource::ActiveLayer), QColor(Qt::green));
        QCOMPARE(canvas.document()->history()->index(), 1);
        canvas.document()->history()->undo();
        QCOMPARE(RasterTools::sample(canvas.document(), target, RasterSampleSource::ActiveLayer).alpha(), 0);
        canvas.setTool(Canvas::Tool::RectangleSelect);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, viewport(canvas, {80, 80}));
        QTest::mouseMove(&canvas, viewport(canvas, {140, 140}));
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, viewport(canvas, {140, 140}));
        QVERIFY(canvas.document()->rasterSelection().path.contains(target));
        QVERIFY(!canvas.document()->rasterSelection().path.contains({40, 60}));
        canvas.setTool(Canvas::Tool::Eyedropper);
        QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, viewport(canvas, {45, 60}));
        QCOMPARE(canvas.brush.color, QColor(Qt::red));
    }

    void mirroredTabletKeepsPressureTiltTwistAndEditableSource() {
        Canvas canvas; prepare(canvas, drawing());
        QVERIFY(ObjectTools::addLayer(canvas.document(), LayerKind::Vector));
        canvas.setCanvasMirrored(true); canvas.setTool(Canvas::Tool::Brush);
        BrushSettings brush; brush.size = 12; brush.color = Qt::black; brush.smoothingEnabled = false;
        canvas.setBrushSettings(brush); canvas.setStabilization(0);
        QPointingDevice pen("Mirror test pen", 191, QInputDevice::DeviceType::Stylus,
            QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position
                | QInputDevice::Capability::Pressure | QInputDevice::Capability::XTilt
                | QInputDevice::Capability::YTilt | QInputDevice::Capability::Rotation, 1, 1);
        const auto send = [&](QEvent::Type type, QPointF point) {
            const auto position = canvas.artworkViewTransform().map(point);
            const bool release = type == QEvent::TabletRelease;
            QTabletEvent event(type, &pen, position, canvas.mapToGlobal(position), release ? 0 : .7,
                20, 10, 0, 30, 0, Qt::NoModifier,
                type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                release ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&canvas, &event);
        };
        send(QEvent::TabletPress, {80, 100});
        canvas.setCanvasMirrored(false); // A contact cannot switch coordinate systems.
        QVERIFY(canvas.canvasMirrored());
        send(QEvent::TabletMove, {120, 100}); send(QEvent::TabletRelease, {130, 100});
        const auto &objects = canvas.shot().layers[canvas.document()->activeLayer()].objects;
        QCOMPARE(objects.size(), 1); QVERIFY(objects.first().brushStroke);
        const auto &samples = objects.first().brushStroke->samples;
        QVERIFY(!samples.isEmpty());
        const auto &input = samples.first().input;
        QVERIFY(qAbs(input.pressure - .7) < .001);
        QVERIFY(qAbs(input.tiltX + 20) < .001); QVERIFY(qAbs(input.tiltY - 10) < .001);
        QVERIFY(qAbs(input.rotation - 150) < .001);
        canvas.setCanvasMirrored(false); QVERIFY(!canvas.canvasMirrored());
    }

    void asymmetricBrushCursorReflectsWithArtwork() {
        Canvas canvas; prepare(canvas, drawing());
        BrushSettings brush; brush.size = 50; brush.angle = 33; brush.roundness = .3;
        brush.pressureSize = false; canvas.setBrushSettings(brush);
        const QPointF point(400, 320); hover(canvas, point);
        const auto original = canvas.brushCursorPath(); QVERIFY(!original.isEmpty());
        canvas.setCanvasMirrored(true); hover(canvas, point);
        QTransform reflection(-1, 0, 0, 1, point.x() * 2, 0);
        const auto expected = reflection.map(original), actual = canvas.brushCursorPath();
        QCOMPARE(actual.elementCount(), expected.elementCount());
        for (int i = 0; i < actual.elementCount(); ++i)
            QVERIFY(QLineF(actual.elementAt(i), expected.elementAt(i)).length() < 1e-7);
    }

    void panelOwnedMaskContactKeepsItsCoordinateSystemUntilRelease() {
        Canvas canvas; prepare(canvas, drawing(true));
        QVERIFY(canvas.document()->addLayerMask());
        auto *mask = canvas.maskTool(); mask->setEnabled(true); QVERIFY(mask->enabled());
        const auto point = viewport(canvas, {45, 60});
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(mask->dragging());
        canvas.setCanvasMirrored(true); QVERIFY(!canvas.canvasMirrored());
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, point);
        QVERIFY(!mask->dragging());
        canvas.setCanvasMirrored(true); QVERIFY(canvas.canvasMirrored());
    }

    void toolbarMenuShortcutAndDocumentLifetime() {
        QTemporaryDir directory; const auto file = directory.filePath("mirror.toonproj");
        QVERIFY(saveProject(drawing(true), file, nullptr));
        MainWindow window(directory.filePath("workspace.ini")); QVERIFY(window.openProject(file));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.raise(); window.activateWindow();
        // Command-line Cocoa tests can remain a background macOS process.
        // Establish Qt's active window for synthetic shortcut delivery, as
        // the existing workspace tests do; real OS input is a separate check.
        QApplication::setActiveWindow(&window);
        QVERIFY(window.isActiveWindow());
        auto *action = window.findChild<QAction *>("canvasMirrorAction");
        auto *toolbar = window.findChild<QToolBar *>("projectToolbar");
        QVERIFY(action && toolbar); QVERIFY(action->isCheckable());
        QVERIFY(toolbar->actions().contains(action));
        auto *button = qobject_cast<QToolButton *>(toolbar->widgetForAction(action));
        QVERIFY(button); QVERIFY(!button->accessibleName().isEmpty());
        QVERIFY(!button->toolTip().isEmpty());
        QCOMPARE(button->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, button), 600);
        QTest::mouseClick(button, Qt::LeftButton); QVERIFY(window.canvas()->canvasMirrored()); QVERIFY(action->isChecked());
        window.canvas()->setFocus(); QTRY_VERIFY(window.canvas()->hasFocus());
        QTest::keyClick(window.canvas(), Qt::Key_M, Qt::ShiftModifier);
        QVERIFY(!window.canvas()->canvasMirrored()); QVERIFY(!action->isChecked());
        action->trigger(); QVERIFY(window.canvas()->canvasMirrored());
        Canvas independent; prepare(independent, drawing()); QVERIFY(!independent.canvasMirrored());
        QVERIFY(window.canvas()->canvasMirrored());
        QVERIFY(window.openProject(file)); QVERIFY(!window.canvas()->canvasMirrored()); QVERIFY(!action->isChecked());
        const auto preview = qEnvironmentVariable("TOON_UI_PREVIEW");
        if (!preview.isEmpty()) {
            action->trigger();
            QVERIFY(QDir().mkpath(preview));
            QVERIFY(window.grab().save(preview + "/canvas-mirror-workspace.png"));
        }
        QVERIFY(window.close());
    }
};

QTEST_MAIN(CanvasMirrorTests)
#include "test_canvas_mirror.moc"
