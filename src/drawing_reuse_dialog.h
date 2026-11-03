#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QToolButton;
class QShowEvent;

namespace toon {
class EffectsPreviewRenderer;
class TimelineController;
class ValueSlider;

// Browsing and previewing never change artwork. Reuse creates another exposure
// of the same drawing; removing an unused drawing is a separate Undo-able edit.
class DrawingReuseDialog final : public QDialog {
    Q_OBJECT
public:
    explicit DrawingReuseDialog(TimelineController *controller, QWidget *parent = nullptr);
    ~DrawingReuseDialog() override;
protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    bool sameContext() const;
    void refresh();
    void filter();
    void updateActions();
    void requestThumbnail();
    void reuse();
    void removeUnused();
    void showStatus(const QString &message);
    TimelineController *m_controller;
    EffectsPreviewRenderer *m_renderer;
    QListWidget *m_drawings;
    QLineEdit *m_search;
    QLabel *m_target, *m_status;
    ValueSlider *m_hold;
    QPushButton *m_reuse;
    QToolButton *m_remove;
    QString m_shotId, m_trackId, m_requestedDrawing;
    quint64 m_context = 0, m_revision = 0;
    int m_previewFrame = -1;
    bool m_refreshing = false, m_requestActive = false;
};
} // namespace toon
