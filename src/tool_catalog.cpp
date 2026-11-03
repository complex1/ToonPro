#include "tool_catalog.h"
#include "standard_brushes.h"
#include "catalog_brushes.h"
#include "icons.h"

#include <QBuffer>
#include <QAction>
#include <QCache>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QUndoCommand>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>

namespace toon {
namespace {
constexpr qint64 maxImageBytes = 6 * 1024 * 1024;
constexpr qint64 maxColorImageBytes = 17 * 1024 * 1024;
constexpr qint64 maxResourcePixels = 64 * 1024 * 1024;
constexpr qsizetype maxOptionBytes = 64 * 1024;

bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
QString freshId(const char *prefix) {
    return QString::fromLatin1(prefix) + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
bool validName(const QString &name) { return !name.trimmed().isEmpty() && name.size() <= 120; }
bool validBrushType(BrushType type) {
    return int(type) >= int(BrushType::Pen) && int(type) <= int(BrushType::DynamicStamp);
}
QString catalogFamilyId(const QString &presetId) {
    if (!presetId.startsWith("builtin/catalog/")) return {};
    const auto category = standardBrushCategory(presetId);
    if (category.isEmpty()) return {};
    QString slug;
    for (const auto character : category.toLower()) {
        if (character.isLetterOrNumber()) slug += character;
        else if (!slug.isEmpty() && !slug.endsWith('-')) slug += '-';
    }
    while (slug.endsWith('-')) slug.chop(1);
    return "group/catalog/" + slug;
}
QString catalogFamilyIcon(const QString &category) {
    if (category == "Line art and ink") return "pen-tool";
    if (category == "Sketch and dry media") return "pencil";
    if (category == "Soft paint and spray") return "blend";
    if (category == "Grass and ground cover") return "sprout";
    if (category == "Trees and foliage") return "trees";
    if (category == "Clouds and atmosphere") return "cloud";
    if (category == "Ground rock and terrain") return "mountain";
    if (category == "Walls and construction surfaces") return "brick-wall";
    if (category == "Architecture and repeating details") return "panels-top-left";
    if (category == "Water and reflections") return "droplet";
    if (category == "Weather and particles") return "sparkles";
    if (category == "Painted color stamps") return "palette";
    if (category == "Manga tones and hatching") return "hash";
    if (category == "Manga effects and decoration") return "brush";
    return "image";
}
BrushType presetBrushType(const BrushPreset &preset) {
    if (preset.builtIn && preset.id.startsWith("builtin/catalog/")) {
        const auto tags = standardBrushTags(preset.id);
        if (tags.contains("Colored stamp")) return BrushType::ColoredStamp;
        if (tags.contains("Pen") || tags.contains("Pencil")) return BrushType::Pen;
        if (tags.contains("Ink")) return BrushType::Ink;
        if (tags.contains("Spray") || tags.contains("Airbrush")) return BrushType::Spray;
        if (tags.contains("Stamp")) return BrushType::Stamp;
        if (tags.contains("Soft")) return BrushType::Soft;
        return BrushType::Custom;
    }
    // A textured pencil is still a drawing pen; a sampled tip alone does not
    // make every native preset a stamp tool in the management filters.
    static const QHash<QString,BrushType> standardTypes{
        {"builtin/hard-round",BrushType::Custom},{"builtin/pressure-round",BrushType::Pen},
        {"builtin/soft-airbrush",BrushType::Soft},{"builtin/g-pen",BrushType::Pen},
        {"builtin/fine-liner",BrushType::Pen},{"builtin/graphite-pencil",BrushType::Pen},
        {"builtin/mechanical-pencil",BrushType::Pen},{"builtin/charcoal",BrushType::Custom},
        {"builtin/dry-ink",BrushType::Ink},{"builtin/flat-paint",BrushType::Custom},
        {"builtin/soft-paint",BrushType::Soft},{"builtin/pigment-wash",BrushType::Custom},
        {"builtin/particle-spray",BrushType::Spray},{"builtin/stipple",BrushType::Spray},
        {"builtin/chisel-marker",BrushType::Pen},{"builtin/calligraphy",BrushType::Ink}};
    if (preset.builtIn && standardTypes.contains(preset.id)) return standardTypes.value(preset.id);
    return inferredBrushType(preset.name,preset.brush);
}
bool validCategory(const QString &category) {
    return category.size() <= ToolCatalog::maxCategoryLength &&
        std::none_of(category.cbegin(), category.cend(), [](QChar c) {
            return c.isNull() || c.category() == QChar::Other_Control;
        });
}
bool validId(const QString &id) { return !id.isEmpty() && id.size() <= 200; }
bool validIcon(const QString &icon) {
    if (icon.isEmpty() || icon.size() > 64) return false;
    return std::all_of(icon.cbegin(), icon.cend(), [](QChar c) {
        return c.isLetterOrNumber() || c == '-';
    });
}

struct BuiltinIconUpgrade {
    const char *id;
    const char *previous;
    FeatureIcon feature;
    Canvas::Tool engine;
    int variant;
};
const QVector<BuiltinIconUpgrade> &drawingIconUpgrades() {
    using T = Canvas::Tool;
    static const QVector<BuiltinIconUpgrade> upgrades{
        {"arc", "spline", FeatureIcon::Arc, T::Arc, 0},
        {"curve", "spline", FeatureIcon::Bezier, T::Curve, 0},
        {"simplify-brush", "spline", FeatureIcon::Simplify, T::VectorSimplify, 0},
        {"gradient-linear", "blend", FeatureIcon::LinearGradient, T::Gradient, 0},
        {"gradient-radial", "radius", FeatureIcon::RadialGradient, T::Gradient, 1},
        {"blur", "blend", FeatureIcon::Blur, T::Retouch, 2},
        {"guide-0", "grid-2x2", FeatureIcon::PerspectiveGrid, T::Guides, 0},
        {"guide-1", "focus", FeatureIcon::VanishingPoint, T::Guides, 1},
        {"guide-2", "split", FeatureIcon::TwoPointPerspective, T::Guides, 2},
        {"guide-3", "minus", FeatureIcon::ParallelGuide, T::Guides, 3},
        {"guide-4", "triangle", FeatureIcon::ThreePointPerspective, T::Guides, 4},
        {"guide-5", "radius", FeatureIcon::Fisheye, T::Guides, 5},
        {"warp", "grid-2x2", FeatureIcon::Warp, T::AdvancedTransform, 1}};
    return upgrades;
}

void upgradeDrawingIcons(QVector<ToolDefinition> &tools, QVector<ToolboxEntry> &entries) {
    // Only known built-in definitions still carrying their shipped symbol are
    // upgraded. Copies, user-picked symbols and custom group artwork survive.
    for (const auto &upgrade : drawingIconUpgrades()) {
        for (auto &tool : tools) {
            if (tool.id != upgrade.id || tool.engine != upgrade.engine || tool.variant != upgrade.variant
                || tool.icon != upgrade.previous) continue;
            tool.icon = featureIconName(upgrade.feature);
            for (auto &entry : entries)
                if (!entry.group && entry.tools.size() == 1 && entry.tools.first() == tool.id
                    && entry.customIcon.isNull() && entry.icon == upgrade.previous)
                    entry.icon = tool.icon;
        }
    }
    for (auto &entry : entries)
        if (entry.id == "group/guides" && entry.group && entry.customIcon.isNull() && entry.icon == "grid-2x2")
            entry.icon = featureIconName(FeatureIcon::PerspectiveGrid);
}
bool normalizeTags(const QStringList &input, QStringList &result) {
    result.clear();
    // Keep hostile or accidentally enormous input bounded before normalization.
    if (input.size() > ToolCatalog::maxTools) return false;
    QSet<QString> seen;
    for (const auto &raw : input) {
        const auto tag = raw.trimmed();
        if (tag.isEmpty()) continue;
        if (tag.size() > ToolCatalog::maxTagLength || tag.contains(',') ||
            std::any_of(tag.cbegin(), tag.cend(), [](QChar c) { return c.isNull() || c.category() == QChar::Other_Control; }))
            return false;
        const auto folded = tag.toCaseFolded();
        if (seen.contains(folded)) continue;
        if (result.size() >= ToolCatalog::maxTags) return false;
        seen.insert(folded); result.push_back(tag);
    }
    return true;
}
QColor metadataColor(QColor color) {
    if (!color.isValid()) return {};
    return QColor(color.name(QColor::HexRgb));
}
bool readMetadata(const QJsonObject &object, QStringList &tags, QColor &color, QString *error) {
    if (object.contains("tags")) {
        const auto value = object.value("tags");
        if (!value.isArray()) return fail(error, "Tool tags must be a list of short names.");
        if (value.toArray().size() > ToolCatalog::maxTools) return fail(error, "A tool has too many tags.");
        QStringList raw;
        for (const auto &tag : value.toArray()) {
            if (!tag.isString()) return fail(error, "A tool tag is not a name.");
            raw.push_back(tag.toString());
        }
        if (!normalizeTags(raw, tags)) return fail(error, "A tool has too many tags or an invalid tag name.");
    }
    if (object.contains("color")) {
        const auto value = object.value("color");
        const auto text = value.toString();
        if (!value.isString() || (!text.isEmpty() && (text.size() != 7 || !text.startsWith('#') || !QColor(text).isValid())))
            return fail(error, "A tool has an invalid label color.");
        if (!text.isEmpty()) color = metadataColor(QColor(text));
    }
    return true;
}
ToolState normalized(ToolState state) {
    state.brush.color = BrushSettings{}.color;
    state.brush.backgroundColor = BrushSettings{}.backgroundColor;
    return state;
}
bool canonicalCatalogTool(const ToolDefinition &definition) {
    return definition.engine == Canvas::Tool::Brush &&
        definition.brushPresetId.startsWith("builtin/catalog/") &&
        definition.id == "brush/" + definition.brushPresetId;
}
// Only unchanged leaves adopt new defaults: editing size must not prevent a
// corrected texture from arriving, nor may a texture fix reset a pressure curve.
QJsonValue mergeRecipeDefaults(const QJsonValue &current, const QJsonValue &previous,
                              const QJsonValue &replacement) {
    if (current == previous) return replacement;
    if (!current.isObject() || !previous.isObject() || !replacement.isObject()) return current;
    auto merged = current.toObject();
    const auto before = previous.toObject(), after = replacement.toObject();
    QSet<QString> keys;
    for (const auto &key : before.keys()) keys.insert(key);
    for (const auto &key : after.keys()) keys.insert(key);
    for (const auto &key : keys)
        merged.insert(key, mergeRecipeDefaults(merged.value(key), before.value(key), after.value(key)));
    return merged;
}
bool upgradeRecipe(ToolDefinition &definition, const BrushPreset &previous,
                   const BrushPreset &replacement, QString *error) {
    auto brush = definition.state.brush;
    auto merged = mergeRecipeDefaults(brushSettingsToJson(brush),
        brushSettingsToJson(previous.brush), brushSettingsToJson(replacement.brush)).toObject();
    const bool customPrimary = brush.tipMask != previous.brush.tipMask || brush.colorTip != previous.brush.colorTip ||
        brush.tipFrames != previous.brush.tipFrames || brush.tipOrder != previous.brush.tipOrder;
    if (customPrimary) {
        const auto before = brushSettingsToJson(brush);
        if (before.contains("tipSet")) merged["tipSet"] = before["tipSet"]; else merged.remove("tipSet");
    }
    auto replaceUneditedImage = [](QImage &current, const QImage &before, const QImage &after) {
        if (current == before) current = after;
    };
    if (!customPrimary) {
        replaceUneditedImage(brush.tipMask, previous.brush.tipMask, replacement.brush.tipMask);
        replaceUneditedImage(brush.colorTip, previous.brush.colorTip, replacement.brush.colorTip);
    }
    replaceUneditedImage(brush.texture.pattern, previous.brush.texture.pattern, replacement.brush.texture.pattern);
    replaceUneditedImage(brush.dual.tipMask, previous.brush.dual.tipMask, replacement.brush.dual.tipMask);
    replaceUneditedImage(brush.surface.image, previous.brush.surface.image, replacement.brush.surface.image);
    if (!brushSettingsFromJson(merged, brush, error)) return false;
    definition.state.brush = std::move(brush);
    if (definition.state.smoothing == previous.smoothing) definition.state.smoothing = replacement.smoothing;
    definition.builtinRecipeRevision = catalogBrushRecipeRevision;
    return true;
}
bool validState(const ToolState &state, QString *error) {
    if (!std::isfinite(state.smoothing) || state.smoothing < 0 || state.smoothing > 100)
        return fail(error, "A tool has invalid stabilization settings.");
    if (!validateBrushSettings(state.brush, error)) return false;
    if (!validateBrushTipImages(state.brush, error)) return false;
    if (QJsonDocument(state.options).toJson(QJsonDocument::Compact).size() > maxOptionBytes)
        return fail(error, "A tool contains too many property settings.");
    for (const QImage *image : {&state.brush.tipMask, &state.brush.texture.pattern, &state.brush.dual.tipMask})
        if (!image->isNull() && (image->format() != QImage::Format_Grayscale8 ||
            image->width() > BrushLibrary::maxTipSide || image->height() > BrushLibrary::maxTipSide))
            return fail(error, "Tool brush resources must be grayscale images no larger than 2048 × 2048.");
    if (!state.brush.colorTip.isNull() &&
        (state.brush.colorTip.format() != QImage::Format_ARGB32_Premultiplied ||
         state.brush.colorTip.width() > BrushLibrary::maxTipSide || state.brush.colorTip.height() > BrushLibrary::maxTipSide))
        return fail(error, "A colored brush tip must be a premultiplied color image no larger than 2048 × 2048.");
    return true;
}
bool validEngine(Canvas::Tool engine, int variant) {
    using T = Canvas::Tool;
    if (int(engine) < int(T::Brush) || int(engine) > int(T::Arc) ||
        engine == T::VectorPen) return false;
    const int maxVariant = engine == T::Gradient || engine == T::AdvancedTransform ? 1 : engine == T::Retouch || engine == T::Guides ? 5 : 0;
    return variant >= 0 && variant <= maxVariant;
}
bool knownFields(const QJsonObject &object, const QSet<QString> &allowed, QString *error) {
    for (const auto &key : object.keys())
        if (!allowed.contains(key)) return fail(error, "The tool library contains an unsupported field: " + key);
    return true;
}
bool integer(const QJsonValue &value, int *out) {
    if (!value.isDouble() || value.toDouble() != std::floor(value.toDouble()) ||
        value.toDouble() < -2147483647 || value.toDouble() > 2147483647) return false;
    *out = value.toInt();
    return true;
}
class OrganizationCommand final : public QUndoCommand {
public:
    OrganizationCommand(QString label, std::function<void()> undo, std::function<void()> redo,
                        std::function<bool(bool)> canNavigate)
        : QUndoCommand(std::move(label)), m_undo(std::move(undo)), m_redo(std::move(redo)), m_canNavigate(std::move(canNavigate)) {}
    bool canNavigate(bool undo) const { return m_canNavigate(undo); }
    void undo() override { m_undo(); }
    void redo() override { m_redo(); }
private:
    std::function<void()> m_undo, m_redo;
    std::function<bool(bool)> m_canNavigate;
};

struct EncodedResource { QString id, base64; };
QCache<qint64, EncodedResource> &encodedResourceCache() {
    // QImage cache keys change when pixels change. Keeping bounded PNG encodings
    // avoids compressing unchanged sampled tips on every scalar-setting save.
    static QCache<qint64, EncodedResource> cache(64 * 1024 * 1024);
    return cache;
}
struct ResourceEncoder {
    QJsonObject resources;
    QHash<qint64, QString> cachedImages;
    QHash<QString, qint64> resourceCosts;
    qint64 pixels = 0;
    bool add(const QImage &image, QJsonObject &object, const char *field, QString *error) {
        if (image.isNull()) return true;
        if (cachedImages.contains(image.cacheKey())) {
            object[QLatin1String(field)] = cachedImages.value(image.cacheKey());
            return true;
        }
        auto &cache = encodedResourceCache();
        auto *encoded = cache.object(image.cacheKey());
        EncodedResource temporary;
        if (!encoded) {
            QByteArray png;
            QBuffer buffer(&png);
            const qint64 limit = image.format() == QImage::Format_Grayscale8 ? maxImageBytes : maxColorImageBytes;
            if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG") || png.size() > limit)
                return fail(error, "A tool's brush image cannot be saved within the resource limit.");
            temporary = {QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex()),
                         QString::fromLatin1(png.toBase64())};
            // QCache deletes an item immediately when its cost exceeds maxCost.
            // Keep our fallback value alive independently of cache ownership.
            const qsizetype cost = temporary.base64.size() * sizeof(QChar);
            if (cost <= cache.maxCost()) {
                cache.insert(image.cacheKey(), new EncodedResource(temporary), int(cost));
                encoded = cache.object(image.cacheKey());
            }
            if (!encoded) encoded = &temporary;
        }
        const QString id = encoded->id;
        const qint64 cost = qint64(image.width()) * image.height() * (image.format() == QImage::Format_Grayscale8 ? 1 : 4);
        if (cost > resourceCosts.value(id)) {
            pixels += cost - resourceCosts.value(id);
            resourceCosts.insert(id, cost);
            if (pixels > maxResourcePixels) return fail(error, "The tool library exceeds its brush image limit.");
        }
        if (!resources.contains(id)) {
            resources[id] = encoded->base64;
        }
        cachedImages.insert(image.cacheKey(), id);
        object[QLatin1String(field)] = id;
        return true;
    }
};
bool readResources(const QJsonObject &object, QHash<QString, QImage> &images, bool allowIcons,
                   bool allowColorTips, const QSet<QString> &colorTipIds, QString *error) {
    if (object.size() > ToolCatalog::maxTools * (allowColorTips ? 5 : 3) + (allowIcons ? ToolCatalog::maxEntries : 0))
        return fail(error, "The tool library contains too many images.");
    qint64 pixels = 0;
    const qint64 encodedLimit = allowColorTips ? maxColorImageBytes : maxImageBytes;
    for (auto it = object.begin(); it != object.end(); ++it) {
        const auto encoded = it.value().toString();
        if (it.key().size() != 64 || !it.value().isString() || encoded.isEmpty() ||
            encoded.size() > (encodedLimit + 2) / 3 * 4)
            return fail(error, "The tool library contains an invalid brush image.");
        auto base64 = QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!base64 || base64.decoded.size() > encodedLimit ||
            QString::fromLatin1(QCryptographicHash::hash(base64.decoded, QCryptographicHash::Sha256).toHex()) != it.key())
            return fail(error, "A tool's brush image is damaged.");
        QBuffer buffer(&base64.decoded);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        const auto size = reader.size();
        if (size.isEmpty() || size.width() > BrushLibrary::maxTipSide || size.height() > BrushLibrary::maxTipSide)
            return fail(error, "A tool's brush image has unsupported dimensions.");
        const auto image = reader.read();
        if (image.isNull()) return fail(error, "A tool image is not a valid PNG.");
        const bool grayscale = !image.hasAlphaChannel() && image.isGrayscale();
        if (base64.decoded.size() > maxImageBytes && grayscale && !colorTipIds.contains(it.key()))
            return fail(error, "A grayscale brush image exceeds its resource limit.");
        pixels += qint64(size.width()) * size.height() * (grayscale && !colorTipIds.contains(it.key()) ? 1 : 4);
        if (pixels > maxResourcePixels) return fail(error, "The tool library exceeds its brush image limit.");
        if (!grayscale && !allowIcons && !allowColorTips) return fail(error, "A tool's brush image is not a valid grayscale PNG.");
        if (!grayscale && !allowColorTips && (image.width() > ToolCatalog::maxIconSide || image.height() > ToolCatalog::maxIconSide))
            return fail(error, "A custom group icon exceeds 256 × 256 pixels.");
        images.insert(it.key(), image.convertToFormat(grayscale ? QImage::Format_Grayscale8 : QImage::Format_ARGB32_Premultiplied));
    }
    return true;
}
bool resolveResource(const QJsonObject &object, const char *field, const QHash<QString, QImage> &images,
                     QImage &image, QString *error) {
    if (!object.contains(QLatin1String(field))) return true;
    const auto value = object.value(QLatin1String(field));
    if (!value.isString() || !images.contains(value.toString()))
        return fail(error, "A tool refers to a missing brush image.");
    image = images.value(value.toString());
    return true;
}
bool encodeCatalog(const QVector<ToolDefinition> &tools, const QVector<ToolboxEntry> &entries,
                   const QString &selected, const QString &active, const QSet<QString> &deleted,
                   QByteArray &bytes, QString *error) {
    ResourceEncoder resources;
    QJsonArray definitions, placements;
    bool dynamicStamps = false, modern = false;
    for (const auto &definition : tools) {
        if (!validState(definition.state, error)) return false;
        modern |= usesModernBrushFeatures(definition.state.brush);
        dynamicStamps |= definition.brushType == BrushType::DynamicStamp || definition.state.brush.colorDynamics.applyToColorTip;
        QJsonObject object{{"id", definition.id}, {"name", definition.name}, {"icon", definition.icon},
            {"brushPresetId", definition.brushPresetId}, {"engine", int(definition.engine)},
            {"variant", definition.variant}, {"brush", brushSettingsToJson(definition.state.brush)},
            {"smoothing", definition.state.smoothing}, {"options", definition.state.options},
            {"tags", QJsonArray::fromStringList(definition.tags)}, {"color", definition.color.isValid() ? definition.color.name(QColor::HexRgb) : QString{}},
            {"brushType", int(definition.brushType)}, {"category", definition.category}};
        if (definition.builtinRecipeRevision > 0)
            object["builtinRecipeRevision"] = definition.builtinRecipeRevision;
        if (!resources.add(definition.state.brush.tipMask, object, "tip", error) ||
            !resources.add(definition.state.brush.colorTip, object, "colorTip", error) ||
            !resources.add(definition.state.brush.texture.pattern, object, "texture", error) ||
            !resources.add(definition.state.brush.dual.tipMask, object, "dualTip", error) ||
            !resources.add(definition.state.brush.surface.image, object, "surfaceImage", error)) return false;
        definitions.push_back(object);
    }
    for (const auto &placement : entries) {
        QJsonObject object{{"id", placement.id}, {"name", placement.name}, {"icon", placement.icon},
            {"group", placement.group}, {"tools", QJsonArray::fromStringList(placement.tools)},
            {"lastToolId", placement.lastToolId}, {"tags", QJsonArray::fromStringList(placement.tags)},
            {"color", placement.color.isValid() ? placement.color.name(QColor::HexRgb) : QString{}}};
        if (!resources.add(placement.customIcon, object, "customIcon", error)) return false;
        placements.push_back(object);
    }
    QStringList deletedPresets(deleted.cbegin(), deleted.cend());
    deletedPresets.sort();
    bytes = QJsonDocument(QJsonObject{{"format", "toonpro.tool-catalog"}, {"version", modern ? 12 : dynamicStamps ? 11 : 10},
        {"tools", definitions}, {"entries", placements}, {"resources", resources.resources},
        {"deletedBrushPresets", QJsonArray::fromStringList(deletedPresets)},
        {"selectedEntryId", selected}, {"activeToolId", active}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > ToolCatalog::maxFileBytes) return fail(error, "The tool library exceeds its file-size limit.");
    return true;
}
} // namespace

void ToolOrganizationHistory::undo() { if (canUndo()) setIndex(index() - 1); }
void ToolOrganizationHistory::redo() { if (canRedo()) setIndex(index() + 1); }
void ToolOrganizationHistory::setIndex(int target) {
    target = qBound(0, target, count());
    if (target == index() || (beforeNavigation && !beforeNavigation(target))) return;
    QUndoStack::setIndex(target);
}
QAction *ToolOrganizationHistory::createUndoAction(QObject *parent, const QString &prefix) const {
    auto *action = new QAction(parent);
    const auto update = [this, action, prefix] {
        action->setEnabled(canUndo());
        action->setText((prefix.isEmpty() ? QStringLiteral("Undo") : prefix) + (undoText().isEmpty() ? QString{} : " " + undoText()));
    };
    connect(this, &QUndoStack::canUndoChanged, action, update);
    connect(this, &QUndoStack::undoTextChanged, action, update);
    connect(action, &QAction::triggered, const_cast<ToolOrganizationHistory*>(this), [this] { const_cast<ToolOrganizationHistory*>(this)->undo(); });
    update(); return action;
}
QAction *ToolOrganizationHistory::createRedoAction(QObject *parent, const QString &prefix) const {
    auto *action = new QAction(parent);
    const auto update = [this, action, prefix] {
        action->setEnabled(canRedo());
        action->setText((prefix.isEmpty() ? QStringLiteral("Redo") : prefix) + (redoText().isEmpty() ? QString{} : " " + redoText()));
    };
    connect(this, &QUndoStack::canRedoChanged, action, update);
    connect(this, &QUndoStack::redoTextChanged, action, update);
    connect(action, &QAction::triggered, const_cast<ToolOrganizationHistory*>(this), [this] { const_cast<ToolOrganizationHistory*>(this)->redo(); });
    update(); return action;
}

ToolCatalog::ToolCatalog(BrushLibrary *library, QString path, QObject *parent)
    : QObject(parent), m_library(library), m_path(std::move(path)) {
    m_history.setUndoLimit(100);
    m_history.beforeNavigation = [this](int target) {
        const bool undo = target < m_history.index();
        for (int i = m_history.index(); i != target; i += undo ? -1 : 1) {
            const auto *command = static_cast<const OrganizationCommand*>(m_history.command(undo ? i - 1 : i));
            if (!command->canNavigate(undo)) return false;
        }
        return true;
    };
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] {
        if (m_autoSaveGuard && !m_autoSaveGuard()) {
            m_saveTimer.start();
            return;
        }
        QString error;
        if (!save(&error)) emit warning(error);
    });
    if (m_library) connect(m_library, &BrushLibrary::changed, this, &ToolCatalog::reconcileBrushes);
    seed();
}

