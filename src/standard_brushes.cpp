#include "standard_brushes.h"
#include "catalog_brushes.h"

#include <array>
#include <cmath>
#include <utility>

namespace toon {
namespace {
struct Metadata {
    const char *id;
    const char *category;
    const char *tags;
};
constexpr std::array<Metadata, 19> metadata{{
    {"builtin/ink", "Drawing", ""},
    {"builtin/sketch", "Drawing", ""},
    {"builtin/blocking", "Drawing", ""},
    {"builtin/hard-round", "Painting", "Standard,Round,Hard,Blocking"},
    {"builtin/pressure-round", "Painting", "Standard,Round,Pressure"},
    {"builtin/soft-airbrush", "Airbrush", "Standard,Soft,Airbrush,Pressure"},
    {"builtin/g-pen", "Inking", "Standard,Pen,Ink,Pressure"},
    {"builtin/fine-liner", "Inking", "Standard,Pen,Ink,Uniform"},
    {"builtin/graphite-pencil", "Sketching", "Standard,Pencil,Grain,Pressure"},
    {"builtin/mechanical-pencil", "Sketching", "Standard,Pencil,Fine,Pressure"},
    {"builtin/charcoal", "Sketching", "Standard,Charcoal,Grain,Pressure"},
    {"builtin/dry-ink", "Inking", "Standard,Ink,Dry,Texture,Pressure"},
    {"builtin/flat-paint", "Painting", "Standard,Flat,Paint,Pressure"},
    {"builtin/soft-paint", "Painting", "Standard,Soft,Paint,Pressure"},
    {"builtin/pigment-wash", "Painting", "Standard,Wash,Texture,Pressure"},
    {"builtin/particle-spray", "Texture", "Standard,Spray,Particles,Random"},
    {"builtin/stipple", "Texture", "Standard,Stipple,Dots,Random"},
    {"builtin/chisel-marker", "Markers", "Standard,Marker,Chisel,Pressure"},
    {"builtin/calligraphy", "Inking", "Standard,Calligraphy,Flat,Pressure"},
}};

// Integer hashing makes the native grain independent of platform RNG state.
qreal grain(int x, int y, quint32 salt) {
    quint32 n = quint32(x) * 0x9e3779b9U ^ quint32(y) * 0x85ebca6bU ^ salt;
    n ^= n >> 16; n *= 0x7feb352dU; n ^= n >> 15;
    n *= 0x846ca68bU; n ^= n >> 16;
    return qreal(n & 0xffffU) / 65535.0;
}

QImage paperGrain(quint32 salt, qreal low, qreal strength) {
    QImage image(64, 64, QImage::Format_Grayscale8);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const qreal fine = grain(x, y, salt);
            const qreal coarse = grain(x / 3, y / 3, salt ^ 0x67a5U);
            row[x] = uchar(qRound(255 * qBound(0.0, low + strength * (.7 * fine + .3 * coarse), 1.0)));
        }
    }
    return image;
}

QImage dryTip(bool rectangular, quint32 salt) {
    QImage image(64, 64, QImage::Format_Grayscale8);
    image.fill(0);
    for (int y = 0; y < image.height(); ++y) {
        auto *row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const qreal nx = (x + .5 - 32) / 29;
            const qreal ny = (y + .5 - 32) / 29;
            const qreal edge = rectangular ? qMax(qAbs(nx), qAbs(ny)) : std::hypot(nx, ny);
            const qreal coverage = qBound(0.0, (1 - edge) * 8, 1.0);
            const qreal fibre = grain(x / 2, y / 11, salt);
            const qreal speckle = grain(x, y, salt ^ 0x123fU);
            row[x] = uchar(qRound(255 * coverage * (fibre < .22 ? .12 : .48 + .52 * speckle)));
        }
    }
    return image;
}

QImage flatTip() {
    QImage image(64, 24, QImage::Format_Grayscale8);
    image.fill(0);
    for (int y = 1; y < image.height() - 1; ++y) {
        auto *row = image.scanLine(y);
        for (int x = 1; x < image.width() - 1; ++x) {
            const qreal edge = qMin(qMin(x, 63 - x) / 2.0, qMin(y, 23 - y) / 2.0);
            row[x] = uchar(qRound(255 * qMin(1.0, edge)));
        }
    }
    return image;
}

