#include "shape_recognition.h"

#include <QCoreApplication>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace toon {
namespace {
constexpr qreal pi = 3.14159265358979323846;
constexpr int sampleLimit = 128;

qreal dot(QPointF a, QPointF b) { return a.x() * b.x() + a.y() * b.y(); }
qreal cross(QPointF a, QPointF b) { return a.x() * b.y() - a.y() * b.x(); }
qreal length(QPointF p) { return std::hypot(p.x(), p.y()); }
QPointF unit(qreal angle) { return {std::cos(angle), std::sin(angle)}; }
qreal wrappedAngle(qreal a) { return std::remainder(a, 2 * pi); }
QString label(const char *text) { return QCoreApplication::translate("HoldShape", text); }

qreal segmentDistance(QPointF p, QPointF a, QPointF b) {
    const auto delta = b - a;
    const qreal t = std::clamp(dot(p - a, delta) / std::max(qreal(1e-12), dot(delta, delta)), qreal(0), qreal(1));
    return length(p - a - delta * t);
}

QVector<QPointF> removeMicroJitter(const QVector<QPointF> &points) {
    // A stationary tablet can still supply hundreds of subpixel samples while
    // the artist holds the tip. Those samples must not count as extra travel or
    // give the endpoint a disproportionate weight in the fit. Simplification
    // depends on the drawn geometry, not on the rate of the pen events.
    QVector<bool> keep(points.size(), false);
    keep.first() = keep.last() = true;
    QVector<std::pair<int, int>> pending{{0, int(points.size()) - 1}};
    int retained = 2;
    qsizetype work = 0;
    while (!pending.isEmpty()) {
        const auto [first, last] = pending.takeLast();
        qreal maximum = .0015; int split = -1;
        for (int i = first + 1; i < last; ++i) {
            if (++work > 2000000) return {};
            const auto distance = segmentDistance(points[i], points[first], points[last]);
            if (distance > maximum) { maximum = distance; split = i; }
        }
        if (split >= 0) {
            if (++retained > 1024) return {};
            keep[split] = true;
            pending.append({first, split}); pending.append({split, last});
        }
    }
    QVector<QPointF> result;
    result.reserve(retained);
    for (int i = 0; i < points.size(); ++i) if (keep[i]) result.append(points[i]);
    return result;
}

struct Samples {
    QVector<QPointF> points;
    QPointF origin;
    qreal scale = 1;
    qreal length = 0;
    qreal gap = 0;
};

std::optional<Samples> prepare(const QVector<QPointF> &input) {
    if (input.size() < 2) return {};
    qreal left = std::numeric_limits<qreal>::infinity(), top = left;
    qreal right = -left, bottom = -left;
    for (const auto p : input) {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y())) return {};
        left = std::min(left, p.x()); right = std::max(right, p.x());
        top = std::min(top, p.y()); bottom = std::max(bottom, p.y());
    }
    Samples result;
    result.scale = std::hypot(right - left, bottom - top);
    if (!std::isfinite(result.scale) || result.scale < 1e-6) return {};
    result.origin = {left + (right - left) * .5, top + (bottom - top) * .5};
    QVector<QPointF> points;
    points.reserve(input.size());
    for (const auto point : input) {
        const auto p = (point - result.origin) / result.scale;
        const qreal step = points.isEmpty() ? 0 : length(p - points.last());
        if (!points.isEmpty() && step < 1e-8) continue;
        points.append(p);
    }
    if (points.size() < 2) return {};
    points = removeMicroJitter(points);
    if (points.size() < 2) return {};
    QVector<qreal> distances;
    distances.reserve(points.size());
    for (int i = 0; i < points.size(); ++i) {
        result.length += i ? length(points[i] - points[i - 1]) : 0;
        // Even a full circle has a normalized length below four. Repeated
        // loops/backtracking cannot become an accidental clean shape.
        if (result.length > 8) return {};
        distances.append(result.length);
    }
    if (points.size() < 2 || result.length < .1) return {};
    result.gap = length(points.last() - points.first());
    const int count = sampleLimit;
    result.points.reserve(count);
    int segment = 1;
    for (int i = 0; i < count; ++i) {
        const qreal target = result.length * i / (count - 1);
        while (segment + 1 < points.size() && distances[segment] < target) ++segment;
        const qreal distance = distances[segment] - distances[segment - 1];
        const qreal t = std::clamp((target - distances[segment - 1]) / distance, qreal(0), qreal(1));
        result.points.append(points[segment - 1] * (1 - t) + points[segment] * t);
    }
    return result;
}

