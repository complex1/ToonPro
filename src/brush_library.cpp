#include "brush_library.h"
#include "catalog_brushes.h"
#include "standard_brushes.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <cmath>
#include <utility>

namespace toon {
namespace {
constexpr qint64 maxMaskBytes = 6 * 1024 * 1024;
constexpr qint64 maxColorBytes = 17 * 1024 * 1024;

bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}

QVector<BrushPreset> builtIns() {
    QVector<BrushPreset> result;
    auto add = [&result](const QString &id, const QString &name, qreal size,
                         qreal flow, qreal smoothing, bool pressure) {
        BrushPreset preset;
        preset.id = id;
        preset.name = name;
        preset.brush.size = size;
        preset.brush.opacity = flow;
        preset.brush.pressureSize = pressure;
        preset.brush.spacing = .02;
        preset.smoothing = smoothing;
        preset.builtIn = true;
        result.push_back(preset);
    };
    add("builtin/ink", "Round Ink", 12, 1, 20, true);
    add("builtin/sketch", "Round Sketch", 6, .25, 10, true);
    add("builtin/blocking", "Round Blocking", 96, 1, 0, false);
    result.append(standardBrushPresets());
    result.append(catalogBrushPresets());
    return result;
}

bool numberIn(qreal value, qreal minimum, qreal maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool validResource(const QImage &image, bool tip, qint64 &totalPixels, QString *error) {
    if (image.isNull()) return true;
    if (image.format() != QImage::Format_Grayscale8 || image.width() > BrushLibrary::maxTipSide ||
        image.height() > BrushLibrary::maxTipSide)
        return fail(error, "Brush resources must be grayscale images no larger than 2048 × 2048.");
    totalPixels += qint64(image.width()) * image.height();
    if (totalPixels > BrushLibrary::maxTipPixels)
        return fail(error, "The brush library exceeds its 64 million resource-pixel limit.");
    // An all-black texture is useful; an entirely empty stamp is almost always
    // a mistaken import and cannot provide a visible brush footprint.
    if (!tip) return true;
    for (int y = 0; y < image.height(); ++y) {
        const auto *row = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x)
            if (row[x]) return true;
    }
    return fail(error, "The brush tip is completely transparent.");
}

bool validPreset(const BrushPreset &preset, qint64 &totalPixels, QString *error) {
    if (preset.builtIn || preset.id.isEmpty() || preset.id.size() > 128 ||
        preset.id.startsWith("builtin/"))
        return fail(error, "The brush preset has an invalid identifier.");
    if (preset.name.trimmed().isEmpty() || preset.name.size() > 120)
        return fail(error, "Brush names must contain 1 to 120 characters.");
    if (!numberIn(preset.smoothing, 0, 100))
        return fail(error, "The brush preset contains invalid smoothing.");
    if (!validateBrushSettings(preset.brush, error)) return false;
    if (preset.source.size() > 2048 || preset.compatibility.size() > 32)
        return fail(error, "The brush preset contains too much source information.");
    for (const auto &note : preset.compatibility)
        if (note.size() > 512)
            return fail(error, "A brush compatibility note is too long.");
    totalPixels += preset.brush.colorTip.sizeInBytes() + preset.brush.surface.image.sizeInBytes();
    if (totalPixels > BrushLibrary::maxTipPixels)
        return fail(error, "The brush library exceeds its 64 MiB resource-memory limit.");
    return validResource(preset.brush.tipMask, true, totalPixels, error) &&
           validResource(preset.brush.texture.pattern, false, totalPixels, error) &&
           validResource(preset.brush.dual.tipMask, true, totalPixels, error);
}

bool validateCustom(const QVector<BrushPreset> &presets, QString *error) {
    if (presets.size() > BrushLibrary::maxCustomPresets)
        return fail(error, "The brush library can store up to 128 custom presets.");
    QSet<QString> ids;
    qint64 pixels = 0;
    for (const auto &preset : presets) {
        if (!validPreset(preset, pixels, error)) return false;
        if (!validateBrushTipImages(preset.brush, error)) return false;
        if (ids.contains(preset.id))
            return fail(error, "The brush library contains duplicate preset identifiers.");
        ids.insert(preset.id);
    }
    return true;
}

QVector<BrushPreset> customOnly(const QVector<BrushPreset> &presets) {
    QVector<BrushPreset> custom;
    for (const auto &preset : presets)
        if (!preset.builtIn) custom.push_back(preset);
    return custom;
}

bool encodeResource(QJsonObject &entry, const char *key, const QImage &image, QString *error) {
    if (image.isNull()) return true;
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG") || png.size() > (image.format() == QImage::Format_ARGB32_Premultiplied ? maxColorBytes : maxMaskBytes))
        return fail(error, "The brush resource could not be encoded within the size limit.");
    entry[QLatin1String(key)] = QString::fromLatin1(png.toBase64());
    return true;
}

bool encode(const QVector<BrushPreset> &custom, QByteArray &bytes, QString *error) {
    if (!validateCustom(custom, error)) return false;
    QJsonArray entries;
    for (const auto &preset : custom) {
        QJsonObject entry{{"id", preset.id}, {"name", preset.name},
                          {"settings", brushSettingsToJson(preset.brush)},
                          {"smoothing", preset.smoothing}, {"source", preset.source},
                          {"compatibility", QJsonArray::fromStringList(preset.compatibility)}};
        if (!encodeResource(entry, "tipMask", preset.brush.tipMask, error) ||
            !encodeResource(entry, "texturePattern", preset.brush.texture.pattern, error) ||
            !encodeResource(entry, "dualTipMask", preset.brush.dual.tipMask, error) ||
            !encodeResource(entry, "colorTip", preset.brush.colorTip, error) ||
            !encodeResource(entry, "surfaceImage", preset.brush.surface.image, error)) return false;
        entries.push_back(entry);
    }
    bool colored = false, extended = false, advanced = false, imageColorDynamics = false, modern = false;
    for (const auto &preset : custom) {
        colored |= !preset.brush.colorTip.isNull();
        extended |= usesExtendedBrushDynamics(preset.brush);
        advanced |= usesAdvancedBrushTips(preset.brush);
        imageColorDynamics |= preset.brush.colorDynamics.applyToColorTip;
        modern |= usesModernBrushFeatures(preset.brush);
    }
    bytes = QJsonDocument(QJsonObject{{"format", "toonpro.brush-library"},
                                     {"version", modern ? 7 : imageColorDynamics ? 6 : advanced ? 5 : extended ? 4 : colored ? 3 : 2}, {"presets", entries}})
                .toJson(QJsonDocument::Compact);
    if (bytes.size() > BrushLibrary::maxFileBytes)
        return fail(error, "The brush library exceeds its file-size limit.");
    return true;
}

bool decodeResource(const QJsonObject &entry, const char *key, QImage &image,
                    qint64 &totalPixels, QString *error, bool colored = false) {
    const qint64 maximum = colored ? maxColorBytes : maxMaskBytes;
    if (!entry.contains(QLatin1String(key))) return true;
    if (!entry.value(QLatin1String(key)).isString())
        return fail(error, "The brush preset contains an invalid resource image.");
    const auto encoded = entry.value(QLatin1String(key)).toString();
    if (encoded.isEmpty() || encoded.size() > (maximum + 2) / 3 * 4)
        return fail(error, "The brush resource exceeds its file-size limit.");
    auto result = QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (!result || result.decoded.size() > maximum)
        return fail(error, "The brush resource has invalid image data.");
    QBuffer buffer(&result.decoded);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "PNG");
    const auto size = reader.size();
    if (!size.isValid() || size.isEmpty() || size.width() > BrushLibrary::maxTipSide ||
        size.height() > BrushLibrary::maxTipSide)
        return fail(error, "The brush resource has unsupported image dimensions.");
    totalPixels += qint64(size.width()) * size.height() * (colored ? 4 : 1);
    if (totalPixels > BrushLibrary::maxTipPixels)
        return fail(error, "The brush library exceeds its 64 million resource-pixel limit.");
    const auto decoded = reader.read();
    if (decoded.isNull() || (!colored && (decoded.hasAlphaChannel() || !decoded.isGrayscale())))
        return fail(error, colored ? "The colored stamp is not a valid PNG." : "The brush resource is not a valid grayscale PNG.");
    image = decoded.convertToFormat(colored ? QImage::Format_ARGB32_Premultiplied : QImage::Format_Grayscale8);
    return !image.isNull() || fail(error, "The brush resource could not be loaded.");
}