QImage sprayTip() {
    QImage image(64, 64, QImage::Format_Grayscale8);
    image.fill(0);
    // A single small native tip holds the particles, avoiding a large per-dab count.
    for (int i = 0; i < 25; ++i) {
        const qreal theta = grain(i, 7, 0x892U) * 6.283185307179586;
        const qreal radius = std::sqrt(grain(i, 13, 0x314U)) * 28;
        const int cx = qRound(31.5 + std::cos(theta) * radius);
        const int cy = qRound(31.5 + std::sin(theta) * radius);
        const qreal dotRadius = 0.8 + grain(i, 23, 0x211U) * 1.1;
        for (int y = qMax(0, cy - 2); y <= qMin(63, cy + 2); ++y)
            for (int x = qMax(0, cx - 2); x <= qMin(63, cx + 2); ++x) {
                const int coverage = qRound(255 * qBound(0.0, dotRadius + .5 - std::hypot(x - cx, y - cy), 1.0));
                image.scanLine(y)[x] = uchar(qMax(int(image.constScanLine(y)[x]), coverage));
            }
    }
    return image;
}

void texture(BrushSettings &brush, const QImage &pattern, const QString &name, qreal depth) {
    brush.texture.enabled = true;
    brush.texture.pattern = pattern;
    brush.texture.name = name;
    brush.texture.depth = depth;
    brush.texture.mode = MaskBlend::Multiply;
}

