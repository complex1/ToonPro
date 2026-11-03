#pragma once
#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;

namespace toon {
class Canvas;
class Document;
class ValueSlider;
class SelectionPanel : public QWidget {
    Q_OBJECT
public:
    explicit SelectionPanel(Canvas *canvas, QWidget *parent = nullptr);
private:
    void refresh();
    void editTransform(ValueSlider *source);
    Canvas *m_canvas;
    Document *m_document;
    QWidget *m_content;
    QWidget *m_selectionActions;
    QWidget *m_transformControls;
    QWidget *m_commitControls;
    QLabel *m_status;
    QToolButton *m_selectAll;
    QToolButton *m_invert;
    QToolButton *m_deselect;
    QPushButton *m_begin;
    QPushButton *m_apply;
    QPushButton *m_cancel;
    ValueSlider *m_x;
    ValueSlider *m_y;
    ValueSlider *m_width;
    ValueSlider *m_height;
    ValueSlider *m_rotation;
    QToolButton *m_aspect;
    bool m_refreshing = false;
};
} // namespace toon
