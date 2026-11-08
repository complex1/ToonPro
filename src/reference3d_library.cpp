#include "reference3d_library.h"
#include "reference3d_builtin.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <cmath>

namespace toon {
namespace {
constexpr qint64 manifestLimit = 4 * 1024 * 1024;
constexpr qint64 libraryLimit = 512LL * 1024 * 1024;
constexpr int entryLimit = 128;
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
bool shortName(const QString &value) { return !value.trimmed().isEmpty() && value.size() <= 120 && !value.contains(QChar::Null); }
bool hashText(const QString &value) {
    if (value.size() != 64) return false;
    for (const auto c : value) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}
QByteArray fileBytes(const QString &path, qint64 limit, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 0 || file.size() > limit) {
        fail(error, "The material file is missing, unreadable or exceeds its size limit."); return {};
    }
    const auto bytes = file.readAll();
    if (bytes.size() != file.size()) { fail(error, "The material file could not be read completely."); return {}; }
    return bytes;
}
QByteArray digest(const QByteArray &data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256); }
QString blobPath(const QString &directory, const QString &hash) { return QDir(directory).filePath(hash + ".t3dscene"); }
QJsonObject toJson(const ReferenceLibraryEntry &entry) {
    QByteArray png;
    if (!entry.thumbnail.isNull()) { QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly); entry.thumbnail.save(&buffer, "PNG"); }
    return {{"id", entry.id}, {"name", entry.name}, {"kind", int(entry.kind)},
            {"tags", QJsonArray::fromStringList(entry.tags)}, {"favorite", entry.favorite},
            {"hash", entry.contentHash}, {"bytes", double(entry.byteSize)}, {"thumbnail", QString::fromLatin1(png.toBase64())}};
}
bool handBone(const QString &semantic) {
    const auto key = semantic.toLower();
    return key.contains("hand") || key.contains("thumb") || key.contains("index") || key.contains("middle")
        || key.contains("ring") || key.contains("little") || key.contains("pinky");
}
bool kindMatches(const ReferenceScene &scene, ReferenceLibraryKind kind, QString *error) {
    if (kind == ReferenceLibraryKind::Scene || kind == ReferenceLibraryKind::Camera) return true;
    if (scene.instances.size() != 1 || !scene.instances.first().asset)
        return fail(error,"Select exactly one character when registering this material kind.");
    const auto &asset = *scene.instances.first().asset;
    const bool shape = kind == ReferenceLibraryKind::BodyShape || kind == ReferenceLibraryKind::HeadShape || kind == ReferenceLibraryKind::HandShape;
    if (shape) {
        const auto expected = kind == ReferenceLibraryKind::BodyShape ? ReferenceStarterKind::Figure
            : kind == ReferenceLibraryKind::HeadShape ? ReferenceStarterKind::Head : ReferenceStarterKind::Hand;
        if (asset.metadata.value("generator") != "toonpro-reference-starter-v1"
            || asset.metadata.value("starterKind").toInt(-1) != int(expected))
            return fail(error,"This shape material kind does not match the selected authored model family.");
    }
    if (kind == ReferenceLibraryKind::HandPose) {
        bool mapped = false;
        for (auto it=asset.humanoidBones.cbegin();it!=asset.humanoidBones.cend();++it) if (handBone(it.key())) mapped = true;
        if (!mapped) return fail(error,"This model has no mapped hand joints for a hand pose.");
    }
    if (kind == ReferenceLibraryKind::Pose && asset.skins.isEmpty())
        return fail(error,"This model has no articulated skeleton for a body pose.");
    return true;
}
}
QString referenceLibraryKindName(ReferenceLibraryKind kind) {
    switch (kind) {
    case ReferenceLibraryKind::Scene: return "Scene";
    case ReferenceLibraryKind::Character: return "Character";
    case ReferenceLibraryKind::Pose: return "Body pose";
    case ReferenceLibraryKind::HandPose: return "Hand pose";
    case ReferenceLibraryKind::BodyShape: return "Body shape";
    case ReferenceLibraryKind::HeadShape: return "Head shape";
    case ReferenceLibraryKind::HandShape: return "Hand shape";
    case ReferenceLibraryKind::Camera: return "Camera";
    }
    return {};
}
ReferenceMaterialLibrary::ReferenceMaterialLibrary(QString directory, QObject *parent)
    : QObject(parent), m_directory(QDir::cleanPath(std::move(directory))) {}
