#include "studio_project.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
constexpr qint64 maxCatalogBytes = 16LL * 1024 * 1024;
constexpr qint64 maxAssetBytes = 16LL * 1024 * 1024 * 1024 * 1024;
constexpr int maxAssets = 10000;
constexpr int maxFolders = 2048;
const QString markerName = QStringLiteral("project.toonstudio");
const QString catalogName = QStringLiteral("catalog.json");
const QString artworkName = QStringLiteral("project.toonproj");
const QStringList assetTypes{"image", "video", "audio", "effect", "model", "file"};

bool fail(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
bool cancelledNow(const std::function<bool()> &cancelled, QString *error) {
    if (!cancelled || !cancelled()) return false;
    fail(error, "Operation cancelled.");
    return true;
}
bool validText(const QString &value, int maximum, bool empty = false) {
    return (empty || !value.trimmed().isEmpty()) && value.size() <= maximum
        && std::none_of(value.cbegin(), value.cend(), [](QChar c) { return c.category() == QChar::Other_Control; });
}
bool validId(const QString &id) {
    return !QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id;
}
bool validFolder(const QString &folder) {
    if (!validText(folder, 240, true) || folder != folder.trimmed() || folder.contains('\\')
        || QDir::isAbsolutePath(folder) || folder.contains(':')) return false;
    if (folder.isEmpty()) return true;
    const auto parts = folder.split('/');
    return std::all_of(parts.cbegin(), parts.cend(), [](const QString &part) {
        return !part.isEmpty() && part != "." && part != ".." && part == part.trimmed();
    });
}
QStringList normalizedTags(const QStringList &tags, QString *error) {
    QStringList result;
    for (const auto &raw : tags) {
        const auto tag = raw.trimmed();
        if (tag.isEmpty()) continue;
        if (!validText(tag, 80) || tag.contains(',')) {
            fail(error, "Tags must contain 1–80 characters without commas or control characters.");
            return {};
        }
        if (!result.contains(tag, Qt::CaseInsensitive)) result.append(tag);
    }
    if (result.size() > 32) {
        fail(error, "Use at most 32 tags per asset.");
        return {};
    }
    return result;
}
bool validAsset(const StudioAsset &asset, QString *error) {
    static const QRegularExpression pathExpression("^assets/[0-9a-f-]{36}(?:\\.[a-z0-9]{1,16})?$");
    if (!validId(asset.id) || !validText(asset.name, 256) || !assetTypes.contains(asset.type)
        || !validFolder(asset.folder) || !pathExpression.match(asset.relativePath).hasMatch()
        || QFileInfo(asset.relativePath).completeBaseName() != asset.id
        || asset.bytes < 0 || asset.bytes > maxAssetBytes || asset.shotIds.size() > maxProjectShots)
        return fail(error, "Invalid project asset metadata.");
    QString tagError;
    const auto tags = normalizedTags(asset.tags, &tagError);
    if (!tagError.isEmpty() || tags != asset.tags) return fail(error, tagError.isEmpty() ? "Invalid asset tags." : tagError);
    QSet<QString> ids;
    for (const auto &id : asset.shotIds) {
        if (!validText(id, 100) || ids.contains(id)) return fail(error, "Invalid asset shot references.");
        ids.insert(id);
    }
    return true;
}

// Every package-owned component is checked before access. In particular a
// replaced assets directory or catalog symlink must never redirect writes.
bool safePath(const QString &root, const QString &relative, QString *result, QString *error,
              bool requireFile = false) {
    const QFileInfo rootInfo(root);
    if (!rootInfo.isDir() || rootInfo.isSymLink() || rootInfo.canonicalFilePath() != root)
        return fail(error, "The project folder is unavailable or has been replaced.");
    if (QDir::isAbsolutePath(relative) || relative.contains('\\') || relative.contains(':'))
        return fail(error, "A project path leaves its folder.");
    const auto parts = relative.split('/');
    QString path = root;
    for (int i = 0; i < parts.size(); ++i) {
        const auto &part = parts[i];
        if (part.isEmpty() || part == "." || part == "..")
            return fail(error, "A project path leaves its folder.");
        path = QDir(path).filePath(part);
        const QFileInfo info(path);
        if (info.isSymLink()) return fail(error, "Project files and asset folders cannot be symbolic links.");
        if (info.exists()) {
            const auto canonical = info.canonicalFilePath();
            if (!canonical.startsWith(root + '/'))
                return fail(error, "A project path leaves its folder.");
            if (i + 1 < parts.size() && !info.isDir())
                return fail(error, "An asset parent is not a directory.");
        } else if (i + 1 < parts.size()) {
            return fail(error, "The project's asset folder is missing.");
        }
    }
    if (requireFile && !QFileInfo(path).isFile()) return fail(error, "The project asset is missing.");
    if (result) *result = path;
    return true;
}
bool readSmallFile(const QString &root, const QString &relative, qint64 limit,
                   QByteArray *bytes, QString *error) {
    QString path;
    if (!safePath(root, relative, &path, error, true)) return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > limit)
        return fail(error, "Cannot read the project catalog or it exceeds its size limit.");
    *bytes = file.read(limit + 1);
    if (bytes->size() > limit || file.error() != QFileDevice::NoError)
        return fail(error, "Cannot read the project catalog or it exceeds its size limit.");
    return true;
}
QByteArray hash(const QByteArray &bytes) { return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256); }
QJsonArray strings(const QStringList &values) {
    QJsonArray array;
    for (const auto &value : values) array.append(value);
    return array;
}
bool readStrings(const QJsonValue &value, int maximum, QStringList *result) {
    if (!value.isArray() || value.toArray().size() > maximum) return false;
    for (const auto &item : value.toArray()) {
        if (!item.isString()) return false;
        result->append(item.toString());
    }
    return true;
}
QByteArray catalogBytes(const QVector<StudioAsset> &assets, const QStringList &folders) {
    QJsonArray entries;
    for (const auto &asset : assets) {
        entries.append(QJsonObject{{"id", asset.id}, {"name", asset.name}, {"type", asset.type},
            {"folder", asset.folder}, {"tags", strings(asset.tags)}, {"shotIds", strings(asset.shotIds)},
            {"path", asset.relativePath}, {"bytes", double(asset.bytes)}, {"removed", asset.removed}});
    }
    return QJsonDocument(QJsonObject{{"version", 1}, {"folders", strings(folders)}, {"assets", entries}})
        .toJson(QJsonDocument::Indented);
}
bool writeSmallFile(const QString &root, const QString &relative, const QByteArray &bytes, QString *error) {
    QString path;
    if (!safePath(root, relative, &path, error)) return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, "Could not save the project catalog: " + file.errorString());
    return true;
}
QString typeForFile(const QString &path) {
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (QStringList{"toonfx", "cube", "3dl", "lut"}.contains(suffix)) return "effect";
    if (QStringList{"obj", "glb", "gltf", "fbx", "vrm", "blend", "toon3d"}.contains(suffix)) return "model";
    // Extension matching avoids probing a large media file on the UI thread.
    const auto mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name();
    if (mime.startsWith("image/")) return "image";
    if (mime.startsWith("video/")) return "video";
    if (mime.startsWith("audio/")) return "audio";
    return "file";
}
QString defaultFolder(const QString &type) {
    if (type == "image") return "Images";
    if (type == "video") return "Video";
    if (type == "audio") return "Audio";
    if (type == "effect") return "Effects";
    if (type == "model") return "Models";
    return "Other";
}
bool copyFile(const QString &source, const QString &destinationRoot, const QString &relativePath,
              qint64 *copied, QString *error,
              const std::function<bool()> &cancelled) {
    QFile input(source);
    if (!QFileInfo(source).isFile() || QFileInfo(source).isSymLink() || !input.open(QIODevice::ReadOnly))
        return fail(error, "Could not read the source asset: " + QFileInfo(source).fileName());
    if (input.size() > maxAssetBytes) return fail(error, "The source asset exceeds 16 TiB.");
    QString destination;
    if (!safePath(destinationRoot, relativePath, &destination, error)) return false;
    QFile output(destination);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(error, "Could not create an asset copy: " + output.errorString());
    qint64 count = 0;
    auto abort = [&] {
        output.close();
        // Revalidate after callbacks/IO: a moved or replaced assets directory
        // must not redirect cancellation cleanup to a different user's file.
        QString ownedPath;
        if (safePath(destinationRoot, relativePath, &ownedPath, nullptr))
            QFile::remove(ownedPath);
        return false;
    };
    while (!input.atEnd()) {
        if (cancelledNow(cancelled, error)) return abort();
        const auto chunk = input.read(1024 * 1024);
        if (chunk.isEmpty() && input.error() != QFileDevice::NoError) {
            fail(error, "Could not finish reading the source asset.");
            return abort();
        }
        if (output.write(chunk) != chunk.size()) {
            fail(error, "Could not finish copying the asset: " + output.errorString());
            return abort();
        }
        count += chunk.size();
        if (count > maxAssetBytes) {
            fail(error, "The source asset exceeds 16 TiB.");
            return abort();
        }
    }
    if (cancelledNow(cancelled, error)) return abort();
    if (!output.flush()) { fail(error, "Could not flush the asset copy."); return abort(); }
    output.close();
    if (copied) *copied = count;
    return true;
}
} // namespace

