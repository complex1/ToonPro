#pragma once
#include <QWidget>
#include <array>
#include <limits>
class QLabel;
class QTabBar;
class QToolButton;
namespace toon {
class Scene3DController;
class ValueSlider;
class Skin3DPanel final : public QWidget {
    Q_OBJECT
public:
    explicit Skin3DPanel(Scene3DController *controller, QWidget *parent = nullptr);
private:
    void refresh();
    Scene3DController *m_controller;
    QLabel *m_status, *m_joint;
    QToolButton *m_bind, *m_unbind, *m_reset;
    QTabBar *m_modes, *m_axes;
    ValueSlider *m_radius, *m_strength, *m_bend;
    std::array<double,3> m_displayed{{std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()}};
    int m_axis = 0;
    bool m_refreshing = false;
};
} // namespace toon
