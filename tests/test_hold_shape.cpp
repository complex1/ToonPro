#include "canvas.h"
#include "editable_object.h"
#include "shape_choice_bar.h"
#include "ui_theme.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QtTest>
#include <cmath>

using namespace toon;

namespace {
Project fixture(bool vector = false) {
    auto project = Project::create();
    project.width = project.height = 256;
    project.transparent = true;
    if (vector) project.shots[0].layers[0].kind = LayerKind::Vector;
    return project;
}

BrushSettings ink() {
    BrushSettings brush;
    brush.size = 5;
    brush.color = Qt::black;
    brush.lockRandomSeed = true;
    brush.randomSeed = 735;
    brush.smoothingEnabled = false;
    return brush;
}

void prepare(Canvas &canvas, const Project &project = fixture()) {
    canvas.resize(640, 480);
    canvas.setProject(project);
    canvas.setZoom(1);
    canvas.setBrushSettings(ink());
    canvas.setStabilization(0);
    canvas.show();
    QApplication::processEvents();
}

QPointingDevice &penDevice() {
    static QPointingDevice pen("Hold shape pen", 477,
        QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
        QInputDevice::Capability::Position | QInputDevice::Capability::Pressure
            | QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt,
        1, 1);
    return pen;
}

void pointer(Canvas &canvas, bool pen, QEvent::Type type, QPointF artwork,
             qreal pressure = 1, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const auto local = canvas.viewTransform().map(
        sheetGeometry(canvas.project(), canvas.shot()).output.topLeft() + artwork);
    const bool press = type == QEvent::MouseButtonPress;
    const bool release = type == QEvent::MouseButtonRelease;
    const auto button = press || release ? Qt::LeftButton : Qt::NoButton;
    const auto buttons = release ? Qt::NoButton : Qt::LeftButton;
    if (pen) {
        QTabletEvent event(press ? QEvent::TabletPress : release ? QEvent::TabletRelease : QEvent::TabletMove,
            &penDevice(), local, canvas.mapToGlobal(local), release ? 0 : pressure,
            12, -8, 0, 27, 0, modifiers, button, buttons);
        QApplication::sendEvent(&canvas, &event);
    } else {
        QMouseEvent event(type, local, canvas.mapToGlobal(local), button, buttons, modifiers);
        QApplication::sendEvent(&canvas, &event);
    }
}

QVector<QPointF> noisyLine() {
    return {{40, 65}, {65, 69}, {90, 62}, {115, 68}, {140, 63}, {165, 67}, {195, 65}};
}

QVector<QPointF> noisyCircle() {
    QVector<QPointF> points;
    for (int i = 0; i <= 48; ++i) {
        const qreal angle = i * 2 * 3.14159265358979323846 / 48;
        const qreal radius = 62 + 2.5 * std::sin(angle * 5);
        points.append(QPointF(128, 128) + QPointF(radius * std::cos(angle), radius * std::sin(angle)));
    }
    return points;
}

void drawHeld(Canvas &canvas, const QVector<QPointF> &points, bool pen = false,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    pointer(canvas, pen, QEvent::MouseButtonPress, points.first(), pen ? .3 : 1, modifiers);
    for (int i = 1; i < points.size(); ++i)
        pointer(canvas, pen, QEvent::MouseMove, points[i], pen ? .3 + .6 * i / (points.size() - 1) : 1, modifiers);
}

ShapeChoiceBar *bar(Canvas &canvas) {
    return canvas.findChild<ShapeChoiceBar *>("shapeChoiceBar");
}

QToolButton *choice(Canvas &canvas, const char *name) {
    return bar(canvas) ? bar(canvas)->findChild<QToolButton *>(name) : nullptr;
}
}