// Small pivoted normal-equation solver; all inputs have already been scaled to
// a unit diagonal. Singular line-like conics are rejected rather than inflated.
template<int N> bool solve(std::array<std::array<qreal, N + 1>, N> a, std::array<qreal, N> &answer) {
    for (int column = 0; column < N; ++column) {
        int pivot = column;
        for (int row = column + 1; row < N; ++row)
            if (std::abs(a[row][column]) > std::abs(a[pivot][column])) pivot = row;
        if (std::abs(a[pivot][column]) < 1e-10) return false;
        std::swap(a[pivot], a[column]);
        const auto divisor = a[column][column];
        for (int j = column; j <= N; ++j) a[column][j] /= divisor;
        for (int row = 0; row < N; ++row) {
            if (row == column) continue;
            const auto factor = a[row][column];
            for (int j = column; j <= N; ++j) a[row][j] -= factor * a[column][j];
        }
    }
    for (int i = 0; i < N; ++i) {
        answer[i] = a[i][N];
        if (!std::isfinite(answer[i])) return false;
    }
    return true;
}

template<int N> void accumulate(std::array<std::array<qreal, N + 1>, N> &a,
                               const std::array<qreal, N> &terms, qreal value) {
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) a[i][j] += terms[i] * terms[j];
        a[i][N] += terms[i] * value;
    }
}

struct Error {
    qreal squared = 0;
    qreal maximum = 0;
    int count = 0;
    void add(qreal distance) { squared += distance * distance; maximum = std::max(maximum, distance); ++count; }
    qreal rms() const { return std::sqrt(squared / std::max(1, count)); }
    qreal score() const { return rms() + maximum * .05; }
};

struct Ellipse {
    QPointF center;
    qreal rx = 0;
    qreal ry = 0;
    qreal angle = 0;
    QPointF local(QPointF p) const {
        const auto d = p - center, axis = unit(angle);
        return {dot(d, axis), cross(axis, d)};
    }
    QPointF map(QPointF p) const {
        const auto axis = unit(angle);
        return center + QPointF(axis.x() * p.x() - axis.y() * p.y(), axis.y() * p.x() + axis.x() * p.y());
    }
    qreal parameter(QPointF p) const { const auto d = local(p); return std::atan2(d.y() / ry, d.x() / rx); }
    QPointF point(qreal a) const { return map({rx * std::cos(a), ry * std::sin(a)}); }
    Error error(const QVector<QPointF> &points) const {
        Error result;
        for (const auto p : points) result.add(length(p - point(parameter(p))));
        return result;
    }
};

std::optional<Ellipse> fitCircle(const QVector<QPointF> &points) {
    std::array<std::array<qreal, 4>, 3> equations{};
    for (const auto p : points) accumulate<3>(equations, {2 * p.x(), 2 * p.y(), 1}, dot(p, p));
    std::array<qreal, 3> solution{};
    if (!solve<3>(equations, solution)) return {};
    const QPointF center(solution[0], solution[1]);
    const qreal radiusSquared = solution[2] + dot(center, center);
    if (radiusSquared <= 0 || radiusSquared > 100) return {};
    const qreal radius = std::sqrt(radiusSquared);
    return Ellipse{center, radius, radius, 0};
}

std::optional<Ellipse> fitEllipse(const QVector<QPointF> &points) {
    std::array<std::array<qreal, 6>, 5> equations{};
    for (const auto p : points) accumulate<5>(equations,
        {p.x() * p.x(), p.x() * p.y(), p.y() * p.y(), p.x(), p.y()}, 1);
    std::array<qreal, 5> c{};
    if (!solve<5>(equations, c)) return {};
    const qreal determinant = c[0] * c[2] - c[1] * c[1] * .25;
    if (determinant <= 1e-8 || c[0] <= 0 || c[2] <= 0) return {};
    const QPointF center((c[1] * c[4] * .5 - c[2] * c[3]) / (2 * determinant),
                         (c[1] * c[3] * .5 - c[0] * c[4]) / (2 * determinant));
    const qreal k = 1 + c[0] * center.x() * center.x() + c[1] * center.x() * center.y() + c[2] * center.y() * center.y();
    const qreal angle = .5 * std::atan2(c[1], c[0] - c[2]);
    const auto axis = unit(angle);
    const qreal eigenX = c[0] * axis.x() * axis.x() + c[1] * axis.x() * axis.y() + c[2] * axis.y() * axis.y();
    const qreal eigenY = c[0] + c[2] - eigenX;
    if (k <= 0 || eigenX <= 0 || eigenY <= 0) return {};
    const qreal rx = std::sqrt(k / eigenX), ry = std::sqrt(k / eigenY);
    if (std::min(rx, ry) < .015 || std::max(rx, ry) > 2 || std::min(rx, ry) / std::max(rx, ry) < .14) return {};
    return Ellipse{center, rx, ry, angle};
}

