#pragma once
#include "timeline_ripple.h"
#include <QDialog>

class QLabel;
class QTabBar;
class QPushButton;
namespace toon {
class TimelineController;
class ValueSlider;
class TimelineRippleDialog final : public QDialog {
    Q_OBJECT
public:
    explicit TimelineRippleDialog(TimelineController *controller, QWidget *parent = nullptr);
    TimelineRippleEdit edit() const;
private:
    void refresh();
    void apply();
    TimelineController *m_controller;
    QTabBar *m_kind;
    ValueSlider *m_start, *m_count, *m_destination;
    QLabel *m_scope, *m_result;
    QPushButton *m_apply;
    int m_duration;
    bool m_refreshing = false;
};
}
