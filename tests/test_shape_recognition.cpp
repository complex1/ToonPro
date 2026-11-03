#include "shape_recognition.h"

#include <QLineF>
#include <QTransform>
#include <QtTest>
#include <cmath>
#include <limits>

using namespace toon;
namespace {
constexpr qreal pi = 3.14159265358979323846;
const RecognizedShape *find(const QVector<RecognizedShape> &shapes, RecognizedShapeKind kind) {
    for (const auto &shape : shapes) if (shape.kind == kind) return &shape;
    return nullptr;
}
QVector<QPointF> ellipsePoints(qreal rx, qreal ry, qreal angle = 0, qreal start = .3,
                              qreal sweep = 2 * pi, qreal noise = 0, int count = 181) {
    QVector<QPointF> result;
    const auto transform = QTransform().translate(350, 270).rotateRadians(angle);
    for (int i = 0; i < count; ++i) {
        const qreal t = start + sweep * i / (count - 1);
        const qreal jitter = noise * std::sin(i * 2.3);
        result.append(transform.map(QPointF((rx + jitter) * std::cos(t), (ry + jitter) * std::sin(t))));
    }
    return result;
}
QVector<QPointF> polygonPoints(const QVector<QPointF> &corners, qreal noise = 0) {
    QVector<QPointF> result;
    for (int edge = 0; edge < corners.size(); ++edge) {
        const auto a = corners[edge], b = corners[(edge + 1) % corners.size()];
        const auto d = b - a;
        const auto normal = QPointF(-d.y(), d.x()) / std::hypot(d.x(), d.y());
        for (int i = 0; i < 30; ++i) {
            const qreal t = qreal(i) / 30;
            result.append(a * (1 - t) + b * t + normal * (noise * std::sin(i * 1.7) * std::sin(t * pi)));
        }
    }
    result.append(result.first());
    return result;
}
QVector<QPointF> subdivide(const QVector<QPointF> &points, int divisions) {
    QVector<QPointF> result{points.first()};
    for (int i = 1; i < points.size(); ++i)
        for (int j = 1; j <= divisions; ++j) result.append(points[i - 1] + (points[i] - points[i - 1]) * (qreal(j) / divisions));
    return result;
}
qreal distance(QPointF a, QPointF b) { return QLineF(a, b).length(); }
}

class ShapeRecognitionTests : public QObject {
    Q_OBJECT
private slots:
    void sparseAndNoisyLines() {
        auto shapes = recognizeStrokeShapes({{10, 30}, {90, 170}});
        QVERIFY(!shapes.isEmpty());
        QCOMPARE(shapes.first().kind, RecognizedShapeKind::Line);
        QCOMPARE(shapes.first().path.elementCount(), 2);
        QVERIFY(distance(shapes.first().path.pointAtPercent(0), {10, 30}) < 1e-8);
        QVERIFY(distance(shapes.first().path.pointAtPercent(1), {90, 170}) < 1e-8);
        QVector<QPointF> noisy;
        for (int i = 0; i < 100; ++i) noisy.append({20 + i * 3., 40 + i * 1.2 + std::sin(i * 2.2)});
        shapes = recognizeStrokeShapes(noisy);
        QVERIFY(!shapes.isEmpty()); QCOMPARE(shapes.first().kind, RecognizedShapeKind::Line);
        QVERIFY(distance(shapes.first().path.pointAtPercent(0), noisy.first()) < 2);
        QVERIFY(distance(shapes.first().path.pointAtPercent(1), noisy.last()) < 2);
    }

