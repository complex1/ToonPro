#include "guide_geometry.h"
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace toon {
namespace {
constexpr long double pointEpsilon = 1e-6L;
bool finite(QPointF point) { return std::isfinite(point.x()) && std::isfinite(point.y()); }
bool finiteRect(QRectF rect) {
    return finite(rect.topLeft()) && std::isfinite(rect.width()) && std::isfinite(rect.height()) &&
        finite(rect.bottomRight()) && rect.width() > 0 && rect.height() > 0;
}
struct Direction { long double x = 0, y = 0; };
Direction difference(QPointF to, QPointF from) {
    return {static_cast<long double>(to.x()) - from.x(), static_cast<long double>(to.y()) - from.y()};
}
long double length(Direction value) { return std::hypot(value.x, value.y); }
long double cross(Direction a, Direction b) { return a.x * b.y - a.y * b.x; }
bool normalize(Direction &direction) {
    const auto magnitude = length(direction);
    if (!std::isfinite(magnitude) || magnitude <= pointEpsilon) return false;
    direction.x /= magnitude; direction.y /= magnitude; return true;
}
bool inQuad(const QVector<QPointF> &points, QPointF point) {
    int sign = 0;
    for (int i = 0; i < 4; ++i) {
        auto edge = difference(points[(i + 1) % 4], points[i]);
        if (!normalize(edge)) return false;
        const auto side = cross(edge, difference(point, points[i]));
        if (std::abs(side) <= pointEpsilon) continue;
        const int current = side > 0 ? 1 : -1;
        if (sign && current != sign) return false;
        sign = current;
    }
    return true;
}
bool quadTransform(const QVector<QPointF> &points, QTransform *transform) {
    static const QPolygonF unit{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)};
    if (!QTransform::quadToQuad(unit, QPolygonF(points), *transform)) return false;
    for (const auto value : {transform->m11(), transform->m12(), transform->m13(),
                            transform->m21(), transform->m22(), transform->m23(),
                            transform->m31(), transform->m32(), transform->m33()}) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

// Slab clipping uses long double intermediates so distant vanishing points never
// create huge paths or require walking thousands of offscreen grid segments.
void addClipped(QPainterPath &path, QPointF origin, Direction direction, QRectF bounds,
                bool segment = false) {
    const auto magnitude = length(direction);
    if (!finite(origin) || !normalize(direction)) return;
    long double first = segment ? 0 : -std::numeric_limits<long double>::infinity();
    long double last = segment ? magnitude : std::numeric_limits<long double>::infinity();
    const auto clipAxis = [&](long double position, long double component, long double low, long double high) {
        if (component == 0) return position >= low && position <= high;
        auto start = (low - position) / component, end = (high - position) / component;
        if (start > end) std::swap(start, end);
        first = std::max(first, start); last = std::min(last, end);
        return first <= last;
    };
    if (!clipAxis(origin.x(), direction.x, bounds.left(), bounds.right()) ||
        !clipAxis(origin.y(), direction.y, bounds.top(), bounds.bottom()) ||
        !std::isfinite(first) || !std::isfinite(last)) return;
    const auto at = [&](long double distance) {
        return QPointF(qreal(std::clamp(static_cast<long double>(origin.x()) + distance * direction.x,
                                       static_cast<long double>(bounds.left()), static_cast<long double>(bounds.right()))),
                       qreal(std::clamp(static_cast<long double>(origin.y()) + distance * direction.y,
                                       static_cast<long double>(bounds.top()), static_cast<long double>(bounds.bottom()))));
    };
    const QPointF a = at(first), b = at(last);
    if (!finite(a) || !finite(b) || length(difference(b, a)) <= pointEpsilon) return;
    path.moveTo(a); path.lineTo(b);
}
void addLine(QPainterPath &path, QPointF a, QPointF b, QRectF bounds, bool segment = false) {
    addClipped(path, a, difference(b, a), bounds, segment);
}
void addGrid(QPainterPath &path, const QTransform &transform, int count, QRectF bounds, bool segment) {
    for (int i = 0; i <= count; ++i) {
        const qreal position = qreal(i) / count;
        addLine(path, transform.map(QPointF(position, 0)), transform.map(QPointF(position, 1)), bounds, segment);
        addLine(path, transform.map(QPointF(0, position)), transform.map(QPointF(1, position)), bounds, segment);
    }
}
void addSnapLine(QVector<QLineF> &lines, QPointF start, Direction direction) {
    if (!normalize(direction)) return;
    // Endpoint separation remains useful to callers even at the largest valid
    // document coordinates. The constraint itself is an infinite line.
    const QPointF end = start + QPointF(qreal(direction.x * 1024), qreal(direction.y * 1024));
    if (!finite(end) || end == start) return;
    for (const auto &line : lines) {
        auto other = difference(line.p2(), line.p1());
        if (normalize(other) && std::abs(cross(direction, other)) < 1e-10L) return;
    }
    lines.append(QLineF(start, end));
}
bool offLine(QPointF a, QPointF b, QPointF point) {
    auto direction = difference(b, a), displacement = difference(point, a);
    return normalize(direction) && length(displacement) > pointEpsilon &&
        std::abs(cross(direction, displacement)) > std::max(pointEpsilon, length(displacement) * 1e-8L);
}
void addPerspectivePlane(QPainterPath &path, QPointF anchor, QPointF left, QPointF right,
                         int count, QRectF bounds) {
    constexpr qreal step = .6;
    const QVector<QPointF> quad{anchor, anchor + (left - anchor) * step,
        anchor + ((left - anchor) + (right - anchor)) * (step / (1 + step)),
        anchor + (right - anchor) * step};
    QTransform transform;
    if (quadTransform(quad, &transform)) addGrid(path, transform, count, bounds, false);
}
struct Ellipse {
    QPointF center;
    Direction x, y;
    long double a = 0, b = 0;
};
bool ellipseGeometry(const GuideConstraint &constraint, Ellipse *ellipse) {
    if (constraint.kind != GuideConstraint::Kind::Ellipse || !finite(constraint.center) ||
        !finite(constraint.axisX) || !finite(constraint.axisY)) return false;
    ellipse->center = constraint.center;
    ellipse->x = {constraint.axisX.x(), constraint.axisX.y()};
    ellipse->y = {constraint.axisY.x(), constraint.axisY.y()};
    ellipse->a = length(ellipse->x); ellipse->b = length(ellipse->y);
    return normalize(ellipse->x) && normalize(ellipse->y) &&
        std::abs(ellipse->x.x * ellipse->y.x + ellipse->x.y * ellipse->y.y) < 1e-8L;
}
GuideConstraint fisheyeEllipse(const DrawingGuide &guide) {
    GuideConstraint constraint;
    constraint.kind = GuideConstraint::Kind::Ellipse;
    if (guide.points.size() != 3) return constraint;
    constraint.center = guide.points[0] / 2 + guide.points[1] / 2;
    const auto axis = difference(guide.points[1], guide.points[0]);
    constraint.axisX = QPointF(qreal(axis.x / 2), qreal(axis.y / 2));
    Direction normal{-axis.y, axis.x};
    if (!normalize(normal)) return constraint;
    const auto handle = difference(guide.points[2], constraint.center);
    const auto radius = std::abs(handle.x * normal.x + handle.y * normal.y);
    constraint.axisY = QPointF(qreal(normal.x * radius), qreal(normal.y * radius));
    return constraint;
}
QPointF ellipsePoint(const Ellipse &ellipse, long double angle) {
    const auto x = ellipse.a * std::cos(angle), y = ellipse.b * std::sin(angle);
    return {qreal(ellipse.center.x() + ellipse.x.x * x + ellipse.y.x * y),
            qreal(ellipse.center.y() + ellipse.x.y * x + ellipse.y.y * y)};
}
Direction ellipseLocal(const Ellipse &ellipse, QPointF point) {
    const auto offset = difference(point, ellipse.center);
    return {(offset.x * ellipse.x.x + offset.y * ellipse.x.y) / ellipse.a,
            (offset.x * ellipse.y.x + offset.y * ellipse.y.y) / ellipse.b};
}
long double ellipseAngle(const Ellipse &ellipse, QPointF point) {
    const auto local = ellipseLocal(ellipse, point);
    return std::atan2(local.y, local.x);
}
constexpr long double tau = 2 * std::numbers::pi_v<long double>;
long double angularStep(const Ellipse &ellipse, qreal tolerance) {
    const long double error = std::isfinite(tolerance) && tolerance > 0 ? tolerance : .5;
    // A circle with the larger radius bounds this ellipse's chord error.
    return std::min(std::numbers::pi_v<long double> / 4,
                    std::sqrt(8 * error / std::max(ellipse.a, ellipse.b)));
}
void addEllipse(QPainterPath &path, const GuideConstraint &constraint, QRectF bounds, qreal tolerance) {
    Ellipse ellipse;
    if (!ellipseGeometry(constraint, &ellipse)) return;
    QVector<long double> crossings{0, tau};
    const auto crossBoundary = [&](long double cosine, long double sine, long double value) {
        const auto radius = std::hypot(cosine, sine);
        if (radius <= pointEpsilon || std::abs(value) > radius) return;
        const auto phase = std::atan2(sine, cosine), angle = std::acos(std::clamp(value / radius, -1.0L, 1.0L));
        for (auto root : {phase - angle, phase + angle}) {
            root = std::fmod(root, tau);
            if (root < 0) root += tau;
            crossings.append(root);
        }
    };
    // Clip in parameter space before flattening. Very distant or large ellipses
    // only allocate segments for their visible arcs, not the offscreen loops.
    for (auto edge : {bounds.left(), bounds.right()})
        crossBoundary(ellipse.a * ellipse.x.x, ellipse.b * ellipse.y.x, static_cast<long double>(edge) - ellipse.center.x());
    for (auto edge : {bounds.top(), bounds.bottom()})
        crossBoundary(ellipse.a * ellipse.x.y, ellipse.b * ellipse.y.y, static_cast<long double>(edge) - ellipse.center.y());
    std::sort(crossings.begin(), crossings.end());
    QVector<QPair<long double, long double>> intervals;
    long double totalAngle = 0;
    for (qsizetype i = 1; i < crossings.size(); ++i) {
        const auto first = crossings[i - 1], last = crossings[i];
        if (last - first < 1e-14L || !bounds.contains(ellipsePoint(ellipse, (first + last) / 2))) continue;
        intervals.append({first, last}); totalAngle += last - first;
    }
    if (intervals.isEmpty()) return;
    const auto step = std::max(angularStep(ellipse, tolerance), totalAngle / (512 - intervals.size()));
    for (const auto &[first, last] : intervals) {
        const int segments = std::clamp(int(std::ceil((last - first) / step)), 1, 512);
        QPointF previous = ellipsePoint(ellipse, first);
        for (int i = 1; i <= segments; ++i) {
            const auto next = ellipsePoint(ellipse, first + (last - first) * i / segments);
            addLine(path, previous, next, bounds, true); previous = next;
        }
    }
}
} // namespace

bool validGuideGeometry(const DrawingGuide &guide) {
    int expected = 0;
    switch (guide.type) {
    case GuideType::Perspective: expected = 4; break;
    case GuideType::VanishingPoint: expected = 1; break;
    case GuideType::TwoPoint: expected = 3; break;
    case GuideType::ParallelRuler: expected = 2; break;
    case GuideType::ThreePoint: expected = 4; break;
    case GuideType::Fisheye: expected = 3; break;
    case GuideType::Ellipse: expected = 3; break;
    default: return false;
    }
    if (guide.points.size() != expected || (guide.limited && !finiteRect(guide.area))) return false;
    for (auto point : guide.points) if (!finite(point)) return false;
    if (guide.type == GuideType::Perspective) {
        int sign = 0;
        for (int i = 0; i < 4; ++i) {
            auto a = difference(guide.points[(i + 1) % 4], guide.points[i]);
            auto b = difference(guide.points[(i + 2) % 4], guide.points[(i + 1) % 4]);
            if (!normalize(a) || !normalize(b)) return false;
            const auto turn = cross(a, b);
            if (std::abs(turn) <= 1e-8L) return false;
            const int current = turn > 0 ? 1 : -1;
            if (sign && current != sign) return false;
            sign = current;
        }
        QTransform transform;
        return quadTransform(guide.points, &transform);
    }
    if (guide.type == GuideType::TwoPoint) {
        return offLine(guide.points[0], guide.points[1], guide.points[2]);
    }
    if (guide.type == GuideType::ThreePoint) {
        if (!offLine(guide.points[0], guide.points[1], guide.points[2])) return false;
        for (int i = 0; i < 3; ++i)
            if (!offLine(guide.points[i], guide.points[(i + 1) % 3], guide.points[3])) return false;
    }
    if (guide.type == GuideType::Fisheye || guide.type == GuideType::Ellipse) {
        Ellipse ellipse;
        return ellipseGeometry(fisheyeEllipse(guide), &ellipse);
    }
    return guide.type != GuideType::ParallelRuler || length(difference(guide.points[1], guide.points[0])) > pointEpsilon;
}

QPainterPath guidePath(const DrawingGuide &guide, const QRectF &visibleBounds, qreal detailTolerance) {
    QPainterPath path;
    if (!guide.visible || !finiteRect(visibleBounds) || !validGuideGeometry(guide)) return path;
    const QRectF bounds = guide.limited ? visibleBounds.intersected(guide.area) : visibleBounds;
    if (!finiteRect(bounds)) return path;
    const int count = std::clamp(guide.subdivisions, 1, 64);
    switch (guide.type) {
    case GuideType::Perspective: {
        QTransform transform;
        if (quadTransform(guide.points, &transform)) addGrid(path, transform, count, bounds, true);
        break;
    }
    case GuideType::VanishingPoint:
        for (int i = 0; i < 2 * count; ++i) {
            const qreal angle = std::numbers::pi_v<qreal> * i / (2 * count);
            addClipped(path, guide.points[0], {std::cos(angle), std::sin(angle)}, bounds);
        }
        break;
    case GuideType::TwoPoint: {
        const auto a = guide.points[2], left = guide.points[0], right = guide.points[1];
        // This plane's two projective vanishing points are exactly the supplied
        // handles. Its near corner is the anchor; equal unit-grid steps produce
        // perspective-correct, rather than equal-angle, converging grid lines.
        addPerspectivePlane(path, a, left, right, count, bounds);
        addLine(path, left, right, bounds);
        if (guide.vertical) {
            const auto horizon = difference(right, left);
            addClipped(path, a, {-horizon.y, horizon.x}, bounds);
        }
        break;
    }
    case GuideType::ThreePoint:
        for (int i = 0; i < 3; ++i)
            addPerspectivePlane(path, guide.points[3], guide.points[i], guide.points[(i + 1) % 3], count, bounds);
        break;
    case GuideType::Fisheye: {
        const auto outer = fisheyeEllipse(guide);
        addLine(path, outer.center - outer.axisX, outer.center + outer.axisX, bounds, true);
        addLine(path, outer.center - outer.axisY, outer.center + outer.axisY, bounds, true);
        for (int i = 1; i <= count; ++i) {
            auto horizontal = outer; horizontal.axisY *= qreal(i) / count;
            addEllipse(path, horizontal, bounds, detailTolerance);
            if (i != count) {
                auto vertical = outer; vertical.axisX *= qreal(i) / count;
                addEllipse(path, vertical, bounds, detailTolerance);
            }
        }
        break;
    }
    case GuideType::Ellipse:
        addEllipse(path, fisheyeEllipse(guide), bounds, detailTolerance);
        break;
    case GuideType::ParallelRuler: {
        auto direction = difference(guide.points[1], guide.points[0]);
        const auto rulerLength = length(direction);
        if (!normalize(direction)) break;
        const Direction normal{-direction.y, direction.x};
        const auto offset = [&](QPointF point) { return normal.x * point.x() + normal.y * point.y(); };
        const auto originOffset = offset(guide.points[0]);
        const auto offsets = {offset(bounds.topLeft()), offset(bounds.topRight()), offset(bounds.bottomLeft()), offset(bounds.bottomRight())};
        const auto [minimum, maximum] = std::minmax_element(offsets.begin(), offsets.end());
        const long double baseSpacing = std::max(1.0L, rulerLength / count);
        // Zooming far out subsamples the same anchored family. Work is bounded
        // by density, independent of the canvas size or ruler's distance away.
        const auto stride = std::max(1.0L, std::ceil((*maximum - *minimum) / (baseSpacing * 4 * count)));
        const auto spacing = baseSpacing * stride;
        auto remainder = std::fmod(originOffset - *minimum, spacing);
        if (remainder < 0) remainder += spacing;
        const auto first = *minimum + remainder;
        for (int i = 0; i <= 4 * count; ++i) {
            const auto at = first + i * spacing;
            if (at > *maximum + pointEpsilon) break;
            // The closest point on this line to the origin avoids enormous
            // coordinates when a ruler handle has been placed far off canvas.
            addClipped(path, QPointF(qreal(normal.x * at), qreal(normal.y * at)), direction, bounds);
        }
        break;
    }
    }
    return path;
}

QVector<QLineF> guideSnapLines(const DrawingGuide &guide, QPointF strokeStart) {
    QVector<QLineF> lines;
    if (!guide.snapping || !finite(strokeStart) || !validGuideGeometry(guide) ||
        (guide.limited && !guide.area.contains(strokeStart))) return lines;
    switch (guide.type) {
    case GuideType::Perspective: {
        if (!inQuad(guide.points, strokeStart)) break;
        QTransform transform;
        if (!quadTransform(guide.points, &transform)) break;
        // Homogeneous vanishing points produce the two local axis directions
        // without inverting a projective transform near its horizon.
        addSnapLine(lines, strokeStart, {transform.m11() - strokeStart.x() * transform.m13(),
                                        transform.m12() - strokeStart.y() * transform.m13()});
        addSnapLine(lines, strokeStart, {transform.m21() - strokeStart.x() * transform.m23(),
                                        transform.m22() - strokeStart.y() * transform.m23()});
        break;
    }
    case GuideType::VanishingPoint:
        addSnapLine(lines, strokeStart, difference(guide.points[0], strokeStart));
        break;
    case GuideType::TwoPoint: {
        addSnapLine(lines, strokeStart, difference(guide.points[0], strokeStart));
        addSnapLine(lines, strokeStart, difference(guide.points[1], strokeStart));
        if (guide.vertical) {
            const auto horizon = difference(guide.points[1], guide.points[0]);
            addSnapLine(lines, strokeStart, {-horizon.y, horizon.x});
        }
        break;
    }
    case GuideType::ParallelRuler:
        addSnapLine(lines, strokeStart, difference(guide.points[1], guide.points[0]));
        break;
    case GuideType::ThreePoint:
        for (int i = 0; i < 3; ++i) addSnapLine(lines, strokeStart, difference(guide.points[i], strokeStart));
        break;
    case GuideType::Fisheye: case GuideType::Ellipse: break; // Curved constraints are exposed by guideSnapConstraints.
    }
    return lines;
}

QPointF projectOntoGuideLine(QPointF point, const QLineF &line) {
    if (!finite(point) || !finite(line.p1()) || !finite(line.p2())) return point;
    auto direction = difference(line.p2(), line.p1());
    if (!normalize(direction)) return point;
    // Subtract the line's normal offset, rather than adding a huge longitudinal
    // displacement to a distant endpoint. This retains the local point's
    // precision even when the line endpoints are far outside the artwork.
    const auto offset = direction.x * line.y1() - direction.y * line.x1();
    const auto distance = direction.x * point.y() - direction.y * point.x() - offset;
    const QPointF result(qreal(point.x() + distance * direction.y), qreal(point.y() - distance * direction.x));
    return finite(result) ? result : point;
}

qreal guideLineDistance(QPointF point, const QLineF &line) {
    if (!finite(point) || !finite(line.p1()) || !finite(line.p2())) return std::numeric_limits<qreal>::infinity();
    auto direction = difference(line.p2(), line.p1());
    if (!normalize(direction)) return std::numeric_limits<qreal>::infinity();
    const auto offset = direction.x * line.y1() - direction.y * line.x1();
    return qreal(std::abs(direction.x * point.y() - direction.y * point.x() - offset));
}

QVector<GuideConstraint> guideSnapConstraints(const DrawingGuide &guide, QPointF strokeStart) {
    QVector<GuideConstraint> constraints;
    if (guide.type == GuideType::Ellipse) {
        if (guide.snapping && finite(strokeStart) && validGuideGeometry(guide) &&
            (!guide.limited || guide.area.contains(strokeStart))) constraints.append(fisheyeEllipse(guide));
        return constraints;
    }
    if (guide.type != GuideType::Fisheye) {
        for (const auto &line : guideSnapLines(guide, strokeStart)) {
            GuideConstraint constraint; constraint.line = line; constraints.append(constraint);
        }
        return constraints;
    }
    if (!guide.snapping || !finite(strokeStart) || !validGuideGeometry(guide) ||
        (guide.limited && !guide.area.contains(strokeStart))) return constraints;
    const auto outer = fisheyeEllipse(guide);
    Ellipse ellipse;
    if (!ellipseGeometry(outer, &ellipse)) return constraints;
    const auto local = ellipseLocal(ellipse, strokeStart);
    if (local.x * local.x + local.y * local.y > 1 + 1e-9L) return constraints;
    const auto addFamily = [&](bool horizontal) {
        const auto along = horizontal ? local.x : local.y;
        const auto across = horizontal ? local.y : local.x;
        auto constraint = outer;
        if (std::abs(across) <= 1e-10L && std::abs(along) < 1 - 1e-10L) {
            constraint.kind = GuideConstraint::Kind::Line;
            const auto axis = horizontal ? outer.axisX : outer.axisY;
            constraint.line = QLineF(outer.center - axis, outer.center + axis);
        } else {
            const auto denominator = std::sqrt(std::max(0.0L, 1 - along * along));
            const qreal ratio = denominator <= 1e-10L ? 1 : qreal(std::clamp(std::abs(across) / denominator, 0.0L, 1.0L));
            if (horizontal) constraint.axisY *= ratio;
            else constraint.axisX *= ratio;
            Ellipse family;
            // A numerically collapsed family is its center axis. This also
            // keeps snapping usable on extremely thin or distant ellipses.
            if (!ellipseGeometry(constraint, &family)) {
                constraint.kind = GuideConstraint::Kind::Line;
                const auto axis = horizontal ? outer.axisX : outer.axisY;
                constraint.line = QLineF(outer.center - axis, outer.center + axis);
            }
        }
        for (const auto &existing : constraints) {
            if (constraint.kind == GuideConstraint::Kind::Ellipse && existing.kind == constraint.kind &&
                QLineF(existing.axisX, constraint.axisX).length() < 1e-6 &&
                QLineF(existing.axisY, constraint.axisY).length() < 1e-6) return;
        }
        constraints.append(constraint);
    };
    addFamily(true); addFamily(false);
    return constraints;
}

QPointF projectOntoGuideConstraint(QPointF point, const GuideConstraint &constraint) {
    if (constraint.kind == GuideConstraint::Kind::Line) return projectOntoGuideLine(point, constraint.line);
    Ellipse ellipse;
    if (!finite(point) || !ellipseGeometry(constraint, &ellipse)) return point;
    const auto offset = difference(point, ellipse.center);
    const long double localX = offset.x * ellipse.x.x + offset.y * ellipse.x.y;
    const long double localY = offset.x * ellipse.y.x + offset.y * ellipse.y.y;
    long double a = ellipse.a, b = ellipse.b, x = std::abs(localX), y = std::abs(localY);
    const bool swapped = a < b;
    if (swapped) { std::swap(a, b); std::swap(x, y); }
    long double projectedX = 0, projectedY = 0;
    const auto aa = a * a, bb = b * b;
    if (y == 0) {
        // On the major axis, the nearest point can be on either side of the
        // minor axis. Handle that medial-axis case without a singular root.
        if (aa == bb || a * x >= aa - bb) projectedX = a;
        else {
            projectedX = aa * x / (aa - bb);
            projectedY = b * std::sqrt(std::max(0.0L, 1 - projectedX * projectedX / aa));
        }
    } else {
        // Shift the closest-point multiplier by b². The function is monotonic
        // for positive s; unlike lambda + b², s retains precision near the
        // major axis instead of subtracting two nearly equal large numbers.
        long double low = 0, high = bb + std::max(0.0L, std::hypot(a * x, b * y));
        if ((x / a) * (x / a) + (y / b) * (y / b) <= 1) high = bb;
        for (int i = 0; i < 96; ++i) {
            const auto s = low / 2 + high / 2;
            if (s == low || s == high) break;
            const auto u = a * x / (s + (aa - bb)), v = b * y / s;
            if (u * u + v * v > 1) low = s;
            else high = s;
        }
        const auto s = low / 2 + high / 2;
        projectedX = aa * x / (s + (aa - bb));
        projectedY = bb * y / s;
        const auto scale = std::hypot(projectedX / a, projectedY / b);
        if (scale > 0 && std::isfinite(scale)) { projectedX /= scale; projectedY /= scale; }
    }
    if (swapped) std::swap(projectedX, projectedY);
    projectedX = std::copysign(projectedX, localX); projectedY = std::copysign(projectedY, localY);
    const QPointF result(qreal(ellipse.center.x() + ellipse.x.x * projectedX + ellipse.y.x * projectedY),
                         qreal(ellipse.center.y() + ellipse.x.y * projectedX + ellipse.y.y * projectedY));
    return finite(result) ? result : point;
}

qreal guideConstraintDistance(QPointF point, const GuideConstraint &constraint) {
    if (constraint.kind == GuideConstraint::Kind::Line) return guideLineDistance(point, constraint.line);
    Ellipse ellipse;
    if (!finite(point) || !ellipseGeometry(constraint, &ellipse)) return std::numeric_limits<qreal>::infinity();
    return QLineF(point, projectOntoGuideConstraint(point, constraint)).length();
}

QPainterPath guideConstraintPath(const GuideConstraint &constraint, const QRectF &visibleBounds, qreal detailTolerance) {
    QPainterPath path;
    if (!finiteRect(visibleBounds)) return path;
    if (constraint.kind == GuideConstraint::Kind::Line) addLine(path, constraint.line.p1(), constraint.line.p2(), visibleBounds);
    else addEllipse(path, constraint, visibleBounds, detailTolerance);
    return path;
}

QVector<QPointF> guideConstraintSamples(const GuideConstraint &constraint, QPointF from, QPointF to, qreal maxError) {
    QVector<QPointF> samples;
    if (!finite(from) || !finite(to)) return samples;
    const auto end = projectOntoGuideConstraint(to, constraint);
    Ellipse ellipse;
    if (constraint.kind == GuideConstraint::Kind::Line || !ellipseGeometry(constraint, &ellipse)) {
        samples.append(end); return samples;
    }
    const auto first = ellipseAngle(ellipse, projectOntoGuideConstraint(from, constraint));
    const auto delta = std::remainder(ellipseAngle(ellipse, end) - first, tau);
    const auto desired = std::ceil(std::abs(delta) / angularStep(ellipse, maxError));
    const int count = int(std::clamp(desired, 1.0L, 512.0L));
    samples.reserve(count);
    for (int i = 1; i < count; ++i) samples.append(ellipsePoint(ellipse, first + delta * i / count));
    samples.append(end);
    return samples;
}

std::optional<qreal> guideEllipseParameter(const GuideConstraint &constraint, QPointF point) {
    Ellipse ellipse;
    if (!finite(point) || !ellipseGeometry(constraint, &ellipse)) return {};
    const auto local = ellipseLocal(ellipse, point);
    if (std::hypot(local.x, local.y) <= 1e-8L) return {};
    return qreal(ellipseAngle(ellipse, projectOntoGuideConstraint(point, constraint)));
}
QPointF guideEllipsePoint(const GuideConstraint &constraint, qreal radians) {
    Ellipse ellipse;
    if (!std::isfinite(radians) || !ellipseGeometry(constraint, &ellipse)) return constraint.center;
    return ellipsePoint(ellipse, radians);
}
qreal unwrapGuideEllipseAngle(qreal radians, qreal previous) {
    if (!std::isfinite(radians) || !std::isfinite(previous)) return previous;
    return previous + qreal(std::remainder(static_cast<long double>(radians) - previous, tau));
}
QVector<QPointF> guideEllipseArcSamples(const GuideConstraint &constraint, qreal first, qreal last, qreal maxError) {
    QVector<QPointF> samples; Ellipse ellipse;
    if (!std::isfinite(first) || !std::isfinite(last) || !ellipseGeometry(constraint, &ellipse)) return samples;
    const long double delta = static_cast<long double>(last) - first;
    const auto desired = std::ceil(std::abs(delta) / angularStep(ellipse, maxError));
    const int count = int(std::clamp(desired, 1.0L, 512.0L)); samples.reserve(count);
    for (int i = 1; i <= count; ++i) samples.append(ellipsePoint(ellipse, first + delta * i / count));
    return samples;
}
} // namespace toon
