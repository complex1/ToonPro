#pragma once

#include <QIcon>
#include <QString>

namespace toon {

// Lucide filenames or a semantic toon-* name returned by featureIconName().
QIcon appIcon(const QString &name);

// One visual identity shared by tool lists, properties and layer controls.
// These names are stable catalog values, independent of a panel's geometry.
enum class FeatureIcon {
    Arc, Bezier, Simplify, Pressure, Tilt, Texture, Tone,
    PerspectiveGrid, VanishingPoint, TwoPointPerspective, ParallelGuide,
    ThreePointPerspective, Fisheye, Warp, LinearGradient, RadialGradient,
    Blur, AlphaLock, Clipping, FillReference, VectorNudge
};
QString featureIconName(FeatureIcon feature);
QString featureIconLabel(FeatureIcon feature);
QIcon featureIcon(FeatureIcon feature);

}