struct Traversal { qreal sweep = 0; qreal backtrack = 0; };
Traversal traversal(const QVector<QPointF> &points, const Ellipse &ellipse, bool close) {
    qreal total = 0, absolute = 0;
    qreal previous = ellipse.parameter(points.first());
    for (int i = 1; i < points.size() + (close ? 1 : 0); ++i) {
        const qreal next = ellipse.parameter(points[i % points.size()]);
        const qreal step = wrappedAngle(next - previous);
        total += step; absolute += std::abs(step); previous = next;
    }
    return {total, (absolute - std::abs(total)) * .5};
}

QPainterPath ellipseArc(const Ellipse &ellipse, qreal start, qreal sweep, bool close) {
    QPainterPath path(ellipse.point(start));
    const int segments = std::max(1, int(std::ceil(std::abs(sweep) / (pi * .5))));
    const qreal step = sweep / segments;
    for (int i = 0; i < segments; ++i) {
        const qreal a = start + step * i, b = a + step;
        const qreal k = 4. / 3 * std::tan(step * .25);
        path.cubicTo(ellipse.map({ellipse.rx * (std::cos(a) - k * std::sin(a)), ellipse.ry * (std::sin(a) + k * std::cos(a))}),
                     ellipse.map({ellipse.rx * (std::cos(b) + k * std::sin(b)), ellipse.ry * (std::sin(b) - k * std::cos(b))}),
                     ellipse.point(b));
    }
    if (close) path.closeSubpath();
    return path;
}

std::optional<RecognizedShape> fitLine(const Samples &s) {
    const auto first = s.points.first(), last = s.points.last();
    if (s.gap < .2 || s.gap / s.length < .88) return {};
    QPointF mean;
    for (const auto p : s.points) mean += p;
    mean /= s.points.size();
    qreal xx = 0, xy = 0, yy = 0;
    for (const auto p : s.points) { const auto d = p - mean; xx += d.x() * d.x(); xy += d.x() * d.y(); yy += d.y() * d.y(); }
    auto axis = unit(.5 * std::atan2(2 * xy, xx - yy));
    if (dot(last - first, axis) < 0) axis = -axis;
    Error error;
    qreal backwards = 0, previous = dot(first - mean, axis);
    for (const auto p : s.points) {
        error.add(std::abs(cross(axis, p - mean)));
        const qreal t = dot(p - mean, axis);
        backwards += std::max(qreal(0), previous - t); previous = t;
    }
    if (error.rms() > .025 || error.maximum > .07 || backwards > .04) return {};
    QPainterPath path(mean + axis * dot(first - mean, axis));
    path.lineTo(mean + axis * dot(last - mean, axis));
    return RecognizedShape{RecognizedShapeKind::Line, label("Line"), path, error.score()};
}

std::optional<RecognizedShape> fitQuadratic(const Samples &s) {
    if (s.gap < .12 || s.gap / s.length < .32) return {};
    const auto a = s.points.first(), b = s.points.last();
    QVector<qreal> parameters;
    for (int i = 0; i < s.points.size(); ++i) parameters.append(qreal(i) / (s.points.size() - 1));
    QPointF control;
    for (int iteration = 0; iteration < 7; ++iteration) {
        QPointF numerator; qreal denominator = 0;
        for (int i = 1; i + 1 < s.points.size(); ++i) {
            const qreal t = parameters[i], u = 1 - t, weight = 2 * t * u;
            numerator += (s.points[i] - a * (u * u) - b * (t * t)) * weight;
            denominator += weight * weight;
        }
        if (denominator < 1e-8) return {};
        control = numerator / denominator;
        if (length(control - a) > 3 || length(control - b) > 3) return {};
        for (int i = 1; i + 1 < s.points.size(); ++i) {
            qreal t = parameters[i];
            for (int j = 0; j < 3; ++j) {
                const auto point = a * ((1 - t) * (1 - t)) + control * (2 * t * (1 - t)) + b * (t * t);
                const auto derivative = (control - a) * (2 * (1 - t)) + (b - control) * (2 * t);
                const auto second = (a - control * 2 + b) * 2;
                const qreal divisor = dot(derivative, derivative) + dot(point - s.points[i], second);
                if (std::abs(divisor) < 1e-8) break;
                t = std::clamp(t - dot(point - s.points[i], derivative) / divisor, qreal(0), qreal(1));
            }
            parameters[i] = t;
        }
    }
    Error error;
    qreal backward = 0;
    for (int i = 0; i < s.points.size(); ++i) {
        const qreal t = parameters[i], u = 1 - t;
        error.add(length(s.points[i] - (a * (u * u) + control * (2 * t * u) + b * (t * t))));
        if (i) backward += std::max(qreal(0), parameters[i - 1] - t);
    }
    const qreal bend = std::abs(cross((b - a) / s.gap, control - a));
    if (error.rms() > .025 || error.maximum > .075 || backward > .06 || bend < .025) return {};
    QPainterPath path(a); path.quadTo(control, b);
    if (s.length / path.length() > 1.25) return {};
    return RecognizedShape{RecognizedShapeKind::Arc, label("Arc"), path, error.score() + .002};
}