class HoldShapeTests final : public QObject {
    Q_OBJECT
private slots:
    void heldStrokeFitsThenCommitsOnce_data() {
        QTest::addColumn<bool>("pen");
        QTest::addColumn<bool>("vector");
        QTest::addColumn<bool>("circle");
        for (bool pen : {false, true}) for (bool vector : {false, true}) for (bool circle : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2-%3")
                .arg(pen ? "pen" : "mouse", vector ? "vector" : "raster", circle ? "circle" : "line")))
                << pen << vector << circle;
    }

    void heldStrokeFitsThenCommitsOnce() {
        QFETCH(bool, pen); QFETCH(bool, vector); QFETCH(bool, circle);
        const auto project = fixture(vector);
        Canvas canvas; prepare(canvas, project);
        QVERIFY(canvas.holdShapeEnabled());
        const auto points = circle ? noisyCircle() : noisyLine();
        QSignalSpy broadChanges(canvas.document(), &Document::changed);
        drawHeld(canvas, points, pen);
        const auto freehand = renderMovieFrame(canvas.project());
        QTRY_VERIFY_WITH_TIMEOUT(renderMovieFrame(canvas.project()) != freehand, 1800);
        QVERIFY(canvas.document()->strokeActive());
        QVERIFY(!canvas.holdShapePending());
        QCOMPARE(canvas.document()->history()->count(), 0);
        QCOMPARE(broadChanges.count(), 0);
        pointer(canvas, pen, QEvent::MouseButtonRelease, points.last());
        QVERIFY(canvas.holdShapePending());
        QVERIFY(canvas.document()->strokeActive());
        QVERIFY(bar(canvas)); QVERIFY(bar(canvas)->isVisible());
        QVERIFY(canvas.rect().contains(bar(canvas)->geometry()));
        QVERIFY(choice(canvas, "shapeChoice0"));
        choice(canvas, "shapeChoice0")->click();
        QVERIFY(!canvas.holdShapePending());
        QVERIFY(!canvas.document()->strokeActive());
        QVERIFY(bar(canvas)->isHidden());
        QCOMPARE(canvas.document()->history()->count(), 1);
        const auto fitted = renderMovieFrame(canvas.project());
        QVERIFY(fitted != freehand);
        if (vector) {
            QCOMPARE(canvas.shot().layers[0].objects.size(), 1);
            QVERIFY(canvas.shot().layers[0].objects[0].brushStroke.has_value());
        }
        canvas.document()->history()->undo();
        QCOMPARE(renderMovieFrame(canvas.project()), renderMovieFrame(project));
        canvas.document()->history()->redo();
        QCOMPARE(renderMovieFrame(canvas.project()), fitted);
    }

    void originalRestoresExactPixelsAndEditableSamples_data() {
        QTest::addColumn<bool>("vector");
        QTest::addColumn<int>("stabilization");
        for (bool vector : {false, true}) for (int stabilization : {0, 60})
            QTest::newRow(qPrintable(QString("%1-%2").arg(vector ? "vector" : "raster").arg(stabilization)))
                << vector << stabilization;
    }

    void originalRestoresExactPixelsAndEditableSamples() {
        QFETCH(bool, vector); QFETCH(int, stabilization);
        Canvas canvas, expected;
        prepare(canvas, fixture(vector)); prepare(expected, fixture(vector));
        auto brush = ink(); brush.sizeDynamics.jitter = .25; brush.opacityDynamics.jitter = .2;
        brush.smoothingEnabled = stabilization > 0;
        canvas.setBrushSettings(brush); expected.setBrushSettings(brush);
        canvas.setStabilization(stabilization); expected.setStabilization(stabilization);
        expected.setHoldShapeEnabled(false);
        const auto points = noisyLine();
        drawHeld(expected, points, true);
        pointer(expected, true, QEvent::MouseButtonRelease, points.last());
        drawHeld(canvas, points, true);
        const auto original = renderMovieFrame(canvas.project());
        QTRY_VERIFY_WITH_TIMEOUT(renderMovieFrame(canvas.project()) != original, 1800);
        pointer(canvas, true, QEvent::MouseButtonRelease, points.last());
        QVERIFY(canvas.holdShapePending());
        QVERIFY(choice(canvas, "shapeChoiceOriginal"));
        choice(canvas, "shapeChoiceOriginal")->click();
        QCOMPARE(renderMovieFrame(canvas.project()), renderMovieFrame(expected.project()));
        QCOMPARE(canvas.document()->history()->count(), 1);
        if (vector) {
            QCOMPARE(canvas.shot().layers[0].objects.size(), 1);
            const auto &actual = *canvas.shot().layers[0].objects[0].brushStroke;
            const auto &reference = *expected.shot().layers[0].objects[0].brushStroke;
            QCOMPARE(actual.samples, reference.samples);
            QCOMPARE(actual.seed, reference.seed);
        }
    }

    void escapeDiscardsHeldAndReleasedDrafts_data() {
        QTest::addColumn<bool>("release");
        QTest::newRow("pen-down") << false;
        QTest::newRow("choice-bar") << true;
    }

    void escapeDiscardsHeldAndReleasedDrafts() {
        QFETCH(bool, release);
        Canvas canvas; prepare(canvas);
        const auto before = renderMovieFrame(canvas.project());
        const auto points = noisyLine();
        drawHeld(canvas, points, true);
        const auto freehand = renderMovieFrame(canvas.project());
        QTRY_VERIFY_WITH_TIMEOUT(renderMovieFrame(canvas.project()) != freehand, 1800);
        if (release) pointer(canvas, true, QEvent::MouseButtonRelease, points.last());
        QTest::keyClick(&canvas, Qt::Key_Escape);
        QVERIFY(!canvas.holdShapePending());
        QVERIFY(!canvas.document()->strokeActive());
        QCOMPARE(canvas.document()->history()->count(), 0);
        QCOMPARE(renderMovieFrame(canvas.project()), before);
        if (bar(canvas)) QVERIFY(bar(canvas)->isHidden());
    }

    void movementAfterHoldRestoresAndResumesOriginal_data() {
        QTest::addColumn<int>("stabilization");
        QTest::newRow("raw") << 0;
        QTest::newRow("stabilized") << 60;
    }

    void movementAfterHoldRestoresAndResumesOriginal() {
        QFETCH(int, stabilization);
        Canvas canvas, expected;
        prepare(canvas, fixture(true)); prepare(expected, fixture(true));
        auto brush = ink(); brush.smoothingEnabled = stabilization > 0;
        canvas.setBrushSettings(brush); expected.setBrushSettings(brush);
        canvas.setStabilization(stabilization); expected.setStabilization(stabilization);
        expected.setHoldShapeEnabled(false);
        const auto points = noisyLine();
        drawHeld(canvas, points, true);
        const auto freehand = renderMovieFrame(canvas.project());
        QTRY_VERIFY_WITH_TIMEOUT(renderMovieFrame(canvas.project()) != freehand, 1800);
        drawHeld(expected, points, true);
        for (auto *target : {&canvas, &expected}) {
            pointer(*target, true, QEvent::MouseMove, {210, 145}, .55);
            pointer(*target, true, QEvent::MouseMove, {175, 195}, .7);
            pointer(*target, true, QEvent::MouseButtonRelease, {175, 195});
        }
        QVERIFY(!canvas.holdShapePending());
        QCOMPARE(canvas.document()->history()->count(), 1);
        QCOMPARE(renderMovieFrame(canvas.project()), renderMovieFrame(expected.project()));
        QCOMPARE(canvas.shot().layers[0].objects[0].brushStroke->samples,
                 expected.shot().layers[0].objects[0].brushStroke->samples);
    }

    void nextActionAcceptsCurrentShape_data() {
        QTest::addColumn<bool>("nextStroke");
        QTest::newRow("canvas-press") << true;
        QTest::newRow("tool-change") << false;
    }

    void nextActionAcceptsCurrentShape() {
        QFETCH(bool, nextStroke);
        Canvas canvas; prepare(canvas);
        const auto blank = renderMovieFrame(canvas.project());
        const auto points = noisyLine();
        drawHeld(canvas, points);
        const auto freehand = renderMovieFrame(canvas.project());
        QTRY_VERIFY_WITH_TIMEOUT(renderMovieFrame(canvas.project()) != freehand, 1800);
        pointer(canvas, false, QEvent::MouseButtonRelease, points.last());
        QVERIFY(canvas.holdShapePending());
        const auto fitted = renderMovieFrame(canvas.project());
        if (nextStroke) {
            pointer(canvas, false, QEvent::MouseButtonPress, {50, 210});
            pointer(canvas, false, QEvent::MouseButtonRelease, {50, 210});
        } else canvas.setTool(Canvas::Tool::Hand);
        QVERIFY(!canvas.holdShapePending());
        QVERIFY(!canvas.document()->strokeActive());
        QCOMPARE(canvas.document()->history()->count(), nextStroke ? 2 : 1);
        if (nextStroke) {
            canvas.document()->history()->undo();
            QCOMPARE(renderMovieFrame(canvas.project()), fitted);
        } else QCOMPARE(renderMovieFrame(canvas.project()), fitted);
        canvas.document()->history()->undo();
        QCOMPARE(renderMovieFrame(canvas.project()), blank);
    }

    void choiceBarDoesNotPassPenIntoArtwork() {
        Canvas canvas; prepare(canvas);
        const auto points = noisyLine();
        drawHeld(canvas, points, true);
        const auto freehand = renderMovieFrame(canvas.project());
        QTRY_VERIFY_WITH_TIMEOUT(renderMovieFrame(canvas.project()) != freehand, 1800);
        pointer(canvas, true, QEvent::MouseButtonRelease, points.last());
        QVERIFY(canvas.holdShapePending());
        auto *popup = bar(canvas); QVERIFY(popup);
        auto *original = choice(canvas, "shapeChoiceOriginal"); QVERIFY(original);
        QVERIFY(popup->testAttribute(Qt::WA_NoMousePropagation));
        const auto fitted = renderMovieFrame(canvas.project());
        for (QWidget *target : {static_cast<QWidget *>(popup), static_cast<QWidget *>(original)}) {
            const QPoint position = target == popup ? QPoint(2, 2) : target->rect().center();
            for (const auto type : {QEvent::TabletPress, QEvent::TabletMove, QEvent::TabletRelease}) {
                const bool release = type == QEvent::TabletRelease;
                QTabletEvent event(type, &penDevice(), position, target->mapToGlobal(position), release ? 0 : .6,
                    0, 0, 0, 0, 0, Qt::NoModifier,
                    type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                    release ? Qt::NoButton : Qt::LeftButton);
                event.setAccepted(false);
                QApplication::sendEvent(target, &event);
                QVERIFY(!event.isAccepted()); // Preserve Qt pen-to-mouse button synthesis.
                QVERIFY(canvas.holdShapePending());
                QCOMPARE(renderMovieFrame(canvas.project()), fitted);
                QCOMPARE(canvas.document()->history()->count(), 0);
            }
        }
        QTest::mouseClick(original, Qt::LeftButton);
        QVERIFY(!canvas.holdShapePending());
        QCOMPARE(canvas.document()->history()->count(), 1);
        QCOMPARE(renderMovieFrame(canvas.project()), freehand);
    }

    void smallPenJitterStillAllowsHolding() {
        Canvas canvas; prepare(canvas);
        const auto points = noisyLine();
        drawHeld(canvas, points, true);
        QTimer jitter;
        jitter.setInterval(60);
        int samples = 0;
        connect(&jitter, &QTimer::timeout, &canvas, [&] {
            ++samples;
            const QPointF offset(1.2 * std::sin(samples), .8 * std::cos(samples));
            pointer(canvas, true, QEvent::MouseMove, points.last() + offset, .9);
        });
        jitter.start();
        bool elapsed = false;
        QTimer::singleShot(950, &canvas, [&elapsed] { elapsed = true; });
        QTRY_VERIFY_WITH_TIMEOUT(elapsed, 1800);
        jitter.stop();
        QVERIFY(samples >= 8);
        pointer(canvas, true, QEvent::MouseButtonRelease, points.last());
        QVERIFY(canvas.holdShapePending());
        QVERIFY(choice(canvas, "shapeChoice0"));
        choice(canvas, "shapeChoice0")->click();
        QCOMPARE(canvas.document()->history()->count(), 1);
    }

    void guideSnappingKeepsItsOwnGeometry_data() {
        QTest::addColumn<bool>("ruler");
        QTest::newRow("global-toggle-only") << false;
        QTest::newRow("horizontal-ruler") << true;
    }

    void guideSnappingKeepsItsOwnGeometry() {
        QFETCH(bool, ruler);
        auto project = fixture(true);
        project.shots[0].guideSettings.snapping = true;
        if (ruler) {
            DrawingGuide guide;
            guide.id = "57446870-2032-4df5-81f5-a862f98ee6c9"; guide.name = "Horizontal ruler";
            guide.type = GuideType::ParallelRuler; guide.points = {{0, 0}, {256, 0}};
            project.shots[0].guides.append(guide);
        }
        QVERIFY2(project.validate().isEmpty(), qPrintable(project.validate()));
        Canvas canvas; prepare(canvas, project);
        QVERIFY(canvas.holdShapeEnabled());
        QVERIFY(canvas.guideSettings().snapping);
        const auto points = noisyLine();
        drawHeld(canvas, points, true);
        const auto drawing = renderMovieFrame(canvas.project());
        bool elapsed = false;
        QTimer::singleShot(900, &canvas, [&elapsed] { elapsed = true; });
        QTRY_VERIFY_WITH_TIMEOUT(elapsed, 1800);
        QCOMPARE(renderMovieFrame(canvas.project()), drawing);
        pointer(canvas, true, QEvent::MouseButtonRelease, points.last());
        QVERIFY(!canvas.holdShapePending());
        QVERIFY(!canvas.document()->strokeActive());
        QCOMPARE(canvas.document()->history()->count(), 1);
        if (bar(canvas)) QVERIFY(bar(canvas)->isHidden());
        if (ruler) {
            QCOMPARE(canvas.shot().layers[0].objects.size(), 1);
            for (const auto &sample : canvas.shot().layers[0].objects[0].brushStroke->samples)
                QVERIFY(qAbs(sample.point.y() - points.first().y()) < 1e-8);
        }
    }

    void excludedAndReleasedStrokesNeverSnapLater_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("released-before-hold") << 0;
        QTest::newRow("disabled") << 1;
        QTest::newRow("tiny-stroke") << 2;
        QTest::newRow("airbrush") << 3;
        QTest::newRow("control") << 4;
        QTest::newRow("command") << 5;
        QTest::newRow("shift") << 6;
    }

    void excludedAndReleasedStrokesNeverSnapLater() {
        QFETCH(int, mode);
        Canvas canvas; prepare(canvas);
        if (mode == 1) {
            canvas.setHoldShapeEnabled(false);
            QVERIFY(!canvas.holdShapeEnabled());
        }
        if (mode == 3) {
            auto brush = ink(); brush.airbrush = true;
            canvas.setBrushSettings(brush);
        }
        const auto points = mode == 2 ? QVector<QPointF>{{40, 40}, {43, 43}, {46, 41}} : noisyLine();
        const auto modifiers = mode == 4 ? Qt::ControlModifier : mode == 5 ? Qt::MetaModifier
            : mode == 6 ? Qt::ShiftModifier : Qt::NoModifier;
        drawHeld(canvas, points, true, modifiers);
        if (mode == 0) pointer(canvas, true, QEvent::MouseButtonRelease, points.last());
        const auto beforeWaiting = renderMovieFrame(canvas.project());
        bool elapsed = false;
        QTimer::singleShot(900, &canvas, [&elapsed] { elapsed = true; });
        QTRY_VERIFY_WITH_TIMEOUT(elapsed, 1800);
        QVERIFY(!canvas.holdShapePending());
        if (mode == 0) QCOMPARE(renderMovieFrame(canvas.project()), beforeWaiting);
        else pointer(canvas, true, QEvent::MouseButtonRelease, points.last(), 1, modifiers);
        QVERIFY(!canvas.holdShapePending());
        QVERIFY(!canvas.document()->strokeActive());
        QCOMPARE(canvas.document()->history()->count(), 1);
        if (bar(canvas)) QVERIFY(bar(canvas)->isHidden());
    }

    void pendingChooserScreenshot() {
        const auto destination = qEnvironmentVariable("TOON_HOLD_SHAPE_SCREENSHOT");
        if (destination.isEmpty()) return;
        applyTheme();
        auto project = fixture();
        project.width = 1280; project.height = 720;
        project.movie = "Hold and Shape";
        auto &shot = project.shots[0];
        shot.name = "Circle preview";
        shot.sheet.left = shot.sheet.right = shot.sheet.top = shot.sheet.bottom = 20;
        shot.sheet.overscan = 10;
        shot.sheet.header = 160;
        Canvas canvas; prepare(canvas, project);
        canvas.resize(960, 720); canvas.fitSheet(); canvas.setBrushSize(10);
        auto points = noisyCircle();
        for (auto &point : points) point = (point - QPointF(128, 128)) * 3 + QPointF(640, 360);
        drawHeld(canvas, points);
        const auto freehand = renderMovieFrame(canvas.project());
        QTRY_VERIFY_WITH_TIMEOUT(renderMovieFrame(canvas.project()) != freehand, 1800);
        pointer(canvas, false, QEvent::MouseButtonRelease, points.last());
        QVERIFY(canvas.holdShapePending());
        QVERIFY(bar(canvas)); QVERIFY(bar(canvas)->isVisible());
        QVERIFY(QDir().mkpath(QFileInfo(destination).absolutePath()));
        QVERIFY(canvas.grab().save(destination));
    }
};

QTEST_MAIN(HoldShapeTests)
#include "test_hold_shape.moc"
