#pragma once

#include "project.h"
#include <QStringList>
#include <QVector>
#include <functional>
#include <optional>

namespace toon {

struct StudioAsset {
    QString id, name, type, folder;
    QStringList tags, shotIds;
    QString relativePath;
    qint64 bytes = 0;
    bool removed = false;
    bool operator==(const StudioAsset &) const = default;
};

// A portable folder contains editable artwork plus a separately saved media
// catalog. Asset imports never decode whole videos/audio files into memory.
// Artwork retains the existing .toonproj container limits in this first pass.
class StudioProject {
public:
    // The destination must not exist or be nested within sourceRoot. Preparing
    // a package is cancellable; publication is one final directory rename.
    static bool create(const QString &root, const Project &project, QString *error = nullptr,
                       const QString &sourceRoot = {},
                       const std::function<bool()> &cancelled = {});
    static std::optional<StudioProject> open(const QString &rootOrMarker, QString *error = nullptr);

    QString rootPath() const { return m_root; }
    QString entryPath() const;
    QString artworkPath() const;
    const QVector<StudioAsset> &assets() const { return m_assets; }
    const QStringList &folders() const { return m_folders; }
    const StudioAsset *asset(const QString &id) const;
    QString assetPath(const QString &id, QString *error = nullptr) const;

    // Imports regular files as project-owned copies. Cancellation is honored
    // before the atomic catalog publication; success means that publication
    // completed, even if a later cancellation request arrives.
    bool importFiles(const QStringList &paths, const QString &folder = {},
                     const QStringList &tags = {}, QString *error = nullptr,
                     const std::function<bool()> &cancelled = {});
    bool updateAsset(const StudioAsset &asset, QString *error = nullptr);
    bool removeAsset(const QString &id, QString *error = nullptr);
    bool restoreAsset(const QString &id, QString *error = nullptr);
    bool createFolder(const QString &folder, QString *error = nullptr);

private:
    bool commit(const QVector<StudioAsset> &assets, const QStringList &folders, QString *error,
                const std::function<bool()> &cancelled = {});
    QString m_root;
    QByteArray m_catalogHash;
    QVector<StudioAsset> m_assets;
    QStringList m_folders;
};

} // namespace toon