const ToolDefinition *ToolCatalog::tool(const QString &id) const {
    for (const auto &item : m_tools) if (item.id == id) return &item;
    return nullptr;
}
const ToolboxEntry *ToolCatalog::entry(const QString &id) const {
    for (const auto &item : m_entries) if (item.id == id) return &item;
    return nullptr;
}
QStringList ToolCatalog::visibleToolIds() const {
    const auto *selected = entry(m_selected);
    return selected ? selected->tools : QStringList{};
}
void ToolCatalog::seed() {
    m_tools.clear();
    m_entries.clear();
    m_deletedBrushPresets.clear();
    using T = Canvas::Tool;
    auto add = [this](QString id, QString name, QString icon, T engine, int variant = 0) {
        ToolDefinition definition;
        definition.id = std::move(id); definition.name = std::move(name); definition.icon = std::move(icon);
        definition.engine = engine; definition.variant = variant;
        m_tools.push_back(std::move(definition));
        return m_tools.last().id;
    };
    auto group = [this](QString id, QString name, QString icon, QStringList tools) {
        m_entries.push_back({std::move(id), std::move(name), std::move(icon), true, tools,
                            tools.isEmpty() ? QString{} : tools.first()});
    };
    QStringList drawing;
    QVector<ToolboxEntry> catalogGroups;
    if (m_library) for (const auto &preset : m_library->presets()) {
        const auto id = add("brush/" + preset.id, preset.name, "brush", T::Brush);
        m_tools.last().brushPresetId = preset.id;
        if (preset.builtIn && preset.id.startsWith("builtin/catalog/"))
            m_tools.last().builtinRecipeRevision = catalogBrushRecipeRevision;
        m_tools.last().state = normalized({preset.brush, preset.smoothing, {}});
        m_tools.last().brushType = presetBrushType(preset);
        m_tools.last().category = preset.builtIn ? standardBrushCategory(preset.id) : "Imported";
        if (m_tools.last().category.isEmpty()) m_tools.last().category = "Drawing";
        if (preset.builtIn) m_tools.last().tags = standardBrushTags(preset.id);
        const auto familyId = catalogFamilyId(preset.id);
        if (familyId.isEmpty()) {
            drawing.push_back(id);
        } else {
            auto family = std::find_if(catalogGroups.begin(), catalogGroups.end(),
                [&](const ToolboxEntry &entry) { return entry.id == familyId; });
            if (family == catalogGroups.end()) {
                const auto category = standardBrushCategory(preset.id);
                catalogGroups.push_back({familyId, category, catalogFamilyIcon(category), true, {id}, id});
            } else {
                family->tools.push_back(id);
            }
        }
    }
    if (drawing.isEmpty()) drawing.push_back(add("brush/round", "Round Brush", "brush", T::Brush));
    drawing.push_back(add("eraser", "Eraser", "eraser", T::Eraser));
    m_tools.last().state.brush.size = 36;
    group("group/drawing", "Drawing", "brush", drawing);
    group("group/shapes", "Shapes", "rectangle-horizontal", {
        add("line", "Line", "minus", T::Line), add("rectangle", "Rectangle", "rectangle-horizontal", T::Rectangle),
        add("ellipse", "Ellipse", "circle", T::Ellipse), add("polygon", "Polygon", "pentagon", T::Polygon),
        add("arc", "Arc", "spline", T::Arc), add("curve", "Bézier Curve", "spline", T::Curve)});
    group("group/color", "Color", "palette", {
        add("eyedropper", "Eyedropper", "pipette", T::Eyedropper), add("bucket", "Paint Bucket", "paint-bucket", T::Bucket),
        add("gradient-linear", "Linear Gradient", "blend", T::Gradient),
        add("gradient-radial", "Radial Gradient", "radius", T::Gradient, 1)});
    group("group/selection", "Selection", "square-dashed", {
        add("select-rectangle", "Rectangle Selection", "square-dashed", T::RectangleSelect),
        add("select-ellipse", "Ellipse Selection", "circle-dashed", T::EllipseSelect),
        add("select-lasso", "Lasso Selection", "lasso", T::LassoSelect),
        add("select-polygon", "Polygon Selection", "pentagon", T::PolygonSelect),
        add("magic-wand", "Magic Wand", "wand-sparkles", T::MagicWand),
        add("color-range", "Color Range", "palette", T::ColorRange)});
    group("group/retouch", "Retouch", "droplet", {
        add("smudge", "Smudge", "droplet", T::Retouch), add("mixer", "Mixer", "paintbrush", T::Retouch, 1),
        add("blur", "Blur", "blend", T::Retouch, 2), add("sharpen", "Sharpen", "triangle", T::Retouch, 3),
        add("dodge", "Dodge", "sun", T::Retouch, 4), add("burn", "Burn", "flame", T::Retouch, 5)});
    QStringList guides;
    const QStringList guideIcons{"grid-2x2", "focus", "split", "minus", "triangle", "radius"};
    for (int i = 0; i <= int(GuideType::Fisheye); ++i)
        guides.push_back(add(QString("guide-%1").arg(i), guideTypeName(GuideType(i)), guideIcons[i], T::Guides, i));
    group("group/guides", "Guides", "grid-2x2", guides);
    group("group/vector-editing", "Vector Editing", "spline", {
        add("path-edit", "Path Edit", "mouse-pointer-2", T::VectorNode),
        add("simplify-brush", "Simplify Brush", "spline", T::VectorSimplify),
        add("width-brush", "Width Brush", "paintbrush", T::VectorWidth),
        add("vector-eraser", "Vector Eraser", "scissors", T::VectorEraser)});
    group("group/scene3d", "3D Reference", "box", {
        add("scene-object", "3D Object", "box", T::SceneObject),
        add("scene-camera", "3D Camera", "video", T::SceneCamera),
        add("scene-rig", "3D Rig", "spline", T::SceneRig)});
    const auto hand = add("hand", "Hand", "hand", T::Hand);
    const auto text = add("text", "Text", "type", T::Text);
    const auto transform = add("transform", "Transform", "move", T::Transform);
    const auto advanced = add("advanced-transform", "Perspective Transform", "square-dashed-mouse-pointer", T::AdvancedTransform);
    const auto warp = add("warp", "Warp", "grid-2x2", T::AdvancedTransform, 1);
    for (const auto &id : {text, hand, transform, advanced, warp}) {
        const auto *definition = tool(id);
        m_entries.push_back({"entry/" + id, definition->name, definition->icon, false, {id}, id});
    }
    m_entries.append(catalogGroups);
    upgradeDrawingIcons(m_tools, m_entries);
    m_selected = m_entries.first().id;
    m_active = m_entries.first().lastToolId;
}

