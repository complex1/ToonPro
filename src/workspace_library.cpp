#include "workspace_library.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSettings>
#include <QUuid>

namespace toon {
namespace {
QString cleanName(const QString &name) { return name.trimmed().normalized(QString::NormalizationForm_C); }
QString nameKey(const QString &name) { return cleanName(name).toCaseFolded(); }
bool validName(const QString &name) {
    const auto value=cleanName(name);
    if(value.isEmpty() || value.size()>100)return false;
    for(const auto character:value)
        if(character.category()==QChar::Other_Control || character.category()==QChar::Separator_Line
            || character.category()==QChar::Separator_Paragraph)return false;
    return true;
}
bool reservedName(const QString &name) {
    static const QSet<QString> reserved{"illustration","anime","storyboarding","animatics","drawing","compact drawing"};
    return reserved.contains(nameKey(name));
}
bool fields(const QJsonObject &object,std::initializer_list<const char *> names) {
    if(object.size()!=qsizetype(names.size()))return false;
    for(const auto *name:names)if(!object.contains(QLatin1String(name)))return false;
    return true;
}
bool fail(const QString &message,QString *error) { if(error)*error=message;return false; }
QByteArray serialize(const QVector<WorkspaceEntry> &entries) {
    QJsonArray items;
    for(const auto &entry:entries) {
        const auto &snapshot=entry.snapshot;
        items.append(QJsonObject{{"id",entry.id},{"name",entry.name},
            {"layout",QString::fromLatin1(snapshot.state.toBase64())},
            {"sha256",QString::fromLatin1(QCryptographicHash::hash(snapshot.state,QCryptographicHash::Sha256).toHex())},
            {"compactDrawing",snapshot.compactDrawing},{"drawerOpen",snapshot.drawerOpen},
            {"drawerPage",snapshot.drawerPage},{"inspectorPage",snapshot.inspectorPage}});
    }
    return QJsonDocument(QJsonObject{{"format","toon-pro-workspaces"},{"version",1},{"stateVersion",3},{"workspaces",items}}).toJson(QJsonDocument::Compact);
}
bool decode(const QByteArray &bytes,QVector<WorkspaceEntry> *output) {
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes,&parse);const auto root=document.object();
    if(parse.error!=QJsonParseError::NoError || !document.isObject()
        || !fields(root,{"format","version","stateVersion","workspaces"})
        || root["format"]!="toon-pro-workspaces" || root["version"]!=1 || root["stateVersion"]!=3
        || !root["workspaces"].isArray() || root["workspaces"].toArray().size()>maxSavedWorkspaces)return false;
    QVector<WorkspaceEntry> entries;QSet<QString> ids,names;
    for(const auto &item:root["workspaces"].toArray()) {
        const auto object=item.toObject();
        if(!item.isObject() || !fields(object,{"id","name","layout","sha256","compactDrawing","drawerOpen","drawerPage","inspectorPage"})
            || !object["id"].isString() || !object["name"].isString() || !object["layout"].isString()
            || !object["sha256"].isString() || !object["compactDrawing"].isBool() || !object["drawerOpen"].isBool()
            || !object["drawerPage"].isDouble() || !object["inspectorPage"].isString())return false;
        WorkspaceEntry entry;entry.id=object["id"].toString();entry.name=object["name"].toString();
        if(QUuid(entry.id).isNull() || QUuid(entry.id).toString(QUuid::WithoutBraces)!=entry.id
            || ids.contains(entry.id) || !validName(entry.name) || cleanName(entry.name)!=entry.name
            || reservedName(entry.name) || names.contains(nameKey(entry.name)))return false;
        const auto encoded=object["layout"].toString().toLatin1();
        if(encoded.size()>((maxWorkspaceStateBytes+2)/3)*4)return false;
        auto &snapshot=entry.snapshot;snapshot.state=QByteArray::fromBase64(encoded);
        if(QString::fromLatin1(snapshot.state.toBase64())!=object["layout"].toString()
            || QString::fromLatin1(QCryptographicHash::hash(snapshot.state,QCryptographicHash::Sha256).toHex())!=object["sha256"].toString())return false;
        snapshot.compactDrawing=object["compactDrawing"].toBool();snapshot.drawerOpen=object["drawerOpen"].toBool();
        const auto page=object["drawerPage"].toDouble();if(page<0 || page>2 || page!=int(page))return false;
        snapshot.drawerPage=int(page);snapshot.inspectorPage=object["inspectorPage"].toString();
        if(!validateWorkspaceSnapshot(snapshot).isEmpty())return false;
        ids.insert(entry.id);names.insert(nameKey(entry.name));entries.append(std::move(entry));
    }
    *output=std::move(entries);return true;
}
}
QString validateWorkspaceSnapshot(const WorkspaceSnapshot &snapshot) {
    if(snapshot.state.size()<9 || snapshot.state.size()>maxWorkspaceStateBytes)
        return "The workspace layout is empty or exceeds the 2 MiB limit.";
    QDataStream stream(snapshot.state);qint32 marker=0,version=0;stream>>marker>>version;
    if(stream.status()!=QDataStream::Ok || marker!=0xff || version!=3)
        return "This workspace uses an unsupported panel layout.";
    if(snapshot.drawerPage<0 || snapshot.drawerPage>2)return "The drawing drawer page is invalid.";
    static const QSet<QString> inspectorPages{"","Drawing","Animation","Audio","Camera","Symbol","Symbols","Cues"};
    if(!inspectorPages.contains(snapshot.inspectorPage))return "The frame-properties category is invalid.";
    return {};
}
WorkspaceLibrary::WorkspaceLibrary(QSettings *settings,QObject *parent):QObject(parent),m_settings(settings) {}
QString WorkspaceLibrary::settingsKey(){return "workspaceLibrary/v1";}
bool WorkspaceLibrary::block(const QString &message,QString *error){m_writable=false;m_error=message;emit changed();return fail(message,error);}
const WorkspaceEntry *WorkspaceLibrary::find(const QString &id) const {
    for(const auto &entry:m_entries)if(entry.id==id)return &entry;return nullptr;
}
bool WorkspaceLibrary::load(QString *error) {
    m_writable=false;m_entries.clear();m_loadedBytes.clear();m_error.clear();m_existed=false;if(error)error->clear();
    if(!m_settings)return block("Workspace settings are no longer available.",error);
    m_settings->sync();
    if(m_settings->status()!=QSettings::NoError)return block("Workspace settings could not be read. Saved layouts were preserved.",error);
    m_existed=m_settings->contains(settingsKey());
    if(!m_existed){m_writable=true;emit changed();return true;}
    const auto stored=m_settings->value(settingsKey());
    if(stored.metaType()!=QMetaType::fromType<QByteArray>())return block("The workspace library is damaged or unsupported. Saved layouts were preserved.",error);
    const auto bytes=stored.toByteArray();
    if(bytes.size()>maxWorkspaceLibraryBytes)return block("The workspace library exceeds the 16 MiB limit. Saved layouts were preserved.",error);
    if(!decode(bytes,&m_entries))return block("The workspace library is damaged or uses an unsupported version. Saved layouts were preserved.",error);
    m_loadedBytes=bytes;m_writable=true;emit changed();return true;
}
QString WorkspaceLibrary::nameError(const QString &name,const QString &exceptId) const {
    if(!validName(name))return "Give the workspace a name of 1 to 100 characters on one line.";
    if(reservedName(name))return "That name belongs to a built-in workspace. Choose another name.";
    for(const auto &entry:m_entries)if(entry.id!=exceptId && nameKey(entry.name)==nameKey(name))
        return "A saved workspace already has that name.";
    return {};
}
bool WorkspaceLibrary::commit(const QVector<WorkspaceEntry> &entries,QString *error) {
    if(error)error->clear();
    if(!m_writable)return fail(m_error.isEmpty()?QString("Load the workspace library before saving."):m_error,error);
    if(!m_settings)return block("Workspace settings are no longer available.",error);
    const auto data=serialize(entries);
    if(data.size()>maxWorkspaceLibraryBytes)return fail("The workspace library exceeds the 16 MiB limit.",error);
    m_settings->sync();
    if(m_settings->status()!=QSettings::NoError)return block("Workspace settings could not be saved. The previous library was preserved.",error);
    const auto existing=m_settings->value(settingsKey());
    if(m_settings->contains(settingsKey())!=m_existed || (m_existed &&
        (existing.metaType()!=QMetaType::fromType<QByteArray>() || existing.toByteArray()!=m_loadedBytes)))
        return block("Saved workspaces changed in another window. Reload the library before saving; the newer layouts were preserved.",error);
    if(entries==m_entries)return true;
    m_settings->setValue(settingsKey(),data);m_settings->sync();
    if(m_settings->status()!=QSettings::NoError) {
        // QSettings uses an atomic file write. Restore its in-memory value as
        // well so a later unrelated settings sync cannot persist this failed edit.
        if(m_existed)m_settings->setValue(settingsKey(),m_loadedBytes);else m_settings->remove(settingsKey());
        m_settings->sync();
        return block("Workspace settings could not be saved. The previous library was preserved.",error);
    }
    m_entries=entries;m_loadedBytes=data;m_existed=true;m_error.clear();emit changed();return true;
}
bool WorkspaceLibrary::add(const QString &name,const WorkspaceSnapshot &snapshot,QString *id,QString *error) {
    const auto requestedName=cleanName(name);if(id)id->clear();const auto invalid=nameError(requestedName);if(!invalid.isEmpty())return fail(invalid,error);
    const auto validation=validateWorkspaceSnapshot(snapshot);if(!validation.isEmpty())return fail(validation,error);
    if(m_entries.size()>=maxSavedWorkspaces)return fail("You can save up to 64 workspaces.",error);
    auto entries=m_entries;const QString created=QUuid::createUuid().toString(QUuid::WithoutBraces);
    entries.append({created,requestedName,snapshot});if(!commit(entries,error))return false;if(id)*id=created;return true;
}
bool WorkspaceLibrary::update(const QString &id,const WorkspaceSnapshot &snapshot,QString *error) {
    const auto validation=validateWorkspaceSnapshot(snapshot);if(!validation.isEmpty())return fail(validation,error);
    auto entries=m_entries;for(auto &entry:entries)if(entry.id==id){entry.snapshot=snapshot;return commit(entries,error);}
    return fail("Choose a saved workspace to update. Built-in workspaces cannot be overwritten.",error);
}
bool WorkspaceLibrary::rename(const QString &id,const QString &name,QString *error) {
    const auto invalid=nameError(name,id);if(!invalid.isEmpty())return fail(invalid,error);
    auto entries=m_entries;for(auto &entry:entries)if(entry.id==id){entry.name=cleanName(name);return commit(entries,error);}
    return fail("Choose a saved workspace to rename. Built-in workspaces cannot be renamed.",error);
}
bool WorkspaceLibrary::remove(const QString &id,QString *error) {
    auto entries=m_entries;if(!entries.removeIf([&](const auto &entry){return entry.id==id;}))
        return fail("Choose a saved workspace to remove. Built-in workspaces cannot be removed.",error);
    return commit(entries,error);
}
} // namespace toon
