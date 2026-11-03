#pragma once
#include <QWidget>

class QLabel;

namespace toon {
class PropertyRowLayout;

// A compact property label and its existing editor. Narrow rows wrap the label,
// leaving useful room for the editor without replacing or resetting it.
class PropertyRow : public QWidget {
    Q_OBJECT
public:
    PropertyRow(const QString &label, QWidget *control, QWidget *parent = nullptr);
    QWidget *control() const;
    QLabel *labelWidget() const;
    // Minimum width of the complete label band, including an optional action.
    void setLabelWidth(int width);
    void setAccessory(QWidget *accessory);

private:
    PropertyRowLayout *m_rowLayout;
    QLabel *m_label;
    QWidget *m_control;
};
} // namespace toon
