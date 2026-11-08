#pragma once
#include "line_art.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>
class QCheckBox;class QLabel;class QPushButton;class QTabBar;class QTimer;class QToolButton;
namespace toon {
class Document;class ValueSlider;class LineArtPreview;class LineArtRamp;
class LineArtDialog final : public QDialog {
    Q_OBJECT
public:
    explicit LineArtDialog(Document *,QWidget *parent=nullptr);
    ~LineArtDialog() override;
    LineArtSettings settings() const { return m_settings; }
    void setSettings(const LineArtSettings &);
    const LineArtResult &previewResult() const { return m_previewResult; }
    bool busy() const;
    void createLayers();
signals:
    void previewReady();
    void layersCreated();
protected:
    void reject() override;
private:
    void schedulePreview();
    void startJob(bool final);
    void finished();
    void refreshControls();
    void invalidateContext();
    QPointer<Document> m_document;
    QString m_sourceId,m_shotId;
    quint64 m_revision=0;
    int m_frame=0;
    bool m_loading=false,m_pending=false,m_final=false,m_invalid=false,m_applying=false,m_jobActive=false;
    LineArtSettings m_settings;
    LineArtResult m_previewResult;
    LineArtCancellation m_cancel;
    QFutureWatcher<LineArtResult> m_watcher;
    QTimer *m_timer;
    QWidget *m_controls,*m_shadingPage;
    QTabBar *m_presets;
    QLabel *m_status;
    QPushButton *m_create;
    LineArtPreview *m_preview;
    LineArtRamp *m_ramp;
    ValueSlider *m_detail,*m_width,*m_smoothing,*m_texture,*m_fragments,*m_dark,*m_bright,*m_spacing,*m_angle;
    QCheckBox *m_base,*m_hideSource;
    QToolButton *m_ink,*m_shade,*m_deep;
};
}
