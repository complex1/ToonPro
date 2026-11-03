#include "palette_library.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace toon {
namespace {
constexpr qint64 maxPaletteFileBytes = 2 * 1024 * 1024;
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validName(const QString &name) {
    if (name.trimmed().isEmpty() || name.size() > 100) return false;
    for (const auto ch : name) if (ch.isSpace() && ch != QChar(' ')) return false;
    for (const auto ch : name) if (ch.category() == QChar::Other_Control) return false;
    return true;
}
bool validId(const QString &id) { const QUuid uuid(id); return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id; }
bool fields(const QJsonObject &object, std::initializer_list<const char *> names) {
    if (object.size() != qsizetype(names.size())) return false;
    for (const auto *name : names) if (!object.contains(QLatin1String(name))) return false;
    return true;
}
bool validColor(const QString &color) {
    if (color.size() != 9 || color[0] != '#') return false;
    for (qsizetype i = 1; i < color.size(); ++i) {
        const auto ch = color[i].toLower();
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    return QColor(color).isValid();
}
QString validate(const QVector<ColorPalette> &entries) {
    if (entries.size() > maxColorPalettes) return "The color library supports up to 64 palettes.";
    QSet<QString> ids, paletteNames;
    for (const auto &palette : entries) {
        if (!validId(palette.id) || ids.contains(palette.id)) return "Each palette and swatch needs a unique identifier.";
        ids.insert(palette.id);
        if (!validName(palette.name)) return "Give the palette a single-line name of 1 to 100 characters.";
        const auto nameKey = palette.name.trimmed().toCaseFolded();
        if (paletteNames.contains(nameKey)) return "A palette already uses that name.";
        paletteNames.insert(nameKey);
        if (palette.swatches.size() > maxPaletteSwatches) return "Each palette supports up to 256 swatches.";
        QSet<QString> swatchNames;
        for (const auto &swatch : palette.swatches) {
            if (!validId(swatch.id) || ids.contains(swatch.id)) return "Each palette and swatch needs a unique identifier.";
            ids.insert(swatch.id);
            if (!validName(swatch.name)) return "Give the swatch a single-line name of 1 to 100 characters.";
            if (!swatch.color.isValid()) return "Choose a valid swatch color.";
            const auto key = swatch.name.trimmed().toCaseFolded();
            if (swatchNames.contains(key)) return "A swatch in this palette already uses that name.";
            swatchNames.insert(key);
        }
    }
    return {};
}
QByteArray serialize(const QVector<ColorPalette> &entries) {
    QJsonArray palettes;
    for (const auto &palette : entries) {
        QJsonArray swatches;
        for (const auto &swatch : palette.swatches) swatches.append(QJsonObject{{"id", swatch.id}, {"name", swatch.name}, {"color", swatch.color.name(QColor::HexArgb)}});
        palettes.append(QJsonObject{{"id", palette.id}, {"name", palette.name}, {"swatches", swatches}});
    }
    return QJsonDocument(QJsonObject{{"format", "toon-pro-color-palettes"}, {"version", 1}, {"palettes", palettes}}).toJson(QJsonDocument::Indented);
}
}
ColorPalette standardColorPalette() {
    ColorPalette palette{"5ea5c5bf-c4eb-4916-96d7-b8acb011f991", "Standard", {}};
    const QStringList names{"Ink", "White", "Cool gray", "Rose", "Peach", "Gold", "Sage", "Blue"};
    const QStringList colors{"#202530", "#ffffff", "#8b9caf", "#b54e55", "#e1a27f", "#e9c96b", "#6f9d82", "#497fc0"};
    for (int i = 0; i < names.size(); ++i)
        palette.swatches.append({QString("5291a713-cdc6-4b70-a8ea-10e44b6c0f%1").arg(i + 1, 2, 16, QChar('0')), names[i], QColor(colors[i])});
    return palette;
}
PaletteLibrary::PaletteLibrary(QString filePath, QObject *parent) : QObject(parent), m_filePath(std::move(filePath)) {}
const ColorPalette *PaletteLibrary::palette(const QString &id) const {
    for (const auto &entry : m_entries) if (entry.id == id) return &entry;
    return nullptr;
}
bool PaletteLibrary::fail(const QString &message, QString *error) { m_error = message; if (error) *error = message; return false; }
bool PaletteLibrary::load(QString *error) {
    m_writable = false; m_error.clear(); m_entries.clear(); m_loadedBytes.clear(); if (error) error->clear();
    const QFileInfo info(m_filePath); m_existed = info.exists() || info.isSymLink();
    auto failLoad = [&](const QString &message) { fail(message, error); emit changed(); return false; };
    if (m_filePath.trimmed().isEmpty()) return failLoad("No color palette library location was provided.");
    if (info.isSymLink()) return failLoad("The color palette library is a symbolic link. The original file was preserved.");
    if (!m_existed) { m_entries = {standardColorPalette()}; m_writable = true; emit changed(); return true; }
    QFile file(m_filePath);
    if (file.size() > maxPaletteFileBytes) return failLoad("The color palette library exceeds 2 MiB. The original file was preserved.");
    if (!file.open(QIODevice::ReadOnly)) return failLoad("Could not read color palettes: " + file.errorString());
    const auto data = file.read(maxPaletteFileBytes + 1);
    if (data.size() > maxPaletteFileBytes || file.error() != QFileDevice::NoError)
        return failLoad("Could not read the bounded color palette library. The original file was preserved.");
    QJsonParseError parse; const auto json = QJsonDocument::fromJson(data, &parse); const auto root = json.object();
    auto invalid = [&] { return failLoad("The color palette library is damaged or uses an unsupported version. The original file was preserved."); };
    if (parse.error != QJsonParseError::NoError || !json.isObject() || !fields(root, {"format", "version", "palettes"})
        || root["format"] != "toon-pro-color-palettes" || root["version"] != 1 || !root["palettes"].isArray()
        || root["palettes"].toArray().size() > maxColorPalettes) return invalid();
    QVector<ColorPalette> entries;
    for (const auto &value : root["palettes"].toArray()) {
        if (!value.isObject()) return invalid();
        const auto object = value.toObject();
        if (!fields(object, {"id", "name", "swatches"}) || !object["id"].isString() || !object["name"].isString()
            || !object["swatches"].isArray() || object["swatches"].toArray().size() > maxPaletteSwatches) return invalid();
        ColorPalette palette{object["id"].toString(), object["name"].toString(), {}};
        for (const auto &item : object["swatches"].toArray()) {
            if (!item.isObject()) return invalid();
            const auto swatch = item.toObject();
            if (!fields(swatch, {"id", "name", "color"}) || !swatch["id"].isString() || !swatch["name"].isString()
                || !swatch["color"].isString() || !validColor(swatch["color"].toString())) return invalid();
            palette.swatches.append({swatch["id"].toString(), swatch["name"].toString(), QColor(swatch["color"].toString())});
        }
        entries.append(palette);
    }
    if (!validate(entries).isEmpty()) return invalid();
    m_entries = std::move(entries); m_loadedBytes = data; m_writable = true; emit changed(); return true;
}
bool PaletteLibrary::commit(const QVector<ColorPalette> &entries, QString *error) {
    if (!m_writable) return fail(m_error.isEmpty() ? "The color palette library is read-only." : m_error, error);
    if (const auto invalid = validate(entries); !invalid.isEmpty()) return fail(invalid, error);
    const auto data = serialize(entries);
    if (data.size() > maxPaletteFileBytes) return fail("The color palette library exceeds the 2 MiB limit.", error);
    if (!QDir().mkpath(QFileInfo(m_filePath).absolutePath())) return fail("Could not create the color palette library folder.", error);
    QLockFile lock(m_filePath + ".lock");
    if (!lock.tryLock(0)) return fail("Another Toon Pro window is saving color palettes. Try again shortly.", error);
    QFile current(m_filePath); const QFileInfo currentInfo(m_filePath);
    if (currentInfo.isSymLink() || current.exists() != m_existed || (m_existed && (!current.open(QIODevice::ReadOnly)
        || current.size() > maxPaletteFileBytes || current.read(maxPaletteFileBytes + 1) != m_loadedBytes || current.error() != QFileDevice::NoError))) {
        m_writable = false;
        return fail("The color palette library changed outside this window. Reopen Toon Pro to reload it; the file was preserved.", error);
    }
    current.close(); QSaveFile file(m_filePath); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return fail("Could not save color palettes: " + file.errorString(), error);
    m_entries = entries; m_loadedBytes = data; m_existed = true; m_error.clear(); if (error) error->clear(); emit changed(); return true;
}
QString PaletteLibrary::createPalette(const QString &name, QString *error) {
    auto entries = m_entries; const auto id = newId(); entries.append({id, name.trimmed(), {}});
    return commit(entries, error) ? id : QString{};
}
bool PaletteLibrary::renamePalette(const QString &id, const QString &name, QString *error) {
    auto entries = m_entries;
    for (auto &entry : entries) if (entry.id == id) { entry.name = name.trimmed(); return commit(entries, error); }
    return fail("Choose a saved palette to rename.", error);
}
bool PaletteLibrary::removePalette(const QString &id, QString *error) {
    auto entries = m_entries;
    if (!entries.removeIf([&](const auto &entry) { return entry.id == id; })) return fail("Choose a saved palette to delete.", error);
    return commit(entries, error);
}
QString PaletteLibrary::addSwatch(const QString &paletteId, const QString &name, const QColor &color, QString *error) {
    if (!color.isValid()) { fail("Choose a valid swatch color.", error); return {}; }
    auto entries = m_entries;
    for (auto &entry : entries) if (entry.id == paletteId) {
        const auto id = newId(); entry.swatches.append({id, name.trimmed(), QColor::fromRgba(color.rgba())});
        return commit(entries, error) ? id : QString{};
    }
    fail("Choose a palette before adding a swatch.", error); return {};
}
bool PaletteLibrary::updateSwatch(const QString &paletteId, const QString &swatchId, const QColor &color, QString *error) {
    if (!color.isValid()) return fail("Choose a valid swatch color.", error);
    auto entries = m_entries;
    for (auto &entry : entries) if (entry.id == paletteId) for (auto &swatch : entry.swatches) if (swatch.id == swatchId) {
        swatch.color = QColor::fromRgba(color.rgba()); return commit(entries, error);
    }
    return fail("Choose a saved swatch to update.", error);
}
bool PaletteLibrary::renameSwatch(const QString &paletteId, const QString &swatchId, const QString &name, QString *error) {
    auto entries = m_entries;
    for (auto &entry : entries) if (entry.id == paletteId) for (auto &swatch : entry.swatches) if (swatch.id == swatchId) {
        swatch.name = name.trimmed(); return commit(entries, error);
    }
    return fail("Choose a saved swatch to rename.", error);
}
bool PaletteLibrary::removeSwatch(const QString &paletteId, const QString &swatchId, QString *error) {
    auto entries = m_entries;
    for (auto &entry : entries) if (entry.id == paletteId) {
        if (!entry.swatches.removeIf([&](const auto &swatch) { return swatch.id == swatchId; })) break;
        return commit(entries, error);
    }
    return fail("Choose a saved swatch to delete.", error);
}
} // namespace toon
