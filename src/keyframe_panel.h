#pragma once
#include "property_animation.h"
#include <QWidget>
#include <array>

class QLabel;
class QTabBar;
class QToolButton;
namespace toon {
class TimelineController;
class ValueSlider;
class EasingGraphWidget;

// The animation page of Frame Properties. Time and selection remain in the
// controller; scalar gestures retain their draft until explicitly resolved.
class KeyframePanel final : public QWidget {
    Q_OBJECT
public:
    explicit KeyframePanel(TimelineController *controller, QWidget *parent = nullptr);
private:
    struct ChannelRow { QToolButton *enable = nullptr, *label = nullptr, *toggle = nullptr; ValueSlider *value = nullptr; QWidget *container = nullptr; };
    void refresh();
    void selectProperty(KeyframeProperty property);
    void selectPath();
    void setCategory(int category);
    void beginValue(KeyframeProperty property);
    void beginPivot();
    void beginEasing();
    void finishValue();
    void commitEditors();
    void cancelEditors();
    void stepKey(int direction);
    void addKey();
    void removeKey();
    void moveKey();
    QVector<int> keyFrames() const;
    TimelineController *m_controller;
    std::array<ChannelRow, keyframePropertyCount> m_channels;
    QLabel *m_context, *m_status, *m_keyContext;
    QToolButton *m_source, *m_transform, *m_motionPath, *m_pivot, *m_pivotReset, *m_pathEnable, *m_pathLabel;
    QWidget *m_pivotControls, *m_motionControls, *m_transformControls, *m_pathControls;
    QToolButton *m_motionCurve, *m_orientAlongPath, *m_easingReset;
    EasingGraphWidget *m_easingGraph;
    QLabel *m_easingContext;
    ValueSlider *m_pivotX, *m_pivotY;
    QToolButton *m_previous, *m_add, *m_remove, *m_next;
    ValueSlider *m_keyTime;
    QTabBar *m_interpolation, *m_categories;
    QString m_layerId, m_objectId;
    KeyframeProperty m_property = KeyframeProperty::PositionX;
    int m_displayedFrame = -1;
    int m_category = 0;
    bool m_pathSelected = false, m_refreshing = false, m_resolving = false, m_valueEditing = false, m_pivotEditing = false, m_easingEditing = false;
};
} // namespace toon
