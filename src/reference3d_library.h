#pragma once
#include "reference3d.h"
#include <QObject>
#include <QStringList>

namespace toon {
enum class ReferenceLibraryKind { Scene, Character, Pose, HandPose, BodyShape, HeadShape, HandShape, Camera };
QString referenceLibraryKindName(ReferenceLibraryKind kind);
struct ReferenceLibraryEntry {
    QString id, name;
    ReferenceLibraryKind kind = ReferenceLibraryKind::Scene;
    QStringList tags;
    bool favorite = false;
    QImage thumbnail;
    QString contentHash;
    qint64 byteSize = 0;
};

// Library materials are reusable artist presets, distinct from mesh shaders.
// The manifest is atomic; content-addressed, self-contained scenes load lazily.
class ReferenceMaterialLibrary final : public QObject {
    Q_OBJECT
public:
    explicit ReferenceMaterialLibrary(QString directory, QObject *parent = nullptr);
    bool load(QString *error = nullptr);
    const QVector<ReferenceLibraryEntry> &entries() const { return m_entries; }
    const ReferenceLibraryEntry *entry(const QString &id) const;
    QString directory() const { return m_directory; }
    bool writable() const { return m_loaded; }
    QString addScene(const ReferenceScene &scene, const QString &name, ReferenceLibraryKind kind,
                     QString *error = nullptr, const QImage &thumbnail = {});
    std::optional<ReferenceScene> materialScene(const QString &id, QString *error = nullptr) const;
    bool applyToInstance(const QString &id, ReferenceInstance &target, QString *error = nullptr) const;
    bool rename(const QString &id, const QString &name, QString *error = nullptr);
    bool setTags(const QString &id, const QStringList &tags, QString *error = nullptr);
    bool setFavorite(const QString &id, bool favorite, QString *error = nullptr);
    bool remove(const QString &id, QString *error = nullptr);
signals:
    void changed();
    void warning(const QString &message);
private:
    bool commit(const QVector<ReferenceLibraryEntry> &next, QString *error, bool alreadyLocked = false);
    QString m_directory;
    QVector<ReferenceLibraryEntry> m_entries;
    QByteArray m_manifestDigest;
    bool m_loaded = false;
};
// Retargets LOCAL rest-relative rotations without replacing shape or placement.
bool applyReferencePose(const ReferenceInstance &source, ReferenceInstance &target,
                        bool handsOnly = false, QString *error = nullptr);
} // namespace toon
