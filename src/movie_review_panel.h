#pragma once

#include <QWidget>

class QLabel;
class QToolButton;
namespace toon {
class MovieReviewController;
class MovieShotStrip;
class ValueSlider;

// Review controls use their own playhead; the current drawing stays untouched.
class MovieReviewPanel final : public QWidget {
    Q_OBJECT
public:
    explicit MovieReviewPanel(MovieReviewController *controller, QWidget *parent = nullptr);
protected:
    void keyPressEvent(QKeyEvent *event) override;
private:
    void refresh();
    void stepShot(int direction);
    MovieReviewController *m_controller;
    MovieShotStrip *m_strip;
    ValueSlider *m_frame;
    QToolButton *m_previous, *m_play, *m_next, *m_loop, *m_quality, *m_edit;
    QLabel *m_context;
    int m_frameCount = -1, m_displayedFrame = -1;
    bool m_refreshing = false;
};
}
