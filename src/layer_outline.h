#pragma once
#include <QColor>
#include <QImage>
#include <functional>

namespace toon {
// Adds a round, alpha-aware outer contour without modifying the input until
// the complete result is ready. Scratch/output memory is bounded to 128 MiB.
bool applyLayerOutline(QImage &image,qreal width,QColor color,
                       const std::function<bool()> &cancelled={});
}
