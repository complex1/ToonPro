#include "guide_geometry.h"
#include <QElapsedTimer>
#include <QPolygonF>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace toon;
namespace {
DrawingGuide makeGuide(GuideType type, QVector<QPointF> points) {
    DrawingGuide guide; guide.type = type; guide.points = points; return guide;
}
bool near(QPointF a, QPointF b, qreal tolerance = 1e-6) { return QLineF(a, b).length() < tolerance; }
bool bounded(const QPainterPath &path, QRectF bounds) {
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto p = path.elementAt(i);
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !bounds.adjusted(-1e-6, -1e-6, 1e-6, 1e-6).contains(QPointF(p.x, p.y))) return false;
    }
    return true;
}
bool includesSegment(const QPainterPath &path, QPointF a, QPointF b) {
    for (int i = 1; i < path.elementCount(); ++i) {
        if (path.elementAt(i).type != QPainterPath::LineToElement) continue;
        const auto previous = path.elementAt(i - 1), current = path.elementAt(i);
        if ((near({previous.x, previous.y}, a) && near({current.x, current.y}, b)) ||
            (near({previous.x, previous.y}, b) && near({current.x, current.y}, a))) return true;
    }
    return false;
}
QTransform projectiveMap(const QVector<QPointF> &quad) {
    QTransform result;
    const QPolygonF unit{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)};
    QTransform::quadToQuad(unit, QPolygonF(quad), result); return result;
}
}

class GuideGeometryTest : public QObject {
    Q_OBJECT
private slots:
    void projectionUsesInfiniteLineAndHandlesDegeneracy() {
        const QLineF horizontal({10, 20}, {20, 20});
        QCOMPARE(projectOntoGuideLine({500, 70}, horizontal), QPointF(500, 20));
        QCOMPARE(guideLineDistance({500, 70}, horizontal), 50);
        const QLineF diagonal({100, 100}, {200, 200});
        QVERIFY(near(projectOntoGuideLine({240, 260}, diagonal), {250, 250}));
        QVERIFY(std::abs(guideLineDistance({240, 260}, diagonal) - std::sqrt(200.0)) < 1e-6);
        const QLineF degenerate({2, 3}, {2, 3});
        QCOMPARE(projectOntoGuideLine({5, 6}, degenerate), QPointF(5, 6));
        QVERIFY(std::isinf(guideLineDistance({5, 6}, degenerate)));
        const QLineF invalid({std::numeric_limits<qreal>::infinity(), 0}, {0, 1});
        QCOMPARE(projectOntoGuideLine({5, 6}, invalid), QPointF(5, 6));
        QVERIFY(std::isinf(guideLineDistance({5, 6}, invalid)));
        const QLineF distant({1e100, 0}, {-1e100, 0});
        QVERIFY(near(projectOntoGuideLine({12, 8}, distant), {12, 0}));
        QCOMPARE(guideLineDistance({12, 8}, distant), 8);
    }

    void validatesGeometryWithoutAcceptingCollapsedOrCrossedQuads() {
        auto quad = makeGuide(GuideType::Perspective, {{0, 0}, {400, 20}, {350, 300}, {50, 250}});
        QVERIFY(validGuideGeometry(quad));
        std::reverse(quad.points.begin(), quad.points.end()); QVERIFY(validGuideGeometry(quad));
        quad.points = {{0, 0}, {400, 300}, {400, 0}, {0, 300}}; QVERIFY(!validGuideGeometry(quad));
        quad.points = {{0, 0}, {400, 0}, {30, 30}, {0, 300}}; QVERIFY(!validGuideGeometry(quad));
        quad.points = {{0, 0}, {400, 0}, {400, 0}, {0, 300}}; QVERIFY(!validGuideGeometry(quad));
        quad.points = {{0, 0}, {400, 0}, {200, 0}, {0, 300}}; QVERIFY(!validGuideGeometry(quad));
        quad.points.removeLast(); QVERIFY(!validGuideGeometry(quad));
        auto two = makeGuide(GuideType::TwoPoint, {{0, 0}, {100, 0}, {30, 60}});
        QVERIFY(validGuideGeometry(two));
        two.points[2] = {50, 0}; QVERIFY(!validGuideGeometry(two));
        two.points[1] = two.points[0]; QVERIFY(!validGuideGeometry(two));
        auto ruler = makeGuide(GuideType::ParallelRuler, {{5, 5}, {5, 5}});
        QVERIFY(!validGuideGeometry(ruler));
        auto vp = makeGuide(GuideType::VanishingPoint, {{0, 0}});
        QVERIFY(validGuideGeometry(vp));
        vp.points[0] = {std::numeric_limits<qreal>::quiet_NaN(), 2}; QVERIFY(!validGuideGeometry(vp));
        vp.points[0] = {1, std::numeric_limits<qreal>::infinity()}; QVERIFY(!validGuideGeometry(vp));
        vp.points[0] = {1, 2}; vp.limited = true; vp.area = {0, 0, 0, 100}; QVERIFY(!validGuideGeometry(vp));
    }

