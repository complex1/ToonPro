#pragma once

#include "project.h"
#include <QDialog>
#include <QPointer>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QToolButton;

namespace toon {
class EffectsPreviewRenderer;
class TimelineController;
class ValueSlider;
enum class SymbolFramePickerMode { ClipStart, TimedPose };

// Browsing is local to this dialog. Only Apply changes the instance's playback;
// the shared definition, parent playhead and clip timing remain untouched.
class SymbolFramePickerDialog final : public QDialog {
    Q_OBJECT
public:
    SymbolFramePickerDialog(TimelineController *, const QString &layerId, QWidget *parent = nullptr,
                            SymbolFramePickerMode mode = SymbolFramePickerMode::ClipStart, int poseParentFrame = -1);
    ~SymbolFramePickerDialog() override;
    bool closedWithFocus() const { return m_closedWithFocus; }
protected:
    void showEvent(QShowEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void done(int result) override;
private:
    bool sameContext() const;
    bool sourceUnchanged() const;
    void checkSource();
    void setFrame(int frame);
    void showPage();
    void updateActions();
    void requestThumbnail();
    void apply();
    void showStatus(const QString &message);
    QPointer<TimelineController> m_controller;
    SymbolFramePickerMode m_mode = SymbolFramePickerMode::ClipStart;
    int m_poseParentFrame = 0;
    int m_parentPlayhead = 0;
    QString m_layerId, m_shotId;
    SymbolInstance m_original;
    // Retained implicit shares provide a cheap, stable source-generation guard,
    // including changes to any nested Symbol used by the thumbnail renderer.
    Project m_previewProject;
    EffectsPreviewRenderer *m_renderer;
    QLabel *m_title, *m_pageLabel, *m_status;
    QListWidget *m_frames;
    ValueSlider *m_frame;
    QCheckBox *m_hold;
    QPushButton *m_apply;
    QToolButton *m_previous, *m_next;
    int m_frameCount = 1, m_selectedFrame = 0, m_page = -1, m_requestedFrame = -1;
    quint64 m_context = 0, m_revision = 0;
    bool m_valid = false, m_stale = false, m_requestActive = false, m_applying = false;
    bool m_closedWithFocus = false;
};

// Scope the native dialog's destruction before returning drawing focus.
void openSymbolFramePicker(TimelineController *, const QString &layerId, QWidget *parent = nullptr,
                           SymbolFramePickerMode mode = SymbolFramePickerMode::ClipStart, int poseParentFrame = -1);
} // namespace toon