    void circularLoopsOfferCircleAndEllipse() {
        const auto points = ellipsePoints(120, 120, .2, .7, 2 * pi - .08, .5);
        const auto shapes = recognizeStrokeShapes(points);
        QVERIFY(!shapes.isEmpty()); QCOMPARE(shapes.first().kind, RecognizedShapeKind::Circle);
        const auto *circle = find(shapes, RecognizedShapeKind::Circle);
        QVERIFY(circle); QVERIFY(find(shapes, RecognizedShapeKind::Ellipse));
        QVERIFY(!find(shapes, RecognizedShapeKind::Rectangle));
        QVERIFY(!find(shapes, RecognizedShapeKind::Triangle));
        QVERIFY(std::abs(circle->path.boundingRect().width() - 240) < 2);
        QVERIFY(std::abs(circle->path.boundingRect().height() - 240) < 2);
        QVERIFY(distance(circle->path.pointAtPercent(0), points.first()) < 2);
        QVERIFY(distance(circle->path.pointAtPercent(0), circle->path.pointAtPercent(1)) < 1e-8);
    }

    void tiltedEllipseRetainsOrientationAndDirection() {
        const auto points = ellipsePoints(170, 75, .63, 1.1, -2 * pi, .5);
        const auto shapes = recognizeStrokeShapes(points);
        QVERIFY(!shapes.isEmpty()); QCOMPARE(shapes.first().kind, RecognizedShapeKind::Ellipse);
        QVERIFY(!find(shapes, RecognizedShapeKind::Circle));
        const auto &path = shapes.first().path;
        QVERIFY(distance(path.pointAtPercent(0), points.first()) < 2);
        const qreal expectedWidth = 2 * std::hypot(170 * std::cos(.63), 75 * std::sin(.63));
        QVERIFY(std::abs(path.boundingRect().width() - expectedWidth) < 3);
        const auto originalDirection = points[5] - points[0];
        const auto cleanDirection = path.pointAtPercent(.025) - path.pointAtPercent(0);
        QVERIFY(QPointF::dotProduct(originalDirection, cleanDirection) > 0);
    }

    void handDrawnLoopAndStationaryTabletJitter() {
        QVector<QPointF> points;
        for (int i = 0; i <= 180; ++i) {
            const qreal t = qreal(i) / 180 * 2 * pi;
            const qreal radius = 120 + 7 * std::sin(3 * t) + 3 * std::cos(5 * t);
            points.append({300 + radius * std::cos(t), 220 + radius * std::sin(t)});
        }
        const auto end = points.last();
        for (int i = 0; i < 800; ++i) points.append(end + QPointF(.12 * std::sin(i * 2.4), .12 * std::cos(i * 1.4)));
        const auto shapes = recognizeStrokeShapes(points);
        QVERIFY(!shapes.isEmpty()); QCOMPARE(shapes.first().kind, RecognizedShapeKind::Circle);
        QVERIFY(find(shapes, RecognizedShapeKind::Ellipse));
    }

    void circularAndQuadraticArcs() {
        for (const auto sweep : {pi * .5, pi, -pi * 1.4}) {
            const auto points = ellipsePoints(130, 130, 0, .4, sweep, .35);
            const auto shapes = recognizeStrokeShapes(points);
            QVERIFY(!shapes.isEmpty()); QCOMPARE(shapes.first().kind, RecognizedShapeKind::Arc);
            QVERIFY(distance(shapes.first().path.pointAtPercent(0), points.first()) < 2);
            QVERIFY(distance(shapes.first().path.pointAtPercent(1), points.last()) < 2);
            QVERIFY(distance(shapes.first().path.pointAtPercent(0), shapes.first().path.pointAtPercent(1)) > 10);
        }
        QVector<QPointF> points;
        const QPointF a(20, 180), control(95, -90), b(340, 130);
        for (int i = 0; i <= 120; ++i) {
            const qreal t = std::pow(qreal(i) / 120, 1.7), u = 1 - t;
            points.append(a * (u * u) + control * (2 * t * u) + b * (t * t));
        }
        const auto shapes = recognizeStrokeShapes(points);
        QVERIFY(!shapes.isEmpty()); QCOMPARE(shapes.first().kind, RecognizedShapeKind::Arc);
        QVERIFY(distance(shapes.first().path.pointAtPercent(0), a) < .01);
        QVERIFY(distance(shapes.first().path.pointAtPercent(1), b) < .01);
    }

