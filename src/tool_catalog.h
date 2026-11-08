#pragma once

#include "brush_library.h"
#include "brush_types.h"
#include "canvas.h"
#include <QHash>
#include <QJsonObject>
#include <QTimer>
#include <QSet>
#include <QUndoStack>
#include <functional>

namespace toon {

// Organization history may activate a different tool. Resolve pending artwork
// before moving its index so a cancelled navigation leaves both histories intact.
class ToolOrganizationHistory final : public QUndoStack {
public:
    std::function<bool(int)> beforeNavigation;
    void undo();
    void redo();
    void setIndex(int index);
    QAction *createUndoAction(QObject *parent, const QString &prefix = {}) const;
    QAction *createRedoAction(QObject *parent, const QString &prefix = {}) const;
};

struct ToolState {
    BrushSettings brush;
    qreal smoothing = 0;
    QJsonObject options;
    bool operator==(const ToolState &) const = default;
};

struct ToolDefinition {
    QString id, name, icon, brushPresetId;
    Canvas::Tool engine = Canvas::Tool::Brush;
    int variant = 0;
    ToolState state;
    QStringList tags{};
    QColor color{};
    BrushType brushType = BrushType::Custom;
    QString category{};
    // Canonical built-ins can adopt corrected defaults once while retaining
    // fields customized by the artist. Copies and imported tools use zero.
    int builtinRecipeRevision = 0;
};

struct ToolboxEntry {
    QString id, name, icon;
    bool group = true;
    QStringList tools;
    QString lastToolId;
    QStringList tags{};
    QColor color{};
    QImage customIcon{};
};

// Local tool organization is independent of a movie's artwork/history. Placements
// refer to stable definitions; duplicating a definition creates independent settings.
class ToolCatalog : public QObject {
    Q_OBJECT
public:
    static constexpr int maxTools = 512;
    static constexpr int maxEntries = 128;
    static constexpr int maxTags = 32;
    static constexpr int maxTagLength = 40;
    static constexpr int maxCategoryLength = 80;
    static constexpr int maxIconSide = 256;
    static constexpr qint64 maxFileBytes = 128 * 1024 * 1024;
    explicit ToolCatalog(BrushLibrary *library, QString path, QObject *parent = nullptr);
    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr);
    bool writable() const { return m_writable; }
    QString path() const { return m_path; }
    const QVector<ToolDefinition> &tools() const { return m_tools; }
    const QVector<ToolboxEntry> &entries() const { return m_entries; }
    // UI projections omit quarantined tools without rewriting saved definitions,
    // custom group membership, icons, names or remembered navigation.
    QVector<ToolDefinition> availableTools() const;
    QVector<ToolboxEntry> availableEntries() const;
    bool toolEnabled(const QString &id) const;
    const ToolDefinition *tool(const QString &id) const;
    const ToolboxEntry *entry(const QString &id) const;
    QString selectedEntryId() const;
    QString activeToolId() const;
    QStringList visibleToolIds() const;
    ToolOrganizationHistory *history() { return &m_history; }
    void setActivationGuard(std::function<bool()> guard) { m_activationGuard = std::move(guard); }
    // Background persistence yields to pen and canvas gestures. Explicit save
    // and session flush remain available for shutdown or deliberate saving.
    void setAutoSaveGuard(std::function<bool()> guard) { m_autoSaveGuard = std::move(guard); }

    bool selectEntry(const QString &id);
    bool activateTool(const QString &id);
    QString addGroup(const QString &name, const QString &icon, const QStringList &toolIds = {});
    QString addStandalone(const QString &toolId);
    bool addTools(const QString &entryId, const QStringList &ids);
    QString duplicateTool(const QString &toolId, const QString &entryId, const QString &name = {});
    QString createBrush(const QString &name, BrushType type, const ToolState &state,
                        const QString &category = {}, const QStringList &tags = {},
                        const QString &destinationEntryId = {});
    bool deleteBrushes(const QStringList &ids);
    bool setBrushMetadata(const QStringList &ids, BrushType type, const QString &category);
    bool setBrushCategory(const QStringList &ids, const QString &category);
    bool renameTool(const QString &id, const QString &name);
    bool renameEntry(const QString &id, const QString &name, const QString &icon = {});
    bool removeEntry(const QString &id);
    bool removeTool(const QString &entryId, const QString &toolId);
    bool moveEntry(const QString &id, int index);
    bool moveTool(const QString &entryId, const QString &toolId, int index);
    bool setToolTags(const QStringList &ids, const QStringList &tags);
    bool addToolTag(const QStringList &ids, const QString &tag);
    bool removeToolTag(const QStringList &ids, const QString &tag);
    bool setToolColor(const QStringList &ids, QColor color);
    bool setEntryTags(const QString &id, const QStringList &tags);
    bool setEntryColor(const QString &id, QColor color);
    bool setEntryIcon(const QString &id, const QString &builtin, const QImage &custom = {});
    bool setEntryTools(const QString &id, const QStringList &ids);
    bool updateToolState(const QString &id, const ToolState &state);

signals:
    // The controller captures old settings here before a different tool activates.
    void aboutToActivate();
    void activationRequested(const QString &toolId);
    void changed();
    void settingsChanged(const QString &toolId);
    void warning(const QString &message);

private:
    struct Snapshot {
        QVector<ToolDefinition> tools;
        QVector<ToolboxEntry> entries;
        QString selected, active;
        QSet<QString> deletedBrushPresets;
    };
    Snapshot snapshot() const;
    QString effectiveActiveTool(const Snapshot &state) const;
    void applySnapshot(Snapshot snapshot, bool preserveCurrentSettings = true);
    bool commit(const QString &label, Snapshot next, bool preserveNavigation = false);
    bool canActivate(Snapshot state);
    void seed();
    void reconcileBrushes();
    void scheduleSave();
    void normalizeSelection(Snapshot &state) const;
    bool validIds(const QStringList &ids) const;
    BrushLibrary *m_library;
    QString m_path;
    QVector<ToolDefinition> m_tools;
    QVector<ToolboxEntry> m_entries;
    QString m_selected, m_active;
    QSet<QString> m_deletedBrushPresets;
    QHash<QString, ToolState> m_latestStates;
    ToolOrganizationHistory m_history;
    std::function<bool()> m_activationGuard;
    std::function<bool()> m_autoSaveGuard;
    QTimer m_saveTimer;
    bool m_writable = false;
};

} // namespace toon