    void perspectiveGridUsesProjectiveSpacingAndLocalAxes() {
        auto quad = makeGuide(GuideType::Perspective, {{100, 80}, {400, 120}, {330, 400}, {150, 310}});
        quad.subdivisions = 2;
        const auto transform = projectiveMap(quad.points);
        const QRectF bounds(0, 0, 600, 500);
        const auto path = guidePath(quad, bounds);
        QCOMPARE(path.elementCount(), 12);
        QVERIFY(includesSegment(path, transform.map(QPointF(.5, 0)), transform.map(QPointF(.5, 1))));
        QVERIFY(includesSegment(path, transform.map(QPointF(0, .5)), transform.map(QPointF(1, .5))));
        QVERIFY(!near(transform.map(QPointF(.5, 0)), (quad.points[0] + quad.points[1]) / 2));
        const auto start = transform.map(QPointF(.35, .65));
        const auto lines = guideSnapLines(quad, start);
        QCOMPARE(lines.size(), 2);
        for (const auto &line : lines) QCOMPARE(line.p1(), start);
        QVERIFY(guideLineDistance(transform.map(QPointF(0, .65)), lines[0]) < 1e-6);
        QVERIFY(guideLineDistance(transform.map(QPointF(1, .65)), lines[0]) < 1e-6);
        QVERIFY(guideLineDistance(transform.map(QPointF(.35, 0)), lines[1]) < 1e-6);
        QVERIFY(guideLineDistance(transform.map(QPointF(.35, 1)), lines[1]) < 1e-6);
        QVERIFY(guideSnapLines(quad, {-30, -30}).isEmpty());
        quad.subdivisions = 1;
        QCOMPARE(guidePath(quad, bounds).elementCount(), 8); // Four border segments, no interior grid.
    }

    void vanishingPointAndParallelConstraintsPassThroughStrokeStart() {
        const auto vp = makeGuide(GuideType::VanishingPoint, {{100, 100}});
        const auto vpLines = guideSnapLines(vp, {200, 200}); QCOMPARE(vpLines.size(), 1);
        QCOMPARE(vpLines[0].p1(), QPointF(200, 200));
        QVERIFY(guideLineDistance({100, 100}, vpLines[0]) < 1e-6);
        QVERIFY(near(projectOntoGuideLine({240, 260}, vpLines[0]), {250, 250}));
        QVERIFY(guideSnapLines(vp, {100, 100}).isEmpty());
        const auto ruler = makeGuide(GuideType::ParallelRuler, {{0, 0}, {100, 50}});
        const auto lines = guideSnapLines(ruler, {300, 200}); QCOMPARE(lines.size(), 1);
        QVERIFY(near(projectOntoGuideLine({380, 180}, lines[0]), {356, 228}));
        const QRectF bounds(0, 0, 600, 500);
        const auto path = guidePath(ruler, bounds); QVERIFY(!path.isEmpty()); QVERIFY(bounded(path, bounds));
        for (int i = 1; i < path.elementCount(); i += 2) {
            const auto a = path.elementAt(i - 1), b = path.elementAt(i);
            QVERIFY(std::abs((b.y - a.y) - .5 * (b.x - a.x)) < 1e-6);
        }
    }