const ReferenceLibraryEntry *ReferenceMaterialLibrary::entry(const QString &id) const {
    for (const auto &value : m_entries) if (value.id == id) return &value;
    return nullptr;
}
bool ReferenceMaterialLibrary::load(QString *error) {
    if (error) error->clear();
    m_loaded = false;
    if (!QDir().mkpath(m_directory)) return fail(error, "The material library directory cannot be created.");
    const auto path = QDir(m_directory).filePath("library.json");
    if (!QFileInfo::exists(path)) { m_entries.clear(); m_manifestDigest.clear(); m_loaded = true; emit changed(); return true; }
    const auto bytes = fileBytes(path, manifestLimit, error); if (bytes.isEmpty()) return false;
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse);
    const auto root = document.object();
    if (parse.error != QJsonParseError::NoError || root.value("version").toInt(-1) != 1 || !root.value("entries").isArray())
        return fail(error, "The material library manifest is invalid or has an unsupported version.");
    const auto rows = root.value("entries").toArray();
    if (rows.size() > entryLimit) return fail(error, "The material library exceeds its entry limit.");
    QVector<ReferenceLibraryEntry> next; QSet<QString> ids, hashes; qint64 total = 0;
    for (const auto &row : rows) {
        if (!row.isObject()) return fail(error, "A material library entry is invalid.");
        const auto object = row.toObject(); ReferenceLibraryEntry value;
        value.id = object.value("id").toString(); value.name = object.value("name").toString();
        const double rawKind = object.value("kind").toDouble(-1), rawBytes = object.value("bytes").toDouble(-1);
        if (!shortName(value.id) || ids.contains(value.id) || !shortName(value.name) || rawKind != std::floor(rawKind)
            || rawKind < 0 || rawKind > int(ReferenceLibraryKind::Camera) || !object.value("favorite").isBool()
            || !object.value("tags").isArray() || !std::isfinite(rawBytes) || rawBytes != std::floor(rawBytes)
            || rawBytes <= 0 || rawBytes > maxReferenceSceneBytes)
            return fail(error, "A material library entry has invalid metadata.");
        value.kind = ReferenceLibraryKind(int(rawKind)); value.byteSize = qint64(rawBytes);
        value.favorite = object.value("favorite").toBool(); value.contentHash = object.value("hash").toString();
        if (!hashText(value.contentHash)) return fail(error, "A material library content hash is invalid.");
        ids.insert(value.id);
        const auto tags = object.value("tags").toArray(); if (tags.size() > 24) return fail(error, "A material has too many tags.");
        for (const auto &tag : tags) {
            if (!tag.isString() || !shortName(tag.toString())) return fail(error, "A material tag is invalid.");
            value.tags.append(tag.toString());
        }
        const auto encoded = object.value("thumbnail").toString().toLatin1();
        if (encoded.size() > 384 * 1024) return fail(error, "A material thumbnail exceeds its size limit.");
        if (!encoded.isEmpty()) {
            QByteArray png = QByteArray::fromBase64(encoded, QByteArray::AbortOnBase64DecodingErrors);
            QBuffer buffer(&png); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer, "PNG");
            const auto size = reader.size();
            if (!size.isValid() || size.width() > 256 || size.height() > 256 || (value.thumbnail = reader.read()).isNull())
                return fail(error, "A material thumbnail is invalid.");
        }
        const QFileInfo blob(blobPath(m_directory, value.contentHash));
        if (blob.isSymLink() || !blob.isFile() || blob.size() != value.byteSize)
            return fail(error, "A material dependency is missing or has changed; the existing library was preserved.");
        if (!hashes.contains(value.contentHash)) { hashes.insert(value.contentHash); total += value.byteSize; }
        if (total > libraryLimit) return fail(error, "The material library exceeds its storage budget.");
        next.append(std::move(value));
    }
    m_entries = std::move(next); m_manifestDigest = digest(bytes); m_loaded = true; emit changed(); return true;
}
bool ReferenceMaterialLibrary::commit(const QVector<ReferenceLibraryEntry> &next, QString *error, bool alreadyLocked) {
    if (!m_loaded) return fail(error, "Load a valid material library before editing it.");
    if (next.size() > entryLimit) return fail(error, "The material library has reached its entry limit.");
    std::unique_ptr<QLockFile> lock;
    if (!alreadyLocked) {
        lock=std::make_unique<QLockFile>(QDir(m_directory).filePath("library.lock"));
        if (!lock->tryLock(0)) return fail(error, "Another window is updating this material library. Try again.");
    }
    const auto path = QDir(m_directory).filePath("library.json");
    QByteArray current;
    if (QFileInfo::exists(path)) { current = fileBytes(path, manifestLimit, error); if (current.isEmpty()) return false; }
    if ((current.isEmpty() ? QByteArray() : digest(current)) != m_manifestDigest)
        return fail(error, "The material library changed in another window. Reload it before editing.");
    QJsonArray rows; qint64 total = 0; QSet<QString> hashes;
    for (const auto &value : next) {
        rows.append(toJson(value));
        if (!hashes.contains(value.contentHash)) { hashes.insert(value.contentHash); total += value.byteSize; }
    }
    if (total > libraryLimit) return fail(error, "The material library exceeds its storage budget.");
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"entries", rows}}).toJson();
    if (bytes.size() > manifestLimit) return fail(error, "The material library manifest is too large.");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, "The material library could not be saved; the previous manifest was preserved.");
    m_entries = next; m_manifestDigest = digest(bytes); emit changed(); return true;
}
QString ReferenceMaterialLibrary::addScene(const ReferenceScene &scene, const QString &name, ReferenceLibraryKind kind,
                                          QString *error, const QImage &thumbnail) {
    if (error) error->clear();
    if (!m_loaded || !shortName(name) || int(kind) < 0 || int(kind) > int(ReferenceLibraryKind::Camera)) {
        fail(error, "Choose a valid material name and a loaded library."); return {};
    }
    if (!kindMatches(scene,kind,error)) return {};
    if (m_entries.size() >= entryLimit) { fail(error,"The material library has reached its entry limit."); return {}; }
    auto bytes = serializeReferenceScene(scene, error); if (bytes.isEmpty()) return {};
    ReferenceLibraryEntry value; value.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    value.name = name.trimmed(); value.kind = kind; value.byteSize = bytes.size(); value.contentHash = QString::fromLatin1(digest(bytes).toHex());
    if (!thumbnail.isNull()) value.thumbnail = thumbnail.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const auto path = blobPath(m_directory, value.contentHash);
    QLockFile lock(QDir(m_directory).filePath("library.lock"));
    if (!lock.tryLock(0)) { fail(error,"Another window is updating this material library. Try again."); return {}; }
    const auto manifestPath=QDir(m_directory).filePath("library.json");
    QByteArray current;
    if (QFileInfo::exists(manifestPath)) { current=fileBytes(manifestPath,manifestLimit,error); if(current.isEmpty()) return {}; }
    if ((current.isEmpty()?QByteArray():digest(current)) != m_manifestDigest) {
        fail(error,"The material library changed in another window. Reload it before editing."); return {};
    }
    qint64 physicalBytes=0;
    for (const auto &file:QDir(m_directory).entryInfoList({"*.t3dscene"},QDir::Files)) {
        if (file.isSymLink()) { fail(error,"The material library contains an unexpected symbolic link."); return {}; }
        physicalBytes+=file.size();
    }
    if (physicalBytes + (QFileInfo::exists(path)?0:bytes.size()) > libraryLimit) {
        fail(error,"The material library has reached its storage budget."); return {};
    }
    bool created=false;
    if (QFileInfo::exists(path)) {
        const auto existing = fileBytes(path, maxReferenceSceneBytes, error);
        if (digest(existing).toHex() != value.contentHash.toLatin1()) { fail(error, "An existing material payload is corrupt."); return {}; }
    } else {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            fail(error, "The material payload could not be saved."); return {};
        }
        created=true;
    }
    auto next = m_entries; next.append(value);
    if (!commit(next, error, true)) { if(created) QFile::remove(path); return {}; }
    return value.id;
}
std::optional<ReferenceScene> ReferenceMaterialLibrary::materialScene(const QString &id, QString *error) const {
    const auto *value = entry(id); if (!value) { fail(error, "Choose an existing material."); return {}; }
    const auto bytes = fileBytes(blobPath(m_directory, value->contentHash), maxReferenceSceneBytes, error);
    if (bytes.size() != value->byteSize || QString::fromLatin1(digest(bytes).toHex()) != value->contentHash) {
        fail(error, "The material payload failed its checksum; it was not loaded."); return {};
    }
    auto scene=deserializeReferenceScene(bytes, error);
    if (scene && !kindMatches(*scene,value->kind,error)) return {};
    return scene;
}
bool applyReferencePose(const ReferenceInstance &source, ReferenceInstance &target, bool handsOnly, QString *error) {
    if (!source.asset || !target.asset) return fail(error, "Choose a source pose and a target character.");
    for (const ReferenceInstance *instance : std::array<const ReferenceInstance*,2>{&source,&target}) {
        auto invalid=validateReferenceAsset(*instance->asset); if(!invalid.isEmpty()) return fail(error,invalid);
        invalid=validateReferenceInstance(*instance); if(!invalid.isEmpty()) return fail(error,invalid);
    }
    auto next = target; QHash<int, int> mapping;
    if (source.asset->id == target.asset->id) {
        for (int i = 0; i < source.asset->nodes.size(); ++i) {
            bool hand = false;
            for (auto it = source.asset->humanoidBones.cbegin(); it != source.asset->humanoidBones.cend(); ++it)
                if (it.value() == i && handBone(it.key())) hand = true;
            if (!handsOnly || hand) mapping.insert(i, i);
        }
    } else {
        for (auto it = source.asset->humanoidBones.cbegin(); it != source.asset->humanoidBones.cend(); ++it)
            if ((!handsOnly || handBone(it.key())) && target.asset->humanoidBones.contains(it.key()))
                mapping.insert(it.value(), target.asset->humanoidBones.value(it.key()));
    }
    if (mapping.isEmpty()) return fail(error, "These models have no compatible humanoid mapping for this pose.");
    for (auto it = mapping.cbegin(); it != mapping.cend(); ++it) {
        if (it.key()<0 || it.key()>=source.asset->nodes.size() || it.value()<0 || it.value()>=target.asset->nodes.size())
            return fail(error,"The pose has an invalid humanoid bone mapping.");
        const auto &from = source.asset->nodes[it.key()]; const auto &to = target.asset->nodes[it.value()];
        const auto pose = source.poseRotations.value(it.key(), from.rotation);
        const auto delta = from.rotation.conjugated() * pose;
        next.poseRotations.insert(it.value(), (to.rotation * delta).normalized());
        // Translation offsets are proportion-independent local displacements;
        // body shape and object placement remain the target's own state.
        if (source.poseTranslations.contains(it.key()))
            next.poseTranslations.insert(it.value(), to.translation + source.poseTranslations.value(it.key()) - from.translation);
        else next.poseTranslations.remove(it.value());
    }
    const auto invalid = validateReferenceInstance(next); if (!invalid.isEmpty()) return fail(error, invalid);
    target = std::move(next); return true;
}
bool ReferenceMaterialLibrary::applyToInstance(const QString &id, ReferenceInstance &target, QString *error) const {
    const auto *value = entry(id); if (!value) return fail(error, "Choose an existing material.");
    const auto scene = materialScene(id, error); if (!scene) return false;
    if (scene->instances.isEmpty()) return fail(error, "This material contains no character.");
    const auto &source = scene->instances.first();
    if (value->kind == ReferenceLibraryKind::Pose || value->kind == ReferenceLibraryKind::HandPose)
        return applyReferencePose(source, target, value->kind == ReferenceLibraryKind::HandPose, error);
    if (value->kind == ReferenceLibraryKind::BodyShape || value->kind == ReferenceLibraryKind::HeadShape || value->kind == ReferenceLibraryKind::HandShape)
        return applyReferenceBuiltinShape(source, target, error);
    if (value->kind != ReferenceLibraryKind::Character)
        return fail(error, "Apply this scene or camera material to the scene rather than to one character.");
    auto next = source; next.id = target.id; next.translation = target.translation; next.rotation = target.rotation; next.scale = target.scale;
    next.locked = target.locked; next.visible = target.visible;
    const auto invalid = validateReferenceInstance(next); if (!invalid.isEmpty()) return fail(error, invalid);
    target = std::move(next); return true;
}
bool ReferenceMaterialLibrary::rename(const QString &id, const QString &name, QString *error) {
    if (!entry(id) || !shortName(name)) return fail(error, "Choose a material and a valid name.");
    auto next = m_entries; for (auto &value : next) if (value.id == id) value.name = name.trimmed(); return commit(next, error);
}
bool ReferenceMaterialLibrary::setTags(const QString &id, const QStringList &tags, QString *error) {
    if (!entry(id) || tags.size() > 24) return fail(error, "Choose a material and at most 24 tags.");
    QStringList clean; for (const auto &tag : tags) { if (!shortName(tag)) return fail(error, "A material tag is invalid."); if (!clean.contains(tag.trimmed())) clean.append(tag.trimmed()); }
    auto next = m_entries; for (auto &value : next) if (value.id == id) value.tags = clean; return commit(next, error);
}
bool ReferenceMaterialLibrary::setFavorite(const QString &id, bool favorite, QString *error) {
    if (!entry(id)) return fail(error, "Choose an existing material.");
    auto next = m_entries; for (auto &value : next) if (value.id == id) value.favorite = favorite; return commit(next, error);
}
bool ReferenceMaterialLibrary::remove(const QString &id, QString *error) {
    if (!entry(id)) return fail(error, "Choose an existing material.");
    const auto hash=entry(id)->contentHash;
    QLockFile lock(QDir(m_directory).filePath("library.lock"));
    if (!lock.tryLock(0)) return fail(error,"Another window is updating this material library. Try again.");
    auto next = m_entries; next.removeIf([&](const auto &value) { return value.id == id; });
    if (!commit(next,error,true)) return false;
    bool referenced=false; for(const auto &value:m_entries) if(value.contentHash==hash) referenced=true;
    // Placed scenes own their decoded assets and saved scenes embed them. Only
    // the now-unreferenced library-owned blob is reclaimed under the same lock.
    if(!referenced) QFile::remove(blobPath(m_directory,hash));
    return true;
}
} // namespace toon