QString StudioProject::entryPath() const { return QDir(m_root).filePath(markerName); }
QString StudioProject::artworkPath() const { return QDir(m_root).filePath(artworkName); }
const StudioAsset *StudioProject::asset(const QString &id) const {
    for (const auto &item : m_assets) if (item.id == id) return &item;
    return nullptr;
}
QString StudioProject::assetPath(const QString &id, QString *error) const {
    if (error) error->clear();
    const auto *item = asset(id);
    QString path;
    if (!item) fail(error, "The asset is no longer in this project.");
    else if (safePath(m_root, item->relativePath, &path, error, true)) return path;
    return {};
}

std::optional<StudioProject> StudioProject::open(const QString &rootOrMarker, QString *error) {
    if (error) error->clear();
    const QFileInfo input(rootOrMarker);
    if (input.isSymLink()) { fail(error, "Project folders and markers cannot be symbolic links."); return {}; }
    const auto directory = input.isDir() ? input.absoluteFilePath() : input.absolutePath();
    if (!input.isDir() && input.fileName() != markerName) {
        fail(error, "Choose a Toon Pro project folder or its project.toonstudio file.");
        return {};
    }
    const QFileInfo rootInfo(directory);
    if (!rootInfo.isDir() || rootInfo.isSymLink()) { fail(error, "The project folder is unavailable."); return {}; }
    StudioProject result;
    result.m_root = rootInfo.canonicalFilePath();
    QByteArray markerBytes;
    if (!readSmallFile(result.m_root, markerName, 65536, &markerBytes, error)) return {};
    QJsonParseError parse;
    auto marker = QJsonDocument::fromJson(markerBytes, &parse);
    if (parse.error != QJsonParseError::NoError || !marker.isObject()
        || marker.object().value("format").toString() != "toon-pro-studio"
        || marker.object().value("version").toInt(-1) != 1
        || marker.object().value("artwork").toString() != artworkName
        || marker.object().value("catalog").toString() != catalogName) {
        fail(error, "Unsupported or damaged project folder. Its original files were preserved.");
        return {};
    }
    if (!safePath(result.m_root, artworkName, nullptr, error, true)) return {};
    QString assetsPath;
    if (!safePath(result.m_root, "assets", &assetsPath, error) || !QFileInfo(assetsPath).isDir()) {
        fail(error, "The project's asset folder is missing or unsafe."); return {};
    }
    QByteArray bytes;
    if (!readSmallFile(result.m_root, catalogName, maxCatalogBytes, &bytes, error)) return {};
    auto catalog = QJsonDocument::fromJson(bytes, &parse);
    const auto object = catalog.object();
    if (parse.error != QJsonParseError::NoError || !catalog.isObject()
        || object.value("version").toInt(-1) != 1 || !object.value("assets").isArray()
        || object.value("assets").toArray().size() > maxAssets
        || !readStrings(object.value("folders"), maxFolders, &result.m_folders)) {
        fail(error, "Unsupported or damaged asset catalog. Its original files were preserved."); return {};
    }
    QSet<QString> folderKeys;
    for (const auto &folder : result.m_folders) {
        if (!validFolder(folder) || folder.isEmpty() || folderKeys.contains(folder.toCaseFolded())) {
            fail(error, "Invalid or duplicate project folder."); return {};
        }
        folderKeys.insert(folder.toCaseFolded());
    }
    QSet<QString> ids, paths;
    for (const auto &value : object.value("assets").toArray()) {
        if (!value.isObject()) { fail(error, "Malformed project asset."); return {}; }
        const auto entry = value.toObject();
        StudioAsset item;
        item.id = entry.value("id").toString(); item.name = entry.value("name").toString();
        item.type = entry.value("type").toString(); item.folder = entry.value("folder").toString();
        item.relativePath = entry.value("path").toString();
        const double size = entry.value("bytes").toDouble(-1);
        if (!entry.value("folder").isString() || !entry.value("bytes").isDouble()
            || !std::isfinite(size) || size < 0 || size > double(maxAssetBytes) || std::floor(size) != size
            || !entry.value("removed").isBool()
            || !readStrings(entry.value("tags"), 32, &item.tags)
            || !readStrings(entry.value("shotIds"), maxProjectShots, &item.shotIds)) {
            fail(error, "Invalid project asset metadata."); return {};
        }
        item.bytes = qint64(size); item.removed = entry.value("removed").toBool();
        if (!validAsset(item, error)) return {};
        if (ids.contains(item.id) || paths.contains(item.relativePath)
            || (!item.folder.isEmpty() && !result.m_folders.contains(item.folder))) {
            fail(error, "Duplicate asset or unknown project folder."); return {};
        }
        // Missing media is represented in the UI; symlinks and escaping paths
        // are invalid even if that entry has been moved to the library trash.
        if (!safePath(result.m_root, item.relativePath, nullptr, error)) return {};
        ids.insert(item.id); paths.insert(item.relativePath); result.m_assets.append(item);
    }
    result.m_catalogHash = hash(bytes);
    return result;
}