    void twoPointUsesBothVanishingPointsAndHorizonPerpendicularVertical() {
        auto guide = makeGuide(GuideType::TwoPoint, {{0, 40}, {600, 160}, {300, 500}});
        const auto start = QPointF(250, 380);
        const auto lines = guideSnapLines(guide, start); QCOMPARE(lines.size(), 3);
        QVERIFY(guideLineDistance(guide.points[0], lines[0]) < 1e-6);
        QVERIFY(guideLineDistance(guide.points[1], lines[1]) < 1e-6);
        const auto vertical = lines[2].p2() - lines[2].p1();
        const auto horizon = guide.points[1] - guide.points[0];
        QVERIFY(std::abs(vertical.x() * horizon.x() + vertical.y() * horizon.y()) < 1e-6);
        const QRectF bounds(-100, -100, 800, 800);
        const auto path = guidePath(guide, bounds); QVERIFY(!path.isEmpty()); QVERIFY(bounded(path, bounds));
        for (int i = 1; i < path.elementCount() - 4; i += 2) {
            const auto a = path.elementAt(i - 1), b = path.elementAt(i);
            const QLineF line({a.x, a.y}, {b.x, b.y});
            QVERIFY(std::min(guideLineDistance(guide.points[0], line), guideLineDistance(guide.points[1], line)) < 1e-6);
        }
        guide.vertical = false; QCOMPARE(guideSnapLines(guide, start).size(), 2);
        QVERIFY(guidePath(guide, bounds).elementCount() < path.elementCount());
    }

    void localAreasRestrictGeometryAndVisibilityIsIndependentFromSnapping() {
        auto guide = makeGuide(GuideType::VanishingPoint, {{50, 50}});
        const QRectF visible(-400, -300, 800, 600);
        guide.limited = true; guide.area = {0, 0, 100, 100};
        auto path = guidePath(guide, visible); QVERIFY(!path.isEmpty()); QVERIFY(bounded(path, guide.area));
        QVERIFY(guideSnapLines(guide, {20, 20}).size() == 1);
        QVERIFY(guideSnapLines(guide, {-20, 20}).isEmpty());
        QVERIFY(guidePath(guide, {300, 300, 100, 100}).isEmpty());
        guide.snapping = false; QVERIFY(guideSnapLines(guide, {20, 20}).isEmpty()); QVERIFY(!guidePath(guide, visible).isEmpty());
        guide.snapping = true; guide.visible = false;
        QVERIFY(guidePath(guide, visible).isEmpty()); QCOMPARE(guideSnapLines(guide, {20, 20}).size(), 1);
        guide.visible = true;
        QVERIFY(guidePath(guide, {0, 0, std::numeric_limits<qreal>::infinity(), 100}).isEmpty());
    }

    void geometryRemainsBoundedWithDistantHandlesAndExtremeDensity() {
        const QRectF viewport(-800, -500, 1600, 1000);
        QVector<DrawingGuide> guides{
            makeGuide(GuideType::VanishingPoint, {{1e6, -1e6}}),
            makeGuide(GuideType::VanishingPoint, {{1e100, -1e100}}),
            makeGuide(GuideType::TwoPoint, {{-1e6, -200}, {1e6, 200}, {0, 500}}),
            makeGuide(GuideType::ParallelRuler, {{999990, 1e6}, {1e6, 1000005}}),
            makeGuide(GuideType::Perspective, {{-1e6, -1e6}, {1e6, -8e5}, {9e5, 1e6}, {-9e5, 8e5}})};
        for (auto &guide : guides) {
            guide.subdivisions = std::numeric_limits<int>::max();
            QVERIFY(validGuideGeometry(guide));
            const auto path = guidePath(guide, viewport);
            QVERIFY(bounded(path, viewport)); QVERIFY(path.elementCount() <= 600);
        }
        QElapsedTimer timer; timer.start();
        for (int i = 0; i < 1000; ++i) {
            const auto &guide = guides[i % guides.size()];
            QVERIFY(guidePath(guide, viewport).elementCount() <= 600);
        }
        QVERIFY2(timer.elapsed() < 5000, "A thousand bounded guide paths must not walk offscreen coordinates.");
    }

