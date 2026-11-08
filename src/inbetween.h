#pragma once
#include "project.h"
#include "cel_edit.h"

namespace toon {
inline constexpr int maxInbetweenDrawings = 64;
enum class InbetweenOutput { WhiteBackground, TransparentInk };
struct InbetweenSample {
    int frame = 0, hold = 1;
    qreal time = 0;
    bool operator==(const InbetweenSample &) const = default;
};
struct InbetweenPlan {
    QString shotId;
    TimelineCelRef first, last;
    QSize size;
    int firstFrame = 0, lastFrame = 0, step = 1;
    CubicEasing easing;
    QVector<InbetweenSample> samples;
    InbetweenOutput output = InbetweenOutput::WhiteBackground;
    bool operator==(const InbetweenPlan &) const = default;
};
// Plans never render pixels or modify the project. Endpoint times are fixed.
InbetweenPlan planInbetweens(const Project &, const QString &shotId,
    const QVector<TimelineCelRef> &, int step, const CubicEasing &, QString *error,
    InbetweenOutput output = InbetweenOutput::WhiteBackground);
// Render only the selected drawing hierarchy in output coordinates, without
// paper, other tracks, camera or animated properties. Retains source alpha.
QImage renderInbetweenSource(const Project &, const InbetweenPlan &, bool last, QString *error);
bool inbetweenImageOpaque(const QImage &);
QImage inbetweenWhiteMatte(const QImage &);
QColor inbetweenInkColor(const QImage &, const QImage &);
// Single-ink reconstruction, not general foreground segmentation. White fills
// are background in this mode; all marks use the detected source ink color.
QImage inbetweenTransparentInk(const QImage &whiteMatted, QColor ink);
} // namespace toon
