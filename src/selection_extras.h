#pragma once
#include <QWidget>
class QComboBox;
class QToolButton;
namespace toon {
class Canvas;
class ValueSlider;
class SelectionExtras : public QWidget {
    Q_OBJECT
public:
    explicit SelectionExtras(Canvas *, QWidget *parent = nullptr);
private:
    void refresh();
    Canvas *m_canvas;
    ValueSlider *m_feather, *m_shade;
    QComboBox *m_saved;
    QToolButton *m_save, *m_load, *m_delete, *m_hatching, *m_animation;
};
}
