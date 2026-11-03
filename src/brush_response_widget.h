#pragma once
#include <QWidget>
#include <array>

namespace toon {
// A normalized five-point input response. Points keep their input positions;
// artists edit output values directly without a second set of numeric fields.
class BrushResponseWidget : public QWidget {
    Q_OBJECT
public:
    using Response = std::array<qreal, 5>;
    explicit BrushResponseWidget(QWidget *parent = nullptr);
    void setResponse(const Response &response);
    const Response &response() const { return m_response; }
    int activeHandle() const { return m_active; }
    QSize sizeHint() const override { return {260, 142}; }
signals:
    void responseEdited(const toon::BrushResponseWidget::Response &response);
protected:
    bool event(QEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void focusInEvent(QFocusEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
private:
    QRectF field() const;
    void editAt(QPointF position);
    void apply(const Response &response);
    void refreshHelp();
    Response m_response{0, .25, .5, .75, 1};
    Response m_original = m_response;
    int m_active = 2;
    bool m_dragging = false;
};
} // namespace toon

Q_DECLARE_METATYPE(toon::BrushResponseWidget::Response)