std::optional<RecognizedShape> fitArc(const Samples &s) {
    std::optional<RecognizedShape> result;
    if (const auto circle = fitCircle(s.points)) {
        const auto error = circle->error(s.points);
        const auto travel = traversal(s.points, *circle, false);
        const qreal sweep = std::abs(travel.sweep);
        if (error.rms() < .025 && error.maximum < .075 && sweep > .2 && sweep < 1.9 * pi &&
            travel.backtrack < .04 + sweep * .08 && s.length / (circle->rx * sweep) < 1.2) {
            result = RecognizedShape{RecognizedShapeKind::Arc, label("Arc"),
                ellipseArc(*circle, circle->parameter(s.points.first()), travel.sweep, false), error.score()};
        }
    }
    if (const auto quadratic = fitQuadratic(s); quadratic && (!result || quadratic->score < result->score)) result = quadratic;
    return result;
}

QVector<QPointF> convexHull(QVector<QPointF> points) {
    std::sort(points.begin(), points.end(), [](QPointF a, QPointF b) { return a.x() == b.x() ? a.y() < b.y() : a.x() < b.x(); });
    points.erase(std::unique(points.begin(), points.end()), points.end());
    QVector<QPointF> hull;
    for (const auto p : points) {
        while (hull.size() >= 2 && cross(hull.last() - hull[hull.size() - 2], p - hull.last()) <= 0) hull.removeLast();
        hull.append(p);
    }
    const auto lower = hull.size();
    for (int i = points.size() - 2; i >= 0; --i) {
        const auto p = points[i];
        while (hull.size() > lower && cross(hull.last() - hull[hull.size() - 2], p - hull.last()) <= 0) hull.removeLast();
        hull.append(p);
    }
    if (!hull.isEmpty()) hull.removeLast();
    return hull;
}

std::optional<RecognizedShape> polygonCandidate(const Samples &s, QVector<QPointF> corners,
                                               RecognizedShapeKind kind, const char *name) {
    if (corners.size() < 3) return {};
    QPointF center;
    qreal perimeter = 0;
    for (int i = 0; i < corners.size(); ++i) { center += corners[i]; perimeter += length(corners[(i + 1) % corners.size()] - corners[i]); }
    center /= corners.size();
    if (perimeter < .5 || (s.length + s.gap) / perimeter < .84 || (s.length + s.gap) / perimeter > 1.2) return {};
    const auto travel = traversal(s.points, Ellipse{center, 1, 1, 0}, true);
    if (std::abs(std::abs(travel.sweep) - 2 * pi) > .1 || travel.backtrack > .4) return {};
    Error error;
    for (const auto p : s.points) {
        qreal nearest = 2;
        for (int i = 0; i < corners.size(); ++i) nearest = std::min(nearest, segmentDistance(p, corners[i], corners[(i + 1) % corners.size()]));
        error.add(nearest);
    }
    if (error.rms() > .03 || error.maximum > .08) return {};
    qreal area = 0;
    for (int i = 0; i < corners.size(); ++i) area += cross(corners[i], corners[(i + 1) % corners.size()]);
    if (std::abs(area) < .025) return {};
    if (area * travel.sweep < 0) std::reverse(corners.begin(), corners.end());
    // Begin at the nearest position on the polygon, retaining the gesture's
    // starting edge instead of unexpectedly moving a pressure taper to a corner.
    int edge = 0; qreal nearest = 2; QPointF start;
    for (int i = 0; i < corners.size(); ++i) {
        const auto a = corners[i], delta = corners[(i + 1) % corners.size()] - a;
        const auto projected = a + delta * std::clamp(dot(s.points.first() - a, delta) / dot(delta, delta), qreal(0), qreal(1));
        const auto distance = length(projected - s.points.first());
        if (distance < nearest) { nearest = distance; start = projected; edge = i; }
    }
    QPainterPath path(start);
    for (int i = 1; i <= corners.size(); ++i) path.lineTo(corners[(edge + i) % corners.size()]);
    path.closeSubpath();
    return RecognizedShape{kind, label(name), path, error.score() + .003};
}

