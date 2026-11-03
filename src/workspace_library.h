#pragma once
#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

class QSettings;
namespace toon {
inline constexpr int maxSavedWorkspaces = 64;
inline constexpr qsizetype maxWorkspaceStateBytes = 2 * 1024 * 1024;
inline constexpr qsizetype maxWorkspaceLibraryBytes = 16 * 1024 * 1024;

// A workspace describes editor chrome only. Window size, artwork, brush/tool
// choices and the global Quick Access contents are deliberately not captured.
struct WorkspaceSnapshot {
    QByteArray state; // QMainWindow::saveState(3), including docks and toolbars.
    bool compactDrawing = false;
    bool drawerOpen = false;
    int drawerPage = 0;
    QString inspectorPage;
    bool operator==(const WorkspaceSnapshot &) const = default;
};
struct WorkspaceEntry {
    QString id, name;
    WorkspaceSnapshot snapshot;
    bool operator==(const WorkspaceEntry &) const = default;
};

QString validateWorkspaceSnapshot(const WorkspaceSnapshot &snapshot);

// Stores one bounded, versioned value alongside the existing workspace.ini
// settings. Invalid/newer or concurrently modified data remains untouched.
// The owner validates restoreState() against the real dock registry and rolls
// back an unsuccessful restore; Qt's opaque body is never interpreted here.
class WorkspaceLibrary final : public QObject {
    Q_OBJECT
public:
    explicit WorkspaceLibrary(QSettings *settings, QObject *parent = nullptr);
    bool load(QString *error = nullptr);
    const QVector<WorkspaceEntry> &entries() const { return m_entries; }
    const WorkspaceEntry *find(const QString &id) const;
    bool writable() const { return m_writable; }
    QString loadError() const { return m_error; }
    static QString settingsKey();
    bool add(const QString &name, const WorkspaceSnapshot &snapshot,
             QString *id = nullptr, QString *error = nullptr);
    bool update(const QString &id, const WorkspaceSnapshot &snapshot, QString *error = nullptr);
    bool rename(const QString &id, const QString &name, QString *error = nullptr);
    bool remove(const QString &id, QString *error = nullptr);
signals:
    void changed();
private:
    bool commit(const QVector<WorkspaceEntry> &entries, QString *error);
    QString nameError(const QString &name, const QString &exceptId = {}) const;
    bool block(const QString &message, QString *error);
    QPointer<QSettings> m_settings;
    QVector<WorkspaceEntry> m_entries;
    QByteArray m_loadedBytes;
    QString m_error;
    bool m_existed = false, m_writable = false;
};
} // namespace toon
