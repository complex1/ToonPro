#include "managed_project_store.h"
#include "project_templates.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMutexLocker>
#include <QPainter>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>

namespace toon {
namespace {
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool fail(QString *e, const QString &s) {
    if (e)
        *e = s;
    return false;
}
bool textValid(const QString &s) {
    return !s.trimmed().isEmpty() && s.size() <= 200 &&
           std::none_of(s.begin(), s.end(), [](QChar c) { return c.category() == QChar::Other_Control; });
}
bool collectionValid(const QString &s) { return s == "content" || s == "assets"; }
bool tagsValid(const QStringList &tags, QString *e) {
    if (tags.size() > 32)
        return fail(e, "Use at most 32 tags.");
    for (const auto &s : tags)
        if (!textValid(s) || s.size() > 80)
            return fail(e, "Tags must contain 1–80 readable characters.");
    return true;
}
QString encodedTags(const QStringList &tags) {
    QStringList result;
    for (const auto &raw : tags) {
        auto s = raw.trimmed();
        if (!result.contains(s, Qt::CaseInsensitive))
            result.append(s);
    }
    return QString::fromUtf8(
        QJsonDocument(QJsonArray::fromStringList(result)).toJson(QJsonDocument::Compact));
}
QStringList decodedTags(const QString &s) {
    QStringList result;
    for (const auto &v : QJsonDocument::fromJson(s.toUtf8()).array())
        result.append(v.toString());
    return result;
}
class Db {
  public:
    QSqlDatabase db;
    QString key = uuid();
    Db(const QString &root, QString *error, bool allowCreate = false) {
        const QFileInfo info(QDir(root).filePath("project.sqlite"));
        if (QFileInfo(root).isSymLink() || info.isSymLink() || (!allowCreate && !info.isFile())) {
            fail(error, "The project catalog is missing or unsafe; reconnect the project disk.");
            return;
        }
        db = QSqlDatabase::addDatabase("QSQLITE", key);
        db.setDatabaseName(QDir(root).filePath("project.sqlite"));
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
        if (!db.open()) {
            fail(error, db.lastError().text());
            return;
        }
        QSqlQuery q(db);
        q.exec("PRAGMA foreign_keys=ON");
        q.exec("PRAGMA synchronous=EXTRA");
    }
    ~Db() {
        if (!db.isValid())
            return;
        db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(key);
    }
};
bool sql(QSqlDatabase &db, const QString &statement, const QVariantList &args, QString *e) {
    QSqlQuery q(db);
    if (!q.prepare(statement))
        return fail(e, q.lastError().text());
    for (const auto &v : args)
        q.addBindValue(v.metaType().id() == QMetaType::QString && v.toString().isNull()
                           ? QVariant(QStringLiteral(""))
                           : v);
    if (!q.exec())
        return fail(e, q.lastError().text());
    return true;
}
bool safe(const QString &root, const QString &relative, QString *out, QString *e) {
    if (relative.isEmpty() || QDir::isAbsolutePath(relative) || relative.contains('\\') ||
        relative.contains(':'))
        return fail(e, "Invalid project-owned path.");
    QString p = root;
    for (const auto &part : relative.split('/')) {
        if (part.isEmpty() || part == "." || part == "..")
            return fail(e, "A project path leaves its folder.");
        p = QDir(p).filePath(part);
        if (QFileInfo(p).isSymLink())
            return fail(e, "Project-owned symbolic links are not supported.");
    }
    if (out)
        *out = p;
    return true;
}
bool cancelled(const std::function<bool()> &c, QString *e) {
    if (!c || !c())
        return false;
    fail(e, "Operation cancelled.");
    return true;
}
bool write(const QString &path, const QByteArray &bytes, QString *e) {
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
        return fail(e, f.errorString());
    return true;
}
QByteArray digest(const QString &path, QString *e) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        fail(e, f.errorString());
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) {
        fail(e, "Could not checksum file.");
        return {};
    }
    return hash.result().toHex();
}
bool folderValid(QSqlDatabase &db, const QString &id, const QString &collection, QString *e) {
    if (id.isEmpty())
        return true;
    QSqlQuery q(db);
    q.prepare("SELECT collection FROM folders WHERE id=?");
    q.addBindValue(id);
    if (!q.exec() || !q.next() || q.value(0).toString() != collection)
        return fail(e, "Choose a folder in this collection.");
    return true;
}
QVector<ManagedItem> readItems(QSqlDatabase &db, QString *e) {
    QVector<ManagedItem> result;
    QSqlQuery q(db);
    if (!q.exec("SELECT "
                "i.id,i.name,i.kind,i.collection,i.folder,i.revision,i.recovery,i.tags,i.removed,COALESCE(r."
                "path,i.path),COALESCE(r.bytes,i.bytes) FROM items i LEFT JOIN revisions r ON "
                "r.id=i.revision ORDER BY i.rowid")) {
        fail(e, q.lastError().text());
        return result;
    }
    while (q.next()) {
        ManagedItem i;
        i.id = q.value(0).toString();
        i.name = q.value(1).toString();
        i.kind = q.value(2).toString();
        i.collection = q.value(3).toString();
        i.folder = q.value(4).toString();
        i.revision = q.value(5).toString();
        i.recovery = q.value(6).toString();
        i.tags = decodedTags(q.value(7).toString());
        i.removed = q.value(8).toBool();
        i.path = q.value(9).toString();
        i.bytes = q.value(10).toLongLong();
        result.append(i);
    }
    return result;
}
QJsonObject meta(const ManagedItem &i) {
    return {{"id", i.id},
            {"name", i.name},
            {"collection", i.collection},
            {"folder", i.folder},
            {"tags", QJsonArray::fromStringList(i.tags)},
            {"removed", i.removed}};
}
bool log(QSqlDatabase &db, const QJsonArray &before, const QJsonArray &after, QString *e) {
    return sql(db, "INSERT INTO operations(before_state,after_state) VALUES(?,?)",
               {QString::fromUtf8(QJsonDocument(before).toJson(QJsonDocument::Compact)),
                QString::fromUtf8(QJsonDocument(after).toJson(QJsonDocument::Compact))},
               e);
}
bool applyMeta(QSqlDatabase &db, const QJsonObject &o, QString *e) {
    return sql(db, "UPDATE items SET name=?,collection=?,folder=?,tags=?,removed=? WHERE id=?",
               {o["name"].toString(), o["collection"].toString(), o["folder"].toString(),
                encodedTags(decodedTags(QString::fromUtf8(QJsonDocument(o["tags"].toArray()).toJson()))),
                o["removed"].toBool(), o["id"].toString()},
               e);
}
bool copyFile(const QString &source, const QString &dest, QString *e,
              const std::function<bool()> &cancel = {}) {
    QFile in(source);
    QSaveFile out(dest);
    out.setDirectWriteFallback(false);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly))
        return fail(e, "Cannot read source or write destination.");
    while (!in.atEnd()) {
        if (cancelled(cancel, e))
            return false;
        const auto chunk = in.read(1024 * 1024);
        if (in.error() != QFile::NoError || out.write(chunk) != chunk.size())
            return fail(e, "Could not complete file copy.");
    }
    if (cancelled(cancel, e))
        return false;
    if (!out.commit())
        return fail(e, out.errorString());
    return true;
}
void pruneRevisions(QSqlDatabase &db, const QString &root, const QString &id) {
    // Retain the explicit save, previous explicit save and latest recovery. This
    // runs only after publication and under the same lease as backup/import.
    const QString where = "item_id=? AND id NOT IN (SELECT revision FROM items WHERE id=? UNION SELECT "
                          "previous FROM items WHERE id=? UNION SELECT recovery FROM items WHERE id=?)";
    QSqlQuery q(db);
    q.prepare("SELECT path FROM revisions WHERE " + where);
    for (int i = 0; i < 4; ++i)
        q.addBindValue(id);
    if (!q.exec())
        return;
    QStringList obsolete;
    while (q.next())
        obsolete.append(q.value(0).toString());
    q.finish();
    if (!sql(db, "DELETE FROM revisions WHERE " + where, {id, id, id, id}, nullptr))
        return;
    for (const auto &relative : obsolete) {
        QString path;
        if (safe(root, relative, &path, nullptr))
            QFile::remove(path);
    }
}
QString documentError(const Project &p) {
    if (p.shots.size() != 1)
        return "An editor document must contain exactly one shot.";
    return p.validate();
}
QString publishRevision(QSqlDatabase &db, const QString &root, const QString &id, const Project &p,
                        QString *e, const std::function<bool()> &cancel) {
    const auto err = documentError(p);
    if (!err.isEmpty()) {
        fail(e, err);
        return {};
    }
    if (cancelled(cancel, e))
        return {};
    const QString revision = uuid(), relative = "documents/" + id + "/revisions/" + revision + ".toonproj";
    QString path;
    if (!safe(root, relative, &path, e) || !QDir().mkpath(QFileInfo(path).absolutePath()))
        return {};
    if (!saveProject(p, path, e, cancel))
        return {};
    const auto hash = digest(path, e);
    if (hash.isEmpty() || cancelled(cancel, e)) {
        QFile::remove(path);
        return {};
    }
    if (!sql(db, "INSERT INTO revisions(id,item_id,path,hash,bytes) VALUES(?,?,?,?,?)",
             {revision, id, relative, QString::fromLatin1(hash), QFileInfo(path).size()}, e)) {
        QFile::remove(path);
        return {};
    }
    return revision;
}
} // namespace
ManagedProjectStore::~ManagedProjectStore() = default;
std::shared_ptr<ManagedProjectStore> ManagedProjectStore::create(const QString &root, const QString &name,
                                                                 const QString &type, QString *error) {
    if (error)
        error->clear();
    if (!textValid(name) || !QStringList{"drawing", "animation", "movie"}.contains(type)) {
        fail(error, "Choose a name and Drawing, Animation or Movie project type.");
        return {};
    }
    QFileInfo target(root);
    if (target.exists() || !target.absoluteDir().exists()) {
        fail(error, "Choose a new project folder in an existing directory.");
        return {};
    }
    QTemporaryDir stage(target.absoluteDir().filePath(".toon-project-XXXXXX"));
    if (!stage.isValid()) {
        fail(error, "Cannot create project staging folder.");
        return {};
    }
    {
        Db connection(stage.path(), error, true);
        auto &db = connection.db;
        if (!db.isOpen())
            return {};
        const QStringList statements{
            "PRAGMA journal_mode=DELETE",
            "CREATE TABLE project(id TEXT PRIMARY KEY,title TEXT NOT NULL,type TEXT NOT NULL)",
            "CREATE TABLE folders(id TEXT PRIMARY KEY,parent TEXT NOT NULL,name TEXT NOT NULL,collection "
            "TEXT NOT NULL,UNIQUE(parent,name,collection))",
            "CREATE TABLE items(id TEXT PRIMARY KEY,name TEXT NOT NULL,kind TEXT NOT NULL,collection TEXT "
            "NOT NULL DEFAULT 'content',folder TEXT NOT NULL DEFAULT '',revision TEXT NOT NULL DEFAULT "
            "'',previous TEXT NOT NULL DEFAULT '',recovery TEXT NOT NULL DEFAULT '',tags TEXT NOT NULL "
            "DEFAULT '[]',removed INTEGER NOT NULL "
            "DEFAULT 0,path TEXT NOT NULL DEFAULT '',bytes INTEGER NOT NULL DEFAULT 0)",
            "CREATE TABLE revisions(id TEXT PRIMARY KEY,item_id TEXT NOT NULL REFERENCES items(id),path TEXT "
            "NOT NULL UNIQUE,hash TEXT NOT NULL,bytes INTEGER NOT NULL)",
            "CREATE TABLE operations(id INTEGER PRIMARY KEY,before_state TEXT NOT NULL,after_state TEXT NOT "
            "NULL)",
            "CREATE INDEX item_location ON items(collection,folder,removed)",
            "PRAGMA user_version=2"};
        for (const auto &statement : statements)
            if (!sql(db, statement, {}, error))
                return {};
        const auto id = uuid();
        if (!sql(db, "INSERT INTO project VALUES(?,?,?)", {id, name.trimmed(), type}, error))
            return {};
        if (!write(QDir(stage.path()).filePath("project.toonstudio"),
                   QJsonDocument(QJsonObject{{"format", "toon-pro-managed"}, {"version", 2}, {"id", id}})
                       .toJson(),
                   error))
            return {};
    }
    if (!QDir().rename(stage.path(), target.absoluteFilePath())) {
        fail(error, "Cannot publish project directory.");
        return {};
    }
    stage.setAutoRemove(false);
    return open(root, error);
}
std::shared_ptr<ManagedProjectStore> ManagedProjectStore::open(const QString &requested, QString *error) {
    if (error)
        error->clear();
    QFileInfo info(requested);
    if (!info.isDir() && info.fileName() != "project.toonstudio") {
        fail(error, "Choose a managed project folder or its project.toonstudio marker.");
        return {};
    }
    const auto root = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (info.isSymLink() || !QFileInfo(root).isDir() || QFileInfo(root).isSymLink()) {
        fail(error, "Choose an available project folder.");
        return {};
    }
    QString marker, dbPath;
    if (!safe(root, "project.toonstudio", &marker, error) || !safe(root, "project.sqlite", &dbPath, error))
        return {};
    QFile file(marker);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
        fail(error, "Project marker is unavailable.");
        return {};
    }
    const auto m = QJsonDocument::fromJson(file.readAll()).object();
    if (m["format"] != "toon-pro-managed" || m["version"].toInt() != 2 || !QFileInfo(dbPath).isFile()) {
        fail(error, "This is not a supported new-format Toon Pro project.");
        return {};
    }
    auto store = std::shared_ptr<ManagedProjectStore>(new ManagedProjectStore);
    store->m_root = QFileInfo(root).canonicalFilePath();
    store->m_lock = std::make_unique<QLockFile>(QDir(root).filePath("project.lock"));
    store->m_lock->setStaleLockTime(0);
    if (!store->m_lock->tryLock(0)) {
        fail(error, "This project is open in another window. Close it there first.");
        return {};
    }
    Db connection(root, error);
    QSqlQuery q(connection.db);
    if (!q.exec("SELECT id,title,type FROM project") || !q.next() ||
        q.value(0).toString() != m["id"].toString()) {
        fail(error, "The project catalog is invalid.");
        return {};
    }
    store->m_title = q.value(1).toString();
    store->m_type = q.value(2).toString();
    QSqlQuery version(connection.db);
    if (!version.exec("PRAGMA user_version") || !version.next() || version.value(0).toInt() != 2 ||
        !QStringList{"drawing", "animation", "movie"}.contains(store->m_type)) {
        fail(error, "Unsupported project catalog schema.");
        return {};
    }
    return store;
}
QVector<ManagedItem> ManagedProjectStore::items(QString *e) const {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    return readItems(d.db, e);
}
QVector<ManagedFolder> ManagedProjectStore::folders(QString *e) const {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    QSqlQuery q(d.db);
    QVector<ManagedFolder> r;
    if (!q.exec("SELECT id,parent,name,collection FROM folders ORDER BY rowid")) {
        fail(e, q.lastError().text());
        return r;
    }
    while (q.next())
        r.append(
            {q.value(0).toString(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString()});
    return r;
}
QString ManagedProjectStore::createFolder(const QString &name, const QString &parent,
                                          const QString &collection, QString *e) {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    if (!textValid(name) || !collectionValid(collection) || !folderValid(d.db, parent, collection, e)) {
        fail(e, "Choose a valid folder name and destination.");
        return {};
    }
    const auto id = uuid();
    if (!sql(d.db, "INSERT INTO folders VALUES(?,?,?,?)", {id, parent, name.trimmed(), collection}, e))
        return {};
    return id;
}
QString ManagedProjectStore::createDocument(const QString &name, const QString &kind, const Project &p,
                                            const QString &folder, const QStringList &tags, QString *e,
                                            const std::function<bool()> &cancel) {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    auto &db = d.db;
    if (!textValid(name) || !QStringList{"drawing", "animation"}.contains(kind) || !tagsValid(tags, e) ||
        !folderValid(db, folder, "content", e))
        return {};
    if ((m_type == "drawing" && kind != "drawing") || (m_type == "animation" && kind != "animation")) {
        fail(e, "The document type does not match this project.");
        return {};
    }
    if (!db.transaction()) {
        fail(e, db.lastError().text());
        return {};
    }
    const auto id = uuid();
    if (!sql(db, "INSERT INTO items(id,name,kind,folder,tags) VALUES(?,?,?,?,?)",
             {id, name.trimmed(), kind, folder, encodedTags(tags)}, e)) {
        db.rollback();
        return {};
    }
    const auto revision = publishRevision(db, m_root, id, p, e, cancel);
    if (revision.isEmpty() || cancelled(cancel, e) ||
        !sql(db, "UPDATE items SET revision=? WHERE id=?", {revision, id}, e) || !db.commit()) {
        db.rollback();
        if (e && e->isEmpty())
            *e = db.lastError().text();
        return {};
    }
    return id;
}
std::optional<Project> ManagedProjectStore::loadDocument(const QString &id, bool recovery, QString *e) const {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    QSqlQuery q(d.db);
    q.prepare(recovery
                  ? "SELECT r.path,r.hash FROM items i JOIN revisions r ON r.id=i.recovery WHERE i.id=?"
                  : "SELECT r.path,r.hash FROM items i JOIN revisions r ON r.id=i.revision WHERE i.id=?");
    q.addBindValue(id);
    if (!q.exec() || !q.next()) {
        fail(e, "This item has no native document revision.");
        return {};
    }
    QString path;
    if (!safe(m_root, q.value(0).toString(), &path, e) ||
        digest(path, e) != q.value(1).toString().toLatin1()) {
        fail(e, "Document is missing or its checksum changed.");
        return {};
    }
    auto p = loadProject(path, e);
    if (p && !documentError(*p).isEmpty()) {
        fail(e, documentError(*p));
        return {};
    }
    return p;
}
QString ManagedProjectStore::saveDocument(const QString &id, const QString &expected, const Project &p,
                                          bool recovery, QString *e, const std::function<bool()> &cancel) {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    auto &db = d.db;
    QSqlQuery q(db);
    q.prepare("SELECT revision FROM items WHERE id=? AND kind IN ('drawing','animation')");
    q.addBindValue(id);
    if (!q.exec() || !q.next() || q.value(0).toString() != expected) {
        fail(e, "The document revision changed; this save cannot replace it.");
        return {};
    }
    if (!db.transaction()) {
        fail(e, db.lastError().text());
        return {};
    }
    const auto revision = publishRevision(db, m_root, id, p, e, cancel);
    if (revision.isEmpty()) {
        db.rollback();
        return {};
    }
    const auto statement =
        recovery ? "UPDATE items SET recovery=? WHERE id=? AND revision=?"
                 : "UPDATE items SET previous=revision,revision=?,recovery='' WHERE id=? AND revision=?";
    if (cancelled(cancel, e) || !sql(db, statement, {revision, id, expected}, e) || !db.commit()) {
        db.rollback();
        if (e && e->isEmpty())
            *e = db.lastError().text();
        return {};
    }
    pruneRevisions(db, m_root, id);
    return revision;
}
bool ManagedProjectStore::discardRecovery(const QString &id, QString *e) {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    if (!sql(d.db, "UPDATE items SET recovery='' WHERE id=?", {id}, e))
        return false;
    pruneRevisions(d.db, m_root, id);
    return true;
}
bool ManagedProjectStore::importFiles(const QStringList &paths, const QString &folder,
                                      const QStringList &tags, QString *e,
                                      const std::function<bool()> &cancel,
                                      const std::function<void(int, int)> &progress) {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    auto &db = d.db;
    if (paths.isEmpty() || !tagsValid(tags, e) || !folderValid(db, folder, "assets", e))
        return false;
    struct Prepared {
        QString id, name, kind, path;
        qint64 bytes;
    };
    QVector<Prepared> prepared;
    const auto cleanup = [&] {
        for (const auto &p : prepared)
            QFile::remove(QDir(m_root).filePath(p.path));
    };
    for (const auto &source : paths) {
        if (cancelled(cancel, e)) {
            cleanup();
            return false;
        }
        QFileInfo info(source);
        if (!info.isFile() || info.isSymLink() || !textValid(info.fileName())) {
            cleanup();
            return fail(e, "Choose readable regular media files.");
        }
        const auto ext = info.suffix().toLower();
        QString kind;
        if (QStringList{"png", "jpg", "jpeg", "webp", "bmp", "tif", "tiff", "gif"}.contains(ext))
            kind = "image";
        else if (QStringList{"mp4", "mov", "mkv", "webm", "avi"}.contains(ext))
            kind = "video";
        else if (QStringList{"wav", "mp3", "m4a", "ogg", "flac", "aac"}.contains(ext))
            kind = "audio";
        else {
            cleanup();
            return fail(e, "Choose an image, video or audio file.");
        }
        const auto id = uuid(), relative = "media/" + id + "/original." + ext;
        QString destination;
        if (!safe(m_root, relative, &destination, e) ||
            !QDir().mkpath(QFileInfo(destination).absolutePath()) ||
            !copyFile(source, destination, e, cancel)) {
            cleanup();
            return false;
        }
        prepared.append({id, info.fileName(), kind, relative, QFileInfo(destination).size()});
        if (progress)
            progress(prepared.size(), paths.size());
    }
    if (cancelled(cancel, e) || !db.transaction()) {
        cleanup();
        return false;
    }
    for (const auto &p : prepared)
        if (!sql(db,
                 "INSERT INTO items(id,name,kind,collection,folder,tags,path,bytes) "
                 "VALUES(?,?,?,'assets',?,?,?,?)",
                 {p.id, p.name, p.kind, folder, encodedTags(tags), p.path, p.bytes}, e)) {
            db.rollback();
            cleanup();
            return false;
        }
    if (cancelled(cancel, e) || !db.commit()) {
        db.rollback();
        cleanup();
        return false;
    }
    return true;
}
bool ManagedProjectStore::organize(const QStringList &ids, const QString &collection, const QString &folder,
                                   const QStringList &tags, QString *e, bool mergeTags) {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    auto &db = d.db;
    if (!collectionValid(collection) || !tagsValid(tags, e) || !folderValid(db, folder, collection, e))
        return false;
    QJsonArray before, after;
    const auto all = readItems(db, e);
    for (const auto &id : ids) {
        auto it = std::find_if(all.begin(), all.end(), [&](const auto &i) { return i.id == id; });
        if (it == all.end() || it->removed)
            return fail(e, "An item is unavailable.");
        if (collection == "content" && ((m_type == "drawing" && it->kind != "drawing") ||
                                        (m_type == "animation" && it->kind != "animation")))
            return fail(e, "This item does not match the project's content type.");
        before.append(meta(*it));
        auto changed = meta(*it);
        changed["collection"] = collection;
        changed["folder"] = folder;
        changed["tags"] =
            QJsonArray::fromStringList(decodedTags(encodedTags(mergeTags ? it->tags + tags : tags)));
        after.append(changed);
    }
    if (ids.isEmpty() || !db.transaction())
        return false;
    for (const auto &o : after)
        if (!applyMeta(db, o.toObject(), e)) {
            db.rollback();
            return false;
        }
    if (!log(db, before, after, e) || !db.commit()) {
        db.rollback();
        return false;
    }
    return true;
}
bool ManagedProjectStore::rename(const QString &id, const QString &name, QString *e) {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    if (!textValid(name))
        return fail(e, "Enter a readable name.");
    for (const auto &i : readItems(d.db, e))
        if (i.id == id) {
            auto a = meta(i), b = a;
            b["name"] = name.trimmed();
            if (!d.db.transaction())
                return false;
            if (!applyMeta(d.db, b, e) || !log(d.db, {a}, {b}, e) || !d.db.commit()) {
                d.db.rollback();
                return false;
            }
            return true;
        }
    return fail(e, "Item not found.");
}
bool ManagedProjectStore::setRemoved(const QStringList &ids, bool removed, QString *e) {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    QJsonArray before, after;
    const auto all = readItems(d.db, e);
    for (const auto &id : ids) {
        auto it = std::find_if(all.begin(), all.end(), [&](const auto &i) { return i.id == id; });
        if (it == all.end())
            return fail(e, "Item not found.");
        before.append(meta(*it));
        auto b = meta(*it);
        b["removed"] = removed;
        after.append(b);
    }
    if (ids.isEmpty() || !d.db.transaction())
        return false;
    for (const auto &o : after)
        if (!applyMeta(d.db, o.toObject(), e)) {
            d.db.rollback();
            return false;
        }
    if (!log(d.db, before, after, e) || !d.db.commit()) {
        d.db.rollback();
        return false;
    }
    return true;
}
bool ManagedProjectStore::undoOrganization(QString *e) {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    QSqlQuery q(d.db);
    if (!q.exec("SELECT id,before_state,after_state FROM operations ORDER BY id DESC LIMIT 1") || !q.next())
        return fail(e, "No organization change to undo.");
    auto id = q.value(0);
    const auto before = QJsonDocument::fromJson(q.value(1).toString().toUtf8()).array(),
               after = QJsonDocument::fromJson(q.value(2).toString().toUtf8()).array();
    const auto all = readItems(d.db, e);
    for (const auto &o : after) {
        const auto obj = o.toObject();
        auto it =
            std::find_if(all.begin(), all.end(), [&](const auto &i) { return i.id == obj["id"].toString(); });
        if (it == all.end() || meta(*it) != obj)
            return fail(e, "The item changed; organization undo is unavailable.");
    }
    if (!d.db.transaction())
        return false;
    for (const auto &o : before)
        if (!applyMeta(d.db, o.toObject(), e)) {
            d.db.rollback();
            return false;
        }
    if (!sql(d.db, "DELETE FROM operations WHERE id=?", {id}, e) || !d.db.commit()) {
        d.db.rollback();
        return false;
    }
    return true;
}
QString ManagedProjectStore::assetPath(const QString &id, QString *e) const {
    QMutexLocker lock(&m_mutex);
    Db d(m_root, e);
    for (const auto &i : readItems(d.db, e))
        if (i.id == id) {
            QString p;
            if (safe(m_root, i.path, &p, e) && QFileInfo(p).isFile())
                return p;
        }
    fail(e, "The asset is missing.");
    return {};
}
bool ManagedProjectStore::backup(const QString &destination, QString *e,
                                 const std::function<bool()> &cancel) const {
    QMutexLocker lock(&m_mutex);
    QFileInfo target(destination);
    if (target.exists() || !target.absoluteDir().exists() ||
        target.absoluteFilePath().startsWith(m_root + "/"))
        return fail(e, "Choose a new backup folder outside this project.");
    QTemporaryDir stage(target.absoluteDir().filePath(".toon-backup-XXXXXX"));
    if (!stage.isValid())
        return fail(e, "Cannot prepare backup.");
    Db d(m_root, e);
    QSqlQuery q(d.db);
    q.prepare("VACUUM INTO ?");
    q.addBindValue(QDir(stage.path()).filePath("project.sqlite"));
    if (!q.exec())
        return fail(e, q.lastError().text());
    QStringList files{"project.toonstudio"};
    QSqlQuery r(d.db);
    if (!r.exec("SELECT path FROM revisions UNION SELECT path FROM items WHERE path<>''"))
        return fail(e, r.lastError().text());
    while (r.next())
        files.append(r.value(0).toString());
    for (const auto &relative : files) {
        QString source, dest;
        if (cancelled(cancel, e) || !safe(m_root, relative, &source, e) ||
            !safe(stage.path(), relative, &dest, e) || !QDir().mkpath(QFileInfo(dest).absolutePath()) ||
            !copyFile(source, dest, e, cancel) || digest(source, e) != digest(dest, e))
            return false;
    }
    if (cancelled(cancel, e) || !QDir().rename(stage.path(), target.absoluteFilePath()))
        return fail(e, "Backup cancelled or could not publish.");
    stage.setAutoRemove(false);
    return true;
}
Project newManagedDocument(const QString &name, bool animation, int width, int height,
                           const QColor &background, bool transparent, double fps, int frames) {
    auto p = *projectFromStarterTemplate("blank", ProjectKind::DrawingOnly);
    p.movie = name;
    p.width = width;
    p.height = height;
    p.transparent = transparent;
    p.fps = fps;
    auto &s = p.shots[0];
    s.name = name;
    s.number = "D001";
    if (!transparent && background != QColor(Qt::white)) {
        auto layer = RasterLayer::create("Background");
        for (int y = 0; y < height; y += tileSize)
            for (int x = 0; x < width; x += tileSize) {
                QImage image(tileSize, tileSize, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
                QPainter painter(&image);
                painter.fillRect(0, 0, qMin(tileSize, width - x), qMin(tileSize, height - y), background);
                painter.end();
                layer.tiles.insert({x / tileSize, y / tileSize}, image);
            }
        layer.locked = true;
        s.layers.prepend(layer);
    }
    if (animation) {
        p.kind = ProjectKind::Film;
        s.durationFrames = frames;
        s.number = "A001";
        auto root = RasterLayer::create("Animation");
        root.group = true;
        s.layers.append(root);
        AnimationTrack track;
        track.id = uuid();
        track.rootGroupLayerId = root.id;
        track.drawingTemplate = drawingTemplateSlots(DrawingTemplate::Simple);
        AnimationDrawing drawing;
        drawing.id = uuid();
        auto group = RasterLayer::create("Drawing 1");
        group.group = true;
        group.parentId = root.id;
        s.layers.append(group);
        auto paint = RasterLayer::create("Drawing");
        paint.parentId = group.id;
        s.layers.append(paint);
        drawing.groupLayerId = group.id;
        drawing.templateLayers.insert("drawing", paint.id);
        track.drawings.append(drawing);
        track.exposures.append({uuid(), drawing.id, 0, qMin(frames, 2)});
        s.animationTracks.append(track);
    }
    return p;
}
} // namespace toon