ToolCatalog::Snapshot ToolCatalog::snapshot() const { return {m_tools, m_entries, m_selected, m_active, m_deletedBrushPresets}; }
void ToolCatalog::normalizeSelection(Snapshot &state) const {
    auto selected = std::find_if(state.entries.begin(), state.entries.end(), [&](const ToolboxEntry &e) { return e.id == state.selected; });
    if (selected == state.entries.end()) {
        state.selected = state.entries.isEmpty() ? QString{} : state.entries.first().id;
        selected = state.entries.begin();
    }
    if (selected == state.entries.end()) { state.active.clear(); return; }
    if (selected->tools.isEmpty()) {
        // Looking into an empty group does not interrupt the tool already in use.
        // Removing that tool's last placement, however, selects a usable fallback.
        const bool stillPlaced = std::any_of(state.entries.cbegin(), state.entries.cend(),
            [&](const ToolboxEntry &e) { return e.tools.contains(state.active); });
        if (stillPlaced) return;
        selected = std::find_if(state.entries.begin(), state.entries.end(),
            [](const ToolboxEntry &e) { return !e.tools.isEmpty(); });
        if (selected == state.entries.end()) { state.active.clear(); return; }
        state.selected = selected->id;
    }
    if (!selected->tools.contains(state.active))
        state.active = selected->tools.contains(selected->lastToolId) ? selected->lastToolId : selected->tools.first();
    selected->lastToolId = state.active;
}

