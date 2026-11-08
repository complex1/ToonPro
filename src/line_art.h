#pragma once
#include <QColor>
#include <QImage>
#include <atomic>
#include <memory>

namespace toon {
constexpr qint64 maxLineArtPixels = 9 * 1024 * 1024;
enum class LineArtShading { None, Cel, Grayscale, Screentone };
struct LineArtSettings {
    double detail = 65, width = 1.5, smoothing = 1.5;
    double textureSuppression = 55;
    int fragments = 3, darkCutoff = 85, lightCutoff = 175;
    QColor ink = Qt::black, shade = QColor("#9696a3"), deepShade = QColor("#454552");
    LineArtShading shading = LineArtShading::None;
    bool base = false;
    double toneSpacing = 8, toneAngle = 45;
    bool operator==(const LineArtSettings &) const = default;
};
struct LineArtInput {
    QImage image;
    QSize size;
};
struct LineArtResult {
    QImage source, lines, shade, deepShade, base, composite;
    QString error;
    bool cancelled = false;
};
using LineArtCancellation = std::shared_ptr<std::atomic_bool>;
// All inputs are immutable snapshots. previewSide=0 generates final canvas pixels.
LineArtResult convertLineArt(const LineArtInput &, const LineArtSettings &,
                             const LineArtCancellation & = {}, int previewSide = 0);
QString validateLineArtSettings(const LineArtSettings &);
// Only line-cleanup settings change; the artist's colors and tone output stay.
LineArtSettings backgroundLineArtSettings(LineArtSettings settings = {});

}
