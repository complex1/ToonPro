#include "brush_types.h"

namespace toon {

QString brushTypeName(BrushType type) {
    switch (type) {
    case BrushType::Pen: return QStringLiteral("Pen");
    case BrushType::Ink: return QStringLiteral("Ink");
    case BrushType::Soft: return QStringLiteral("Soft brush");
    case BrushType::Spray: return QStringLiteral("Spray");
    case BrushType::Stamp: return QStringLiteral("Stamp");
    case BrushType::ColoredStamp: return QStringLiteral("Colored stamp");
    case BrushType::Custom: return QStringLiteral("Custom");
    case BrushType::DynamicStamp: return QStringLiteral("Dynamic stamp");
    }
    return {};
}

QString brushTypeIcon(BrushType type) {
    switch (type) {
    case BrushType::Pen: return QStringLiteral("pen-tool");
    case BrushType::Ink: return QStringLiteral("brush");
    case BrushType::Soft: return QStringLiteral("blend");
    case BrushType::Spray: return QStringLiteral("sparkles");
    case BrushType::Stamp: return QStringLiteral("image");
    case BrushType::ColoredStamp: return QStringLiteral("palette");
    case BrushType::Custom: return QStringLiteral("paintbrush");
    case BrushType::DynamicStamp: return QStringLiteral("sparkles");
    }
    return QStringLiteral("brush");
}

BrushSettings brushSettingsForType(BrushType type) {
    BrushSettings brush;
    switch (type) {
    case BrushType::Pen:
        brush.size = 6;
        break;
    case BrushType::Ink:
        brush.size = 12;
        break;
    case BrushType::Soft:
        brush.size = 80;
        brush.hardness = 0;
        brush.opacity = .18;
        brush.spacing = .08;
        break;
    case BrushType::Spray:
        brush.size = 8;
        brush.hardness = .8;
        brush.opacity = .45;
        brush.spacing = .7;
        brush.scatter = 3;
        brush.scatterBothAxes = true;
        brush.count = 4;
        brush.sizeDynamics.jitter = .65;
        brush.pressureSize = false;
        break;
    case BrushType::Stamp:
    case BrushType::ColoredStamp:
        brush.size = 96;
        brush.spacing = 1;
        brush.pressureSize = false;
        break;
    case BrushType::DynamicStamp:
        brush.size = 96;
        brush.spacing = .8;
        brush.pressureSize = false;
        brush.tipOrder = BrushTipOrder::Shuffle;
        brush.sizeDynamics.jitter = .25;
        brush.sizeDynamics.minimum = .5;
        brush.angleDynamics.jitter = .12;
        brush.colorDynamics.enabled = true;
        brush.colorDynamics.saturation = .15;
        break;
    case BrushType::Custom:
        break;
    }
    return brush;
}

qreal brushTypeSmoothing(BrushType type) {
    return type == BrushType::Pen ? 10 : type == BrushType::Ink ? 20 : 0;
}

BrushType inferredBrushType(const QString &name, const BrushSettings &brush) {
    if (brush.connected.mode != BrushConnectedMode::Off || brush.surface.kind != BrushSurfaceKind::Off) return BrushType::Custom;
    if (usesModernBrushFeatures(brush) && brush.tipFrames.size() > 1) return BrushType::DynamicStamp;
    if (brush.clusterSpray.enabled) return BrushType::Spray;
    if (!brush.colorTip.isNull()) return BrushType::ColoredStamp;
    if (brush.scatter > 0 || brush.airbrush) return BrushType::Spray;
    if (!brush.tipMask.isNull()) return BrushType::Stamp;
    if (brush.hardness < .4) return BrushType::Soft;
    if (name.contains("ink", Qt::CaseInsensitive)) return BrushType::Ink;
    if (name.contains("pen", Qt::CaseInsensitive)) return BrushType::Pen;
    return BrushType::Custom;
}

} // namespace toon
