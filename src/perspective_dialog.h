#pragma once
#include "perspective_detection.h"
#include "perspective_source.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>
#include <memory>
class QLabel;class QListWidget;class QPushButton;class QTabBar;class QTimer;class QToolButton;
namespace toon {
class Document;class ValueSlider;class PerspectivePreview;
struct PerspectivePreviewJob { PerspectiveSource source; PerspectiveDetectionResult detection; };
class PerspectiveDialog final:public QDialog {
    Q_OBJECT
public:
    explicit PerspectiveDialog(Document *,QWidget *parent=nullptr);
    ~PerspectiveDialog() override;
    bool busy() const;
    const PerspectiveDetectionResult &previewResult() const { return m_result; }
    const PerspectiveSource &source() const { return m_source; }
    void useImageFile(const QString &path);
    void useSelectedLayer();
    void addGuides();
signals:
    void previewReady();
    void guidesAdded();
protected:
    void reject() override;
private:
    void schedule(bool refit=false);
    void startJob();
    void finished();
    void invalidateContext();
    void refreshCandidates(bool defaults);
    void refreshProposal();
    PerspectiveMode mode() const;
    QVector<int> chosenCandidates() const;
    QPointer<Document> m_document;
    QString m_shotId,m_sourceId,m_filePath;
    quint64 m_revision=0,m_generation=0,m_runningGeneration=0;
    int m_frame=0;
    bool m_invalid=false,m_running=false,m_pending=false,m_refit=false,m_fileSource=false,m_applying=false,m_refreshing=false,m_ready=false;
    int m_manualFamilies=0;
    PerspectiveSource m_source;
    PerspectiveDetectionSettings m_settings;
    PerspectiveDetectionResult m_result;
    QVector<DrawingGuide> m_proposal;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QFutureWatcher<PerspectivePreviewJob> m_watcher;
    QTimer *m_timer;
    PerspectivePreview *m_preview;
    QTabBar *m_sources,*m_modes,*m_interaction;
    QListWidget *m_candidates;
    ValueSlider *m_sensitivity,*m_density;
    QLabel *m_status,*m_sourceName;
    QPushButton *m_add;
    QToolButton *m_chooseFile;
};
}