    void threePointUsesThreeIndependentVanishingPointsAndRejectsCollapsedPlanes() {
        auto guide = makeGuide(GuideType::ThreePoint, {{-600, 80}, {1200, 180}, {340, -900}, {310, 470}});
        QVERIFY(validGuideGeometry(guide));
        const QPointF start(400, 360);
        const auto lines = guideSnapLines(guide, start);
        const auto constraints = guideSnapConstraints(guide, start);
        QCOMPARE(lines.size(), 3); QCOMPARE(constraints.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(guideLineDistance(guide.points[i], lines[i]) < 1e-6);
            QVERIFY(guideLineDistance(start, lines[i]) < 1e-6);
            QCOMPARE(constraints[i].kind, GuideConstraint::Kind::Line);
        }
        const QRectF viewport(-200, -200, 1200, 1000);
        guide.subdivisions = 64;
        const auto path = guidePath(guide, viewport);
        QVERIFY(!path.isEmpty()); QVERIFY(bounded(path, viewport)); QVERIFY(path.elementCount() <= 780);
        for (int i = 1; i < path.elementCount(); i += 2) {
            const auto a = path.elementAt(i - 1), b = path.elementAt(i);
            const QLineF line({a.x, a.y}, {b.x, b.y});
            QVERIFY(std::min({guideLineDistance(guide.points[0], line), guideLineDistance(guide.points[1], line),
                              guideLineDistance(guide.points[2], line)}) < 1e-5);
        }
        auto invalid = guide; invalid.points[2] = (guide.points[0] + guide.points[1]) / 2;
        QVERIFY(!validGuideGeometry(invalid)); QVERIFY(guidePath(invalid, viewport).isEmpty());
        QVERIFY(guideSnapConstraints(invalid, start).isEmpty());
        for (int i = 0; i < 3; ++i) {
            invalid = guide; invalid.points[3] = (guide.points[i] + guide.points[(i + 1) % 3]) / 2;
            QVERIFY(!validGuideGeometry(invalid));
        }
    }

    void fisheyeCreatesCurvedFamiliesThroughTheStrokeOriginAndStraightCenterAxes() {
        auto guide = makeGuide(GuideType::Fisheye, {{0, 200}, {400, 200}, {250, 300}});
        QVERIFY(validGuideGeometry(guide)); // A radius handle can also have an along-axis component.
        const QPointF start(260, 170);
        const auto constraints = guideSnapConstraints(guide, start);
        QCOMPARE(constraints.size(), 2);
        QVERIFY(guideSnapLines(guide, start).isEmpty());
        for (const auto &constraint : constraints) {
            QCOMPARE(constraint.kind, GuideConstraint::Kind::Ellipse);
            QVERIFY(guideConstraintDistance(start, constraint) < 1e-6);
            QCOMPARE(constraint.center, QPointF(200, 200));
        }
        QCOMPARE(constraints[0].axisX, QPointF(200, 0));
        QCOMPARE(constraints[1].axisY, QPointF(0, 100));
        QVERIFY(QLineF({}, constraints[0].axisY).length() < 100);
        QVERIFY(QLineF({}, constraints[1].axisX).length() < 200);
        const auto middle = guideSnapConstraints(guide, {200, 200});
        QCOMPARE(middle.size(), 2);
        for (const auto &constraint : middle) QCOMPARE(constraint.kind, GuideConstraint::Kind::Line);
        QVERIFY(near(projectOntoGuideConstraint({275, 215}, middle[0]), {275, 200}));
        QVERIFY(near(projectOntoGuideConstraint({275, 215}, middle[1]), {200, 215}));
        QVERIFY(guideSnapConstraints(guide, {410, 200}).isEmpty());
        // Boundary families coincide; they are returned once, including at endpoints.
        QCOMPARE(guideSnapConstraints(guide, {400, 200}).size(), 1);
        QCOMPARE(guideSnapConstraints(guide, {320, 280}).size(), 1);
        guide.visible = false; QCOMPARE(guideSnapConstraints(guide, start).size(), 2);
        guide.snapping = false; QVERIFY(guideSnapConstraints(guide, start).isEmpty());
        guide.snapping = true; guide.limited = true; guide.area = {0, 0, 100, 100};
        QVERIFY(guideSnapConstraints(guide, start).isEmpty());
        guide.limited = false; guide.points[2] = {170, 200}; QVERIFY(!validGuideGeometry(guide));
        guide.points[2] = {200, 300}; guide.points[1] = guide.points[0]; QVERIFY(!validGuideGeometry(guide));
    }

