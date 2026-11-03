#pragma once

#include <QColor>
#include <QHash>
#include <QPointer>
#include <QToolBar>
#include <QVector>

class QSettings;

namespace toon {
class Canvas;
class ToolCatalog;

struct QuickAccessItem {
    enum class Kind { Tool, Color, Navigation };
    Kind kind = Kind::Tool;
    QString value;
    QString id() const;
    bool operator==(const QuickAccessItem &) const = default;
};

// Preferences refer to tools by stable identity, never copy their settings or
// artwork. The same actions remain available in both workspace modes.
class QuickAccessToolbar final : public QToolBar {
    Q_OBJECT
public:
    static constexpr int maxItems = 48;
    QuickAccessToolbar(Canvas *canvas, ToolCatalog *catalog, QSettings *settings,
                       QWidget *parent = nullptr);
    const QVector<QuickAccessItem> &items() const { return m_items; }
    bool addTool(const QString &toolId);
    bool addColor(const QColor &color);
    // Supported IDs: hand, zoom-in, zoom-out, fit-sheet, reset-rotation.
    bool addNavigation(const QString &navigationId);
    bool removeItem(const QString &itemId);
    bool moveItem(const QString &itemId, int index);
    QAction *itemAction(const QString &itemId) const;
    QAction *drawingModeAction() const { return m_drawingModeAction; }
    bool drawingMode() const { return m_drawingMode; }
    void setDrawingMode(bool enabled);

signals:
    void drawingModeRequested();

private:
    bool appendItem(QuickAccessItem item);
    void restore();
    void save();
    void rebuildItems();
    void refreshCatalog();
    void refreshActive();
    void refreshAvailability();
    void activate(const QuickAccessItem &item);
    void showAddMenu();
    void chooseTool();
    void showItemMenu(const QPoint &position);
    void configureButton(QAction *action);
    QPointer<Canvas> m_canvas;
    QPointer<ToolCatalog> m_catalog;
    QPointer<QSettings> m_settings;
    QVector<QuickAccessItem> m_items;
    QHash<QString, QAction *> m_itemActions;
    QVector<QAction *> m_groupSeparators;
    QAction *m_drawingModeAction = nullptr;
    QAction *m_addAction = nullptr;
    bool m_drawingMode = false;
};
} // namespace toon