bool StudioProject::create(const QString &root, const Project &project, QString *error,
                           const QString &sourceRoot, const std::function<bool()> &cancelled) {
    if (error) error->clear();
    if (cancelledNow(cancelled, error)) return false;
    const auto validation = project.validate();
    if (!validation.isEmpty()) return fail(error, validation);
    if (root.trimmed().isEmpty()) return fail(error, "Choose a project folder.");
    const QFileInfo target(QDir::cleanPath(QFileInfo(root).absoluteFilePath()));
    if (target.exists() || target.isSymLink())
        return fail(error, "Choose a new project folder. Existing folders and files are preserved.");
    if (!target.absoluteDir().exists()) return fail(error, "The project's parent folder does not exist.");
    std::optional<StudioProject> source;
    if (!sourceRoot.isEmpty()) {
        source = open(sourceRoot, error);
        if (!source) return false;
        const auto destination = QDir(target.absoluteDir().canonicalPath()).filePath(target.fileName());
        if (destination == source->rootPath() || destination.startsWith(source->rootPath() + '/'))
            return fail(error, "Choose a destination outside the source project folder.");
    }
    // Prepare alongside the destination. A single final directory rename
    // publishes a complete package; cancellation never leaves a half project.
    QTemporaryDir staging(target.absoluteDir().filePath(".toon-studio-XXXXXX"));
    if (!staging.isValid()) return fail(error, "Could not prepare the project folder.");
    const auto stagingRoot = QFileInfo(staging.path()).canonicalFilePath();
    if (!QDir(stagingRoot).mkdir("assets")) return fail(error, "Could not create the asset folder.");
    if (!saveProject(project, QDir(stagingRoot).filePath(artworkName), error, cancelled)) return false;
    QVector<StudioAsset> entries;
    QStringList folders{"Images", "Video", "Audio", "Effects", "Models", "Other"};
    if (source) {
        entries = source->assets(); folders = source->folders();
        for (const auto &entry : entries) {
            if (cancelledNow(cancelled, error)) return false;
            const auto input = source->assetPath(entry.id, error);
            if (input.isEmpty()) return false;
            qint64 copied = 0;
            if (!copyFile(input, stagingRoot, entry.relativePath, &copied, error, cancelled)) return false;
            if (copied != entry.bytes) return fail(error, "An asset changed while copying the project: " + entry.name);
        }
        // Do not quietly create a partial copy if another window changed the
        // catalog while its asset bytes were being copied.
        QByteArray current;
        if (!readSmallFile(source->rootPath(), catalogName, maxCatalogBytes, &current, error)) return false;
        if (hash(current) != source->m_catalogHash) return fail(error, "The source asset catalog changed. Retry the project copy.");
    }
    if (!writeSmallFile(stagingRoot, catalogName, catalogBytes(entries, folders), error)) return false;
    if (cancelledNow(cancelled, error)) return false;
    const auto marker = QJsonDocument(QJsonObject{{"format", "toon-pro-studio"}, {"version", 1},
        {"artwork", artworkName}, {"catalog", catalogName}, {"title", project.movie}})
        .toJson(QJsonDocument::Indented);
    if (!writeSmallFile(stagingRoot, markerName, marker, error)) return false;
    if (cancelledNow(cancelled, error)) return false;
    if (QFileInfo::exists(target.absoluteFilePath()) || QFileInfo(target.absoluteFilePath()).isSymLink()
        || !QDir().rename(stagingRoot, target.absoluteFilePath()))
        return fail(error, "Could not publish the project folder; the destination may already exist.");
    staging.setAutoRemove(false);
    return true;
}

