#pragma once
#include <QWidget>
#include <array>
class QLabel;
class QTabBar;
class QToolButton;
namespace toon {
class Scene3DController;
class ValueSlider;
class PosePropertyPages;
class Pose3DPanel final:public QWidget {
    Q_OBJECT
public:
    explicit Pose3DPanel(Scene3DController *controller,QWidget *parent=nullptr);
private:
    void refresh();
    void wire(ValueSlider *field,const QString &label);
    void updateRotation(int axis);
    void updateTranslation(int axis);
    void updateIK(int axis);
    void updateLimit(bool minimum);
    Scene3DController *m_controller;
    QLabel *m_status;
    QTabBar *m_modes,*m_handles,*m_limitAxes;
    PosePropertyPages *m_pages;
    QWidget *m_body;
    QToolButton *m_rotationAxes[3];
    QToolButton *m_ikEnabled,*m_pin,*m_mirror,*m_resetSelected,*m_resetAll,*m_limitsEnabled;
    ValueSlider *m_rotation[3],*m_translation[3],*m_ik[3],*m_limitMin,*m_limitMax;
    std::array<double,11> m_displayed;
    QString m_displayedJoint;
    int m_limitAxis=0;
    bool m_refreshing=false;
};
} // namespace toon
