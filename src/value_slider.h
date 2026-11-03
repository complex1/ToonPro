#pragma once
#include <QWidget>

class QDoubleSpinBox;
class QSlider;

namespace toon {
// One value, editable through either the track or the numeric field.
class ValueSlider : public QWidget {
    Q_OBJECT
public:
    ValueSlider(const QString &label, double minimum, double maximum, double value,
                int decimals = 0, const QString &suffix = {}, QWidget *parent = nullptr);
    ~ValueSlider() override;
    double value() const;
    bool editingActive() const { return m_editing; }
    void setSingleStep(double step);
    void setRange(double minimum, double maximum);
    // Useful slider travel can be narrower than the exact-entry range.
    void setSliderRange(double minimum, double maximum);
    void setDecimals(int decimals);
    void setSuffix(const QString &suffix);
public slots:
    void setValue(double value);
    void setValueSilently(double value);
    // Resolve pending typed text without moving focus (for Save/tool switches).
    void commitEditing();
    void cancelEditing();
signals:
    void valueChanged(double value);
    void editingStarted();
    // A drag commits once on release; numeric and keyboard steps commit individually.
    void editingFinished();
    void editingCancelled();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void updateValue(double value, bool notify = true);
    void configureTrack();
    void beginEditing();
    void finishEditing();
    void finishInteraction();
    void refreshNumberHelp();
    QSlider *m_slider;
    QDoubleSpinBox *m_number;
    QString m_unit;
    double m_scale = 1;
    double m_current = 0, m_original = 0;
    double m_trackMinimum = 0, m_trackMaximum = 100;
    bool m_customTrackRange = false;
    bool m_editing = false, m_textEditing = false, m_finishing = false;
    bool m_ignoreSliderDrag = false;
};
} // namespace toon
