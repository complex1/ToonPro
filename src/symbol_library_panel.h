#pragma once
#include "project.h"
#include <QWidget>
#include <QStringList>
#include <QHash>
#include <functional>

class QAction;
class QLabel;
class QLineEdit;
class QListWidget;
class QTabBar;
class QToolButton;
namespace toon {
class Canvas;
class TimelineController;
class ToolCatalog;
class ValueSlider;

// Definitions belong to the movie; playback belongs to the selected instance.
class SymbolLibraryPanel final : public QWidget {
    Q_OBJECT
public:
    SymbolLibraryPanel(Canvas *canvas, TimelineController *timeline,
                       ToolCatalog *catalog = nullptr, QWidget *parent = nullptr);
    QString currentSymbolId() const;
    void setCurrentSymbol(const QString &id);
    void setTransitionGuard(std::function<bool()> guard) { m_transitionGuard = std::move(guard); }
public slots:
    void convertLayers(const QStringList &ids);
    void editCurrentInstance();
signals:
    void editRequested(const QString &symbolId);
    void transformRequested();
private:
    void refresh();
    void filter();
    bool prepare();
    QString askName(const QString &title, const QString &initial);
    void showError(const QString &message);
    void createSymbol();
    void insert();
    void edit();
    void rename();
    void duplicate();
    void remove();
    void makeUnique();
    void breakApart();
    void commitPlayback();
    void commitEditors();
    void cancelEditors();
    QString activeInstanceId() const;
    Canvas *m_canvas;
    TimelineController *m_timeline;
    QListWidget *m_list;
    QLineEdit *m_search;
    QLabel *m_empty, *m_error, *m_instanceName;
    QWidget *m_instance;
    QToolButton *m_add, *m_convert, *m_insert, *m_edit, *m_transform;
    QAction *m_rename, *m_duplicate, *m_remove, *m_unique, *m_break;
    QTabBar *m_playback;
    ValueSlider *m_start, *m_first, *m_speed;
    QString m_editLayerId, m_lastInstanceSymbol;
    // Retain the implicit shares so pointer identity remains valid across Undo.
    QVector<SymbolDefinition> m_displayedDefinitions;
    QVector<Shot> m_displayedShots;
    QHash<QString,int> m_useCounts;
    int m_ownerDuration = -1, m_symbolDuration = -1;
    bool m_refreshing = false, m_resolving = false;
    std::function<bool()> m_transitionGuard;
};
}