bool StudioProject::commit(const QVector<StudioAsset> &assets, const QStringList &folders, QString *error,
                           const std::function<bool()> &cancelled) {
    if (assets.size() > maxAssets || folders.size() > maxFolders) return fail(error, "The asset catalog is full.");
    QString catalogPath, lockPath, assetsPath;
    if (!safePath(m_root, catalogName, &catalogPath, error, true)
        || !safePath(m_root, "catalog.lock", &lockPath, error)
        || !safePath(m_root, "assets", &assetsPath, error)) return false;
    if (!QFileInfo(assetsPath).isDir()) return fail(error, "The project's asset folder is missing.");
    QLockFile lock(lockPath);
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(0)) return fail(error, "The asset catalog is being updated. Try again shortly.");
    QByteArray current;
    if (!readSmallFile(m_root, catalogName, maxCatalogBytes, &current, error)) return false;
    if (hash(current) != m_catalogHash)
        return fail(error, "The asset catalog changed in another operation. Reload the project assets and retry.");
    const auto bytes = catalogBytes(assets, folders);
    if (bytes.size() > maxCatalogBytes) return fail(error, "The asset catalog exceeds 16 MiB.");
    if (cancelledNow(cancelled, error)) return false;
    if (!writeSmallFile(m_root, catalogName, bytes, error)) return false;
    m_assets = assets; m_folders = folders; m_catalogHash = hash(bytes);
    return true;
}

