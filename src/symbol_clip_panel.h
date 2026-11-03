#pragma once
#include "symbol_types.h"
#include <QWidget>
#include <optional>

class QLabel;
class QTabBar;
class QToolButton;
namespace toon {
class TimelineController;
class ValueSlider;
class SymbolClipPanel final : public QWidget {
    Q_OBJECT
public:
    explicit SymbolClipPanel(TimelineController *controller, QWidget *parent = nullptr);
private:
    enum Field { Start = 1, Duration = 2, First = 4, Speed = 8, Slip = 16, Playback = 32, PoseFrame = 64, PoseTime = 128 };
    void refresh();
    void beginEdit(int field);
    void commitEditors();
    void cancelEditors();
    const SymbolInstance *selectedInstance() const;
    QString selectedId() const;
    TimelineController *m_controller;
    QWidget *m_controls, *m_poseControls;
    QLabel *m_name, *m_status, *m_poseStatus;
    QTabBar *m_playback;
    QToolButton *m_throughEnd, *m_pick, *m_posePick, *m_posePrevious, *m_poseDelete, *m_poseNext;
    ValueSlider *m_start, *m_duration, *m_first, *m_speed, *m_slip, *m_poseFrame, *m_poseTime;
    int m_poseTargetFrame = 0, m_editPoseFrame = 0;
    QString m_id, m_shotId;
    std::optional<SymbolInstance> m_snapshot;
    int m_dirty = 0;
    bool m_refreshing = false, m_resolving = false;
};
} // namespace toon
