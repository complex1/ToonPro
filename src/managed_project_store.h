#pragma once
#include "project.h"
#include <QMutex>
#include <memory>
class QLockFile;
namespace toon {
struct ManagedItem {
    QString id, name, kind, collection, folder, revision, recovery, path;
    QStringList tags;
    qint64 bytes = 0;
    bool removed = false;
};
struct ManagedFolder {
    QString id, parent, name, collection;
};
// One project lease, shared by its dashboard and document sessions. Methods
// serialize disk work and open thread-local SQL connections; never use on a pen path.
class ManagedProjectStore final {
  public:
    static std::shared_ptr<ManagedProjectStore> create(const QString &root, const QString &name,
                                                       const QString &type, QString *error);
    static std::shared_ptr<ManagedProjectStore> open(const QString &root, QString *error);
    ~ManagedProjectStore();
    QString root() const { return m_root; }
    QString title() const { return m_title; }
    QString type() const { return m_type; }
    QVector<ManagedItem> items(QString *error = nullptr) const;
    QVector<ManagedFolder> folders(QString *error = nullptr) const;
    QString createFolder(const QString &name, const QString &parent, const QString &collection,
                         QString *error);
    QString createDocument(const QString &name, const QString &kind, const Project &document,
                           const QString &folder, const QStringList &tags, QString *error,
                           const std::function<bool()> &cancelled = {});
    std::optional<Project> loadDocument(const QString &id, bool recovery, QString *error) const;
    QString saveDocument(const QString &id, const QString &expectedRevision, const Project &document,
                         bool recovery, QString *error, const std::function<bool()> &cancelled = {});
    bool discardRecovery(const QString &id, QString *error);
    bool importFiles(const QStringList &paths, const QString &folder, const QStringList &tags, QString *error,
                     const std::function<bool()> &cancelled = {},
                     const std::function<void(int, int)> &progress = {});
    bool organize(const QStringList &ids, const QString &collection, const QString &folder,
                  const QStringList &tags, QString *error, bool mergeTags = false);
    bool rename(const QString &id, const QString &name, QString *error);
    bool setRemoved(const QStringList &ids, bool removed, QString *error);
    bool undoOrganization(QString *error);
    bool backup(const QString &destination, QString *error,
                const std::function<bool()> &cancelled = {}) const;
    QString assetPath(const QString &id, QString *error) const;

  private:
    ManagedProjectStore() = default;
    QString m_root, m_title, m_type;
    std::unique_ptr<QLockFile> m_lock;
    mutable QMutex m_mutex;
};
Project newManagedDocument(const QString &name, bool animation, int width, int height,
                           const QColor &background, bool transparent, double fps = 24, int frames = 120);
} // namespace toon
