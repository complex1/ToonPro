#include "selection_geometry.h"
#include <QBitArray>
#include <QPainter>
#include <cmath>
#include <new>

namespace toon {
QPainterPath selectionPathFromMask(const QImage &mask, QPoint origin, bool *tooComplex) {
    *tooComplex = false;
    QPainterPath path; path.setFillRule(Qt::WindingFill);
    if (mask.isNull()) return path;
    if (mask.format() != QImage::Format_Grayscale8 || qint64(mask.width()) * mask.height() > 32 * 1024 * 1024) {
        *tooComplex = true; return {};
    }
    const int width = mask.width(), height = mask.height();
    const auto pixels = mask.constBits(); const auto stride = mask.bytesPerLine();
    // Trace actual pixel boundaries instead of unioning thousands of overlapping
    // row rectangles. The latter makes QPainterPath::simplified() quadratic on
    // textured artwork. One bit per pixel records its top edge; every closed
    // contour has a top edge, including holes, so no four-edge image is needed.
    QBitArray visitedTop(width * height);
    const auto selected = [=](int x, int y) {
        return x >= 0 && x < width && y >= 0 && y < height && pixels[y * stride + x] != 0;
    };
    const auto boundary = [&](int x, int y, int direction) {
        // Directions are clockwise. Selected pixels stay on the right, giving
        // holes the opposite winding to their surrounding selected region.
        switch (direction) {
        case 0: return selected(x, y) && !selected(x, y - 1); // east
        case 1: return selected(x - 1, y) && !selected(x, y); // south
        case 2: return selected(x - 1, y - 1) && !selected(x - 1, y); // west
        default: return selected(x, y - 1) && !selected(x - 1, y - 1); // north
        }
    };
    constexpr int dx[]{1, 0, -1, 0}, dy[]{0, 1, 0, -1};
    for (int y = 0; y < height; ++y) {
        const auto row = pixels + y * stride;
        const auto above = y ? row - stride : nullptr;
        for (int x = 0; x < width; ++x) {
            if (!row[x] || (above && above[x]) || visitedTop.testBit(y * width + x)) continue;
            int edgeX = x, edgeY = y, direction = 0;
            path.moveTo(origin + QPoint(x, y));
            do {
                if (direction == 0) visitedTop.setBit(edgeY * width + edgeX);
                edgeX += dx[direction]; edgeY += dy[direction];
                // At a diagonal contact, turn right to keep the two selected
                // pixels as separate contours instead of crossing their edges.
                int next = (direction + 1) % 4;
                if (!boundary(edgeX, edgeY, next)) {
                    next = direction;
                    if (!boundary(edgeX, edgeY, next)) next = (direction + 3) % 4;
                }
                if (next != direction) path.lineTo(origin + QPoint(edgeX, edgeY));
                direction = next;
                if (path.elementCount() > maxSelectionPathElements) { *tooComplex = true; return {}; }
            } while (edgeX != x || edgeY != y || direction != 0);
            path.closeSubpath();
        }
    }
    return path;
}
namespace {
bool geometryError(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
bool boundedArea(const QRect &area, QString *error) {
    return qint64(area.width()) * area.height() <= 32 * 1024 * 1024
        || geometryError(error, "The selection exceeds the 32-million-pixel working limit.");
}
QImage pathMask(const QPainterPath &path, const QRect &area, const QRectF &bounds) {
    QImage mask(area.size(), QImage::Format_Grayscale8);
    if (mask.isNull()) return {};
    mask.fill(0);
    QPainter painter(&mask); painter.translate(-area.topLeft()); painter.setClipRect(bounds);
    painter.setPen(Qt::NoPen); painter.setBrush(Qt::white); painter.drawPath(path);
    return mask;
}
bool finishMask(const QImage &mask, QPoint origin, QPainterPath *result, QString *error) {
    bool complex = false; auto path = selectionPathFromMask(mask, origin, &complex);
    if (complex) return geometryError(error, "The selection is too detailed. Increase tolerance or select a simpler region.");
    *result = std::move(path); return true;
}
// Distance from a pixel center to the area covered by another pixel. Doubling
// coordinates keeps half-pixel cell edges exact without floating-point grids.
int horizontalDistance(int distance, int maximum) {
    if (!distance) return 0;
    if (distance > 101) return maximum;
    const int doubled = 2 * distance - 1;
    return qMin(maximum, doubled * doubled);
}
// Lower envelope of squared distance parabolas, swept from top to bottom. A
// second reversed sweep covers the other direction. Each candidate enters and
// leaves the envelope once, so a large radius never multiplies the pixel work.
QVector<int> verticalDistance(const QVector<int> &source, int maximum) {
    struct Candidate { qint64 center, cost; qreal start; };
    QVector<Candidate> envelope; envelope.reserve(source.size());
    QVector<int> output(source.size(), maximum); int first = 0;
    for (int i = 0; i < source.size(); ++i) {
        if (i && source[i - 1] < maximum) {
            Candidate candidate{2LL * i - 1, source[i - 1], -1e100};
            while (envelope.size() > first) {
                const auto &last = envelope.last();
                candidate.start = qreal(candidate.cost + candidate.center * candidate.center
                    - last.cost - last.center * last.center) / qreal(2 * (candidate.center - last.center));
                if (candidate.start > last.start) break;
                envelope.removeLast();
            }
            if (envelope.size() == first) candidate.start = -1e100;
            envelope.append(candidate);
        }
        const qint64 position = 2LL * i;
        while (first + 1 < envelope.size() && envelope[first + 1].start <= position) ++first;
        if (first < envelope.size()) {
            const auto &candidate = envelope[first]; const auto delta = position - candidate.center;
            output[i] = int(qMin(qint64(maximum), candidate.cost + delta * delta));
        }
        output[i] = qMin(output[i], source[i]);
    }
    return output;
}
}
bool selectionBooleanPath(const QPainterPath &left, const QPainterPath &right,
                          SelectionPathOperation operation, const QRectF &bounds,
                          QPainterPath *result, QString *error) {
    if (!result) return false;
    try {
        QRectF region = left.boundingRect();
        if (operation == SelectionPathOperation::Union) region |= right.boundingRect();
        else if (operation == SelectionPathOperation::Intersection) region &= right.boundingRect();
        const QRect area = region.intersected(bounds).toAlignedRect();
        if (area.isEmpty()) { *result = {}; return true; }
        if (!boundedArea(area, error)) return false;
        auto mask = pathMask(left, area, bounds); const auto other = pathMask(right, area, bounds);
        if (mask.isNull() || other.isNull()) return geometryError(error, "There is not enough memory for the selection.");
        for (int y = 0; y < area.height(); ++y) {
            auto row = mask.scanLine(y); const auto second = other.constScanLine(y);
            for (int x = 0; x < area.width(); ++x) {
                switch (operation) {
                case SelectionPathOperation::Union: row[x] |= second[x]; break;
                case SelectionPathOperation::Difference: row[x] &= uchar(~second[x]); break;
                case SelectionPathOperation::Intersection: row[x] &= second[x]; break;
                }
            }
        }
        return finishMask(mask, area.topLeft(), result, error);
    } catch (const std::bad_alloc &) { return geometryError(error, "There is not enough memory for the selection."); }
}
bool selectionResizePath(const QPainterPath &path, qreal signedPixels, const QRectF &bounds,
                         QPainterPath *result, QString *error) {
    if (!result || !std::isfinite(signedPixels) || std::abs(signedPixels) < 1 || std::abs(signedPixels) > 100) return false;
    try {
        if (path.isEmpty()) { *result = {}; return true; }
        const qreal radius = std::abs(signedPixels); const int halo = int(std::ceil(radius)) + 1;
        // Include empty pixels around the drawing area so contracting a full
        // canvas also shrinks its outside boundary.
        const QRect area = path.boundingRect().intersected(bounds).toAlignedRect().adjusted(-halo, -halo, halo, halo);
        if (!boundedArea(area, error)) return false;
        auto mask = pathMask(path, area, bounds);
        if (mask.isNull()) return geometryError(error, "There is not enough memory for the selection.");
        const int width = mask.width(), height = mask.height();
        const qreal threshold = 4 * radius * radius; const int maximum = int(std::ceil(threshold)) + 1;
        const bool growing = signedPixels > 0;
        QVector<int> distances(width * height, maximum);
        for (int y = 0; y < height; ++y) {
            const auto row = mask.constScanLine(y); auto target = distances.data() + y * width;
            int distance = width + 102;
            for (int x = 0; x < width; ++x) {
                distance = bool(row[x]) == growing ? 0 : distance + 1;
                target[x] = horizontalDistance(distance, maximum);
            }
            distance = width + 102;
            for (int x = width - 1; x >= 0; --x) {
                distance = bool(row[x]) == growing ? 0 : distance + 1;
                target[x] = qMin(target[x], horizontalDistance(distance, maximum));
            }
        }
        QVector<int> column(height), reversed(height);
        for (int x = 0; x < width; ++x) {
            for (int y = 0; y < height; ++y) column[y] = reversed[height - 1 - y] = distances[y * width + x];
            const auto forward = verticalDistance(column, maximum), backward = verticalDistance(reversed, maximum);
            for (int y = 0; y < height; ++y) {
                const bool nearby = qMin(forward[y], backward[height - 1 - y]) <= threshold;
                const bool within = bounds.contains(QPointF(area.x() + x + .5, area.y() + y + .5));
                mask.scanLine(y)[x] = within && (growing ? nearby : !nearby) ? 255 : 0;
            }
        }
        return finishMask(mask, area.topLeft(), result, error);
    } catch (const std::bad_alloc &) { return geometryError(error, "There is not enough memory for the selection."); }
}
} // namespace toon