bool knownFields(const QJsonObject &object, const QSet<QString> &allowed, QString *error) {
    for (const auto &key : object.keys())
        if (!allowed.contains(key))
            return fail(error, "The brush library contains an unknown field: " + key);
    return true;
}

bool decode(const QByteArray &bytes, QVector<BrushPreset> &presets, QString *error) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, "The brush library is not valid JSON.");
    const auto root = document.object();
    const auto versionValue = root.value("version");
    if (root.value("format").toString() != "toonpro.brush-library" || !versionValue.isDouble() ||
        (versionValue.toDouble() != 1 && versionValue.toDouble() != 2 && versionValue.toDouble() != 3 && versionValue.toDouble() != 4 && versionValue.toDouble() != 5 && versionValue.toDouble() != 6 && versionValue.toDouble() != 7))
        return fail(error, "This brush library format or version is not supported.");
    const int version = versionValue.toInt();
    if (!knownFields(root, {"format", "version", "presets"}, error)) return false;
    if (!root.value("presets").isArray())
        return fail(error, "The brush library has no valid preset list.");
    const auto entries = root.value("presets").toArray();
    if (entries.size() > BrushLibrary::maxCustomPresets)
        return fail(error, "The brush library can store up to 128 custom presets.");
    qint64 resourcePixels = 0;
    for (const auto &value : entries) {
        if (!value.isObject()) return fail(error, "The brush library contains an invalid preset.");
        const auto entry = value.toObject();
        QSet<QString> allowed{"id", "name", "source", "smoothing", "compatibility", "tipMask"};
        if (version == 1) allowed.unite({"size", "opacity", "spacing", "pressureSize"});
        else allowed.unite({"settings", "texturePattern", "dualTipMask"});
        if (version >= 3) allowed.insert("colorTip");
        if (version >= 7) allowed.insert("surfaceImage");
        if (!knownFields(entry, allowed, error)) return false;
        for (const auto *key : {"id", "name", "source"})
            if (!entry.value(QLatin1String(key)).isString())
                return fail(error, "The brush preset has invalid text fields.");
        if (!entry.value("smoothing").isDouble() || !entry.value("compatibility").isArray())
            return fail(error, "The brush preset has invalid settings.");
        BrushPreset preset;
        preset.id = entry.value("id").toString();
        preset.name = entry.value("name").toString();
        preset.source = entry.value("source").toString();
        preset.smoothing = entry.value("smoothing").toDouble();
        if (version == 1) {
            for (const auto *key : {"size", "opacity", "spacing"})
                if (!entry.value(QLatin1String(key)).isDouble())
                    return fail(error, "The legacy brush has invalid numeric settings.");
            if (!entry.value("pressureSize").isBool())
                return fail(error, "The legacy brush has invalid pressure settings.");
            preset.brush.size = entry.value("size").toDouble();
            // Version 1 called dab flow opacity. Stroke opacity remains at 1.
            preset.brush.opacity = entry.value("opacity").toDouble();
            preset.brush.spacing = entry.value("spacing").toDouble();
            preset.brush.pressureSize = entry.value("pressureSize").toBool();
            if (!numberIn(preset.brush.opacity, .01, 1) || !numberIn(preset.brush.spacing, .01, 1))
                return fail(error, "The legacy brush has unsupported settings.");
        } else {
            if (!entry.value("settings").isObject())
                return fail(error, "The brush preset has no valid settings object.");
            if (!brushSettingsFromJson(entry.value("settings").toObject(), preset.brush, error)) return false;
            if (version < 7 && usesModernBrushFeatures(preset.brush))
                return fail(error, "Connected brushes, surfaces and advanced dynamics require brush library format 7.");
            if (version < 6 && preset.brush.colorDynamics.applyToColorTip)
                return fail(error, "Image color dynamics require brush library format 6.");
            if (version < 5 && usesAdvancedBrushTips(preset.brush))
                return fail(error, "Tip sets and larger brushes require brush library format 5.");
            if (version < 4 && usesExtendedBrushDynamics(preset.brush))
                return fail(error, "Response curves, stroke jitter and rotation controls require brush library format 4.");
        }
        const auto compatibility = entry.value("compatibility").toArray();
        if (compatibility.size() > 32)
            return fail(error, "The brush preset contains too much source information.");
        for (const auto &note : compatibility) {
            if (!note.isString()) return fail(error, "The brush preset has an invalid compatibility note.");
            preset.compatibility.push_back(note.toString());
        }
        qint64 noPixels = 0;
        if (!validPreset(preset, noPixels, error)) return false;
        if (!decodeResource(entry, "tipMask", preset.brush.tipMask, resourcePixels, error)) return false;
        if (version >= 2 &&
            (!decodeResource(entry, "texturePattern", preset.brush.texture.pattern, resourcePixels, error) ||
             !decodeResource(entry, "dualTipMask", preset.brush.dual.tipMask, resourcePixels, error))) return false;
        if (version >= 3 && !decodeResource(entry, "colorTip", preset.brush.colorTip, resourcePixels, error, true)) return false;
        if (version >= 7 && !decodeResource(entry, "surfaceImage", preset.brush.surface.image, resourcePixels, error, true)) return false;
        presets.push_back(preset);
    }
    return validateCustom(presets, error);
}
} // namespace

