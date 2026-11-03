#pragma once
#include <QWidget>
#include <QVector>
#include <array>

class QLabel;
class QTabBar;
class QToolButton;
namespace toon {
class TimelineController;
class ValueSlider;
class CameraPanel final : public QWidget {
    Q_OBJECT
public:
    explicit CameraPanel(TimelineController *controller, QWidget *parent = nullptr);
    void activate();
private:
    void refresh();
    bool selectTarget();
    void changeMode(int mode);
    void beginValue();
    void previewValue(int index, double value);
    void finishValue();
    void commitEditors();
    void cancelEditors();
    void stepKey(int direction);
    void moveKey();
    QVector<int> keyFrames() const;
    QString preferredScene() const;
    TimelineController *m_controller;
    QTabBar *m_mode, *m_interpolation;
    QLabel *m_context, *m_status;
    QWidget *m_shotPage, *m_scenePage;
    std::array<ValueSlider *,4> m_shotValues{};
    std::array<ValueSlider *,8> m_sceneValues{};
    QToolButton *m_enable, *m_framing, *m_view, *m_previous, *m_add, *m_remove, *m_next;
    ValueSlider *m_keyTime;
    QString m_layerId;
    int m_displayedFrame = -1, m_displayedMode = 0;
    bool m_refreshing = false, m_resolving = false, m_editing = false;
};
} // namespace toon
