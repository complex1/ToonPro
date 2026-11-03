#include "layer_effects.h"
#include "raster.h"
#include <QJsonArray>
#include <QSet>
#include <cmath>

namespace toon {
namespace {
bool bounded(qreal value, qreal low, qreal high) {
    return std::isfinite(value) && value >= low && value <= high;
}
bool keysMatch(const QJsonObject &json, const QSet<QString> &keys) {
    if (json.size() != keys.size()) return false;
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) if (!keys.contains(it.key())) return false;
    return true;
}
bool opaqueColor(const QJsonValue &json, QColor *color) {
    if (!json.isString()) return false;
    const auto text = json.toString(); const QColor parsed(text);
    if (text.size() != 7 || !text.startsWith('#') || !parsed.isValid()
        || parsed.alpha() != 255 || parsed.name(QColor::HexRgb) != text.toLower()) return false;
    *color = parsed; return true;
}
}
QString validateLayerEffects(const LayerEffects &e) {
    if (!bounded(e.blurRadius, 0, 128) || !bounded(e.shadowRadius, 0, 128) || !bounded(e.glowRadius, 0, 128))
        return "Effect radii must be between 0 and 128 pixels.";
    if (!bounded(e.shadowOffset.x(), -512, 512) || !bounded(e.shadowOffset.y(), -512, 512))
        return "Shadow offsets must be between -512 and 512 pixels.";
    if (!e.shadowColor.isValid() || e.shadowColor.alpha() != 255 || !e.glowColor.isValid() || e.glowColor.alpha() != 255)
        return "Effect colors must be opaque.";
    if (!bounded(e.shadowOpacity, 0, 1) || !bounded(e.glowOpacity, 0, 1)) return "Invalid effect opacity.";
    if (!bounded(e.exposure, -4, 4) || !bounded(e.contrast, -100, 100)
        || !bounded(e.saturation, -100, 100) || !bounded(e.hue, -180, 180)) return "Invalid color correction values.";
    return {};
}
bool hasLayerEffects(const LayerEffects &e) {
    return e.enabled && (e.blurEnabled || e.shadowEnabled || e.glowEnabled || e.colorEnabled);
}
QString validateLayerMask(const LayerMask &mask, bool pixelsRequired) {
    if (mask.defaultValue < 0 || mask.defaultValue > 255) return "Invalid layer mask default value.";
    if (!bounded(mask.position.x(), -32768, 32768) || !bounded(mask.position.y(), -32768, 32768)
        || !bounded(mask.pivot.x(), -32768, 32768) || !bounded(mask.pivot.y(), -32768, 32768))
        return "Mask position and pivot must remain within the supported drawing coordinates.";
    if (!bounded(mask.scale.x(), -100, 100) || !bounded(mask.scale.y(), -100, 100)
        || !bounded(mask.rotation, -36000, 36000)) return "Invalid mask scale or rotation.";
    if (!bounded(mask.opacity, 0, 1) || !bounded(mask.feather, 0, 128)) return "Invalid mask strength or feather.";
    if (mask.tiles.size() > maxProjectMaskTiles) return "Project exceeds the 1024-mask-tile memory limit.";
    for (auto it = mask.tiles.cbegin(); it != mask.tiles.cend(); ++it) {
        if (qAbs(qint64(it.key().first)) > 128 || qAbs(qint64(it.key().second)) > 128)
            return "Invalid layer mask tile coordinate.";
        if (!pixelsRequired && it->isNull()) continue;
        if (it->size() != QSize(tileSize, tileSize) || it->format() != QImage::Format_Grayscale8
            || it->sizeInBytes() > qint64(tileSize) * tileSize)
            return "Layer masks require 256-pixel Grayscale8 tiles.";
    }
    return {};
}
QJsonObject layerEffectsToJson(const LayerEffects &e) {
    return {{"enabled", e.enabled}, {"blurEnabled", e.blurEnabled}, {"blurRadius", e.blurRadius},
        {"shadowEnabled", e.shadowEnabled}, {"shadowRadius", e.shadowRadius},
        {"shadowOffset", QJsonArray{e.shadowOffset.x(), e.shadowOffset.y()}},
        {"shadowColor", e.shadowColor.name(QColor::HexRgb)}, {"shadowOpacity", e.shadowOpacity},
        {"glowEnabled", e.glowEnabled}, {"glowRadius", e.glowRadius},
        {"glowColor", e.glowColor.name(QColor::HexRgb)}, {"glowOpacity", e.glowOpacity},
        {"colorEnabled", e.colorEnabled}, {"exposure", e.exposure}, {"contrast", e.contrast},
        {"saturation", e.saturation}, {"hue", e.hue}};
}
bool layerEffectsFromJson(const QJsonObject &json, LayerEffects *effects, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    if (!effects) return fail("Missing effect destination.");
    LayerEffects parsed;
    const auto expected = layerEffectsToJson(parsed);
    QSet<QString> keys; for (auto it = expected.constBegin(); it != expected.constEnd(); ++it) keys.insert(it.key());
    if (!keysMatch(json, keys)) return fail("Invalid layer effect fields.");
    for (auto it = expected.constBegin(); it != expected.constEnd(); ++it)
        if (json.value(it.key()).type() != it.value().type()) return fail("Invalid layer effect value type.");
    parsed.enabled = json["enabled"].toBool(); parsed.blurEnabled = json["blurEnabled"].toBool();
    parsed.blurRadius = json["blurRadius"].toDouble(); parsed.shadowEnabled = json["shadowEnabled"].toBool();
    parsed.shadowRadius = json["shadowRadius"].toDouble();
    const auto offset = json["shadowOffset"].toArray();
    if (offset.size() != 2 || !offset[0].isDouble() || !offset[1].isDouble()) return fail("Invalid shadow offset.");
    parsed.shadowOffset = {offset[0].toDouble(), offset[1].toDouble()};
    if (!opaqueColor(json["shadowColor"], &parsed.shadowColor) || !opaqueColor(json["glowColor"], &parsed.glowColor))
        return fail("Effect colors require opaque #RRGGBB values.");
    parsed.shadowOpacity = json["shadowOpacity"].toDouble(); parsed.glowEnabled = json["glowEnabled"].toBool();
    parsed.glowRadius = json["glowRadius"].toDouble(); parsed.glowOpacity = json["glowOpacity"].toDouble();
    parsed.colorEnabled = json["colorEnabled"].toBool(); parsed.exposure = json["exposure"].toDouble();
    parsed.contrast = json["contrast"].toDouble(); parsed.saturation = json["saturation"].toDouble(); parsed.hue = json["hue"].toDouble();
    const auto validation = validateLayerEffects(parsed); if (!validation.isEmpty()) return fail(validation);
    *effects = parsed; return true;
}
QJsonObject layerMaskToJson(const LayerMask &mask) {
    QJsonArray tiles;
    for (auto it = mask.tiles.cbegin(); it != mask.tiles.cend(); ++it) tiles.append(QJsonArray{it.key().first, it.key().second});
    QJsonObject result{{"enabled", mask.enabled}, {"defaultValue", mask.defaultValue}, {"tiles", tiles}};
    if (layerMaskHasProperties(mask)) result.insert("properties", QJsonObject{
        {"position", QJsonArray{mask.position.x(), mask.position.y()}},
        {"scale", QJsonArray{mask.scale.x(), mask.scale.y()}},
        {"rotation", mask.rotation}, {"pivot", QJsonArray{mask.pivot.x(), mask.pivot.y()}},
        {"opacity", mask.opacity}, {"feather", mask.feather}, {"inverted", mask.inverted}});
    return result;
}
bool layerMaskFromJson(const QJsonObject &json, LayerMask *mask, QString *error) {
    auto fail = [error](const QString &message) { if (error) *error = message; return false; };
    auto keys=QSet<QString>{"enabled", "defaultValue", "tiles"};
    if (json.contains("properties")) keys.insert("properties");
    if (!mask || !keysMatch(json, keys) || !json["enabled"].isBool()
        || !json["defaultValue"].isDouble() || !json["tiles"].isArray()) return fail("Invalid layer mask metadata.");
    const auto value = json["defaultValue"].toDouble();
    if (!bounded(value, 0, 255) || std::floor(value) != value) return fail("Invalid layer mask default value.");
    LayerMask parsed; parsed.enabled = json["enabled"].toBool(); parsed.defaultValue = int(value);
    if (json.contains("properties")) {
        if (!json["properties"].isObject()) return fail("Invalid mask property metadata.");
        const auto properties=json["properties"].toObject();
        if (!keysMatch(properties,{"position","scale","rotation","pivot","opacity","feather","inverted"})
            || !properties["rotation"].isDouble() || !properties["opacity"].isDouble()
            || !properties["feather"].isDouble() || !properties["inverted"].isBool())
            return fail("Invalid mask property fields.");
        const auto point=[&](const QString &key,QPointF *output) {
            if (!properties[key].isArray()) return false;
            const auto a=properties[key].toArray();
            if (a.size()!=2 || !a[0].isDouble() || !a[1].isDouble()) return false;
            *output={a[0].toDouble(),a[1].toDouble()};return true;
        };
        if (!point("position",&parsed.position) || !point("scale",&parsed.scale) || !point("pivot",&parsed.pivot))
            return fail("Invalid mask position, scale or pivot.");
        parsed.rotation=properties["rotation"].toDouble();parsed.opacity=properties["opacity"].toDouble();
        parsed.feather=properties["feather"].toDouble();parsed.inverted=properties["inverted"].toBool();
        const auto validation=validateLayerMask(parsed,false);if (!validation.isEmpty()) return fail(validation);
    }
    const auto tiles = json["tiles"].toArray();
    if (tiles.size() > maxProjectMaskTiles) return fail("Project exceeds the 1024-mask-tile memory limit.");
    QPair<int,int> previous; bool first = true;
    for (const auto &entry : tiles) {
        if (!entry.isArray()) return fail("Invalid layer mask tile position.");
        const auto position = entry.toArray();
        if (position.size() != 2) return fail("Invalid layer mask tile position.");
        for (const auto &coordinate : position) {
            const auto n = coordinate.toDouble();
            if (!coordinate.isDouble() || !bounded(n, -128, 128) || std::floor(n) != n)
                return fail("Invalid layer mask tile coordinate.");
        }
        const QPair<int,int> key{position[0].toInt(), position[1].toInt()};
        if (!first && !(previous < key)) return fail("Layer mask tiles must be unique and ordered.");
        parsed.tiles.insert(key, QImage{}); previous = key; first = false;
    }
    *mask = std::move(parsed); return true;
}
bool layerMaskHasProperties(const LayerMask &mask) {
    return !mask.position.isNull() || mask.scale!=QPointF(1,1) || mask.rotation!=0
        || !mask.pivot.isNull() || mask.opacity!=1 || mask.feather!=0 || mask.inverted;
}
QTransform layerMaskTransform(const LayerMask &mask) {
    QTransform result;
    result.translate(mask.position.x()+mask.pivot.x(),mask.position.y()+mask.pivot.y());
    result.rotate(mask.rotation);result.scale(mask.scale.x(),mask.scale.y());
    result.translate(-mask.pivot.x(),-mask.pivot.y());return result;
}
} // namespace toon
