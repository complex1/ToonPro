#pragma once
#include "project.h"
#include <QImage>
#include <QTransform>
#include <atomic>

namespace toon {
struct PerspectiveSource {
    QImage image;
    QTransform imageToArtwork;
    QString description;
    QString error;
};
// Read-only snapshots in artwork coordinates. View zoom/camera are excluded.
PerspectiveSource capturePerspectiveSource(const Project &, int shot, const QString &layerId,
                                           int frame, bool sourceEditing = false,
                                           const std::atomic_bool *cancelled = nullptr);
PerspectiveSource loadPerspectiveImage(const QString &path, QSize artworkSize,
                                      const std::atomic_bool *cancelled = nullptr);
}
