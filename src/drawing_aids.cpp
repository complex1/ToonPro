#include "drawing_aids.h"
#include <QJsonArray>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <algorithm>
#include <initializer_list>

namespace toon {
namespace {
bool knownKeys(const QJsonObject &object, std::initializer_list<const char *> keys) {
    for (auto entry = object.constBegin(); entry != object.constEnd(); ++entry) {
        bool found = false;
        for (const auto *key : keys) if (entry.key() == QLatin1String(key)) { found = true; break; }
        if (!found) return false;
    }
    return true;
}
bool finite(qreal value, qreal minimum, qreal maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}
bool validId(const QString &id, bool optional = false) { return (optional && id.isEmpty()) || !QUuid(id).isNull(); }
bool validPoint(QPointF point, qreal limit = 1000000) { return finite(point.x(), -limit, limit) && finite(point.y(), -limit, limit); }
bool validColor(const QColor &color) { return color.isValid(); }
bool validKind(CelShadeKind kind) { return kind == CelShadeKind::Shadow || kind == CelShadeKind::Highlight; }
bool number(const QJsonValue &value, qreal *out, qreal minimum, qreal maximum) {
    if (!value.isDouble() || !finite(value.toDouble(), minimum, maximum)) return false;
    *out = value.toDouble(); return true;
}
bool integer(const QJsonValue &value, int *out, int minimum, int maximum) {
    qreal parsed;
    if (!number(value, &parsed, minimum, maximum) || std::floor(parsed) != parsed) return false;
    *out = int(parsed); return true;
}
bool point(const QJsonValue &value, QPointF *out, qreal limit = 1000000) {
    if (!value.isArray()) return false;
    const auto values = value.toArray(); qreal x, y;
    if (values.size() != 2 || !number(values[0], &x, -limit, limit) || !number(values[1], &y, -limit, limit)) return false;
    *out = QPointF(x, y); return true;
}
QJsonArray pointJson(QPointF point) { return {point.x(), point.y()}; }
bool color(const QJsonValue &value, QColor *out) {
    if (!value.isString()) return false;
    const auto text = value.toString();
    if (text.size() != 9 || text.front() != '#') return false;
    for (int i = 1; i < text.size(); ++i) {
        const auto ch = text[i].unicode();
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) return false;
    }
    *out = QColor(text); return out->isValid();
}
QString rgba(const QColor &color) { return QColor::fromRgba(color.rgba()).name(QColor::HexArgb); }
QJsonArray pathJson(const QPainterPath &path) {
    QJsonArray points;
    for (int i = 0; i < path.elementCount(); ++i) { const auto element = path.elementAt(i); points.append(QJsonArray{int(element.type), element.x, element.y}); }
    return points;
}
bool readPath(const QJsonValue &value, QPainterPath *path, int *total) {
    if (!value.isArray()) return false;
    const auto elements = value.toArray();
    if (elements.size() < 2 || elements.size() > 32768 || (*total += elements.size()) > maxCelDraftPathElements) return false;
    QVector<QPointF> points; QVector<int> types; points.reserve(elements.size()); types.reserve(elements.size());
    for (const auto &element : elements) {
        if (!element.isArray()) return false;
        const auto triple = element.toArray(); int type; qreal x, y;
        if (triple.size() != 3 || !integer(triple[0], &type, 0, 3) || !number(triple[1], &x, -1000000, 1000000) || !number(triple[2], &y, -1000000, 1000000)) return false;
        types.append(type); points.append({x, y});
    }
    if (types.first() != QPainterPath::MoveToElement) return false;
    for (int i = 0; i < points.size(); ++i) {
        switch (types[i]) {
        case QPainterPath::MoveToElement: path->moveTo(points[i]); break;
        case QPainterPath::LineToElement: path->lineTo(points[i]); break;
        case QPainterPath::CurveToElement:
            if (i + 2 >= points.size() || types[i + 1] != QPainterPath::CurveToDataElement || types[i + 2] != QPainterPath::CurveToDataElement) return false;
            path->cubicTo(points[i], points[i + 1], points[i + 2]); i += 2; break;
        default: return false;
        }
    }
    return path->elementCount() >= 2;
}
QString draftKey(const CelShadingDraft &draft) {
    return QUuid(draft.shotId).toString() + '/' + QUuid(draft.outlineLayerId).toString() + '/' + QString::number(draft.frame);
}
}