std::optional<RecognizedShape> fitRectangle(const Samples &s, const QVector<QPointF> &hull) {
    std::optional<RecognizedShape> result;
    for (int i = 0; i < hull.size(); ++i) {
        const auto delta = hull[(i + 1) % hull.size()] - hull[i];
        if (length(delta) < .025) continue;
        const auto axis = delta / length(delta), normal = QPointF(-axis.y(), axis.x());
        qreal left = 2, right = -2, top = 2, bottom = -2;
        for (const auto p : hull) {
            const qreal x = dot(axis, p), y = dot(normal, p);
            left = std::min(left, x); right = std::max(right, x); top = std::min(top, y); bottom = std::max(bottom, y);
        }
        if (std::min(right - left, bottom - top) < .06) continue;
        const QVector<QPointF> corners{axis * left + normal * top, axis * right + normal * top,
                                      axis * right + normal * bottom, axis * left + normal * bottom};
        const auto candidate = polygonCandidate(s, corners, RecognizedShapeKind::Rectangle, "Rectangle");
        if (candidate && (!result || candidate->score < result->score)) result = candidate;
    }
    return result;
}

std::optional<RecognizedShape> fitTriangle(const Samples &s, QVector<QPointF> hull) {
    while (hull.size() > 3) {
        int smallest = 0; qreal leastArea = 10;
        for (int i = 0; i < hull.size(); ++i) {
            const qreal area = std::abs(cross(hull[i] - hull[(i + hull.size() - 1) % hull.size()], hull[(i + 1) % hull.size()] - hull[i]));
            if (area < leastArea) { leastArea = area; smallest = i; }
        }
        hull.removeAt(smallest);
    }
    return polygonCandidate(s, hull, RecognizedShapeKind::Triangle, "Triangle");
}
} // namespace

QVector<RecognizedShape> recognizeStrokeShapes(const QVector<QPointF> &points) {
    const auto prepared = prepare(points);
    if (!prepared) return {};
    const auto &s = *prepared;
    QVector<RecognizedShape> result;
    const bool closed = s.gap < .22 && s.length > 1.6;
    if (closed) {
        const auto addEllipse = [&](const std::optional<Ellipse> &ellipse, RecognizedShapeKind kind, const char *name) {
            if (!ellipse) return;
            const auto error = ellipse->error(s.points);
            const auto travel = traversal(s.points, *ellipse, true);
            if (error.rms() > .04 || error.maximum > .1 || std::abs(std::abs(travel.sweep) - 2 * pi) > .1 || travel.backtrack > .35) return;
            const auto path = ellipseArc(*ellipse, ellipse->parameter(s.points.first()), std::copysign(2 * pi, travel.sweep), true);
            const qreal ratio = (s.length + s.gap) / path.length();
            if (ratio < .82 || ratio > 1.22) return;
            result.append({kind, label(name), path, error.score() + (kind == RecognizedShapeKind::Ellipse ? .004 : 0)});
        };
        addEllipse(fitCircle(s.points), RecognizedShapeKind::Circle, "Circle");
        addEllipse(fitEllipse(s.points), RecognizedShapeKind::Ellipse, "Ellipse");
        const auto hull = convexHull(s.points);
        if (const auto rectangle = fitRectangle(s, hull)) result.append(*rectangle);
        if (const auto triangle = fitTriangle(s, hull)) result.append(*triangle);
    } else {
        if (const auto line = fitLine(s)) result.append(*line);
        if (const auto arc = fitArc(s)) result.append(*arc);
    }
    std::stable_sort(result.begin(), result.end(), [](const RecognizedShape &a, const RecognizedShape &b) { return a.score < b.score; });
    const QTransform toArtwork(s.scale, 0, 0, s.scale, s.origin.x(), s.origin.y());
    for (auto &candidate : result) candidate.path = toArtwork.map(candidate.path);
    return result;
}
} // namespace toon