BrushLibrary::BrushLibrary(QString path, QObject *parent)
    : QObject(parent), m_path(std::move(path)), m_presets(builtIns()) {}

bool BrushLibrary::load(QString *error) {
    if (error) error->clear();
    m_writable = false;
    if (m_path.isEmpty()) return fail(error, "The brush library has no storage location.");
    QVector<BrushPreset> custom;
    if (QFileInfo::exists(m_path)) {
        QFile file(m_path);
        if (!file.open(QIODevice::ReadOnly))
            return fail(error, "Could not open the brush library: " + file.errorString());
        if (file.size() > maxFileBytes)
            return fail(error, "The brush library exceeds its file-size limit.");
        const auto bytes = file.read(maxFileBytes + 1);
        if (bytes.size() > maxFileBytes || file.error() != QFileDevice::NoError || !file.atEnd())
            return fail(error, "Could not read the complete brush library.");
        if (!decode(bytes, custom, error)) return false;
    }
    auto next = builtIns();
    next += custom;
    m_presets = std::move(next);
    m_writable = true;
    emit changed();
    return true;
}

bool BrushLibrary::checkWritable(QString *error) const {
    if (error) error->clear();
    return m_writable || fail(error, "The brush library could not be loaded. Its existing file has been preserved.");
}

bool BrushLibrary::commit(const QVector<BrushPreset> &presets, QString *error) {
    if (!checkWritable(error)) return false;
    QByteArray bytes;
    if (!encode(customOnly(presets), bytes, error)) return false;
    const auto directory = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(directory))
        return fail(error, "Could not create the brush library folder.");
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, "Could not save the brush library: " + file.errorString());
    m_presets = presets;
    emit changed();
    return true;
}