    void tiltedRectangleStartingOnAnEdge() {
        const auto transform = QTransform().translate(300, 250).rotate(27);
        QVector<QPointF> corners{{0, -75}, {150, -75}, {150, 75}, {-150, 75}, {-150, -75}};
        for (auto &corner : corners) corner = transform.map(corner);
        const auto points = polygonPoints(corners, .65);
        const auto shapes = recognizeStrokeShapes(points);
        QVERIFY(!shapes.isEmpty()); QCOMPARE(shapes.first().kind, RecognizedShapeKind::Rectangle);
        QVERIFY(distance(shapes.first().path.pointAtPercent(0), points.first()) < 3);
        QVERIFY(!find(shapes, RecognizedShapeKind::Circle));
        QVERIFY(!find(shapes, RecognizedShapeKind::Triangle));
        QVERIFY(std::abs(shapes.first().path.length() - 900) < 15);
    }

    void triangle() {
        const auto points = polygonPoints({{80, 290}, {190, 45}, {360, 270}}, .7);
        const auto shapes = recognizeStrokeShapes(points);
        QVERIFY(!shapes.isEmpty()); QCOMPARE(shapes.first().kind, RecognizedShapeKind::Triangle);
        QVERIFY(!find(shapes, RecognizedShapeKind::Circle));
        QVERIFY(!find(shapes, RecognizedShapeKind::Rectangle));
    }

    void coordinatesAndEventDensityDoNotChangeRecognition() {
        const auto source = ellipsePoints(160, 82, .42, .7, 2 * pi, .25, 91);
        const auto reference = recognizeStrokeShapes(source);
        QVERIFY(!reference.isEmpty());
        for (const qreal scale : {.01, 1., 1000.}) {
            QVector<QPointF> transformed;
            const QPointF offset(1234567, -2345678);
            for (const auto p : subdivide(source, 17)) transformed.append(p * scale + offset);
            const auto result = recognizeStrokeShapes(transformed);
            QCOMPARE(result.size(), reference.size());
            for (int i = 0; i < result.size(); ++i) {
                QCOMPARE(result[i].kind, reference[i].kind);
                QVERIFY(std::abs(result[i].score - reference[i].score) < 1e-7);
                QCOMPARE(result[i].path.elementCount(), reference[i].path.elementCount());
                for (int p = 0; p < result[i].path.elementCount(); ++p) {
                    const auto actual = result[i].path.elementAt(p), expected = reference[i].path.elementAt(p);
                    QCOMPARE(actual.type, expected.type);
                    QVERIFY(distance((QPointF(actual.x, actual.y) - offset) / scale, QPointF(expected.x, expected.y)) < .01);
                }
            }
        }
    }

    void rejectsDotsInvalidDataRetracingAndScribbles() {
        QVERIFY(recognizeStrokeShapes({}).isEmpty());
        QVERIFY(recognizeStrokeShapes({{20, 20}}).isEmpty());
        QVERIFY(recognizeStrokeShapes({{20, 20}, {20, 20}, {20, 20}}).isEmpty());
        QVERIFY(recognizeStrokeShapes({{0, 0}, {std::numeric_limits<qreal>::infinity(), 10}}).isEmpty());
        QVERIFY(recognizeStrokeShapes({{0, 0}, {10, std::numeric_limits<qreal>::quiet_NaN()}}).isEmpty());
        QVERIFY(recognizeStrokeShapes({{0, 0}, {300, 0}, {20, 0}, {250, 0}}).isEmpty());
        auto repeated = ellipsePoints(100, 100, 0, .3, 4 * pi);
        QVERIFY(recognizeStrokeShapes(repeated).isEmpty());
        const auto bowTie = polygonPoints({{0, 0}, {200, 200}, {0, 200}, {200, 0}});
        QVERIFY(recognizeStrokeShapes(bowTie).isEmpty());
        QVector<QPointF> zigzag;
        for (int i = 0; i <= 120; ++i) zigzag.append({i * 3., std::sin(i * .15) * 70});
        QVERIFY(recognizeStrokeShapes(zigzag).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ShapeRecognitionTests)
#include "test_shape_recognition.moc"