bool StudioProject::importFiles(const QStringList &paths, const QString &rawFolder,
                                const QStringList &rawTags, QString *error,
                                const std::function<bool()> &cancelled) {
    if (error) error->clear();
    if (paths.isEmpty()) return fail(error, "Choose at least one asset to import.");
    if (paths.size() + m_assets.size() > maxAssets) return fail(error, "The project supports up to 10,000 assets.");
    const auto folder = rawFolder.trimmed();
    if (!validFolder(folder)) return fail(error, "Use a relative folder name without empty, dot or parent components.");
    QString tagError;
    const auto tags = normalizedTags(rawTags, &tagError);
    if (!tagError.isEmpty()) return fail(error, tagError);
    QString assetsPath;
    if (!safePath(m_root, "assets", &assetsPath, error) || !QFileInfo(assetsPath).isDir())
        return fail(error, "The project's asset folder is missing or unsafe.");
    QVector<StudioAsset> entries = m_assets;
    QStringList folders = m_folders, created;
    auto rollback = [&] {
        for (const auto &relative : created) {
            QString path;
            if (safePath(m_root, relative, &path, nullptr)) QFile::remove(path);
        }
        return false;
    };
    for (const auto &source : paths) {
        if (cancelledNow(cancelled, error)) return rollback();
        const QFileInfo info(source);
        if (!info.isFile() || info.isSymLink()) {
            fail(error, "Choose regular source files; folders and symbolic links cannot be imported.");
            return rollback();
        }
        StudioAsset entry;
        entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        entry.name = info.fileName();
        if (!validText(entry.name, 256)) { fail(error, "The asset filename is too long or contains control characters."); return rollback(); }
        entry.type = typeForFile(source); entry.tags = tags;
        entry.folder = folder.isEmpty() ? defaultFolder(entry.type) : folder;
        // Reuse a folder's original casing when a caller typed a matching name.
        const auto match = std::find_if(folders.cbegin(), folders.cend(), [&](const QString &f) {
            return f.compare(entry.folder, Qt::CaseInsensitive) == 0;
        });
        if (match != folders.cend()) entry.folder = *match;
        else folders.append(entry.folder);
        auto suffix = info.suffix().toLower();
        static const QRegularExpression safeSuffix("^[a-z0-9]{1,16}$");
        if (!safeSuffix.match(suffix).hasMatch()) suffix.clear();
        entry.relativePath = "assets/" + entry.id + (suffix.isEmpty() ? QString() : "." + suffix);
        if (!copyFile(source, m_root, entry.relativePath, &entry.bytes, error, cancelled)) return rollback();
        created.append(entry.relativePath); entries.append(entry);
    }
    if (!commit(entries, folders, error, cancelled)) return rollback();
    return true;
}

