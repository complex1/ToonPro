#pragma once
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <functional>

class QComboBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QToolButton;

namespace toon {
class Document;

class ShotsPanel : public QWidget {
    Q_OBJECT
public:
    explicit ShotsPanel(Document *document, std::function<bool()> prepareTransition, QWidget *parent = nullptr);
    QStringList selectedShotIds() const;
    void setProductionFilters(const QString &episode, const QString &stage);
signals:
    void exportRequested(const QStringList &shotIds);
protected:
    void showEvent(QShowEvent *event) override;
private:
    void requestRefresh();
    void refresh();
    void updateActions();
    void queueSelection();
    bool settleSelection();
    void restoreSelection(const QStringList &ids, const QString &current);
    bool perform(const std::function<bool()> &action, const QString &failure);
    void renameItem(QTreeWidgetItem *item, int column);
    void assignScene();
    void moveSelected(int offset);
    Q_INVOKABLE void moveDropped(const QString &source, const QString &target, bool before);
    void deleteSelected();
    void setStatus(const QString &text);
    int shotIndex(const QString &id) const;
    QPointer<Document> m_document;
    std::function<bool()> m_prepareTransition;
    QTreeWidget *m_list;
    QComboBox *m_sceneFilter, *m_scene;
    QLabel *m_summary, *m_status;
    QToolButton *m_add, *m_duplicate, *m_delete, *m_up, *m_down, *m_export;
    QStringList m_settledSelection, m_sceneTargets;
    QString m_settledCurrent, m_sceneValue;
    QString m_episodeFilter, m_stageFilter;
    bool m_refreshing = false, m_processing = false, m_pendingRefresh = false;
    bool m_selectionPending = false, m_followActive = true;
};
} // namespace toon
