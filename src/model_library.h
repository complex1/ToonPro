#pragma once
#include "scene3d.h"
#include <QObject>
#include <QStringList>
namespace toon {
struct ModelLibraryEntry {
    QString id, name;
    QStringList tags;
    bool favorite = false;
    bool builtIn = false;
    Model3DPtr model;
    QImage thumbnail;
};
class ModelLibrary : public QObject {
    Q_OBJECT
public:
    explicit ModelLibrary(QString directory, QObject *parent = nullptr);
    const QVector<ModelLibraryEntry> &entries() const { return m_entries; }
    QString directory() const { return m_directory; }
    bool writable() const { return m_writable; }
    bool load(QString *error = nullptr);
    const ModelLibraryEntry *entry(const QString &id) const;
    // Import/add returns stable model ID; empty on failure, no partial library mutation.
    QString importFile(const QString &path, QString *error = nullptr);
    QString addModel(Model3DPtr model, const QString &name = {}, QString *error = nullptr);
    bool rename(const QString &id, const QString &name, QString *error = nullptr);
    bool setTags(const QString &id, const QStringList &tags, QString *error = nullptr);
    bool setFavorite(const QString &id, bool favorite, QString *error = nullptr);
    bool remove(const QString &id, QString *error = nullptr);
signals:
    void changed();
    void warning(const QString &message);
private:
    bool commit(const QVector<ModelLibraryEntry> &entries, QString *error);
    bool fail(const QString &message, QString *error);
    QString m_directory;
    QVector<ModelLibraryEntry> m_entries;
    bool m_writable = false;
};
} // namespace toon
