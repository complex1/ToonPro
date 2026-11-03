#pragma once
#include "project.h"
#include <atomic>
#include <memory>

namespace toon {
class Document;
enum class CelPaintIssueKind { Hole, Leak, Stray, OutsidePalette };
struct CelPaintCheckSettings {
    int tolerance = 12;
    int maximumHolePixels = 256;
    int maximumStrayPixels = 4;
    QVector<QColor> approvedColors;
};
struct CelPaintCheckTarget {
    QString shotId, layerId, name;
    QRect bounds;
    RasterLayer original;
    QImage image;
};
struct CelPaintIssue {
    CelPaintIssueKind kind = CelPaintIssueKind::Hole;
    int target = 0;
    QRect bounds;
    int pixelCount = 0;
    QVector<int> pixels; // Row-major indices in target.image; empty for inspection-only regions.
    bool repairable = true;
};
struct CelPaintCheckResult {
    QVector<CelPaintCheckTarget> targets;
    QVector<CelPaintIssue> issues;
    CelPaintCheckSettings settings;
    QString error;
    bool cancelled = false, truncated = false;
};
struct CelPaintRepair {
    CelPaintCheckTarget source;
    RasterLayer replacement;
    QImage preview;
};
struct CelPaintRepairResult {
    QVector<CelPaintRepair> drawings;
    QString error;
    bool cancelled = false;
};
using CelPaintCancellation = std::shared_ptr<std::atomic_bool>;
QString celPaintIssueName(CelPaintIssueKind kind);
// Checks raw raster paint in the artwork frame. Results are suspects, not proof
// of an error; guide/composite pixels and intentional negative space need review.
CelPaintCheckResult inspectCelPaint(QVector<CelPaintCheckTarget> targets,
    const CelPaintCheckSettings &settings, const CelPaintCancellation &cancel = {});
CelPaintRepairResult prepareCelPaintRepairs(const CelPaintCheckResult &inspection,
    const QVector<int> &issueIndices, QColor holeColor, const CelPaintCancellation &cancel = {});
// All targets are checked before replacing any artwork. One Undo restores all
// accepted repairs; stale, locked or animated-transformed sources are rejected.
bool applyCelPaintRepairs(Document *document, const CelPaintRepairResult &repairs, QString *error = nullptr);
}
