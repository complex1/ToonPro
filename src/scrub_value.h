#pragma once

#include <QWidget>

class QLineEdit;

namespace toon {
// A compact scalar field: click for exact entry, drag horizontally for live edits.
// Only user gestures emit edit signals; synchronizing from a document is silent.
class ScrubValue final : public QWidget {
    Q_OBJECT
public:
    ScrubValue(const QString &label, double minimum, double maximum, double value,
               QWidget *parent = nullptr);
    double value() const { return m_value; }
    void setRange(double minimum, double maximum);
    void setSingleStep(double step);
    void setDecimals(int decimals);
    void setSuffix(const QString &suffix);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
public slots:
    void setValue(double value);
    void cancelEditing();
signals:
    void editingStarted();
    void valueChanged(double value);
    void editingFinished();
    void editingCancelled();
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    bool event(QEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
private:
    enum class Mode { Idle, Pressed, Dragging, Text };
    void beginEditing(Mode mode);
    void finishEditing();
    void openEditor();
    void applyValue(double value);
    void updateFromText();
    QString numberText() const;
    double bounded(double value) const;
    QLineEdit *m_editor;
    double m_minimum = 0, m_maximum = 100, m_value = 100;
    double m_original = 100, m_step = 1;
    int m_decimals = 0;
    QString m_suffix = "%";
    QPointF m_pressPosition;
    Mode m_mode = Mode::Idle;
    bool m_finishing = false;
};
}