    void ellipseProjectionIsNearestAndRotationIndependent() {
        const qreal angle = .63;
        const QPointF unitX(std::cos(angle), std::sin(angle)), unitY(-std::sin(angle), std::cos(angle));
        GuideConstraint constraint; constraint.kind = GuideConstraint::Kind::Ellipse;
        constraint.center = {340, 230}; constraint.axisX = unitX * 240; constraint.axisY = unitY * 70;
        for (const auto point : {QPointF(640, 650), QPointF(370, 180), constraint.center,
                                 constraint.center + unitX * 90, constraint.center + unitY * 10}) {
            const auto projected = projectOntoGuideConstraint(point, constraint);
            const auto delta = projected - constraint.center;
            const qreal x = QPointF::dotProduct(delta, unitX) / 240, y = QPointF::dotProduct(delta, unitY) / 70;
            QVERIFY(std::abs(x * x + y * y - 1) < 1e-8);
            qreal closest = std::numeric_limits<qreal>::infinity();
            for (int i = 0; i < 10000; ++i) {
                const qreal t = i * 2 * std::acos(-1.0) / 10000;
                closest = std::min(closest, QLineF(point, constraint.center + constraint.axisX * std::cos(t) + constraint.axisY * std::sin(t)).length());
            }
            QVERIFY(guideConstraintDistance(point, constraint) <= closest + 1e-6);
            QVERIFY(near(projectOntoGuideConstraint(projected, constraint), projected));
        }
        auto rotated = makeGuide(GuideType::Fisheye, {constraint.center - constraint.axisX,
            constraint.center + constraint.axisX, constraint.center + constraint.axisY + unitX * 100});
        const auto start = constraint.center + constraint.axisX * .3 + constraint.axisY * .4;
        const auto families = guideSnapConstraints(rotated, start); QCOMPARE(families.size(), 2);
        for (const auto &family : families) QVERIFY(guideConstraintDistance(start, family) < 1e-6);
        GuideConstraint invalid = constraint; invalid.axisY = invalid.axisX;
        QCOMPARE(projectOntoGuideConstraint(start, invalid), start);
        QVERIFY(std::isinf(guideConstraintDistance(start, invalid)));
    }

    void fixedEllipseIsOneRotatedCurveIndependentOfStrokeStart() {
        const qreal rotation = .71; const QPointF axis(std::cos(rotation) * 210, std::sin(rotation) * 210);
        const QPointF normal(-std::sin(rotation) * 23, std::cos(rotation) * 23), center(330, 240);
        auto guide = makeGuide(GuideType::Ellipse, {center - axis, center + axis, center + normal + axis * .2});
        QVERIFY(validGuideGeometry(guide));
        const auto first = guideSnapConstraints(guide, center + axis), second = guideSnapConstraints(guide, center + normal);
        QCOMPARE(first.size(), 1); QCOMPARE(second.size(), 1); QCOMPARE(first[0].kind, GuideConstraint::Kind::Ellipse);
        QCOMPARE(first[0].center, center); QVERIFY(near(first[0].axisX, axis)); QVERIFY(near(first[0].axisY, normal));
        QCOMPARE(first[0].axisX, second[0].axisX); QCOMPARE(first[0].axisY, second[0].axisY);
        const auto path = guidePath(guide, {0, 0, 700, 500}, .1); QVERIFY(!path.isEmpty());
        for (int i = 0; i < path.elementCount(); ++i) {
            const auto point = path.elementAt(i); QVERIFY(guideConstraintDistance({point.x, point.y}, first[0]) < 1e-6);
        }
        guide.visible = false; QVERIFY(guidePath(guide, {0, 0, 700, 500}).isEmpty());
        QCOMPARE(guideSnapConstraints(guide, center + axis).size(), 1);
        guide.snapping = false; QVERIFY(guideSnapConstraints(guide, center + axis).isEmpty());
        guide.snapping = true; guide.limited = true; guide.area = {0, 0, 10, 10};
        QVERIFY(guideSnapConstraints(guide, center).isEmpty()); guide.limited = false;
        guide.points[2] = center + axis; QVERIFY(!validGuideGeometry(guide));
        guide.points[2] = center + normal; guide.points[1] = guide.points[0]; QVERIFY(!validGuideGeometry(guide));
    }

