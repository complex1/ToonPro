#pragma once
#include <QWidget>
#include <QStringList>
#include <QVector3D>
class QComboBox;
class QTreeWidget;
class QToolButton;
class QTabWidget;
class QLabel;
namespace toon {
class Scene3DController;
class ValueSlider;
class RigPropertyPages;
class Rig3DPanel final : public QWidget {
    Q_OBJECT
public:
    explicit Rig3DPanel(Scene3DController *controller, QWidget *parent = nullptr);
    // Export chrome once when embedded in the shared scrolling inspector.
    QWidget *takeNavigation();
    QWidget *takeViewActions();
    void setViewportHeight(int height);
    int editingMode() const { return m_activeMode; }
signals:
    void editingModeAboutToChange(int previous, int current);
    void editingModeChanged(int current);
private:
    void refresh();
    void updateAxis(int axis);
    void showEditingMode(int index);
    void updateHierarchyHeight();
    Scene3DController *m_controller;
    QTreeWidget *m_joints;
    QComboBox *m_parent;
    QToolButton *m_create, *m_humanoid, *m_child, *m_root, *m_remove, *m_mirror;
    QToolButton *m_mirrorEdits, *m_visible;
    QTabWidget *m_modes;
    QWidget *m_navigation, *m_viewActions;
    RigPropertyPages *m_pages;
    QLabel *m_restStatus;
    QToolButton *m_views[3];
    ValueSlider *m_axes[3];
    QStringList m_treeSignature, m_parentSignature;
    QString m_displayedSelection;
    QVector3D m_displayedPosition;
    bool m_refreshing = false;
    int m_activeMode = 0;
    int m_viewportHeight = 480;
};
} // namespace toon
