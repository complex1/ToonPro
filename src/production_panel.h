#pragma once
#include <QPointer>
#include <QWidget>
#include <functional>

class QLabel;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QTabWidget;
class QPushButton;
namespace toon {
class Document;
class ShotsPanel;
class ProjectAssetsPanel;
class ProductionPanel : public QWidget {
    Q_OBJECT
public:
    explicit ProductionPanel(Document *, std::function<bool()> prepareTransition, QWidget *parent = nullptr);
    bool finishEditing();
    void setProjectRoot(const QString &root);
    ProjectAssetsPanel *assetsPanel() const { return m_assets; }
    void showAssets();
signals:
    void drawingRequested();
    void exportRequested(const QStringList &ids);
protected:
    void showEvent(QShowEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    void refresh();
    void status(const QString &);
    QPointer<Document> m_document;
    ShotsPanel *m_shots;
    ProjectAssetsPanel *m_assets;
    QTabWidget *m_tabs;
    QLabel *m_title, *m_target, *m_status, *m_episodeLabel;
    QLineEdit *m_episode;
    QComboBox *m_stage, *m_episodeFilter, *m_stageFilter;
    QPlainTextEdit *m_notes;
    QPushButton *m_open;
    QString m_id, m_originalEpisode, m_originalStage, m_originalNotes;
    bool m_refreshing = false, m_dirty = false, m_committing = false;
};
}
