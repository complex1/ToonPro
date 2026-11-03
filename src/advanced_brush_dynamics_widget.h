#pragma once
#include "brush_settings.h"
#include <QWidget>
#include <QVector>
class QCheckBox;
namespace toon {
class ValueSlider;
class AdvancedBrushDynamicsWidget : public QWidget {
    Q_OBJECT
public:
    enum class Section { Spray, Speed };
    explicit AdvancedBrushDynamicsWidget(Section section, QWidget *parent = nullptr);
    void setSettings(const BrushSettings &settings);
signals:
    void clusterSprayEdited(const toon::BrushClusterSpray &settings);
    void speedDynamicsEdited(const toon::BrushSpeedDynamics &settings);
private:
    void refresh();
    void edited();
    Section m_section;
    BrushSettings m_settings;
    QCheckBox *m_enabled;
    QWidget *m_controls;
    QWidget *m_preview = nullptr;
    QVector<ValueSlider *> m_values;
};
} // namespace toon
