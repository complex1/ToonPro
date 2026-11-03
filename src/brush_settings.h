#pragma once
#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QRect>
#include <QVector>
#include <array>
#include "connected_brush_settings.h"
#include "brush_surface_settings.h"
#include "advanced_brush_dynamics_settings.h"

namespace toon {
enum class BrushControl { Off, Pressure, Tilt, Wheel, Fade, InitialDirection, Direction, Rotation };
enum class BrushBlend { Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, LinearDodge, LinearBurn, Difference, Exclusion, Subtract, Divide, Hue, Saturation, Color, Luminosity, Dissolve };
enum class MaskBlend { Multiply, Darken, Lighten, Subtract, ColorBurn, LinearHeight, Add, Difference };
enum class SymmetryMode { Off, Vertical, Horizontal, DualAxis, Diagonal, Wave, Circle, Spiral, ParallelLines, Radial };
enum class BrushTipOrder { Random, Ordered, Shuffle, Pressure, Direction };
// Frames share the existing grayscale/RGBA primary image as an atlas. Existing
// resource deduplication and project/library budgets therefore also cover sets.
struct BrushTipFrame {
    QRect rect;
    QPointF anchor{.5, .5};
    qreal weight = 1;
    bool operator==(const BrushTipFrame &) const = default;
};
inline constexpr qreal maxBrushSize = 1024;

struct BrushDynamic {
    qreal jitter = 0;
    qreal minimum = 0;
    BrushControl control = BrushControl::Off;
    int fade = 25;
    // Five uniformly spaced input samples, interpolated without allocation per dab.
    std::array<qreal, 5> response{0, .25, .5, .75, 1};
    bool jitterPerStroke = false;
    bool operator==(const BrushDynamic &) const = default;
};
struct BrushTexture {
    bool enabled = false;
    QImage pattern;
    QString name;
    bool invert = false;
    qreal scale = 1;
    qreal brightness = 0;
    qreal contrast = 0;
    bool eachTip = false;
    MaskBlend mode = MaskBlend::Multiply;
    qreal depth = 1;
    BrushDynamic dynamics;
    bool operator==(const BrushTexture &) const = default;
};
struct DualBrush {
    bool enabled = false;
    QImage tipMask;
    QString name;
    MaskBlend mode = MaskBlend::Multiply;
    qreal size = 24;
    qreal spacing = .25;
    qreal scatter = 0;
    int count = 1;
    bool operator==(const DualBrush &) const = default;
};
struct BrushColorDynamics {
    bool enabled = false;
    bool perTip = true;
    // Explicit opt-in: old colored tips retain their original image colors.
    bool applyToColorTip = false;
    qreal tintAmount = 0;
    qreal foregroundBackground = 0;
    BrushControl control = BrushControl::Off;
    int fade = 25;
    qreal hue = 0;
    qreal saturation = 0;
    qreal brightness = 0;
    qreal purity = 0;
    bool operator==(const BrushColorDynamics &) const = default;
};
struct BrushPose {
    bool overrideTiltX = false, overrideTiltY = false, overrideRotation = false, overridePressure = false;
    qreal tiltX = 0, tiltY = 0, rotation = 0, pressure = 1;
    bool operator==(const BrushPose &) const = default;
};
struct BrushSymmetry {
    SymmetryMode mode = SymmetryMode::Off;
    // Normalized output-frame coordinates. Geometry is in artwork pixels.
    QPointF center = {.5, .5};
    qreal angle = 0;
    int count = 6;
    qreal radius = 200;
    qreal distance = 200;
    qreal amplitude = 80;
    qreal wavelength = 300;
    bool operator==(const BrushSymmetry &) const = default;
};
struct BrushInput {
    qreal pressure = 1;
    qreal tiltX = 0, tiltY = 0;
    qreal rotation = 0;
    qreal wheel = 1;
    qreal speed = 0; // Artwork pixels per second, captured with the stroke.
    bool operator==(const BrushInput &) const = default;
};
struct BrushSettings {
    QColor color = QColor("#202530");
    QColor backgroundColor = Qt::white;
    qreal size = 12;
    // Legacy name retained for preset migration: opacity is dab FLOW.
    qreal opacity = 1;
    qreal strokeOpacity = 1;
    bool pressureSize = true;
    QImage tipMask;
    // Optional RGBA stamp; its alpha defines the tip and its colors replace the foreground.
    QImage colorTip;
    QVector<BrushTipFrame> tipFrames;
    BrushTipOrder tipOrder = BrushTipOrder::Shuffle;
    qreal spacing = .02;
    qreal hardness = 1;
    BrushBlend blendMode = BrushBlend::Normal;
    qreal angle = 0;
    qreal roundness = 1;
    bool flipX = false, flipY = false;
    BrushDynamic sizeDynamics{0, .05, BrushControl::Off, 25};
    BrushDynamic angleDynamics;
    BrushDynamic roundnessDynamics{0, .01, BrushControl::Off, 25};
    qreal tiltScale = 1;
    bool projection = false;
    qreal scatter = 0;
    bool scatterBothAxes = false;
    BrushControl scatterControl = BrushControl::Off;
    int scatterFade = 25;
    int count = 1;
    qreal countJitter = 0;
    BrushControl countControl = BrushControl::Off;
    int countFade = 25;
    BrushTexture texture;
    DualBrush dual;
    BrushColorDynamics colorDynamics;
    BrushConnectedSettings connected;
    BrushSurfacePattern surface;
    BrushClusterSpray clusterSpray;
    BrushSpeedDynamics speedDynamics;
    BrushDynamic opacityDynamics;
    BrushDynamic flowDynamics;
    BrushPose pose;
    bool noise = false;
    bool wetEdges = false;
    bool airbrush = false;
    qreal airbrushRate = 20;
    bool smoothingEnabled = true;
    bool protectTexture = false;
    BrushSymmetry symmetry;
    bool lockRandomSeed = false;
    quint32 randomSeed = 1;
    bool operator==(const BrushSettings &) const = default;
};
// Versioned library helpers. Images are encoded by BrushLibrary, not these helpers.
bool usesModernBrushFeatures(const BrushSettings &settings);
bool usesExtendedBrushDynamics(const BrushSettings &settings);
bool usesAdvancedBrushTips(const BrushSettings &settings);
bool validateBrushTipImages(const BrushSettings &settings, QString *error = nullptr);
QJsonObject brushSettingsToJson(const BrushSettings &settings);
bool brushSettingsFromJson(const QJsonObject &object, BrushSettings &settings, QString *error = nullptr);
bool validateBrushSettings(const BrushSettings &settings, QString *error = nullptr);
} // namespace toon