bool hasDrawingAids(const DrawingAids &aids) {
    return !aids.lightTable.pins.isEmpty() || !aids.shadingDrafts.isEmpty() || aids.lightTable.solo || !aids.lightTable.visible;
}
QString validateDrawingAids(const DrawingAids &aids, bool requireImages) {
    if (!validId(aids.activeShotId, true) || aids.activeFrame < 0 || aids.activeFrame > 1000000)
        return "Invalid drawing-aid resume context.";
    if (aids.lightTable.pins.size() > maxLightTablePins) return "A light table supports up to eight snapshots.";
    QSet<QUuid> ids; qint64 bytes = 0;
    for (const auto &pin : aids.lightTable.pins) {
        if (!validId(pin.id) || ids.contains(QUuid(pin.id))) return "Invalid or duplicate light-table pin identifier.";
        ids.insert(QUuid(pin.id));
        if (pin.name.trimmed().isEmpty() || pin.name.size() > 2048 || std::any_of(pin.name.cbegin(), pin.name.cend(), [](QChar c) { return c.category() == QChar::Other_Control || c == QChar::LineSeparator || c == QChar::ParagraphSeparator; }))
            return "Light-table names must contain 1 to 2048 characters on one line.";
        if (!validId(pin.shotId, true) || !validId(pin.layerId, true) || pin.shotId.isEmpty() != pin.layerId.isEmpty()) return "Invalid light-table source identifiers.";
        if (!validPoint(pin.bounds.topLeft()) || !finite(pin.bounds.width(), .000001, 1000000) || !finite(pin.bounds.height(), .000001, 1000000)
            || !validPoint(pin.offset, 100000) || !finite(pin.scale, .01, 20) || !finite(pin.rotation, -360, 360) || !finite(pin.opacity, 0, 1))
            return "Invalid light-table placement, scale, angle or opacity.";
        if (requireImages || !pin.image.isNull()) {
            if (pin.image.isNull() || pin.image.format() != QImage::Format_ARGB32_Premultiplied || pin.image.width() > maxLightTablePinDimension || pin.image.height() > maxLightTablePinDimension)
                return "Light-table snapshots need RGBA pixels up to 2048 pixels per side.";
            bytes += pin.image.sizeInBytes();
            if (bytes > maxLightTablePinBytes) return "Light-table snapshots exceed the 64 MiB pixel budget.";
        }
    }
    if (!aids.lightTable.selectedPinId.isEmpty() && !ids.contains(QUuid(aids.lightTable.selectedPinId))) return "Selected light-table pin is missing.";
    if (aids.shadingDrafts.size() > maxCelShadingDrafts) return "A project supports up to 64 unfinished shading drafts.";
    QSet<QString> targets; int pathElements = 0;
    for (const auto &draft : aids.shadingDrafts) {
        if (!validId(draft.shotId) || !validId(draft.outlineLayerId) || draft.frame < 0 || draft.frame > 1000000) return "Invalid shading-draft target.";
        const auto key = draftKey(draft); if (targets.contains(key)) return "Duplicate shading-draft target."; targets.insert(key);
        if (draft.boundaries.size() > 256 || draft.fills.size() > 64 || draft.operations.size() != draft.boundaries.size() + draft.fills.size()) return "Invalid shading draft operation list or limits.";
        if (std::count(draft.operations.cbegin(), draft.operations.cend(), true) != draft.fills.size()) return "Shading draft operations do not match their regions.";
        if (draft.finish != CelBoundaryFinish::Hidden && draft.finish != CelBoundaryFinish::RegionColor) return "Invalid shading boundary finish.";
        if (!validColor(draft.shadow) || !validColor(draft.highlight) || !validColor(draft.shadowGuide) || !validColor(draft.highlightGuide)
            || !finite(draft.width, 1, 12) || draft.kind < 0 || draft.kind > 1 || draft.mode < 0 || draft.mode > 1) return "Invalid shading draft controls.";
        for (const auto &boundary : draft.boundaries) {
            if (!validKind(boundary.kind) || !validColor(boundary.guideColor) || !finite(boundary.width, 1, 12) || boundary.path.elementCount() < 2 || boundary.path.elementCount() > 32768)
                return "Invalid shading boundary.";
            pathElements += boundary.path.elementCount(); if (pathElements > maxCelDraftPathElements) return "Shading drafts exceed the one-million path-element limit.";
            for (int i = 0; i < boundary.path.elementCount(); ++i) { const auto e = boundary.path.elementAt(i); if (!validPoint({e.x, e.y})) return "Invalid shading boundary coordinates."; }
        }
        for (const auto &fill : draft.fills)
            if (!validKind(fill.kind) || !validColor(fill.color) || fill.color.alpha() == 0 || !validPoint(fill.seed)) return "Invalid shading fill region.";
    }
    return {};
}
QJsonObject drawingAidsToJson(const DrawingAids &aids) {
    QJsonArray pins, drafts;
    for (const auto &pin : aids.lightTable.pins) pins.append(QJsonObject{
        {"id", pin.id}, {"name", pin.name}, {"shotId", pin.shotId}, {"layerId", pin.layerId},
        {"width", pin.image.width()}, {"height", pin.image.height()},
        {"bounds", QJsonArray{pin.bounds.x(), pin.bounds.y(), pin.bounds.width(), pin.bounds.height()}},
        {"offset", pointJson(pin.offset)}, {"scale", pin.scale}, {"rotation", pin.rotation}, {"opacity", pin.opacity}, {"visible", pin.visible}});
    for (const auto &draft : aids.shadingDrafts) {
        QJsonArray boundaries, fills, operations;
        for (const auto &boundary : draft.boundaries) boundaries.append(QJsonObject{{"kind", int(boundary.kind)}, {"color", rgba(boundary.guideColor)}, {"width", boundary.width}, {"fillRule", int(boundary.path.fillRule())}, {"path", pathJson(boundary.path)}});
        for (const auto &fill : draft.fills) fills.append(QJsonObject{{"kind", int(fill.kind)}, {"color", rgba(fill.color)}, {"seed", pointJson(fill.seed)}});
        for (const auto operation : draft.operations) operations.append(operation);
        drafts.append(QJsonObject{{"shotId", draft.shotId}, {"outlineLayerId", draft.outlineLayerId}, {"frame", draft.frame},
            {"boundaries", boundaries}, {"fills", fills}, {"operations", operations}, {"finish", int(draft.finish)},
            {"shadow", rgba(draft.shadow)}, {"highlight", rgba(draft.highlight)}, {"shadowGuide", rgba(draft.shadowGuide)}, {"highlightGuide", rgba(draft.highlightGuide)},
            {"width", draft.width}, {"kind", draft.kind}, {"mode", draft.mode}});
    }
    return {{"lightTable", QJsonObject{{"pins", pins}, {"selectedPinId", aids.lightTable.selectedPinId}, {"solo", aids.lightTable.solo}, {"visible", aids.lightTable.visible}}},
        {"shadingDrafts", drafts}, {"activeShotId", aids.activeShotId}, {"activeFrame", aids.activeFrame}};
}
std::optional<DrawingAids> drawingAidsFromJson(const QJsonObject &json, QString *error) {
    const auto fail = [error](const QString &message) -> std::optional<DrawingAids> { if (error) *error = message; return {}; };
    if (!knownKeys(json, {"lightTable", "shadingDrafts", "activeShotId", "activeFrame"})) return fail("Unknown drawing-aid metadata field.");
    if (!json.value("lightTable").isObject() || !json.value("shadingDrafts").isArray() || !json.value("activeShotId").isString()) return fail("Invalid drawing-aid metadata.");
    DrawingAids aids; aids.activeShotId = json.value("activeShotId").toString();
    if (!integer(json.value("activeFrame"), &aids.activeFrame, 0, 1000000)) return fail("Invalid drawing-aid resume frame.");
    const auto light = json.value("lightTable").toObject();
    if (!knownKeys(light, {"pins", "selectedPinId", "solo", "visible"})) return fail("Unknown light-table metadata field.");
    if (!light.value("pins").isArray() || !light.value("selectedPinId").isString() || !light.value("solo").isBool() || !light.value("visible").isBool()) return fail("Invalid light-table metadata.");
    aids.lightTable.selectedPinId = light.value("selectedPinId").toString(); aids.lightTable.solo = light.value("solo").toBool(); aids.lightTable.visible = light.value("visible").toBool();
    const auto pins = light.value("pins").toArray(); if (pins.size() > maxLightTablePins) return fail("A light table supports up to eight snapshots.");
    qint64 bytes = 0;
    for (const auto &value : pins) {
        if (!value.isObject()) return fail("Invalid light-table pin."); const auto entry = value.toObject(); LightTablePin pin;
        if (!knownKeys(entry, {"id", "name", "shotId", "layerId", "width", "height", "bounds", "offset", "scale", "rotation", "opacity", "visible"})) return fail("Unknown light-table pin field.");
        for (const auto *key : {"id", "name", "shotId", "layerId"}) if (!entry.value(key).isString()) return fail("Invalid light-table identity metadata.");
        pin.id = entry.value("id").toString(); pin.name = entry.value("name").toString(); pin.shotId = entry.value("shotId").toString(); pin.layerId = entry.value("layerId").toString();
        int width, height;
        if (!integer(entry.value("width"), &width, 1, maxLightTablePinDimension) || !integer(entry.value("height"), &height, 1, maxLightTablePinDimension)) return fail("Invalid light-table snapshot dimensions.");
        bytes += qint64(width) * height * 4; if (bytes > maxLightTablePinBytes) return fail("Light-table snapshots exceed the 64 MiB pixel budget.");
        const auto bounds = entry.value("bounds").toArray(); qreal x, y, w, h;
        if (!entry.value("bounds").isArray() || bounds.size() != 4 || !number(bounds[0], &x, -1000000, 1000000) || !number(bounds[1], &y, -1000000, 1000000)
            || !number(bounds[2], &w, .000001, 1000000) || !number(bounds[3], &h, .000001, 1000000) || !point(entry.value("offset"), &pin.offset, 100000)
            || !number(entry.value("scale"), &pin.scale, .01, 20) || !number(entry.value("rotation"), &pin.rotation, -360, 360) || !number(entry.value("opacity"), &pin.opacity, 0, 1) || !entry.value("visible").isBool())
            return fail("Invalid light-table placement or visibility.");
        pin.bounds = QRectF(x, y, w, h); pin.visible = entry.value("visible").toBool(); aids.lightTable.pins.append(pin);
    }
    const auto drafts = json.value("shadingDrafts").toArray(); if (drafts.size() > maxCelShadingDrafts) return fail("A project supports up to 64 unfinished shading drafts.");
    int elements = 0;
    for (const auto &value : drafts) {
        if (!value.isObject()) return fail("Invalid shading draft."); const auto entry = value.toObject(); CelShadingDraft draft;
        if (!knownKeys(entry, {"shotId", "outlineLayerId", "frame", "boundaries", "fills", "operations", "finish", "shadow", "highlight", "shadowGuide", "highlightGuide", "width", "kind", "mode"})) return fail("Unknown shading-draft metadata field.");
        if (!entry.value("shotId").isString() || !entry.value("outlineLayerId").isString() || !entry.value("boundaries").isArray() || !entry.value("fills").isArray() || !entry.value("operations").isArray()) return fail("Invalid shading-draft metadata.");
        draft.shotId = entry.value("shotId").toString(); draft.outlineLayerId = entry.value("outlineLayerId").toString(); int finish;
        if (!integer(entry.value("frame"), &draft.frame, 0, 1000000) || !integer(entry.value("finish"), &finish, 0, 1) || !integer(entry.value("kind"), &draft.kind, 0, 1) || !integer(entry.value("mode"), &draft.mode, 0, 1)
            || !number(entry.value("width"), &draft.width, 1, 12) || !color(entry.value("shadow"), &draft.shadow) || !color(entry.value("highlight"), &draft.highlight)
            || !color(entry.value("shadowGuide"), &draft.shadowGuide) || !color(entry.value("highlightGuide"), &draft.highlightGuide)) return fail("Invalid shading draft controls.");
        draft.finish = CelBoundaryFinish(finish);
        const auto boundaries = entry.value("boundaries").toArray(), fills = entry.value("fills").toArray(), operations = entry.value("operations").toArray();
        if (boundaries.size() > 256 || fills.size() > 64 || operations.size() != boundaries.size() + fills.size()) return fail("Invalid shading draft operation list or limits.");
        for (const auto &item : boundaries) {
            if (!item.isObject()) return fail("Invalid shading boundary."); const auto boundaryJson = item.toObject(); CelBoundary boundary; int kind, fillRule;
            if (!knownKeys(boundaryJson, {"kind", "color", "width", "fillRule", "path"})) return fail("Unknown shading boundary field.");
            if (!integer(boundaryJson.value("kind"), &kind, 0, 1) || !integer(boundaryJson.value("fillRule"), &fillRule, 0, 1) || !number(boundaryJson.value("width"), &boundary.width, 1, 12)
                || !color(boundaryJson.value("color"), &boundary.guideColor) || !readPath(boundaryJson.value("path"), &boundary.path, &elements)) return fail("Invalid shading boundary geometry or controls.");
            boundary.kind = CelShadeKind(kind); boundary.path.setFillRule(Qt::FillRule(fillRule)); draft.boundaries.append(boundary);
        }
        for (const auto &item : fills) {
            if (!item.isObject()) return fail("Invalid shading fill."); const auto fillJson = item.toObject(); CelShadeFill fill; int kind; QPointF seed;
            if (!knownKeys(fillJson, {"kind", "color", "seed"})) return fail("Unknown shading fill field.");
            if (!integer(fillJson.value("kind"), &kind, 0, 1) || !color(fillJson.value("color"), &fill.color) || !point(fillJson.value("seed"), &seed)
                || seed.x() != std::floor(seed.x()) || seed.y() != std::floor(seed.y())) return fail("Invalid shading fill seed or color.");
            fill.kind = CelShadeKind(kind); fill.seed = seed.toPoint(); draft.fills.append(fill);
        }
        for (const auto &operation : operations) { if (!operation.isBool()) return fail("Invalid shading operation type."); draft.operations.append(operation.toBool()); }
        aids.shadingDrafts.append(draft);
    }
    const auto validation = validateDrawingAids(aids, false); if (!validation.isEmpty()) return fail(validation);
    return aids;
}
bool celShadingDraftEqual(const CelShadingDraft &a, const CelShadingDraft &b) {
    if (a.shotId != b.shotId || a.outlineLayerId != b.outlineLayerId || a.frame != b.frame || a.finish != b.finish
        || a.operations != b.operations || a.shadow != b.shadow || a.highlight != b.highlight || a.shadowGuide != b.shadowGuide || a.highlightGuide != b.highlightGuide
        || a.width != b.width || a.kind != b.kind || a.mode != b.mode || a.boundaries.size() != b.boundaries.size() || a.fills.size() != b.fills.size()) return false;
    for (int i = 0; i < a.boundaries.size(); ++i) {
        const auto &left = a.boundaries[i], &right = b.boundaries[i];
        if (left.path != right.path || left.kind != right.kind || left.guideColor != right.guideColor || left.width != right.width) return false;
    }
    for (int i = 0; i < a.fills.size(); ++i) {
        const auto &left = a.fills[i], &right = b.fills[i];
        if (left.seed != right.seed || left.kind != right.kind || left.color != right.color) return false;
    }
    return true;
}
bool lightTableStateEqual(const LightTableState &a, const LightTableState &b) {
    if (a.selectedPinId != b.selectedPinId || a.solo != b.solo || a.visible != b.visible || a.pins.size() != b.pins.size()) return false;
    for (int i = 0; i < a.pins.size(); ++i) {
        const auto &left = a.pins[i], &right = b.pins[i];
        if (left.id != right.id || left.name != right.name || left.shotId != right.shotId || left.layerId != right.layerId || left.bounds != right.bounds
            || left.offset != right.offset || left.scale != right.scale || left.rotation != right.rotation || left.opacity != right.opacity || left.visible != right.visible || left.image != right.image) return false;
    }
    return true;
}
bool drawingAidsEqual(const DrawingAids &a, const DrawingAids &b) {
    if (!lightTableStateEqual(a.lightTable, b.lightTable) || a.activeShotId != b.activeShotId || a.activeFrame != b.activeFrame || a.shadingDrafts.size() != b.shadingDrafts.size()) return false;
    for (int i = 0; i < a.shadingDrafts.size(); ++i) if (!celShadingDraftEqual(a.shadingDrafts[i], b.shadingDrafts[i])) return false;
    return true;
}
} // namespace toon
