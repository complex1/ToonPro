#include "drawing_guide.h"
#include "guide_geometry.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace toon {
namespace {
constexpr qint64 maxGuideSetBytes = 1024 * 1024;
constexpr qreal coordinateLimit = 1000000;

bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
bool bounded(qreal number) {
    return std::isfinite(number) && std::abs(number) <= coordinateLimit;
}
bool opacityValid(qreal opacity) {
    return std::isfinite(opacity) && opacity >= 0 && opacity <= 1;
}
bool areaValid(const QRectF &area) {
    return bounded(area.x()) && bounded(area.y()) && bounded(area.right()) && bounded(area.bottom())
        && std::isfinite(area.width()) && std::isfinite(area.height()) && area.width() > 0 && area.height() > 0;
}
QString typeKey(GuideType type) {
    switch (type) {
    case GuideType::Perspective: return "perspective";
    case GuideType::VanishingPoint: return "vanishing-point";
    case GuideType::TwoPoint: return "two-point";
    case GuideType::ParallelRuler: return "parallel-ruler";
    case GuideType::ThreePoint: return "three-point";
    case GuideType::Fisheye: return "fisheye";
    case GuideType::Ellipse: return "ellipse";
    }
    return {};
}
std::optional<GuideType> parseType(const QJsonValue &value) {
    if (!value.isString()) return {};
    for (auto type : {GuideType::Perspective, GuideType::VanishingPoint, GuideType::TwoPoint, GuideType::ParallelRuler,
                      GuideType::ThreePoint, GuideType::Fisheye, GuideType::Ellipse})
        if (value.toString() == typeKey(type)) return type;
    return {};
}
bool parseColor(const QJsonValue &value, QColor *color) {
    if (!value.isString()) return false;
    const auto text = value.toString();
    if ((text.size() != 7 && text.size() != 9) || !text.startsWith('#')) return false;
    for (qsizetype i = 1; i < text.size(); ++i) {
        const auto c = text[i].toLower();
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
    }
    *color = QColor(text);
    return color->isValid();
}
bool parseNumbers(const QJsonValue &value, int count, QVector<qreal> *numbers) {
    if (!value.isArray()) return false;
    const auto array = value.toArray();
    if (array.size() != count) return false;
    numbers->clear();
    for (const auto &number : array) {
        if (!number.isDouble() || !std::isfinite(number.toDouble())) return false;
        numbers->append(number.toDouble());
    }
    return true;
}
}

int guidePointCount(GuideType type) {
    switch (type) {
    case GuideType::Perspective: return 4;
    case GuideType::VanishingPoint: return 1;
    case GuideType::TwoPoint: return 3;
    case GuideType::ParallelRuler: return 2;
    case GuideType::ThreePoint: return 4;
    case GuideType::Fisheye: return 3;
    case GuideType::Ellipse: return 3;
    }
    return 0;
}
QString guideTypeName(GuideType type) {
    switch (type) {
    case GuideType::Perspective: return "Perspective";
    case GuideType::VanishingPoint: return "Vanishing point";
    case GuideType::TwoPoint: return "Two-point perspective";
    case GuideType::ParallelRuler: return "Parallel ruler";
    case GuideType::ThreePoint: return "Three-point perspective";
    case GuideType::Fisheye: return "Fisheye perspective";
    case GuideType::Ellipse: return "Ellipse ruler";
    }
    return {};
}

QString validateGuides(const QVector<DrawingGuide> &guides, const GuideSettings &settings) {
    if (guides.size() > maxDrawingGuides) return "A shot supports up to 64 drawing guides.";
    if (!settings.color.isValid() || !opacityValid(settings.opacity)) return "Invalid shared drawing guide color or opacity.";
    QSet<QUuid> ids;
    for (const auto &guide : guides) {
        const auto id = QUuid(guide.id);
        if (id.isNull() || ids.contains(id)) return "Invalid or duplicate drawing guide identifier.";
        ids.insert(id);
        if (guide.name.trimmed().isEmpty() || guide.name.size() > 100)
            return "Drawing guide names must contain between 1 and 100 characters.";
        const auto count = guidePointCount(guide.type);
        if (!count || guide.points.size() != count) return "Invalid drawing guide type or control-point count.";
        for (const auto &point : guide.points)
            if (!bounded(point.x()) || !bounded(point.y())) return "Drawing guide coordinates must be finite and within ±1,000,000 pixels.";
        if (!guide.color.isValid() || !opacityValid(guide.opacity)) return "Invalid drawing guide color or opacity.";
        if (guide.subdivisions < 1 || guide.subdivisions > 64) return "Drawing guide subdivisions must be between 1 and 64.";
        if (!areaValid(guide.area)) return "Drawing guide areas must have positive dimensions and bounded, finite coordinates.";
        if (!validGuideGeometry(guide)) return "Drawing guide geometry must have distinct points and a valid perspective plane or ellipse.";
    }
    return {};
}

QJsonObject guidesToJson(const QVector<DrawingGuide> &guides, const GuideSettings &settings) {
    QJsonArray entries;
    for (const auto &guide : guides) {
        QJsonArray points;
        for (const auto &point : guide.points) points.append(QJsonArray{point.x(), point.y()});
        entries.append(QJsonObject{{"id", guide.id}, {"name", guide.name}, {"type", typeKey(guide.type)},
            {"points", points}, {"visible", guide.visible}, {"locked", guide.locked}, {"snapping", guide.snapping},
            {"customColor", guide.customColor}, {"color", guide.color.name(QColor::HexArgb)}, {"opacity", guide.opacity},
            {"subdivisions", guide.subdivisions}, {"vertical", guide.vertical}, {"limited", guide.limited},
            {"area", QJsonArray{guide.area.x(), guide.area.y(), guide.area.width(), guide.area.height()}}});
    }
    return {{"settings", QJsonObject{{"visible", settings.visible}, {"snapping", settings.snapping},
        {"handles", settings.handles}, {"color", settings.color.name(QColor::HexArgb)}, {"opacity", settings.opacity}}},
        {"guides", entries}};
}

