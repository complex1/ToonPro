#include "canvas.h"
#include "editable_object.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QtTest>

using namespace toon;
namespace {
Project fixture(bool vector = false) {
    auto project = Project::create(); project.width = project.height = 256; project.transparent = true;
    if (vector) project.shots[0].layers[0].kind = LayerKind::Vector;
    return project;
}
BrushSettings ink() {
    BrushSettings brush; brush.size = 8; brush.color = Qt::black;
    brush.lockRandomSeed = true; brush.randomSeed = 42;
    return brush;
}
void prepare(Canvas &canvas, const Project &project) {
    canvas.resize(640, 480); canvas.setProject(project); canvas.setZoom(1);
    canvas.setBrushSettings(ink()); canvas.setStabilization(100);
}
void pointer(Canvas &canvas, bool pen, QEvent::Type type, QPointF artwork,
             Qt::KeyboardModifiers modifiers, qreal pressure = 1) {
    static QPointingDevice device("Constrained pen", 401, QInputDevice::DeviceType::Stylus,
        QPointingDevice::PointerType::Pen, QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
    const auto local = canvas.viewTransform().map(sheetGeometry(canvas.project(), canvas.shot()).output.topLeft() + artwork);
    const bool press = type == QEvent::MouseButtonPress, release = type == QEvent::MouseButtonRelease;
    const auto button = press || release ? Qt::LeftButton : Qt::NoButton;
    const auto buttons = release ? Qt::NoButton : Qt::LeftButton;
    if (pen) {
        QTabletEvent event(press ? QEvent::TabletPress : release ? QEvent::TabletRelease : QEvent::TabletMove,
            &device, local, canvas.mapToGlobal(local), release ? 0 : pressure, 0, 0, 0, 0, 0, modifiers, button, buttons);
        QApplication::sendEvent(&canvas, &event);
    } else {
        QMouseEvent event(type, local, canvas.mapToGlobal(local), button, buttons, modifiers);
        QApplication::sendEvent(&canvas, &event);
    }
}
}
class BrushConstraintTests final : public QObject {
    Q_OBJECT
private slots:
    void constrainedGestures_data() {
        QTest::addColumn<bool>("pen"); QTest::addColumn<bool>("vector"); QTest::addColumn<int>("mode");
        for (bool pen : {false, true}) for (bool vector : {false, true}) for (int mode = 0; mode < 5; ++mode)
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(pen ? "pen" : "mouse", vector ? "vector" : "raster").arg(mode)))
                << pen << vector << mode;
    }
    void constrainedGestures() {
        QFETCH(bool, pen); QFETCH(bool, vector); QFETCH(int, mode);
        const auto project = fixture(vector); Canvas canvas; prepare(canvas, project);
        const auto modifiers = mode == 0 || mode == 2 ? Qt::ControlModifier : mode == 1 ? Qt::MetaModifier
            : mode == 3 ? Qt::ShiftModifier : Qt::ControlModifier | Qt::ShiftModifier;
        const QPointF start(40, 40), first = mode == 2 ? QPointF(48, 100) : QPointF(100, 55);
        const QPointF last(140, 180);
        const qreal initialPressure = pen ? .35 : 1, middlePressure = pen ? .6 : 1, finalPressure = pen ? .9 : 1;
        QSignalSpy broadChanges(canvas.document(), &Document::changed);
        pointer(canvas, pen, QEvent::MouseButtonPress, start, modifiers, initialPressure);
        // Contact jitter must not choose the wrong axis.
        if (mode != 3) pointer(canvas, pen, QEvent::MouseMove, start + QPointF(.5, 1), modifiers, initialPressure);
        pointer(canvas, pen, QEvent::MouseMove, first, modifiers, middlePressure);
        // Releasing a modifier during the stroke keeps its constraint latched.
        pointer(canvas, pen, QEvent::MouseMove, last, Qt::NoModifier, finalPressure);
        QTest::qWait(25); QVERIFY(canvas.document()->strokeActive());
        QCOMPARE(broadChanges.count(), 0); QCOMPARE(canvas.document()->history()->count(), 0);
        pointer(canvas, pen, QEvent::MouseButtonRelease, last, Qt::NoModifier);
        QVERIFY(!canvas.document()->strokeActive()); QCOMPARE(canvas.document()->history()->count(), 1);

        Document expected; expected.load(project); expected.beginStroke(start, initialPressure, ink(), false);
        if (mode == 3) expected.continueStroke(last, finalPressure);
        else {
            expected.continueStroke(mode == 2 ? QPointF(start.x(), first.y()) : QPointF(first.x(), start.y()), middlePressure);
            expected.continueStroke(mode == 2 ? QPointF(start.x(), last.y()) : QPointF(last.x(), start.y()), finalPressure);
        }
        expected.endStroke();
        const auto result = renderMovieFrame(canvas.project()); QCOMPARE(result, renderMovieFrame(expected.project()));
        if (vector) {
            QCOMPARE(canvas.shot().layers[0].objects.size(), 1);
            const auto &actual = *canvas.shot().layers[0].objects[0].brushStroke;
            const auto &reference = *expected.activeShot().layers[0].objects[0].brushStroke;
            QCOMPARE(actual.samples.size(), reference.samples.size());
            for (int i = 0; i < actual.samples.size(); ++i) {
                QVERIFY(QLineF(actual.samples[i].point, reference.samples[i].point).length() < 1e-8);
                QVERIFY(qAbs(actual.samples[i].input.pressure - reference.samples[i].input.pressure) < 1e-8);
            }
        }
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), renderMovieFrame(project));
        canvas.document()->history()->redo(); QCOMPARE(renderMovieFrame(canvas.project()), result);
    }
    void shiftPreviewReplacesOldDirectionAndFlushesOnRelease() {
        Canvas canvas; prepare(canvas, fixture());
        const auto screenPoint = canvas.viewTransform().map(sheetGeometry(canvas.project(), canvas.shot()).output.topLeft() + QPointF(150, 40));
        const auto cleanPixel = canvas.grab().toImage().pixelColor((screenPoint * canvas.devicePixelRatioF()).toPoint());
        pointer(canvas, true, QEvent::MouseButtonPress, {40, 40}, Qt::ShiftModifier);
        pointer(canvas, true, QEvent::MouseMove, {200, 40}, Qt::ShiftModifier);
        QTRY_VERIFY_WITH_TIMEOUT(renderMovieFrame(canvas.project()).pixelColor(150, 40).alpha() > 0, 500);
        QVERIFY(canvas.grab().toImage().pixelColor((screenPoint * canvas.devicePixelRatioF()).toPoint()) != cleanPixel);
        pointer(canvas, true, QEvent::MouseMove, {40, 200}, Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(renderMovieFrame(canvas.project()).pixelColor(150, 40).alpha(), 0, 500);
        QCOMPARE(canvas.grab().toImage().pixelColor((screenPoint * canvas.devicePixelRatioF()).toPoint()), cleanPixel);
        auto image = renderMovieFrame(canvas.project()); QCOMPARE(image.pixelColor(150, 40).alpha(), 0);
        QVERIFY(image.pixelColor(40, 150).alpha() > 0);
        // Release includes a fresh endpoint, without waiting for the preview timer.
        pointer(canvas, true, QEvent::MouseButtonRelease, {200, 200}, Qt::ShiftModifier);
        image = renderMovieFrame(canvas.project()); QCOMPARE(image.pixelColor(40, 150).alpha(), 0);
        QVERIFY(image.pixelColor(150, 150).alpha() > 0);
        QTest::qWait(25); QCOMPARE(renderMovieFrame(canvas.project()), image);
    }
    void escapeCancelsPendingPreviewAndNextStrokeIsFreehand() {
        Canvas canvas; prepare(canvas, fixture()); canvas.setStabilization(0);
        const auto before = renderMovieFrame(canvas.project());
        pointer(canvas, false, QEvent::MouseButtonPress, {40, 40}, Qt::ShiftModifier);
        pointer(canvas, false, QEvent::MouseMove, {200, 200}, Qt::ShiftModifier);
        QTest::keyClick(&canvas, Qt::Key_Escape); QTest::qWait(25);
        QCOMPARE(renderMovieFrame(canvas.project()), before); QCOMPARE(canvas.document()->history()->count(), 0);
        pointer(canvas, false, QEvent::MouseButtonPress, {40, 40}, Qt::NoModifier);
        pointer(canvas, false, QEvent::MouseMove, {180, 40}, Qt::NoModifier);
        pointer(canvas, false, QEvent::MouseButtonRelease, {180, 180}, Qt::NoModifier);
        QVERIFY(renderMovieFrame(canvas.project()).pixelColor(150, 40).alpha() > 0);
        QVERIFY(renderMovieFrame(canvas.project()).pixelColor(180, 150).alpha() > 0);
    }
    void eraserRestoresPreviousPreview() {
        auto project = fixture(); Document background; background.load(project);
        auto brush = ink(); brush.size = 200; brush.pressureSize = false;
        background.beginStroke({128, 128}, 1, brush, false); background.endStroke();
        project = background.project(); Canvas canvas; prepare(canvas, project); canvas.setTool(Canvas::Tool::Eraser);
        const auto before = renderMovieFrame(project);
        pointer(canvas, false, QEvent::MouseButtonPress, {128, 128}, Qt::ShiftModifier);
        pointer(canvas, false, QEvent::MouseMove, {200, 128}, Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(renderMovieFrame(canvas.project()).pixelColor(180, 128).alpha(), 0, 500);
        pointer(canvas, false, QEvent::MouseButtonRelease, {128, 200}, Qt::ShiftModifier);
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(180, 128), before.pixelColor(180, 128));
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(128, 180).alpha(), 0);
        canvas.document()->history()->undo(); QCOMPARE(renderMovieFrame(canvas.project()), before);
    }
    void sampledLineRetainsSeedPoseSelectionAndSymmetry_data() {
        QTest::addColumn<bool>("vector");
        QTest::newRow("raster") << false; QTest::newRow("vector") << true;
    }
    void sampledLineRetainsSeedPoseSelectionAndSymmetry() {
        QFETCH(bool, vector);
        const auto project = fixture(vector);
        Document actual, expected; actual.load(project); expected.load(project);
        auto brush = ink(); brush.size = 20; brush.spacing = .6; brush.symmetry.mode = SymmetryMode::Vertical;
        brush.lockRandomSeed = false;
        brush.sizeDynamics.jitter = .3; brush.angleDynamics.jitter = .4;
        QImage a(15, 11, QImage::Format_ARGB32_Premultiplied); a.fill(Qt::transparent);
        QImage b = a;
        { QPainter p(&a); p.fillRect(1, 1, 5, 9, Qt::red); }
        { QPainter p(&b); p.fillRect(5, 1, 9, 3, Qt::blue); }
        QVERIFY(setBrushTipImages(brush, {a, b}));
        BrushInput initial; initial.pressure = .25; initial.tiltX = 12; initial.rotation = 20;
        BrushInput last; last.pressure = .85; last.tiltY = -30; last.rotation = 90;
        QPainterPath selection; selection.addRect(QRectF(20, 20, 160, 210));
        for (auto *document : {&actual, &expected}) {
            document->setBrushRandomSeed(711);
            document->setRasterSelection(selection);
            QVERIFY(document->beginStroke({40, 40}, initial, brush, false));
        }
        actual.previewStrokeLine({200, 40}, last);
        actual.previewStrokeLine({40, 200}, last);
        actual.previewStrokeLine({200, 180}, last);
        expected.continueStroke({200, 180}, last);
        actual.endStroke(); expected.endStroke();
        QCOMPARE(renderMovieFrame(actual.project()), renderMovieFrame(expected.project()));
        if (vector) QCOMPARE(actual.activeShot().layers[0].objects[0].brushStroke->samples,
                             expected.activeShot().layers[0].objects[0].brushStroke->samples);
    }
    void rotatedCanvasLocksInArtworkAxes() {
        Canvas canvas; prepare(canvas, fixture(true)); canvas.setRotation(37); canvas.setZoom(.7);
        pointer(canvas, true, QEvent::MouseButtonPress, {40, 40}, Qt::MetaModifier);
        pointer(canvas, true, QEvent::MouseMove, {120, 50}, Qt::MetaModifier);
        pointer(canvas, true, QEvent::MouseButtonRelease, {180, 150}, Qt::MetaModifier);
        QCOMPARE(canvas.shot().layers[0].objects.size(), 1);
        for (const auto &sample : canvas.shot().layers[0].objects[0].brushStroke->samples)
            QVERIFY(qAbs(sample.point.y() - 40) < 1e-8);
    }
};
QTEST_MAIN(BrushConstraintTests)
#include "brush_constraint_tests.moc"
