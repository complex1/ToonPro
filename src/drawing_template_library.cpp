#include "drawing_template_library.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace toon {
namespace {
constexpr qint64 maxTemplateFileBytes = 1024 * 1024;
bool fields(const QJsonObject &object, std::initializer_list<const char *> names) {
    if (object.size() != qsizetype(names.size())) return false;
    for (const auto *name : names) if (!object.contains(QLatin1String(name))) return false;
    return true;
}
bool validName(const QString &name) { return !name.trimmed().isEmpty() && name.size() <= 100; }
QByteArray serialize(const QVector<DrawingTemplateEntry> &entries) {
    QJsonArray items;
    for (const auto &entry : entries) {
        QJsonArray layers;
        for (const auto &slot : entry.layers)
            layers.append(QJsonObject{{"id",slot.id},{"name",slot.name},{"kind",layerKindKey(slot.kind)}});
        items.append(QJsonObject{{"id",entry.id},{"name",entry.name},{"layers",layers}});
    }
    return QJsonDocument(QJsonObject{{"format","toon-pro-drawing-templates"},{"version",1},{"templates",items}}).toJson(QJsonDocument::Indented);
}
}
DrawingTemplateLibrary::DrawingTemplateLibrary(QString filePath, QObject *parent)
    : QObject(parent), m_filePath(std::move(filePath)) {}
bool DrawingTemplateLibrary::fail(const QString &message, QString *error) {
    m_error=message; if (error) *error=message; return false;
}
bool DrawingTemplateLibrary::load(QString *error) {
    m_writable=false; m_error.clear(); m_entries.clear(); m_loadedBytes.clear();
    if (error) error->clear();
    QFile file(m_filePath); m_existed=file.exists();
    if (!m_existed) { m_writable=true; emit changed(); return true; }
    if (file.size()>maxTemplateFileBytes) return fail("Drawing template library exceeds 1 MiB. The original file was preserved.",error);
    if (!file.open(QIODevice::ReadOnly)) return fail("Could not read drawing templates: "+file.errorString(),error);
    const auto data=file.read(maxTemplateFileBytes+1);
    if (data.size()>maxTemplateFileBytes) return fail("Drawing template library exceeds 1 MiB. The original file was preserved.",error);
    if (file.error()!=QFileDevice::NoError) return fail("Could not read drawing templates: "+file.errorString(),error);
    QJsonParseError parse; const auto json=QJsonDocument::fromJson(data,&parse); const auto root=json.object();
    auto invalid=[&] { return fail("Drawing template library is damaged or uses an unsupported version. The original file was preserved.",error); };
    if (parse.error!=QJsonParseError::NoError || !json.isObject() || !fields(root,{"format","version","templates"})
        || root["format"]!="toon-pro-drawing-templates" || root["version"]!=1 || !root["templates"].isArray()
        || root["templates"].toArray().size()>maxDrawingTemplates) return invalid();
    QVector<DrawingTemplateEntry> entries; QSet<QString> ids,names;
    for (const auto &value : root["templates"].toArray()) {
        const auto object=value.toObject();
        if (!value.isObject() || !fields(object,{"id","name","layers"}) || !object["id"].isString()
            || !object["name"].isString() || !object["layers"].isArray()) return invalid();
        DrawingTemplateEntry entry{object["id"].toString(),object["name"].toString(),{}};
        const auto nameKey=entry.name.trimmed().toCaseFolded();
        if (QUuid(entry.id).isNull() || ids.contains(entry.id) || !validName(entry.name) || names.contains(nameKey)
            || object["layers"].toArray().size()>=maxDrawingLayers) return invalid();
        for (const auto &item : object["layers"].toArray()) {
            const auto layer=item.toObject();
            if (!item.isObject() || !fields(layer,{"id","name","kind"}) || !layer["id"].isString()
                || !layer["name"].isString() || !layer["kind"].isString()) return invalid();
            const auto kind=layerKindFromKey(layer["kind"].toString());
            if (!kind) return invalid();
            entry.layers.append({layer["id"].toString(),layer["name"].toString(),*kind});
        }
        if (!validateDrawingTemplate(entry.layers).isEmpty()) return invalid();
        ids.insert(entry.id); names.insert(nameKey); entries.append(entry);
    }
    m_entries=std::move(entries); m_loadedBytes=data; m_writable=true; emit changed(); return true;
}
bool DrawingTemplateLibrary::commit(const QVector<DrawingTemplateEntry> &entries, QString *error) {
    if (!m_writable) return fail("Drawing templates are read-only. "+m_error,error);
    QFile current(m_filePath);
    if (current.exists()!=m_existed || (m_existed && (!current.open(QIODevice::ReadOnly)
        || current.size()>maxTemplateFileBytes || current.read(maxTemplateFileBytes+1)!=m_loadedBytes))) {
        m_writable=false;
        return fail("The drawing template library changed outside Toon Pro. Reopen the application before saving templates; the file was preserved.",error);
    }
    // Windows cannot atomically replace the destination while this read handle
    // is still open. Finish the conflict check before QSaveFile publishes it.
    current.close();
    const auto data=serialize(entries);
    if (data.size()>maxTemplateFileBytes) return fail("Drawing template library exceeds the 1 MiB limit.",error);
    if (!QDir().mkpath(QFileInfo(m_filePath).absolutePath())) return fail("Could not create the drawing template library folder.",error);
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit())
        return fail("Could not save drawing templates: "+file.errorString(),error);
    m_entries=entries; m_loadedBytes=data; m_existed=true; m_error.clear(); if (error) error->clear(); emit changed(); return true;
}
bool DrawingTemplateLibrary::saveTemplate(const QString &name, const QVector<DrawingTemplateSlot> &layers, QString *error) {
    if (!validName(name)) return fail("Give the template a name of 1 to 100 characters.",error);
    const auto validation=validateDrawingTemplate(layers); if (!validation.isEmpty()) return fail(validation,error);
    auto entries=m_entries; const auto key=name.trimmed().toCaseFolded();
    for (auto &entry : entries) if (entry.name.trimmed().toCaseFolded()==key) {
        entry.name=name.trimmed(); entry.layers=layers; return commit(entries,error);
    }
    if (entries.size()>=maxDrawingTemplates) return fail("The drawing template library supports up to 128 presets.",error);
    entries.append({QUuid::createUuid().toString(QUuid::WithoutBraces),name.trimmed(),layers}); return commit(entries,error);
}
bool DrawingTemplateLibrary::removeTemplate(const QString &id, QString *error) {
    auto entries=m_entries;
    if (!entries.removeIf([&](const auto &entry){return entry.id==id;})) return fail("Choose a saved template to remove.",error);
    return commit(entries,error);
}
} // namespace toon