bool BrushLibrary::addPresets(const QVector<BrushPreset> &presets, QString *error) {
    if (!checkWritable(error)) return false;
    if (presets.isEmpty()) return true;
    auto next = m_presets;
    for (auto preset : presets) {
        if (preset.builtIn)
            return fail(error, "Built-in brush presets cannot be added or replaced.");
        if (preset.id.isEmpty()) preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        preset.name = preset.name.trimmed();
        // Color is a drawing choice, independent of the selected brush preset.
        preset.brush.color = BrushSettings{}.color;
        preset.brush.backgroundColor = BrushSettings{}.backgroundColor;
        next.push_back(std::move(preset));
    }
    return commit(next, error);
}

bool BrushLibrary::saveCurrent(const QString &name, const BrushSettings &brush, qreal smoothing, QString *error) {
    BrushPreset preset;
    preset.name = name;
    preset.brush = brush;
    preset.smoothing = smoothing;
    return addPresets({preset}, error);
}

bool BrushLibrary::renamePreset(const QString &id, const QString &name, QString *error) {
    if (!checkWritable(error)) return false;
    auto next = m_presets;
    for (auto &preset : next) {
        if (preset.id != id) continue;
        if (preset.builtIn) return fail(error, "Built-in brushes cannot be renamed.");
        preset.name = name.trimmed();
        return commit(next, error);
    }
    return fail(error, "The brush preset no longer exists.");
}

bool BrushLibrary::removePreset(const QString &id, QString *error) {
    if (!checkWritable(error)) return false;
    auto next = m_presets;
    for (int i = 0; i < next.size(); ++i) {
        if (next[i].id != id) continue;
        if (next[i].builtIn) return fail(error, "Built-in brushes cannot be deleted.");
        next.removeAt(i);
        return commit(next, error);
    }
    return fail(error, "The brush preset no longer exists.");
}

} // namespace toon