QVector<BrushPreset> makePresets() {
    QVector<BrushPreset> presets;
    auto add = [&](const char *suffix, const char *name, qreal size, qreal smoothing = 0) -> BrushSettings & {
        BrushPreset preset;
        preset.id = QString("builtin/%1").arg(QLatin1String(suffix));
        preset.name = QLatin1String(name);
        preset.source = "Toon Pro native starter pack";
        preset.builtIn = true;
        preset.smoothing = smoothing;
        preset.brush.size = size;
        preset.brush.spacing = .08;
        presets.push_back(std::move(preset));
        return presets.last().brush;
    };
    presets.reserve(16);

    auto &hard = add("hard-round", "Hard Round", 24);
    hard.pressureSize = false;

    auto &pressure = add("pressure-round", "Pressure Round", 24, 5);
    pressure.sizeDynamics.minimum = .08;
    pressure.flowDynamics.control = BrushControl::Pressure;
    pressure.flowDynamics.minimum = .3;

    auto &airbrush = add("soft-airbrush", "Soft Airbrush", 96);
    airbrush.hardness = 0;
    airbrush.opacity = .12;
    airbrush.spacing = .15;
    airbrush.pressureSize = false;
    airbrush.flowDynamics.control = BrushControl::Pressure;
    airbrush.flowDynamics.minimum = .05;
    airbrush.airbrush = true;
    airbrush.airbrushRate = 16;

    auto &pen = add("g-pen", "G Pen", 16, 15);
    pen.spacing = .04;
    pen.sizeDynamics.minimum = .025;

    auto &liner = add("fine-liner", "Fine Liner", 4, 10);
    liner.pressureSize = false;
    liner.spacing = .1;

    const auto finePaper = paperGrain(0xa14fU, .08, .9);
    const auto roughPaper = paperGrain(0xf31aU, .02, .96);
    auto &graphite = add("graphite-pencil", "Graphite Pencil", 9, 5);
    graphite.hardness = .75;
    graphite.opacity = .42;
    graphite.sizeDynamics.minimum = .35;
    graphite.flowDynamics.control = BrushControl::Pressure;
    graphite.flowDynamics.minimum = .12;
    texture(graphite, finePaper, "Native fine paper", .95);

    auto &mechanical = add("mechanical-pencil", "Mechanical Pencil", 3, 8);
    mechanical.pressureSize = false;
    mechanical.opacity = .65;
    mechanical.flowDynamics.control = BrushControl::Pressure;
    mechanical.flowDynamics.minimum = .15;
    texture(mechanical, finePaper, "Native fine paper", .6);

    auto &charcoal = add("charcoal", "Charcoal", 36);
    charcoal.tipMask = dryTip(false, 0x542fU);
    charcoal.opacity = .65;
    charcoal.spacing = .18;
    charcoal.sizeDynamics.minimum = .25;
    charcoal.angleDynamics.jitter = .18;
    charcoal.flowDynamics.control = BrushControl::Pressure;
    charcoal.flowDynamics.minimum = .2;
    texture(charcoal, roughPaper, "Native rough paper", .9);

    auto &dry = add("dry-ink", "Dry Ink", 22, 5);
    dry.tipMask = dryTip(true, 0x853cU);
    dry.spacing = .14;
    dry.roundness = .65;
    dry.angleDynamics.control = BrushControl::Direction;
    dry.sizeDynamics.minimum = .1;
    texture(dry, roughPaper, "Native rough paper", .85);

    const auto flat = flatTip();
    auto &flatPaint = add("flat-paint", "Flat Paint", 44);
    flatPaint.tipMask = flat;
    flatPaint.angle = -35;
    flatPaint.opacity = .55;
    flatPaint.spacing = .1;
    flatPaint.sizeDynamics.minimum = .3;
    flatPaint.flowDynamics.control = BrushControl::Pressure;
    flatPaint.flowDynamics.minimum = .3;

    auto &softPaint = add("soft-paint", "Soft Paint", 48);
    softPaint.hardness = .35;
    softPaint.opacity = .25;
    softPaint.sizeDynamics.minimum = .35;
    softPaint.flowDynamics.control = BrushControl::Pressure;
    softPaint.flowDynamics.minimum = .1;

    auto &wash = add("pigment-wash", "Pigment Wash", 72);
    wash.hardness = .3;
    wash.opacity = .16;
    wash.strokeOpacity = .45;
    wash.spacing = .14;
    wash.sizeDynamics.minimum = .5;
    wash.flowDynamics.control = BrushControl::Pressure;
    wash.flowDynamics.minimum = .2;
    texture(wash, finePaper, "Native fine paper", .4);
    presets.last().compatibility = {"Native textured wash; no fluid or pigment simulation."};

    auto &spray = add("particle-spray", "Particle Spray", 52);
    spray.tipMask = sprayTip();
    spray.opacity = .6;
    spray.spacing = .32;
    spray.sizeDynamics.minimum = .4;
    spray.sizeDynamics.jitter = .2;
    spray.angleDynamics.jitter = 1;
    spray.scatter = .3;
    spray.scatterBothAxes = true;
    spray.flowDynamics.control = BrushControl::Pressure;
    spray.flowDynamics.minimum = .2;

    auto &stipple = add("stipple", "Stipple", 4);
    stipple.pressureSize = false;
    stipple.spacing = 1.6;
    stipple.scatter = 4;
    stipple.scatterBothAxes = true;
    stipple.count = 2;
    stipple.sizeDynamics.jitter = .55;
    stipple.sizeDynamics.minimum = .25;
    stipple.flowDynamics.jitter = .25;
    stipple.flowDynamics.minimum = .5;

    auto &marker = add("chisel-marker", "Chisel Marker", 28, 8);
    marker.tipMask = flat;
    marker.angle = -35;
    marker.pressureSize = false;
    marker.strokeOpacity = .55;
    marker.spacing = .06;
    marker.opacityDynamics.control = BrushControl::Pressure;
    marker.opacityDynamics.minimum = .65;

    auto &calligraphy = add("calligraphy", "Calligraphy", 24, 12);
    calligraphy.tipMask = flat;
    calligraphy.angle = -45;
    calligraphy.spacing = .05;
    calligraphy.sizeDynamics.minimum = .45;
    return presets;
}
} // namespace

QVector<BrushPreset> standardBrushPresets() {
    static const auto presets = makePresets();
    return presets;
}

QString standardBrushCategory(const QString &id) {
    for (const auto &entry : metadata)
        if (id == QLatin1String(entry.id)) return QLatin1String(entry.category);
    return catalogBrushCategory(id);
}

QStringList standardBrushTags(const QString &id) {
    for (const auto &entry : metadata)
        if (id == QLatin1String(entry.id)) return QString::fromLatin1(entry.tags).split(',', Qt::SkipEmptyParts);
    return catalogBrushTags(id);
}
} // namespace toon