void ToolCatalog::applySnapshot(Snapshot state, bool preserveCurrentSettings) {
    normalizeSelection(state);
    const bool activating = state.active != m_active;
    if (activating) emit aboutToActivate();
    // Organization undo restores names/placements, while settings keep their most
    // recent value even for a temporarily removed duplicate.
    if (preserveCurrentSettings) {
        for (const auto &definition : m_tools) m_latestStates.insert(definition.id, definition.state);
        for (auto &definition : state.tools)
            if (m_latestStates.contains(definition.id)) definition.state = m_latestStates.value(definition.id);
    } else {
        // Reconciliation deliberately installs merged settings after validating
        // the full candidate. Old organization snapshots must not restore them.
        m_latestStates.clear();
    }
    m_tools = std::move(state.tools); m_entries = std::move(state.entries);
    m_selected = std::move(state.selected); m_active = std::move(state.active);
    m_deletedBrushPresets = std::move(state.deletedBrushPresets);
    emit changed();
    if (activating) emit activationRequested(m_active);
    scheduleSave();
}
bool ToolCatalog::canActivate(Snapshot state) {
    normalizeSelection(state);
    return state.active == m_active || !m_activationGuard || m_activationGuard();
}
bool ToolCatalog::commit(const QString &label, Snapshot next, bool preserveNavigation) {
    if (!canActivate(next)) { emit changed(); return false; }
    const auto previous = snapshot();
    const auto navigated = [this, preserveNavigation](Snapshot state) {
        if (preserveNavigation) {
            state.selected = m_selected; state.active = m_active;
            for (auto &item : state.entries) if (const auto *current = entry(item.id)) item.lastToolId = current->lastToolId;
        }
        return state;
    };
    m_history.push(new OrganizationCommand(label, [this, previous, navigated] { applySnapshot(navigated(previous)); },
        [this, next, navigated] { applySnapshot(navigated(next)); },
        [this, previous, next, navigated](bool undo) { return canActivate(navigated(undo ? previous : next)); }));
    return true;
}
void ToolCatalog::scheduleSave() { if (m_writable) m_saveTimer.start(); }
bool ToolCatalog::validIds(const QStringList &ids) const {
    QSet<QString> seen;
    if (ids.size() > maxTools) return false;
    for (const auto &id : ids) {
        if (!tool(id) || seen.contains(id)) return false;
        seen.insert(id);
    }
    return true;
}
bool ToolCatalog::selectEntry(const QString &id) {
    const auto *selected = entry(id);
    if (!selected) return false;
    if (id == m_selected) return true;
    auto next = snapshot();
    next.selected = id;
    next.active = selected->tools.contains(selected->lastToolId) ? selected->lastToolId
        : selected->tools.isEmpty() ? m_active : selected->tools.first();
    if (!canActivate(next)) { emit changed(); return false; }
    applySnapshot(std::move(next));
    return true;
}
bool ToolCatalog::activateTool(const QString &id) {
    if (!tool(id)) return false;
    if (!visibleToolIds().contains(id)) {
        for (const auto &placement : m_entries) if (placement.tools.contains(id)) {
            auto next = snapshot(); next.selected = placement.id; next.active = id;
            if (!canActivate(next)) { emit changed(); return false; }
            applySnapshot(std::move(next));
            return true;
        }
        return !addStandalone(id).isEmpty();
    }
    if (id == m_active) return true;
    auto next = snapshot(); next.active = id;
    for (auto &item : next.entries) if (item.id == next.selected) item.lastToolId = id;
    if (!canActivate(next)) { emit changed(); return false; }
    applySnapshot(std::move(next));
    return true;
}
QString ToolCatalog::addGroup(const QString &name, const QString &icon, const QStringList &ids) {
    if (!m_writable) return {};
    if (m_entries.size() >= maxEntries || !validName(name) || !validIcon(icon) || !validIds(ids)) return {};
    auto next = snapshot();
    const QString id = freshId("group/");
    next.entries.push_back({id, name.trimmed(), icon, true, ids, ids.isEmpty() ? QString{} : ids.first()});
    next.selected = id; next.active = ids.isEmpty() ? m_active : next.entries.last().lastToolId;
    if (!commit("Add group", std::move(next))) return {};
    return id;
}
QString ToolCatalog::addStandalone(const QString &toolId) {
    if (!m_writable) return {};
    const auto *definition = tool(toolId);
    if (!definition || m_entries.size() >= maxEntries) return {};
    auto next = snapshot();
    const auto id = freshId("entry/");
    next.entries.push_back({id, definition->name, definition->icon, false, {toolId}, toolId});
    next.selected = id; next.active = toolId;
    if (!commit("Add standalone tool", std::move(next))) return {};
    return id;
}
bool ToolCatalog::addTools(const QString &entryId, const QStringList &ids) {
    if (!m_writable) return false;
    const auto *target = entry(entryId);
    if (!target || !target->group || !validIds(ids)) return false;
    auto next = snapshot();
    bool added = false;
    for (auto &item : next.entries) if (item.id == entryId) {
        for (const auto &id : ids) if (!item.tools.contains(id)) { item.tools.push_back(id); added = true; }
        if (item.lastToolId.isEmpty() && !item.tools.isEmpty()) item.lastToolId = item.tools.first();
    }
    if (added) return commit("Add tools to group", std::move(next));
    return true;
}
QString ToolCatalog::duplicateTool(const QString &toolId, const QString &entryId, const QString &name) {
    if (!m_writable) return {};
    const auto *source = tool(toolId);
    const auto *target = entry(entryId);
    if (!source || m_tools.size() >= maxTools || (!name.isEmpty() && !validName(name)) ||
        (!entryId.isEmpty() && !target) || ((!target || !target->group) && m_entries.size() >= maxEntries)) return {};
    // Capture current settings before copying the active tool.
    if (toolId == m_active) emit aboutToActivate();
    source = tool(toolId);
    auto next = snapshot();
    ToolDefinition copy = *source;
    copy.id = freshId("tool/"); copy.brushPresetId.clear();
    copy.builtinRecipeRevision = 0;
    copy.name = name.isEmpty() ? source->name.left(115) + " Copy" : name.trimmed();
    const auto id = copy.id;
    next.tools.push_back(copy);
    if (target && target->group) {
        for (auto &item : next.entries) if (item.id == entryId) {
            const int sourceIndex = int(item.tools.indexOf(toolId));
            item.tools.insert(sourceIndex < 0 ? item.tools.size() : sourceIndex + 1, id);
            item.lastToolId = id;
        }
        next.selected = entryId;
    } else {
        const auto newEntryId = freshId("entry/");
        const ToolboxEntry placement{newEntryId, copy.name, copy.icon, false, {id}, id};
        const int index = target ? int(std::find_if(next.entries.begin(), next.entries.end(),
            [&](const ToolboxEntry &e) { return e.id == entryId; }) - next.entries.begin()) + 1 : int(next.entries.size());
        next.entries.insert(index, placement);
        next.selected = newEntryId;
    }
    next.active = id;
    if (!commit("Duplicate tool", std::move(next))) return {};
    return id;
}
QString ToolCatalog::createBrush(const QString &name, BrushType type, const ToolState &state,
                                 const QString &category, const QStringList &tags,
                                 const QString &destinationEntryId) {
    const auto reject = [this](const QString &reason) { emit warning(reason); return QString{}; };
    if (!m_writable) return reject("The tool library is read-only because it could not be loaded.");
    if (m_tools.size() >= maxTools) return reject("The tool library is full. Delete a brush before adding another.");
    if (!validName(name)) return reject("Brush names must contain 1 to 120 characters.");
    if (!validBrushType(type)) return reject("Choose a supported brush type.");
    if (!validCategory(category)) return reject("Brush categories must be single-line names of up to 80 characters.");
    QStringList normalizedTags;
    if (!normalizeTags(tags, normalizedTags)) return reject("Use up to 32 tags, each containing at most 40 characters.");
    const auto *destination = entry(destinationEntryId);
    if (!destinationEntryId.isEmpty() && (!destination || !destination->group))
        return reject("Choose a group for the new brush.");
    QString error;
    if (!validState(state, &error)) return reject(error);
    if (type == BrushType::Stamp && state.brush.tipMask.isNull())
        return reject("Choose a stamp image before creating this brush.");
    if (type == BrushType::Stamp) {
        bool visible = false;
        for (int y = 0; y < state.brush.tipMask.height() && !visible; ++y) {
            const auto *row = state.brush.tipMask.constScanLine(y);
            visible = std::any_of(row, row + state.brush.tipMask.width(), [](uchar alpha) { return alpha != 0; });
        }
        if (!visible) return reject("The stamp image is completely transparent.");
    }
    if (type == BrushType::ColoredStamp && state.brush.colorTip.isNull())
        return reject("Choose a colored stamp image before creating this brush.");
    ToolDefinition definition;
    definition.id = freshId("tool/");
    definition.name = name.trimmed();
    definition.icon = brushTypeIcon(type);
    definition.state = normalized(state);
    definition.tags = normalizedTags;
    definition.brushType = type;
    definition.category = category.trimmed();
    auto next = snapshot();
    next.tools.append(definition);
    for (auto &item : next.entries) if (item.id == destinationEntryId) {
        item.tools.append(definition.id);
        if (item.lastToolId.isEmpty()) item.lastToolId = definition.id;
    }
    // Resource compression and the combined library budget can fail even when
    // scalar settings are valid. Reject before adding an unsavable brush.
    QByteArray encoded;
    if (!encodeCatalog(next.tools, next.entries, next.selected, next.active, next.deletedBrushPresets, encoded, &error))
        return reject(error);
    if (!commit("Create brush", std::move(next), true)) return {};
    return definition.id;
}
bool ToolCatalog::deleteBrushes(const QStringList &ids) {
    if (!m_writable || ids.isEmpty() || !validIds(ids)) return false;
    for (const auto &id : ids) if (tool(id)->engine != Canvas::Tool::Brush) return false;
    auto next = snapshot();
    for (const auto &item : next.tools) if (ids.contains(item.id) && !item.brushPresetId.isEmpty())
        next.deletedBrushPresets.insert(item.brushPresetId);
    if (next.deletedBrushPresets.size() > maxTools * 8) {
        emit warning("The tool library has reached its deleted preset history limit.");
        return false;
    }
    next.tools.removeIf([&](const ToolDefinition &item) { return ids.contains(item.id); });
    next.entries.removeIf([&](const ToolboxEntry &item) { return !item.group && ids.contains(item.tools.first()); });
    for (auto &item : next.entries) {
        item.tools.removeIf([&](const QString &id) { return ids.contains(id); });
        if (!item.tools.contains(item.lastToolId)) item.lastToolId = item.tools.isEmpty() ? QString{} : item.tools.first();
    }
    if (ids.contains(next.active)) next.active.clear();
    return commit(ids.size() == 1 ? "Delete brush" : "Delete brushes", std::move(next));
}
bool ToolCatalog::setBrushMetadata(const QStringList &ids, BrushType type, const QString &category) {
    if (!m_writable || ids.isEmpty() || !validIds(ids) || !validBrushType(type) || !validCategory(category)) return false;
    for (const auto &id : ids) if (tool(id)->engine != Canvas::Tool::Brush) return false;
    auto next = snapshot();
    bool changed = false;
    for (auto &item : next.tools) if (ids.contains(item.id)) {
        if (item.brushType == type && item.category == category.trimmed()) continue;
        item.brushType = type; item.category = category.trimmed(); changed = true;
    }
    return !changed || commit("Set brush type and category", std::move(next), true);
}
bool ToolCatalog::setBrushCategory(const QStringList &ids, const QString &category) {
    if (!m_writable || ids.isEmpty() || !validIds(ids) || !validCategory(category)) return false;
    for (const auto &id : ids) if (tool(id)->engine != Canvas::Tool::Brush) return false;
    auto next = snapshot();
    bool changed = false;
    for (auto &item : next.tools) if (ids.contains(item.id) && item.category != category.trimmed()) {
        item.category = category.trimmed(); changed = true;
    }
    return !changed || commit("Set brush category", std::move(next), true);
}
bool ToolCatalog::renameTool(const QString &id, const QString &name) {
    if (!m_writable) return false;
    if (!tool(id) || !validName(name)) return false;
    if (tool(id)->name == name.trimmed()) return true;
    auto next = snapshot();
    for (auto &item : next.tools) if (item.id == id) item.name = name.trimmed();
    for (auto &item : next.entries) if (!item.group && item.tools.contains(id)) item.name = name.trimmed();
    return commit("Rename tool", std::move(next), true);
}
bool ToolCatalog::renameEntry(const QString &id, const QString &name, const QString &icon) {
    if (!m_writable) return false;
    const auto *target = entry(id);
    if (!target || !validName(name) || (!icon.isEmpty() && !validIcon(icon))) return false;
    if (!target->group) return renameTool(target->tools.first(), name);
    if (target->name == name.trimmed() && (icon.isEmpty() || target->icon == icon)) return true;
    auto next = snapshot();
    for (auto &item : next.entries) if (item.id == id) { item.name = name.trimmed(); if (!icon.isEmpty()) item.icon = icon; }
    return commit("Rename group", std::move(next), true);
}
bool ToolCatalog::removeEntry(const QString &id) {
    if (!m_writable) return false;
    if (!entry(id)) return false;
    auto next = snapshot();
    const int index = int(std::find_if(next.entries.begin(), next.entries.end(), [&](const ToolboxEntry &e) { return e.id == id; }) - next.entries.begin());
    next.entries.removeAt(index);
    if (next.selected == id) {
        next.selected = next.entries.isEmpty() ? QString{} : next.entries[std::min(index, int(next.entries.size()) - 1)].id;
        next.active.clear();
    }
    return commit("Remove Toolbox entry", std::move(next));
}
bool ToolCatalog::removeTool(const QString &entryId, const QString &toolId) {
    if (!m_writable) return false;
    const auto *target = entry(entryId);
    if (!target || !target->tools.contains(toolId)) return false;
    if (!target->group) return removeEntry(entryId);
    auto next = snapshot();
    for (auto &item : next.entries) if (item.id == entryId) {
        const int index = int(item.tools.indexOf(toolId));
        item.tools.removeAll(toolId);
        if (item.lastToolId == toolId) item.lastToolId = item.tools.isEmpty() ? QString{} : item.tools[std::min(index, int(item.tools.size()) - 1)];
        if (next.selected == entryId && next.active == toolId) next.active = item.lastToolId;
    }
    return commit("Remove tool from group", std::move(next));
}
bool ToolCatalog::moveEntry(const QString &id, int index) {
    if (!m_writable) return false;
    if (index < 0 || index >= m_entries.size() || !entry(id)) return false;
    auto next = snapshot();
    const int old = int(std::find_if(next.entries.begin(), next.entries.end(), [&](const ToolboxEntry &e) { return e.id == id; }) - next.entries.begin());
    if (old != index) { next.entries.move(old, index); commit("Move Toolbox entry", std::move(next)); }
    return true;
}
bool ToolCatalog::moveTool(const QString &entryId, const QString &toolId, int index) {
    if (!m_writable) return false;
    const auto *target = entry(entryId);
    if (!target || !target->group || index < 0 || index >= target->tools.size() || !target->tools.contains(toolId)) return false;
    const int old = int(target->tools.indexOf(toolId));
    if (old == index) return true;
    auto next = snapshot();
    for (auto &item : next.entries) if (item.id == entryId) item.tools.move(old, index);
    return commit("Move tool", std::move(next));
}
bool ToolCatalog::setToolTags(const QStringList &ids, const QStringList &tags) {
    QStringList normalizedTags;
    if (!m_writable || ids.isEmpty() || !validIds(ids) || !normalizeTags(tags, normalizedTags)) return false;
    auto next = snapshot();
    bool changed = false;
    for (auto &item : next.tools) if (ids.contains(item.id) && item.tags != normalizedTags) {
        item.tags = normalizedTags; changed = true;
    }
    return !changed || commit("Set tool tags", std::move(next), true);
}
bool ToolCatalog::addToolTag(const QStringList &ids, const QString &tag) {
    QStringList normalizedTag;
    if (!m_writable || ids.isEmpty() || !validIds(ids) || !normalizeTags({tag}, normalizedTag) || normalizedTag.isEmpty()) return false;
    auto next = snapshot();
    bool changed = false;
    for (auto &item : next.tools) if (ids.contains(item.id)) {
        auto tags = item.tags; tags.append(normalizedTag.first());
        QStringList normalizedTags;
        if (!normalizeTags(tags, normalizedTags)) return false;
        if (item.tags != normalizedTags) { item.tags = normalizedTags; changed = true; }
    }
    return !changed || commit("Add tool tag", std::move(next), true);
}
bool ToolCatalog::removeToolTag(const QStringList &ids, const QString &tag) {
    QStringList normalizedTag;
    if (!m_writable || ids.isEmpty() || !validIds(ids) || !normalizeTags({tag}, normalizedTag) || normalizedTag.isEmpty()) return false;
    auto next = snapshot();
    bool changed = false;
    for (auto &item : next.tools) if (ids.contains(item.id)) {
        const auto previousSize = item.tags.size();
        item.tags.removeIf([&](const QString &existing) { return existing.toCaseFolded() == normalizedTag.first().toCaseFolded(); });
        changed = changed || item.tags.size() != previousSize;
    }
    return !changed || commit("Remove tool tag", std::move(next), true);
}
bool ToolCatalog::setToolColor(const QStringList &ids, QColor color) {
    if (!m_writable || ids.isEmpty() || !validIds(ids)) return false;
    color = metadataColor(color);
    auto next = snapshot(); bool changed = false;
    for (auto &item : next.tools) if (ids.contains(item.id) && item.color != color) { item.color = color; changed = true; }
    return !changed || commit("Set tool color", std::move(next), true);
}
bool ToolCatalog::setEntryTags(const QString &id, const QStringList &tags) {
    const auto *target = entry(id); QStringList normalizedTags;
    if (!m_writable || !target || !target->group || !normalizeTags(tags, normalizedTags)) return false;
    if (target->tags == normalizedTags) return true;
    auto next = snapshot();
    for (auto &item : next.entries) if (item.id == id) item.tags = normalizedTags;
    return commit("Set group tags", std::move(next), true);
}
bool ToolCatalog::setEntryColor(const QString &id, QColor color) {
    const auto *target = entry(id);
    if (!m_writable || !target || !target->group) return false;
    color = metadataColor(color);
    if (target->color == color) return true;
    auto next = snapshot();
    for (auto &item : next.entries) if (item.id == id) item.color = color;
    return commit("Set group color", std::move(next), true);
}
bool ToolCatalog::setEntryIcon(const QString &id, const QString &builtin, const QImage &custom) {
    const auto *target = entry(id);
    if (!m_writable || !target || !target->group || !validIcon(builtin)) return false;
    QImage icon;
    if (!custom.isNull()) {
        icon = custom.width() > maxIconSide || custom.height() > maxIconSide
            ? custom.scaled(maxIconSide, maxIconSide, Qt::KeepAspectRatio, Qt::SmoothTransformation) : custom;
        icon = icon.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        icon.setDevicePixelRatio(1);
        // PNG files from external sources may carry large text metadata; icons
        // retain only pixels so a tiny image cannot inflate every library save.
        if (!icon.textKeys().isEmpty()) {
            QImage pixels(icon.size(), QImage::Format_ARGB32_Premultiplied);
            if (pixels.isNull()) return false;
            for (int y = 0; y < icon.height(); ++y) std::copy_n(icon.constScanLine(y), icon.width() * 4, pixels.scanLine(y));
            icon = std::move(pixels);
        }
        if (icon.isNull()) return false;
    }
    if (target->icon == builtin && target->customIcon == icon) return true;
    auto next = snapshot();
    for (auto &item : next.entries) if (item.id == id) { item.icon = builtin; item.customIcon = icon; }
    return commit("Set group icon", std::move(next), true);
}
bool ToolCatalog::setEntryTools(const QString &id, const QStringList &ids) {
    const auto *target = entry(id);
    if (!m_writable || !target || !target->group || !validIds(ids)) return false;
    if (target->tools == ids) return true;
    auto next = snapshot();
    for (auto &item : next.entries) if (item.id == id) {
        item.tools = ids;
        if (!ids.contains(item.lastToolId)) item.lastToolId = ids.isEmpty() ? QString{} : ids.first();
    }
    return commit("Assign group tools", std::move(next));
}
bool ToolCatalog::updateToolState(const QString &id, const ToolState &state) {
    if (!tool(id)) return false;
    QString error;
    if (!validState(state, &error)) { emit warning(error); return false; }
    const auto value = normalized(state);
    for (auto &item : m_tools) if (item.id == id) {
        if (item.state == value) return true;
        item.state = value;
        m_latestStates.insert(id, value);
        emit settingsChanged(id);
        scheduleSave();
        return true;
    }
    return false;
}
void ToolCatalog::reconcileBrushes() {
    if (!m_library) return;
    auto next = snapshot();
    bool upgraded = false;
    QStringList upgradedSettings;
    // An unknown future revision belongs to a newer application. Retain it
    // verbatim, rather than interpreting it as an old recipe to be replaced.
    const bool needsUpgrade = std::any_of(next.tools.cbegin(), next.tools.cend(), [](const ToolDefinition &definition) {
        return canonicalCatalogTool(definition) && definition.builtinRecipeRevision > 0 && definition.builtinRecipeRevision < catalogBrushRecipeRevision;
    });
    const auto originalRecipes = needsUpgrade ? catalogBrushPresets(1) : QVector<BrushPreset>{};
    const auto previousRecipes = needsUpgrade ? catalogBrushPresets(2) : QVector<BrushPreset>{};
    const auto thirdRecipes = needsUpgrade ? catalogBrushPresets(3) : QVector<BrushPreset>{};
    const auto fourthRecipes = needsUpgrade ? catalogBrushPresets(4) : QVector<BrushPreset>{};
    const auto replacements = needsUpgrade ? catalogBrushPresets() : QVector<BrushPreset>{};
    for (auto &definition : next.tools) {
        if (!canonicalCatalogTool(definition) || definition.builtinRecipeRevision <= 0 || definition.builtinRecipeRevision >= catalogBrushRecipeRevision) continue;
        const auto &prior = definition.builtinRecipeRevision == 1 ? originalRecipes : definition.builtinRecipeRevision == 2 ? previousRecipes : definition.builtinRecipeRevision == 3 ? thirdRecipes : fourthRecipes;
        const auto previous = std::find_if(prior.cbegin(), prior.cend(),
            [&](const BrushPreset &preset) { return preset.id == definition.brushPresetId; });
        const auto replacement = std::find_if(replacements.cbegin(), replacements.cend(),
            [&](const BrushPreset &preset) { return preset.id == definition.brushPresetId; });
        if (previous == prior.cend() || replacement == replacements.cend()) continue;
        const auto before = definition.state;
        QString error;
        if (!upgradeRecipe(definition, *previous, *replacement, &error)) {
            emit warning("Brush corrections were deferred. " + error + " Existing tools were kept unchanged.");
            return;
        }
        if (definition.state != before) upgradedSettings.append(definition.id);
        upgraded = true;
    }
    // A family is introduced once. Existing definitions and deletion tombstones
    // record that introduction even after the artist removes its Toolbox group.
    // This keeps removed groups and custom placements removed on every reload.
    QSet<QString> introducedFamilies;
    for (const auto &definition : m_tools) {
        const auto family = catalogFamilyId(definition.brushPresetId);
        if (!family.isEmpty()) introducedFamilies.insert(family);
    }
    for (const auto &presetId : m_deletedBrushPresets) {
        const auto family = catalogFamilyId(presetId);
        if (!family.isEmpty()) introducedFamilies.insert(family);
    }
    bool added = false;
    for (const auto &preset : m_library->presets()) {
        const auto id = "brush/" + preset.id;
        if (tool(id) || m_deletedBrushPresets.contains(preset.id)) continue;
        if (next.tools.size() >= maxTools) { emit warning("The tool library is full. Additional brushes remain in the brush library."); break; }
        ToolDefinition definition;
        definition.id = id; definition.name = preset.name; definition.icon = "brush";
        definition.brushPresetId = preset.id; definition.engine = Canvas::Tool::Brush;
        if (preset.builtIn && preset.id.startsWith("builtin/catalog/"))
            definition.builtinRecipeRevision = catalogBrushRecipeRevision;
        definition.state = normalized({preset.brush, preset.smoothing, {}});
        definition.brushType = presetBrushType(preset);
        definition.category = preset.builtIn ? standardBrushCategory(preset.id) : "Imported";
        if (definition.category.isEmpty()) definition.category = "Drawing";
        if (preset.builtIn) definition.tags = standardBrushTags(preset.id);
        next.tools.push_back(std::move(definition));
        const auto familyId = catalogFamilyId(preset.id);
        const auto destinationId = familyId.isEmpty() ? QString("group/drawing") : familyId;
        auto destination = std::find_if(next.entries.begin(), next.entries.end(),
            [&](const ToolboxEntry &entry) { return entry.id == destinationId; });
        if (destination != next.entries.end()) {
            // Never turn an artist's standalone entry into a group.
            if (destination->group) {
                destination->tools.push_back(id);
                if (destination->lastToolId.isEmpty()) destination->lastToolId = id;
            }
        } else if (!familyId.isEmpty() && !introducedFamilies.contains(familyId) &&
                   next.entries.size() < maxEntries) {
            const auto category = standardBrushCategory(preset.id);
            next.entries.push_back({familyId, category, catalogFamilyIcon(category), true, {id}, id});
        }
        // Imports/legacy presets use Drawing when it still exists; catalog
        // families never flood that list. Unplaced tools stay available in the
        // brush manager and Add Tools when the artist's Toolbox is full.
        added = true;
    }
    if (added || upgraded) {
        // An upgrade can add native images to a catalog already at its resource
        // limit. Validate the whole candidate once before replacing the usable
        // catalog; deferred source presets can be reconciled after space is freed.
        normalizeSelection(next);
        QByteArray encoded;
        QString error;
        if (!encodeCatalog(next.tools, next.entries, next.selected, next.active,
                           next.deletedBrushPresets, encoded, &error)) {
            emit warning((upgraded ? QString("Brush corrections were deferred. ") : QString("New brushes were deferred. ")) +
                         error + " Existing tools were kept unchanged.");
            return;
        }
        // Imported definitions are external library changes, not commands owned by
        // older organization snapshots. Begin a fresh organization history.
        m_history.clear();
        applySnapshot(std::move(next), !upgraded);
        // Preview caches key by tool ID; changed() refreshes organization but
        // does not invalidate an existing brush sample. Notify actual edits only
        // after the whole upgrade succeeds, including a deferred retry.
        for (const auto &id : upgradedSettings) emit settingsChanged(id);
        if (upgradedSettings.contains(m_active)) emit activationRequested(m_active);
    }
}

