#pragma once
#include <QScrollArea>
#include <QGradient>
namespace toon {
class Canvas;
class DrawingPropertyPages;
class DrawingToolPanel:public QScrollArea {
    Q_OBJECT
public:
    explicit DrawingToolPanel(Canvas *canvas,QWidget *parent=nullptr);
protected:
    bool eventFilter(QObject *watched,QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
private:
    void refresh();
    void updateTransformFooter();
    Canvas *m_canvas;
    DrawingPropertyPages *m_pages;
    QWidget *m_transformFooter = nullptr;
};
class GradientStopsWidget:public QWidget {
    Q_OBJECT
public:
    explicit GradientStopsWidget(QWidget *parent=nullptr);
    QGradientStops stops()const{return m_stops;}
    void setStops(const QGradientStops &stops);
    int selectedIndex()const{return m_selected;}
    void setSelectedColor(const QColor &color);
    void removeSelected();
    QSize sizeHint()const override{return {240,76};}
    QSize minimumSizeHint()const override{return {180,76};}
signals:
    void stopsChanged();
protected:
    void paintEvent(QPaintEvent*)override;
    void mousePressEvent(QMouseEvent*)override;
    void mouseMoveEvent(QMouseEvent*)override;
    void mouseReleaseEvent(QMouseEvent*)override;
    void mouseDoubleClickEvent(QMouseEvent*)override;
    void keyPressEvent(QKeyEvent*)override;
private:
    qreal position(qreal x)const;
    int hit(QPointF)const;
    void sortSelected(qreal position);
    QGradientStops m_stops{{0,Qt::black},{1,Qt::white}};
    int m_selected=0;
    bool m_drag=false;
};
}
