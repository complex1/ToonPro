#include "canvas.h"
#include "stroke_filter.h"
#include <QApplication>
#include <QFocusEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QtTest>
#include <cmath>

using namespace toon;
static QPointF screenPoint(const Canvas &canvas, QPointF artwork) {
    return canvas.viewTransform().map(sheetGeometry(canvas.project(), canvas.shot()).output.topLeft() + artwork);
}
static void mouse(Canvas &canvas, QEvent::Type type, QPointF artwork) {
    const auto position = screenPoint(canvas, artwork);
    QMouseEvent event(type, position, canvas.mapToGlobal(position),
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&canvas, &event);
}
static Project drawingProject() {
    auto project = Project::create(); project.transparent = true; return project;
}
class StabilizerTest : public QObject {
    Q_OBJECT
private slots:
    void disabledPassesInputExactly() {
        StrokeFilter filter;
        const auto start = filter.begin({10.3, 20.7}, .35, 0);
        QCOMPARE(start.point, QPointF(10.3, 20.7)); QCOMPARE(start.pressure, .35);
        const auto samples = filter.advance({11.23, 22.51}, .61);
        QCOMPARE(samples.size(), 1);
        QCOMPARE(samples.first().point, QPointF(11.23, 22.51)); QCOMPARE(samples.first().pressure, .61);
        const auto end = filter.finish({11.4, 22.7}, .7);
        QCOMPARE(end.size(), 1); QCOMPARE(end.first().point, QPointF(11.4, 22.7));
        QVERIFY(!filter.active());
    }
    void subdivisionDoesNotChangeGeometryOrPressure() {
        const QVector<StrokeSample> path{{{0, 0}, .2}, {{40, 0}, .8}, {{70, 40}, .6}, {{70, 90}, .3}};
        auto draw = [&](int divisions) {
            StrokeFilter filter; filter.begin(path.first().point, path.first().pressure, 65);
            QVector<StrokeSample> result;
            for (int i = 1; i < path.size(); ++i) {
                for (int j = 1; j <= divisions; ++j) {
                    const qreal part = qreal(j) / divisions;
                    result += filter.advance(path[i - 1].point + (path[i].point - path[i - 1].point) * part,
                        path[i - 1].pressure + (path[i].pressure - path[i - 1].pressure) * part);
                }
            }
            result += filter.finish(path.last().point, path.last().pressure);
            return result;
        };
        const auto coarse = draw(1), dense = draw(8);
        QCOMPARE(coarse.size(), dense.size());
        for (int i = 0; i < coarse.size(); ++i) {
            QVERIFY(QLineF(coarse[i].point, dense[i].point).length() < 1e-8);
            QVERIFY(std::abs(coarse[i].pressure - dense[i].pressure) < 1e-8);
        }
    }
    void reducesJitterAndClosesBoundedGap() {
        StrokeFilter filter; filter.begin({0, 0}, .8, 100);
        qreal totalSquared = 0; int count = 0;
        for (int i = 1; i <= 100; ++i) {
            const QPointF raw(i * 2, (i % 2 ? 3 : -3));
            const auto samples = filter.advance(raw, .8);
            for (const auto &sample : samples) {
                if (sample.point.x() > 30) { totalSquared += sample.point.y() * sample.point.y(); ++count; }
                QVERIFY(sample.pressure > .79999 && sample.pressure < .80001);
            }
            QVERIFY(QLineF(filter.tip().point, raw).length() <= 25.0001);
        }
        QVERIFY(count > 100);
        QVERIFY(std::sqrt(totalSquared / count) < 1);
        const auto finish = filter.finish({200, -3}, .8);
        QVERIFY(!finish.isEmpty()); QCOMPARE(finish.last().point, QPointF(200, -3));
        QVERIFY(!filter.active()); QVERIFY(filter.advance({300, 100}, 1).isEmpty());
        QCOMPARE(filter.begin({800, 400}, .25, 100).point, QPointF(800, 400));
        QCOMPARE(filter.tip().pressure, .25);
    }
    void mouseEndpointUndoCancelAndNewStroke() {
        Canvas canvas; canvas.resize(1200, 900); canvas.setProject(drawingProject()); canvas.show();
        canvas.setZoom(.8); canvas.setStabilization(100); canvas.setBrushSize(8);
        mouse(canvas, QEvent::MouseButtonPress, {800, 400});
        mouse(canvas, QEvent::MouseMove, {900, 400});
        const auto rawScreen = screenPoint(canvas, {900, 400});
        QVERIFY(canvas.brushCursorRect().center().x() < rawScreen.x() - 10);
        mouse(canvas, QEvent::MouseButtonRelease, {920, 400});
        QCOMPARE(canvas.document()->history()->count(), 1);
        auto picture = renderMovieFrame(canvas.project());
        QVERIFY(picture.pixelColor(919, 400).alpha() > 240);
        canvas.document()->history()->undo();
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(850, 400).alpha(), 0);
        canvas.document()->history()->redo();
        mouse(canvas, QEvent::MouseButtonPress, {800, 500});
        mouse(canvas, QEvent::MouseMove, {900, 500});
        QTest::keyClick(&canvas, Qt::Key_Escape);
        mouse(canvas, QEvent::MouseButtonRelease, {920, 500});
        QCOMPARE(canvas.document()->history()->count(), 1);
        QCOMPARE(renderMovieFrame(canvas.project()), picture);
        mouse(canvas, QEvent::MouseButtonPress, {1100, 550});
        mouse(canvas, QEvent::MouseButtonRelease, {1100, 550});
        QCOMPARE(canvas.document()->history()->count(), 2);
        picture = renderMovieFrame(canvas.project());
        QVERIFY(picture.pixelColor(1100, 550).alpha() > 240);
        QCOMPARE(picture.pixelColor(1000, 480).alpha(), 0);
        canvas.setTool(Canvas::Tool::Eraser);
        mouse(canvas, QEvent::MouseButtonPress, {810, 400});
        mouse(canvas, QEvent::MouseMove, {900, 400});
        canvas.finishStroke();
        QCOMPARE(renderMovieFrame(canvas.project()).pixelColor(899, 400).alpha(), 0);
        QCOMPARE(canvas.document()->history()->count(), 3);
    }
    void tabletPressureAndViewIndependentCursor() {
        Canvas canvas; canvas.resize(1200, 900); canvas.setProject(drawingProject()); canvas.show();
        canvas.setZoom(.85); canvas.setRotation(33); canvas.setBrushSize(40); canvas.setStabilization(70);
        QPointingDevice pen("Test pen", 12, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        auto send = [&](QEvent::Type type, QPointF point, qreal pressure) {
            const auto position = screenPoint(canvas, point);
            QTabletEvent event(type, &pen, position, canvas.mapToGlobal(position), pressure, 0, 0, 0, 0, 0,
                Qt::NoModifier, type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
            QApplication::sendEvent(&canvas, &event);
        };
        StrokeFilter reference; reference.begin({800, 450}, .2, 70);
        send(QEvent::TabletPress, {800, 450}, .2);
        reference.advance({900, 470}, .8);
        send(QEvent::TabletMove, {900, 470}, .8);
        QVERIFY(QLineF(canvas.brushCursorRect().center(), screenPoint(canvas, reference.tip().point)).length() < 1e-7);
        QVERIFY(std::abs(canvas.brushCursorRect().width() - 40 * .85 * reference.tip().pressure) < 1e-7);
        send(QEvent::TabletRelease, {910, 470}, 0);
        QVERIFY(renderMovieFrame(canvas.project()).pixelColor(909, 470).alpha() > 240);
        QCOMPARE(canvas.document()->history()->count(), 1);
    }
    void finishingOnToolChangeAndFocusLoss() {
        Canvas canvas; canvas.resize(1200, 900); canvas.setProject(drawingProject()); canvas.show();
        canvas.setZoom(.8); canvas.setStabilization(100); canvas.setBrushSize(10);
        mouse(canvas, QEvent::MouseButtonPress, {800, 400}); mouse(canvas, QEvent::MouseMove, {900, 400});
        canvas.setTool(Canvas::Tool::Hand);
        QVERIFY(!canvas.document()->strokeActive());
        QVERIFY(renderMovieFrame(canvas.project()).pixelColor(899, 400).alpha() > 240);
        canvas.setTool(Canvas::Tool::Brush);
        mouse(canvas, QEvent::MouseButtonPress, {800, 500}); mouse(canvas, QEvent::MouseMove, {900, 500});
        QFocusEvent focus(QEvent::FocusOut); QApplication::sendEvent(&canvas, &focus);
        QVERIFY(!canvas.document()->strokeActive());
        QVERIFY(renderMovieFrame(canvas.project()).pixelColor(899, 500).alpha() > 240);
        QCOMPARE(canvas.document()->history()->count(), 2);
    }
};
QTEST_MAIN(StabilizerTest)
#include "test_stabilizer.moc"