bool ToolCatalog::save(QString *error) {
    if (error) error->clear();
    m_saveTimer.stop();
    if (!m_writable)
        return fail(error, "The tool library could not be loaded. Its existing file has been preserved.");
    if (m_path.isEmpty()) return fail(error, "The tool library has no storage location.");
    QByteArray bytes;
    if (!encodeCatalog(m_tools, m_entries, m_selected, m_active, m_deletedBrushPresets, bytes, error)) return false;
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) return fail(error, "Could not create the tool library folder.");
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, "Could not save the tool library: " + file.errorString());
    return true;
}

bool ToolCatalog::load(QString *error) {
    if (error) error->clear();
    m_saveTimer.stop();
    m_history.clear();
    m_writable = false;
    if (m_path.isEmpty()) return fail(error, "The tool library has no storage location.");
    if (!QFileInfo::exists(m_path)) {
        m_writable = true;
        reconcileBrushes();
        return true;
    }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, "Could not open the tool library: " + file.errorString());
    if (file.size() > maxFileBytes) return fail(error, "The tool library exceeds its file-size limit.");
    const auto bytes = file.read(maxFileBytes + 1);
    if (bytes.size() > maxFileBytes || file.error() != QFileDevice::NoError || !file.atEnd())
        return fail(error, "Could not read the complete tool library.");
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, "The tool library is not valid JSON.");
    const auto root = document.object();
    if (root.value("format").toString() != "toonpro.tool-catalog" ||
        !root.value("version").isDouble() || (root.value("version").toDouble() != 1 && root.value("version").toDouble() != 2 && root.value("version").toDouble() != 3 && root.value("version").toDouble() != 4 && root.value("version").toDouble() != 5 && root.value("version").toDouble() != 6 && root.value("version").toDouble() != 7 && root.value("version").toDouble() != 8 && root.value("version").toDouble() != 9 && root.value("version").toDouble() != 10 && root.value("version").toDouble() != 11 && root.value("version").toDouble() != 12))
        return fail(error, "This tool library format or version is not supported.");
    const bool hasBrushMetadata = root.value("version").toInt() >= 6;
    QSet<QString> rootFields{"format", "version", "tools", "entries", "resources", "selectedEntryId", "activeToolId"};
    if (hasBrushMetadata) rootFields.insert("deletedBrushPresets");
    if (!knownFields(root, rootFields, error)) return false;
    if (!root.value("tools").isArray() || !root.value("entries").isArray() || !root.value("resources").isObject() ||
        !root.value("selectedEntryId").isString() || !root.value("activeToolId").isString())
        return fail(error, "The tool library has invalid organization fields.");
    const auto definitions = root.value("tools").toArray();
    const auto placements = root.value("entries").toArray();
    if (definitions.size() > maxTools || placements.size() > maxEntries)
        return fail(error, "The tool library exceeds its tool or group limit.");
    QHash<QString, QImage> resources;
    const bool hasMetadata = root.value("version").toInt() >= 3;
    QSet<QString> colorTipIds;
    if (hasBrushMetadata) for (const auto &value : definitions) {
        const auto id = value.toObject().value("colorTip").toString();
        if (!id.isEmpty()) colorTipIds.insert(id);
        const auto surfaceId = value.toObject().value("surfaceImage").toString();
        if (!surfaceId.isEmpty()) colorTipIds.insert(surfaceId);
    }
    if (!readResources(root.value("resources").toObject(), resources, hasMetadata, hasBrushMetadata, colorTipIds, error)) return false;
    QHash<QString, QImage> colorResources;
    Snapshot next;
    if (hasBrushMetadata) {
        if (!root.value("deletedBrushPresets").isArray() || root.value("deletedBrushPresets").toArray().size() > maxTools * 8)
            return fail(error, "The tool library contains invalid deleted brush information.");
        for (const auto &value : root.value("deletedBrushPresets").toArray()) {
            const auto id = value.toString();
            if (!value.isString() || id.isEmpty() || id.size() > 128 || next.deletedBrushPresets.contains(id))
                return fail(error, "The tool library contains an invalid or duplicate deleted brush identifier.");
            next.deletedBrushPresets.insert(id);
        }
    }
    QSet<QString> toolIds, entryIds;
    for (const auto &value : definitions) {
        if (!value.isObject()) return fail(error, "The tool library contains an invalid tool.");
        const auto object = value.toObject();
        QSet<QString> fields{"id", "name", "icon", "brushPresetId", "engine", "variant", "brush", "smoothing", "options", "tip", "texture", "dualTip"};
        fields.insert("builtinRecipeRevision");
        if (hasMetadata) fields.unite({"tags", "color"});
        if (hasBrushMetadata) fields.unite({"brushType", "category", "colorTip"});
        if (root.value("version").toInt() >= 12) fields.insert("surfaceImage");
        if (!knownFields(object, fields, error)) return false;
        for (const char *key : {"id", "name", "icon", "brushPresetId"})
            if (!object.value(QLatin1String(key)).isString()) return fail(error, "A tool contains invalid text fields.");
        ToolDefinition definition;
        definition.id = object.value("id").toString(); definition.name = object.value("name").toString();
        definition.icon = object.value("icon").toString(); definition.brushPresetId = object.value("brushPresetId").toString();
        int engine;
        if (!validId(definition.id) || !validName(definition.name) || !validIcon(definition.icon) ||
            definition.brushPresetId.size() > 128 || toolIds.contains(definition.id) ||
            !integer(object.value("engine"), &engine) || !integer(object.value("variant"), &definition.variant) ||
            !validEngine(Canvas::Tool(engine), definition.variant))
            return fail(error, "A tool has an invalid or duplicate identifier or type.");
        definition.engine = Canvas::Tool(engine);
        if (object.contains("builtinRecipeRevision")) {
            if (!integer(object.value("builtinRecipeRevision"), &definition.builtinRecipeRevision) ||
                definition.builtinRecipeRevision < 0 ||
                (definition.builtinRecipeRevision == 0 && canonicalCatalogTool(definition)) ||
                (definition.builtinRecipeRevision > 0 && !canonicalCatalogTool(definition)))
                return fail(error, "A tool contains invalid built-in brush revision information.");
        } else if (canonicalCatalogTool(definition)) {
            definition.builtinRecipeRevision = 1;
        }
        if (!object.value("brush").isObject() || !object.value("smoothing").isDouble() || !object.value("options").isObject())
            return fail(error, "A tool has invalid property settings.");
        if (!brushSettingsFromJson(object.value("brush").toObject(), definition.state.brush, error) ||
            !resolveResource(object, "tip", resources, definition.state.brush.tipMask, error) ||
            !resolveResource(object, "texture", resources, definition.state.brush.texture.pattern, error) ||
            !resolveResource(object, "dualTip", resources, definition.state.brush.dual.tipMask, error)) return false;
        if (hasBrushMetadata && object.contains("colorTip")) {
            const auto id = object.value("colorTip").toString();
            if (!object.value("colorTip").isString() || !resources.contains(id))
                return fail(error, "A tool refers to a missing colored brush image.");
            if (!colorResources.contains(id)) colorResources.insert(id, resources.value(id).convertToFormat(QImage::Format_ARGB32_Premultiplied));
            definition.state.brush.colorTip = colorResources.value(id);
        }
        definition.state.smoothing = object.value("smoothing").toDouble();
        definition.state.options = object.value("options").toObject();
        if (object.contains("surfaceImage")) {
            QImage image;
            if (!resolveResource(object, "surfaceImage", resources, image, error)) return false;
            definition.state.brush.surface.image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        }
        if (!validState(definition.state, error)) return false;
        if (root.value("version").toInt() < 12 && usesModernBrushFeatures(definition.state.brush))
            return fail(error, "Connected brushes, surfaces and advanced dynamics require tool catalog version 12.");
        if (root.value("version").toInt() < 11 && definition.state.brush.colorDynamics.applyToColorTip)
            return fail(error, "Image color dynamics require tool catalog version 11.");
        if (root.value("version").toInt() < 9 && usesAdvancedBrushTips(definition.state.brush))
            return fail(error, "Tip sets and larger brushes require tool catalog version 9.");
        if (root.value("version").toInt() < 7 && usesExtendedBrushDynamics(definition.state.brush))
            return fail(error, "Extended brush dynamics require tool catalog version 7.");
        if (hasMetadata && !readMetadata(object, definition.tags, definition.color, error)) return false;
        if (hasBrushMetadata) {
            int type;
            if (!integer(object.value("brushType"), &type) || !validBrushType(BrushType(type)) ||
                !object.value("category").isString() || !validCategory(object.value("category").toString()) ||
                next.deletedBrushPresets.contains(definition.brushPresetId))
                return fail(error, "A tool contains invalid brush management information.");
            definition.brushType = BrushType(type);
            if (root.value("version").toInt() < 11 && definition.brushType == BrushType::DynamicStamp)
                return fail(error, "Dynamic stamp types require tool catalog version 11.");
            definition.category = object.value("category").toString().trimmed();
        } else if (definition.engine == Canvas::Tool::Brush) {
            definition.brushType = inferredBrushType(definition.name, definition.state.brush);
            definition.category = definition.brushPresetId.isEmpty() ? "My brushes" :
                definition.brushPresetId.startsWith("builtin/") ? "Drawing" : "Imported";
        }
        toolIds.insert(definition.id);
        next.tools.push_back(std::move(definition));
    }
    for (const auto &value : placements) {
        if (!value.isObject()) return fail(error, "The tool library contains an invalid Toolbox entry.");
        const auto object = value.toObject();
        QSet<QString> fields{"id", "name", "icon", "group", "tools", "lastToolId"};
        if (hasMetadata) fields.unite({"tags", "color", "customIcon"});
        if (!knownFields(object, fields, error)) return false;
        for (const char *key : {"id", "name", "icon", "lastToolId"})
            if (!object.value(QLatin1String(key)).isString()) return fail(error, "A Toolbox entry contains invalid text fields.");
        ToolboxEntry placement;
        placement.id = object.value("id").toString(); placement.name = object.value("name").toString();
        placement.icon = object.value("icon").toString(); placement.lastToolId = object.value("lastToolId").toString();
        if (!validId(placement.id) || !validName(placement.name) || !validIcon(placement.icon) || entryIds.contains(placement.id) ||
            !object.value("group").isBool() || !object.value("tools").isArray())
            return fail(error, "A Toolbox entry has an invalid or duplicate identifier.");
        placement.group = object.value("group").toBool();
        if (hasMetadata) {
            if (!readMetadata(object, placement.tags, placement.color, error) ||
                !resolveResource(object, "customIcon", resources, placement.customIcon, error)) return false;
            if (!placement.customIcon.isNull()) {
                if (placement.customIcon.width() > maxIconSide || placement.customIcon.height() > maxIconSide || !placement.group)
                    return fail(error, "A custom group icon is invalid or exceeds 256 × 256 pixels.");
                placement.customIcon = placement.customIcon.convertToFormat(QImage::Format_ARGB32_Premultiplied);
            }
            if (!placement.group && (!placement.tags.isEmpty() || placement.color.isValid()))
                return fail(error, "Standalone tools store tags and colors on their tool definition.");
        }
        const auto members = object.value("tools").toArray();
        if (members.size() > maxTools || (!placement.group && members.size() != 1))
            return fail(error, "A standalone Toolbox entry must contain exactly one tool.");
        QSet<QString> seen;
        for (const auto &member : members) {
            if (!member.isString() || !toolIds.contains(member.toString()) || seen.contains(member.toString()))
                return fail(error, "A Toolbox entry refers to a missing or duplicate tool.");
            seen.insert(member.toString()); placement.tools.push_back(member.toString());
        }
        if ((!placement.lastToolId.isEmpty() && !seen.contains(placement.lastToolId)) ||
            (!placement.tools.isEmpty() && placement.lastToolId.isEmpty()))
            return fail(error, "A Toolbox entry has an invalid last-used tool.");
        entryIds.insert(placement.id);
        next.entries.push_back(std::move(placement));
    }
    next.selected = root.value("selectedEntryId").toString(); next.active = root.value("activeToolId").toString();
    if ((next.entries.isEmpty() && (!next.selected.isEmpty() || !next.active.isEmpty())) ||
        (!next.entries.isEmpty() && !entryIds.contains(next.selected)))
        return fail(error, "The tool library refers to a missing selected Toolbox entry.");
    const bool activePlaced = std::any_of(next.entries.cbegin(), next.entries.cend(),
        [&](const ToolboxEntry &e) { return e.tools.contains(next.active); });
    const bool anyPlaced = std::any_of(next.entries.cbegin(), next.entries.cend(),
        [](const ToolboxEntry &e) { return !e.tools.isEmpty(); });
    if ((!next.active.isEmpty() && !activePlaced) || (next.active.isEmpty() && anyPlaced))
        return fail(error, "The tool library refers to an unplaced active tool.");
    for (const auto &placement : next.entries) if (placement.id == next.selected && !placement.tools.isEmpty()) {
        if (!placement.tools.contains(next.active) || next.active != placement.lastToolId)
            return fail(error, "The tool library refers to an invalid active tool.");
    }
    // One-time upgrade keeps existing placements, names and active tool intact.
    // Later launches honor artists who remove this default group.
    if (root.value("version").toInt() == 1 && next.tools.size() <= maxTools - 4) {
        const QVector<ToolDefinition> additions{
            {"path-edit", "Path Edit", "mouse-pointer-2", {}, Canvas::Tool::VectorNode, 0, {}},
            {"simplify-brush", "Simplify Brush", "spline", {}, Canvas::Tool::VectorSimplify, 0, {}},
            {"width-brush", "Width Brush", "paintbrush", {}, Canvas::Tool::VectorWidth, 0, {}},
            {"vector-eraser", "Vector Eraser", "scissors", {}, Canvas::Tool::VectorEraser, 0, {}}};
        QStringList added;
        for (const auto &definition : additions) if (!toolIds.contains(definition.id)) {
            next.tools.append(definition); added.append(definition.id); toolIds.insert(definition.id);
        }
        if (!added.isEmpty() && next.entries.size() < maxEntries) {
            const QString id = entryIds.contains("group/vector-editing") ? freshId("group/") : QString("group/vector-editing");
            next.entries.append({id, "Vector Editing", "spline", true, added, added.first()});
            entryIds.insert(id);
            if (next.selected.isEmpty()) { next.selected = id; next.active = added.first(); }
        }
    }
    if (root.value("version").toInt() < 4 && next.tools.size() <= maxTools - 2 && next.entries.size() < maxEntries) {
        QStringList added;
        const QVector<ToolDefinition> additions{
            {"scene-object", "3D Object", "box", {}, Canvas::Tool::SceneObject, 0, {}},
            {"scene-camera", "3D Camera", "video", {}, Canvas::Tool::SceneCamera, 0, {}}};
        for (const auto &definition : additions) if (!toolIds.contains(definition.id)) {
            next.tools.append(definition);added.append(definition.id);toolIds.insert(definition.id);
        }
        if (!added.isEmpty()) {
            const auto id=entryIds.contains("group/scene3d")?freshId("group/"):QString("group/scene3d");
            next.entries.append({id,"3D Reference","box",true,added,added.first()});
            entryIds.insert(id);
            if(next.selected.isEmpty()){next.selected=id;next.active=added.first();}
        }
    }
    if (root.value("version").toInt() < 5 && !toolIds.contains("scene-rig") && next.tools.size() < maxTools) {
        // A group may have been renamed, recolored or recreated. Membership is
        // its identity for this migration; never reset the artist's metadata.
        auto destination = std::find_if(next.entries.begin(), next.entries.end(), [](const ToolboxEntry &entry) {
            return entry.group && (entry.tools.contains("scene-object") || entry.tools.contains("scene-camera"));
        });
        if (destination != next.entries.end() || next.entries.size() < maxEntries) {
            next.tools.append({"scene-rig", "3D Rig", "spline", {}, Canvas::Tool::SceneRig, 0, {}});
            if (destination != next.entries.end()) {
                destination->tools.append("scene-rig");
            } else {
                const auto id = entryIds.contains("group/scene3d") ? freshId("group/") : QString("group/scene3d");
                next.entries.append({id, "3D Reference", "box", true, {"scene-rig"}, "scene-rig"});
                if (next.selected.isEmpty()) { next.selected = id; next.active = "scene-rig"; }
            }
        }
    }
    if(root.value("version").toInt()<8&&next.tools.size()<maxTools&&next.entries.size()<maxEntries){
        auto existing=std::find_if(next.tools.cbegin(),next.tools.cend(),[](const auto &tool){return tool.engine==Canvas::Tool::Text;});
        QString id;
        if(existing!=next.tools.cend())id=existing->id;
        else{id=toolIds.contains("text")?freshId("tool/"):QString("text");next.tools.append({id,"Text","type",{},Canvas::Tool::Text,0,{}});}
        const bool placed=std::any_of(next.entries.cbegin(),next.entries.cend(),[&](const auto &entry){return entry.tools.contains(id);});
        if(!placed){const auto entryId=entryIds.contains("entry/text")?freshId("entry/"):QString("entry/text");next.entries.append({entryId,"Text","type",false,{id},id});if(next.selected.isEmpty()){next.selected=entryId;next.active=id;}}
    }
    if(root.value("version").toInt()<10&&next.tools.size()<maxTools){
        // Add the new engine once. Never recreate a removed Shapes group or
        // overwrite its name, tags, colors, order, or remembered active tool.
        const bool present=std::any_of(next.tools.cbegin(),next.tools.cend(),[](const auto &tool){return tool.engine==Canvas::Tool::Arc;});
        auto destination=std::find_if(next.entries.begin(),next.entries.end(),[](const ToolboxEntry &entry){return entry.group&&entry.id=="group/shapes";});
        if(!present&&(destination!=next.entries.end()||next.entries.size()<maxEntries)){
            const auto id=toolIds.contains("arc")?freshId("tool/"):QString("arc");
            next.tools.append({id,"Arc","spline",{},Canvas::Tool::Arc,0,{}});
            if(destination!=next.entries.end()){
                const bool wasEmpty=destination->tools.isEmpty();
                destination->tools.append(id);
                if(wasEmpty)destination->lastToolId=id;
                if(next.selected==destination->id&&wasEmpty)next.active=id;
            }else{
                const auto entryId=entryIds.contains("entry/arc")?freshId("entry/"):QString("entry/arc");
                next.entries.append({entryId,"Arc","spline",false,{id},id});
                if(next.selected.isEmpty()){next.selected=entryId;next.active=id;}
            }
            normalizeSelection(next);
        }
    }
    upgradeDrawingIcons(next.tools, next.entries);
    // Loading is deliberate replacement; previously cached session states must
    // not replace settings read from disk. Notify before clearing those caches.
    emit aboutToActivate();
    m_latestStates.clear();
    m_tools = std::move(next.tools); m_entries = std::move(next.entries);
    m_selected = std::move(next.selected); m_active = std::move(next.active);
    m_deletedBrushPresets = std::move(next.deletedBrushPresets);
    m_history.clear();
    m_writable = true;
    emit changed();
    emit activationRequested(m_active);
    reconcileBrushes();
    return true;
}

} // namespace toon