bool StudioProject::updateAsset(const StudioAsset &requested, QString *error) {
    if (error) error->clear();
    const auto *original = asset(requested.id);
    if (!original) return fail(error, "The asset is no longer in this project.");
    StudioAsset changed = requested;
    changed.name = changed.name.trimmed(); changed.folder = changed.folder.trimmed();
    QString tagError;
    changed.tags = normalizedTags(changed.tags, &tagError);
    if (!tagError.isEmpty()) return fail(error, tagError);
    // Content identity and trash state are changed only by their dedicated
    // operations; a metadata edit cannot redirect an asset to another file.
    if (changed.relativePath != original->relativePath || changed.bytes != original->bytes
        || changed.removed != original->removed) return fail(error, "Asset content and removal state cannot be changed by editing metadata.");
    if (!validAsset(changed, error)) return false;
    auto entries = m_assets; auto folders = m_folders;
    if (!changed.folder.isEmpty()) {
        const auto match = std::find_if(folders.cbegin(), folders.cend(), [&](const QString &f) {
            return f.compare(changed.folder, Qt::CaseInsensitive) == 0;
        });
        if (match != folders.cend()) changed.folder = *match;
        else folders.append(changed.folder);
    }
    for (auto &entry : entries) if (entry.id == changed.id) { entry = changed; break; }
    return commit(entries, folders, error);
}
bool StudioProject::removeAsset(const QString &id, QString *error) {
    if (error) error->clear();
    auto entries = m_assets;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const StudioAsset &a) { return a.id == id; });
    if (it == entries.end()) return fail(error, "The asset is no longer in this project.");
    if (it->removed) return true;
    it->removed = true;
    return commit(entries, m_folders, error);
}
bool StudioProject::restoreAsset(const QString &id, QString *error) {
    if (error) error->clear();
    auto entries = m_assets;
    auto it = std::find_if(entries.begin(), entries.end(), [&](const StudioAsset &a) { return a.id == id; });
    if (it == entries.end()) return fail(error, "The asset is no longer in this project.");
    if (!it->removed) return true;
    if (assetPath(id, error).isEmpty()) return false;
    it->removed = false;
    return commit(entries, m_folders, error);
}
bool StudioProject::createFolder(const QString &rawFolder, QString *error) {
    if (error) error->clear();
    const auto folder = rawFolder.trimmed();
    if (folder.isEmpty() || !validFolder(folder)) return fail(error, "Use a relative folder name without empty, dot or parent components.");
    if (m_folders.contains(folder, Qt::CaseInsensitive)) return fail(error, "This project folder already exists.");
    auto folders = m_folders; folders.append(folder);
    return commit(m_assets, folders, error);
}
} // namespace toon
