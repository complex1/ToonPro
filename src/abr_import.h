#pragma once
#include "brush_settings.h"
#include <QImage>
#include <QStringList>
#include <QVector>
#include <optional>

namespace toon {
struct AbrBrush {
    QString name;
    QString sourceId;
    // Eight-bit coverage, where 0 is transparent and 255 is opaque.
    QImage mask;
    QSize sourceSize;
    QString sampleId;
    std::optional<BrushSettings> settings;
    QStringList compatibility;
    QString category;
    bool computed = false;
};

struct AbrImportResult {
    int version = 0;
    int subversion = 0;
    QVector<AbrBrush> brushes;
    QStringList warnings;
    QString error;
};

// Reads sampled tips from ABR v1/v2/v6.1/v6.2/v10.1/v10.2 with 8-bit raw or
// PackBits data. Large tips are resized to at most 1024 px on the longest edge.
// A malformed file produces an error and no brushes; unsupported samples are
// skipped with warnings. Modern descriptors map names and a documented subset
// of geometry/dynamics by sample UUID, with per-preset compatibility details.
AbrImportResult importAbr(const QString &path);
} // namespace toon