    void unwrappedEllipseArcsRetainDirectionAcrossTwoRevolutions() {
        GuideConstraint ellipse; ellipse.kind = GuideConstraint::Kind::Ellipse;
        ellipse.center = {300, 240}; ellipse.axisX = {180, 90}; ellipse.axisY = {-18, 36};
        const qreal pi = std::acos(-1.0), step = pi / 18;
        for (const int direction : {-1, 1}) {
            qreal previous = 170 * pi / 180;
            for (int i = 1; i <= 72; ++i) {
                const auto next = guideEllipseParameter(ellipse, guideEllipsePoint(ellipse, previous + direction * step));
                QVERIFY(next.has_value()); const auto unwrapped = unwrapGuideEllipseAngle(*next, previous);
                QVERIFY(std::abs(unwrapped - previous - direction * step) < 1e-8);
                const auto samples = guideEllipseArcSamples(ellipse, previous, unwrapped, .05);
                QVERIFY(!samples.isEmpty()); QVERIFY(samples.size() <= 512);
                auto from = guideEllipsePoint(ellipse, previous);
                for (const auto point : samples) {
                    QVERIFY(guideConstraintDistance(point, ellipse) < 1e-6);
                    QVERIFY(guideConstraintDistance((from + point) / 2, ellipse) <= .050001); from = point;
                }
                previous = unwrapped;
            }
            QVERIFY(std::abs(previous - 170 * pi / 180 - direction * 4 * pi) < 1e-7);
        }
        QVERIFY(!guideEllipseParameter(ellipse, ellipse.center));
        const auto fullLoop = guideEllipseArcSamples(ellipse, 0, 2 * pi, .1);
        QVERIFY(fullLoop.size() > 20); QVERIFY(near(fullLoop.last(), guideEllipsePoint(ellipse, 0)));
        ellipse.axisX *= 10000; ellipse.axisY *= 10000;
        QVERIFY(guideEllipseArcSamples(ellipse, 0, 2 * pi, 1e-8).size() <= 512);
    }

    void ellipseProjectionCommutesWithRotatedMirroredUniformView() {
        GuideConstraint ellipse; ellipse.kind = GuideConstraint::Kind::Ellipse;
        ellipse.center = {210, 180}; ellipse.axisX = {220, 110}; ellipse.axisY = {-9, 18};
        QTransform view; view.translate(870, 320); view.rotate(37); view.scale(-2.5, 2.5);
        auto displayed = ellipse; displayed.center = view.map(ellipse.center);
        displayed.axisX = view.map(ellipse.center + ellipse.axisX) - displayed.center;
        displayed.axisY = view.map(ellipse.center + ellipse.axisY) - displayed.center;
        for (const auto point : {QPointF(320, 260), QPointF(120, 90), ellipse.center + ellipse.axisY * .3})
            QVERIFY(near(view.map(projectOntoGuideConstraint(point, ellipse)), projectOntoGuideConstraint(view.map(point), displayed), 1e-5));
        const auto centerProjection = projectOntoGuideConstraint(ellipse.center, ellipse);
        QVERIFY(std::isfinite(centerProjection.x())); QVERIFY(std::isfinite(centerProjection.y()));
        QVERIFY(std::abs(QLineF(centerProjection, ellipse.center).length() - QLineF({}, ellipse.axisY).length()) < 1e-6);
    }

