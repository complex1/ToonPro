#pragma once
#include <QScrollArea>
class QTreeWidget;
class QTabWidget;
class QTabBar;
class QToolButton;
class QLabel;
class QLineEdit;
class QStackedWidget;
namespace toon {
class Scene3DController;
class Rig3DPanel;
class ScenePropertyPages;
class ValueSlider;
class Scene3DPanel final : public QScrollArea {
    Q_OBJECT
public:
    explicit Scene3DPanel(Scene3DController *controller, QWidget *parent = nullptr);
    void setCameraMode(bool camera);
    void setRigMode();
protected:
    void resizeEvent(QResizeEvent *event) override;
    bool viewportEvent(QEvent *event) override;
private:
    void refresh();
    void layoutPinnedControls();
    void showPage(int index);
    void restoreRigScroll(int mode);
    void updateObjectValue(ValueSlider *field);
    void updateCameraValue(ValueSlider *field);
    void wireEdit(ValueSlider *field, bool camera);
    Scene3DController *m_controller;
    QTreeWidget *m_objects;
    QTabWidget *m_tabs;
    QTabBar *m_objectModes, *m_cameraModes;
    QWidget *m_objectPage, *m_cameraPage, *m_transformControls;
    QWidget *m_header = nullptr, *m_footer = nullptr;
    QWidget *m_scrollHeaderSpacer = nullptr, *m_scrollFooterSpacer = nullptr;
    QStackedWidget *m_modePages, *m_actionPages;
    ScenePropertyPages *m_pages = nullptr;
    int m_currentPage = 0;
    int m_scrollPositions[3]{};
    int m_rigScrollPositions[3]{};
    Rig3DPanel *m_rigPage;
    QLabel *m_status;
    QToolButton *m_create, *m_duplicate, *m_remove, *m_reset, *m_fit, *m_fitSelected, *m_cameraLock, *m_textured;
    QTabBar *m_projection;
    ValueSlider *m_axes[3];
    ValueSlider *m_fov, *m_distance, *m_yaw, *m_pitch, *m_orthoScale;
    bool m_refreshing = false;
    bool m_layingOutPinned = false;
};
} // namespace toon