bool guidesFromJson(const QJsonObject &object, QVector<DrawingGuide> *guides, GuideSettings *settings, QString *error) {
    if (!guides || !settings) return fail(error, "Missing drawing guide destination.");
    if (!object.value("settings").isObject() || !object.value("guides").isArray())
        return fail(error, "Missing drawing guide settings or guide list.");
    const auto config = object.value("settings").toObject();
    GuideSettings parsedSettings;
    for (const char *key : {"visible", "snapping", "handles"})
        if (!config.value(QLatin1String(key)).isBool()) return fail(error, "Invalid drawing guide visibility, snapping or handle settings.");
    if (!config.value("opacity").isDouble() || !parseColor(config.value("color"), &parsedSettings.color))
        return fail(error, "Invalid shared drawing guide color or opacity.");
    parsedSettings.visible = config.value("visible").toBool();
    parsedSettings.snapping = config.value("snapping").toBool();
    parsedSettings.handles = config.value("handles").toBool();
    parsedSettings.opacity = config.value("opacity").toDouble();
    const auto entries = object.value("guides").toArray();
    if (entries.size() > maxDrawingGuides) return fail(error, "A shot supports up to 64 drawing guides.");
    QVector<DrawingGuide> parsedGuides;
    parsedGuides.reserve(entries.size());
    for (const auto &value : entries) {
        if (!value.isObject()) return fail(error, "Invalid drawing guide entry.");
        const auto entry = value.toObject();
        DrawingGuide guide;
        if (!entry.value("id").isString() || !entry.value("name").isString())
            return fail(error, "Invalid drawing guide identifier or name.");
        const auto type = parseType(entry.value("type"));
        if (!type) return fail(error, "Unsupported drawing guide type.");
        guide.type = *type;
        guide.id = entry.value("id").toString();
        guide.name = entry.value("name").toString();
        for (const char *key : {"visible", "locked", "snapping", "customColor", "vertical", "limited"})
            if (!entry.value(QLatin1String(key)).isBool()) return fail(error, "Invalid drawing guide toggle settings.");
        guide.visible = entry.value("visible").toBool();
        guide.locked = entry.value("locked").toBool();
        guide.snapping = entry.value("snapping").toBool();
        guide.customColor = entry.value("customColor").toBool();
        guide.vertical = entry.value("vertical").toBool();
        guide.limited = entry.value("limited").toBool();
        if (!entry.value("opacity").isDouble() || !parseColor(entry.value("color"), &guide.color))
            return fail(error, "Invalid drawing guide color or opacity.");
        guide.opacity = entry.value("opacity").toDouble();
        const auto subdivisions = entry.value("subdivisions");
        const auto n = subdivisions.toDouble();
        if (!subdivisions.isDouble() || !std::isfinite(n) || n < 1 || n > 64 || std::floor(n) != n)
            return fail(error, "Drawing guide subdivisions must be an integer between 1 and 64.");
        guide.subdivisions = int(n);
        if (!entry.value("points").isArray() || entry.value("points").toArray().size() != guidePointCount(guide.type))
            return fail(error, "Invalid drawing guide control-point list.");
        QVector<qreal> numbers;
        for (const auto &point : entry.value("points").toArray()) {
            if (!parseNumbers(point, 2, &numbers)) return fail(error, "Invalid drawing guide coordinates.");
            guide.points.append({numbers[0], numbers[1]});
        }
        if (!parseNumbers(entry.value("area"), 4, &numbers)) return fail(error, "Invalid drawing guide area.");
        guide.area = {numbers[0], numbers[1], numbers[2], numbers[3]};
        parsedGuides.append(guide);
    }
    const auto validation = validateGuides(parsedGuides, parsedSettings);
    if (!validation.isEmpty()) return fail(error, validation);
    *guides = parsedGuides;
    *settings = parsedSettings;
    if (error) error->clear();
    return true;
}

bool saveGuideSet(const QVector<DrawingGuide> &guides, const GuideSettings &settings, const QString &path, QString *error) {
    const auto validation = validateGuides(guides, settings);
    if (!validation.isEmpty()) return fail(error, validation);
    auto object = guidesToJson(guides, settings);
    object.insert("format", "toon-pro-guides");
    object.insert("version", 1);
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (bytes.size() > maxGuideSetBytes) return fail(error, "Drawing guide set exceeds the 1 MiB limit.");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, file.errorString());
    if (error) error->clear();
    return true;
}

bool loadGuideSet(const QString &path, QVector<DrawingGuide> *guides, GuideSettings *settings, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    if (file.size() > maxGuideSetBytes) return fail(error, "Drawing guide set exceeds the 1 MiB limit.");
    const auto bytes = file.read(maxGuideSetBytes + 1);
    if (bytes.size() > maxGuideSetBytes) return fail(error, "Drawing guide set exceeds the 1 MiB limit.");
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return fail(error, "Invalid drawing guide JSON.");
    const auto object = document.object();
    if (object.value("format") != "toon-pro-guides" || object.value("version") != 1)
        return fail(error, "This file is not a supported Toon Pro guide set (version 1 required).");
    return guidesFromJson(object, guides, settings, error);
}
} // namespace toon