    void ellipseSamplesFollowShortestCurveInsteadOfCuttingAcrossIt() {
        GuideConstraint constraint; constraint.kind = GuideConstraint::Kind::Ellipse;
        constraint.center = {300, 300}; constraint.axisX = {200, 0}; constraint.axisY = {0, 100};
        const QPointF first(500, 300), last(300, 400);
        const auto points = guideConstraintSamples(constraint, first, last, .1);
        QVERIFY(points.size() > 4); QVERIFY(points.size() <= 512); QVERIFY(near(points.last(), last));
        QPointF previous = first;
        for (auto point : points) {
            QVERIFY(point.x() <= previous.x() + 1e-6); QVERIFY(point.y() >= previous.y() - 1e-6);
            QVERIFY(guideConstraintDistance(point, constraint) < 1e-6);
            QVERIFY(guideConstraintDistance((point + previous) / 2, constraint) <= .100001);
            previous = point;
        }
        const qreal degrees = std::acos(-1.0) / 180;
        const auto at = [&](qreal a) { return constraint.center + constraint.axisX * std::cos(a * degrees) + constraint.axisY * std::sin(a * degrees); };
        const auto aroundSeam = guideConstraintSamples(constraint, at(175), at(-175), .1);
        QVERIFY(aroundSeam.size() < points.size());
        for (auto point : aroundSeam) QVERIFY(point.x() < 110);
        constraint.axisX *= 5000; constraint.axisY *= 5000;
        QVERIFY(guideConstraintSamples(constraint, constraint.center + constraint.axisX, constraint.center - constraint.axisX, 1e-6).size() <= 512);
        GuideConstraint line; line.line = {{0, 10}, {100, 10}};
        QCOMPARE(guideConstraintSamples(line, {0, 10}, {200, 50}), QVector<QPointF>({{200, 10}}));
    }

    void fisheyeFlatteningClipsEverySegmentAndBoundsWorkAtDistantCoordinates() {
        auto guide = makeGuide(GuideType::Fisheye, {{0, 200}, {400, 200}, {200, 300}});
        guide.subdivisions = 8;
        const QRectF viewport(100, 130, 200, 140);
        const auto path = guidePath(guide, viewport, .2);
        QVERIFY(!path.isEmpty()); QVERIFY(bounded(path, viewport));
        for (int i = 0; i < path.elementCount(); ++i)
            QCOMPARE(path.elementAt(i).type, i % 2 ? QPainterPath::LineToElement : QPainterPath::MoveToElement);
        guide.limited = true; guide.area = {180, 140, 40, 100};
        QVERIFY(bounded(guidePath(guide, viewport), guide.area));
        guide.limited = false; guide.subdivisions = std::numeric_limits<int>::max();
        guide.points = {{-1e6, 0}, {1e6, 0}, {0, 1e6}};
        const QRectF distantViewport(-800, -500, 1600, 1000);
        QVERIFY(validGuideGeometry(guide));
        const auto distant = guidePath(guide, distantViewport, .1);
        QVERIFY(!distant.isEmpty()); QVERIFY(bounded(distant, distantViewport));
        QVERIFY(distant.elementCount() <= 2 * 127 * 512 + 4);
        // Huge offscreen loops are clipped in parameter space before flattening.
        QVERIFY(distant.elementCount() < 1000);
        const auto constraint = guideSnapConstraints(guide, {300, 200}).first();
        const auto preview = guideConstraintPath(constraint, distantViewport, .1);
        QVERIFY(!preview.isEmpty()); QVERIFY(bounded(preview, distantViewport));
        guide.visible = false; QVERIFY(guidePath(guide, distantViewport).isEmpty());
    }
};

QTEST_APPLESS_MAIN(GuideGeometryTest)
#include "test_guide_geometry.moc"
