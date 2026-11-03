#include "movie_review_panel.h"
#include "movie_review_controller.h"
#include "movie_playback_sequence.h"
#include "icons.h"
#include "ui_metrics.h"
#include "ui_theme.h"
#include "value_slider.h"
#include <QAbstractScrollArea>
#include <QActionGroup>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QShowEvent>
#include <QTabletEvent>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace toon {
namespace {
QToolButton *reviewButton(QWidget *parent, const char *name, const char *icon, const QString &help, bool checkable = false) {
    auto *button = new QToolButton(parent);
    button->setObjectName(name); button->setIcon(appIcon(icon));
    button->setIconSize({ui::metrics::actionIcon, ui::metrics::actionIcon});
    button->setFixedSize(ui::metrics::actionTarget, ui::metrics::actionTarget);
    button->setToolTip(help); button->setAccessibleName(help); button->setCheckable(checkable);
    return button;
}
QString reviewTime(int frame, double fps) {
    const qint64 milliseconds = qRound64(qMax(0, frame) * 1000.0 / qMax(1.0, fps));
    return QString("%1:%2.%3").arg(milliseconds / 60000).arg((milliseconds / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}
}

// A virtual strip keeps a long movie cheap: only visible cards are painted and
// requested, and thumbnail images are bounded independently of the shot count.
class MovieShotStrip final : public QAbstractScrollArea {
public:
    explicit MovieShotStrip(MovieReviewController *controller, QWidget *parent) : QAbstractScrollArea(parent), m_controller(controller) {
        setObjectName("movieShotStrip"); setAccessibleName("Shots and global playhead");
        setAccessibleDescription("Click a shot to review it. Double-click or press Enter to edit. Left and right step one frame; Space plays or pauses.");
        setFocusPolicy(Qt::StrongFocus); setMinimumHeight(108); setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        viewport()->setAttribute(Qt::WA_TabletTracking); viewport()->setCursor(Qt::ArrowCursor);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded); setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { viewport()->update(); requestVisible(); });
        connect(controller, &MovieReviewController::sequenceChanged, this, [this] {
            m_images.clear(); m_lastActive = -1; updateRange(); refresh();
        });
        connect(controller, &MovieReviewController::thumbnailReady, this, [this](int shotIndex, const QImage &image) {
            if (image.isNull()) return;
            m_images.insert(shotIndex, image);
            // Evict the farthest cached shot. The current visible strip remains
            // stable while a long sequence is browsed in either direction.
            while (m_images.size() > 64) {
                int farthest = -1, distance = -1;
                const auto &segments = m_controller->segments();
                const int center = qBound(0, (horizontalScrollBar()->value()+viewport()->width()/2)/cardWidth, qMax(0, int(segments.size())-1));
                const int current = segments.isEmpty() ? 0 : segments[center].shotIndex;
                for (auto it = m_images.cbegin(); it != m_images.cend(); ++it)
                    if (std::abs(it.key() - current) > distance) { farthest = it.key(); distance = std::abs(it.key() - current); }
                m_images.remove(farthest);
            }
            viewport()->update();
        });
        updateRange();
    }
    void refresh() {
        const int active = segmentAtFrame(m_controller->currentFrame());
        if (active != m_lastActive) {
            m_lastActive = active;
            if (active >= 0) {
                const int left = active * cardWidth, right = left + cardWidth;
                if (left < horizontalScrollBar()->value()) horizontalScrollBar()->setValue(left);
                else if (right > horizontalScrollBar()->value() + viewport()->width()) horizontalScrollBar()->setValue(right - viewport()->width());
            }
        }
        viewport()->update(); requestVisible();
    }
protected:
    void showEvent(QShowEvent *event) override { QAbstractScrollArea::showEvent(event); updateRange(); refresh(); }
    void resizeEvent(QResizeEvent *event) override { QAbstractScrollArea::resizeEvent(event); updateRange(); requestVisible(); }
    void changeEvent(QEvent *event) override {
        QAbstractScrollArea::changeEvent(event);
        if (event->type() == QEvent::FontChange) { setMinimumHeight(76 + 2 * fontMetrics().height()); viewport()->update(); }
    }
    void paintEvent(QPaintEvent *) override {
        QPainter painter(viewport()); painter.fillRect(viewport()->rect(), QColor(ui::base));
        const auto &segments = m_controller->segments();
        if (segments.isEmpty()) { painter.setPen(QColor(ui::muted)); painter.drawText(viewport()->rect(), Qt::AlignCenter, "No shots to review"); return; }
        const auto &project = m_controller->project();
        const int first = qMax(0, horizontalScrollBar()->value() / cardWidth);
        const int last = qMin(int(segments.size()) - 1, (horizontalScrollBar()->value() + viewport()->width()) / cardWidth);
        const int textHeight = fontMetrics().height();
        for (int index = first; index <= last; ++index) {
            const auto &segment = segments[index];
            if (segment.shotIndex < 0 || segment.shotIndex >= project.shots.size()) continue;
            const auto &shot = project.shots[segment.shotIndex];
            const QRect card(index * cardWidth - horizontalScrollBar()->value() + 3, 3, cardWidth - 6, viewport()->height() - 6);
            const bool active = m_controller->currentFrame() >= segment.firstFrame && m_controller->currentFrame() < segment.firstFrame + segment.frameCount;
            painter.fillRect(card, QColor(active ? ui::selected : ui::panel));
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QColor(active ? ui::accent : ui::border)); painter.drawRect(card.adjusted(0, 0, -1, -1));
            const QRect preview = card.adjusted(4, 4, -4, -2 * textHeight - 10);
            painter.fillRect(preview, QColor(ui::canvas));
            const auto image = m_images.value(segment.shotIndex);
            if (!image.isNull()) {
                const QSize size = image.size().scaled(preview.size(), Qt::KeepAspectRatio);
                const QRect target(preview.center().x() - size.width()/2, preview.center().y() - size.height()/2, size.width(), size.height());
                painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(target, image);
            } else { painter.setPen(QColor(ui::muted)); painter.drawText(preview, Qt::AlignCenter, shot.number); }
            painter.setPen(QColor(ui::text));
            painter.drawText(QRect(card.left()+5, card.bottom()-2*textHeight-7, card.width()-10, textHeight), Qt::AlignVCenter,
                             fontMetrics().elidedText(shot.number + " · " + shot.name, Qt::ElideRight, card.width()-10));
            painter.setPen(QColor(ui::muted));
            painter.drawText(QRect(card.left()+5, card.bottom()-textHeight-6, card.width()-10, textHeight), Qt::AlignVCenter,
                             fontMetrics().elidedText(QString("%1 f · %2 s").arg(segment.frameCount).arg(segment.frameCount / qMax(1., project.fps), 0, 'f', 2), Qt::ElideRight, card.width()-10));
            if (active) {
                const qreal position = qreal(m_controller->currentFrame()-segment.firstFrame) / qMax(1, segment.frameCount);
                const int x = card.left() + qRound(position * (card.width()-1));
                painter.setPen(QPen(QColor(ui::focus), 2)); painter.drawLine(x, card.top(), x, card.bottom());
                painter.setBrush(QColor(ui::focus)); painter.setPen(Qt::NoPen);
                painter.drawPolygon(QPolygon{{x-4, card.top()}, {x+4, card.top()}, {x, card.top()+5}});
            }
        }
    }
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton) { QAbstractScrollArea::mousePressEvent(event); return; }
        setFocus(Qt::MouseFocusReason);
        const int index = indexAt(event->position().toPoint());
        if (index >= 0) m_controller->setFrame(m_controller->segments()[index].firstFrame);
        event->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && indexAt(event->position().toPoint()) >= 0) { m_controller->editCurrentShot(); event->accept(); }
        else QAbstractScrollArea::mouseDoubleClickEvent(event);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (event->modifiers() & ~Qt::KeypadModifier) { QAbstractScrollArea::keyPressEvent(event); return; }
        if (event->key() == Qt::Key_Space) { if (!event->isAutoRepeat()) m_controller->setPlaying(!m_controller->playing()); event->accept(); }
        else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) {
            m_controller->setFrame(qBound(0, m_controller->currentFrame() + (event->key() == Qt::Key_Left ? -1 : 1), qMax(0, m_controller->frameCount()-1))); event->accept();
        } else if (event->key() == Qt::Key_Home || event->key() == Qt::Key_End) {
            m_controller->setFrame(event->key() == Qt::Key_Home ? 0 : qMax(0, m_controller->frameCount()-1)); event->accept();
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { if (!event->isAutoRepeat()) m_controller->editCurrentShot(); event->accept(); }
        else if (event->key() == Qt::Key_Escape) { m_controller->setMode(ReviewMode::Shot); event->accept(); }
        else QAbstractScrollArea::keyPressEvent(event);
    }
    void wheelEvent(QWheelEvent *event) override {
        const auto pixels = event->pixelDelta(); const auto angle = event->angleDelta();
        const int delta = pixels.isNull() ? (angle.x() ? angle.x() : angle.y()) / 2 : (pixels.x() ? pixels.x() : pixels.y());
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta); event->accept();
    }
    bool viewportEvent(QEvent *event) override {
        if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove || event->type() == QEvent::TabletRelease) {
            const auto *tablet = static_cast<QTabletEvent *>(event);
            if (event->type() == QEvent::TabletPress) {
                QMouseEvent mouse(QEvent::MouseButtonPress, tablet->position(), tablet->globalPosition(), tablet->button(), tablet->buttons(), tablet->modifiers(), tablet->pointingDevice());
                mousePressEvent(&mouse);
            }
            event->accept(); return true;
        }
        if (event->type() == QEvent::ToolTip) {
            auto *help = static_cast<QHelpEvent *>(event); const int index = indexAt(help->pos());
            if (index >= 0) {
                const auto &segment = m_controller->segments()[index]; const auto &project = m_controller->project();
                const auto &shot = project.shots[segment.shotIndex];
                QToolTip::showText(help->globalPos(), QString("%1 · %2\n%3 · Frames %4–%5\nClick to review · Double-click to edit")
                    .arg(shot.number, shot.name, shot.scene.isEmpty() ? QString("Unassigned scene") : shot.scene)
                    .arg(segment.firstFrame+1).arg(segment.firstFrame+segment.frameCount), viewport());
            }
            return true;
        }
        return QAbstractScrollArea::viewportEvent(event);
    }
private:
    int indexAt(QPoint position) const {
        if (!viewport()->rect().contains(position)) return -1;
        const int index = (position.x()+horizontalScrollBar()->value()) / cardWidth;
        return index >= 0 && index < m_controller->segments().size() ? index : -1;
    }
    int segmentAtFrame(int frame) const {
        const auto &segments = m_controller->segments();
        const auto it = std::upper_bound(segments.cbegin(), segments.cend(), frame, [](int value, const MoviePlaybackSegment &segment) { return value < segment.firstFrame; });
        return it == segments.cbegin() ? -1 : int(it-segments.cbegin())-1;
    }
    void updateRange() {
        horizontalScrollBar()->setRange(0, qMax(0, int(m_controller->segments().size())*cardWidth-viewport()->width()));
        horizontalScrollBar()->setPageStep(viewport()->width()); horizontalScrollBar()->setSingleStep(cardWidth);
    }
    void requestVisible() {
        if (m_requestPending || !isVisible()) return;
        m_requestPending = true;
        QTimer::singleShot(0, this, [this] {
            m_requestPending = false;
            if (!isVisible()) return;
            const auto &segments = m_controller->segments();
            const int first = qMax(0, horizontalScrollBar()->value()/cardWidth);
            const int last = qMin(int(segments.size())-1, (horizontalScrollBar()->value()+viewport()->width())/cardWidth);
            for (int i = first; i <= last; ++i) if (!m_images.contains(segments[i].shotIndex)) m_controller->requestThumbnail(segments[i].shotIndex);
        });
    }
    static constexpr int cardWidth = 144;
    MovieReviewController *m_controller;
    QHash<int, QImage> m_images;
    int m_lastActive = -1;
    bool m_requestPending = false;
};

MovieReviewPanel::MovieReviewPanel(MovieReviewController *controller, QWidget *parent) : QWidget(parent), m_controller(controller) {
    setObjectName("movieReviewPanel"); setFocusPolicy(Qt::StrongFocus);
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(ui::metrics::controlGap);
    auto *transport = new QHBoxLayout; transport->setContentsMargins(0, 0, 0, 0); transport->setSpacing(2); layout->addLayout(transport);
    m_previous = reviewButton(this, "moviePreviousShot", "skip-back", "Previous shot");
    m_play = reviewButton(this, "moviePlay", "play", "Play scene or movie (Space)");
    m_next = reviewButton(this, "movieNextShot", "skip-forward", "Next shot");
    m_loop = reviewButton(this, "movieLoop", "repeat", "Loop scene or movie", true);
    m_quality = reviewButton(this, "moviePreviewQuality", "sliders-horizontal", "Playback preview resolution");
    m_quality->setProperty("compactDrawingAction", true); m_quality->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_quality->setPopupMode(QToolButton::InstantPopup); m_quality->setFocusPolicy(Qt::TabFocus);
    auto *qualityMenu = new QMenu(m_quality); qualityMenu->setObjectName("moviePreviewQualityMenu"); m_quality->setMenu(qualityMenu);
    auto *qualityGroup = new QActionGroup(qualityMenu); qualityGroup->setExclusive(true);
    const QStringList names{"Full", "Half", "Quarter", "Eighth"};
    for (int i=0;i<4;++i) {
        const int divisor = 1 << i; auto *option = qualityMenu->addAction(names[i]);
        option->setObjectName("moviePreview"+names[i]); option->setData(divisor); option->setCheckable(true); qualityGroup->addAction(option);
        option->setToolTip("Review preview only. Artwork and exports remain unchanged.");
        connect(option, &QAction::triggered, this, [this, divisor] { m_controller->setPreviewDivisor(divisor); });
    }
    m_edit = reviewButton(this, "movieEditShot", "pencil", "Edit the current shot at the reviewed frame (Enter)");
    for (auto *button : {m_previous, m_play, m_next, m_loop, m_quality}) transport->addWidget(button);
    transport->addStretch(); transport->addWidget(m_edit);
    m_frame = new ValueSlider("Global review frame", 1, 1, 1, 0, {}, this); m_frame->setObjectName("movieCurrentFrame"); layout->addWidget(m_frame);
    m_strip = new MovieShotStrip(controller, this); layout->addWidget(m_strip, 1);
    m_context = new QLabel(this); m_context->setObjectName("movieReviewContext"); m_context->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); layout->addWidget(m_context);
    connect(m_previous, &QToolButton::clicked, this, [this] { stepShot(-1); });
    connect(m_next, &QToolButton::clicked, this, [this] { stepShot(1); });
    connect(m_play, &QToolButton::clicked, this, [this] { m_frame->commitEditing(); m_controller->setPlaying(!m_controller->playing()); });
    connect(m_loop, &QToolButton::toggled, controller, &MovieReviewController::setLooping);
    connect(m_edit, &QToolButton::clicked, this, [this] { m_frame->commitEditing(); m_controller->editCurrentShot(); });
    connect(m_frame, &ValueSlider::editingStarted, this, [this] { if (!m_refreshing) m_controller->setPlaying(false); });
    connect(m_frame, &ValueSlider::valueChanged, this, [this](double value) { if (!m_refreshing) { m_controller->setFrame(qRound(value)-1); refresh(); } });
    connect(controller, &MovieReviewController::changed, this, &MovieReviewPanel::refresh);
    connect(controller, &MovieReviewController::sequenceChanged, this, [this] { m_frameCount = m_displayedFrame = -1; refresh(); });
    refresh();
}
void MovieReviewPanel::stepShot(int direction) {
    const auto &segments = m_controller->segments();
    if (segments.isEmpty()) return;
    const int frame = m_controller->currentFrame();
    int index = 0;
    while (index+1 < segments.size() && segments[index+1].firstFrame <= frame) ++index;
    if (direction < 0 && frame > segments[index].firstFrame) m_controller->setFrame(segments[index].firstFrame);
    else m_controller->setFrame(segments[qBound(0, index+direction, int(segments.size())-1)].firstFrame);
}
void MovieReviewPanel::refresh() {
    if (m_refreshing) return;
    const QScopedValueRollback<bool> guard(m_refreshing, true);
    const int count = m_controller->frameCount(), frame = m_controller->currentFrame();
    if (m_frameCount != count) { m_frameCount = count; m_frame->setRange(1, qMax(1, count)); }
    if (m_displayedFrame != frame) { m_displayedFrame = frame; if (!m_frame->editingActive()) m_frame->setValueSilently(frame+1); }
    m_frame->setEnabled(count > 0); m_play->setEnabled(count > 1); m_loop->setEnabled(count > 0); m_edit->setEnabled(count > 0); m_quality->setEnabled(count > 0);
    m_previous->setEnabled(count > 0 && frame > 0);
    const auto &segments = m_controller->segments();
    m_next->setEnabled(!segments.isEmpty() && frame < segments.last().firstFrame);
    const bool playing = m_controller->playing();
    m_play->setIcon(appIcon(playing ? "pause" : "play")); m_play->setToolTip(playing ? "Pause review (Space)" : "Play scene or movie (Space)"); m_play->setAccessibleName(m_play->toolTip());
    { const QSignalBlocker blocker(m_loop); m_loop->setChecked(m_controller->looping()); }
    const int divisor = m_controller->previewDivisor(); m_quality->setText(QString("1/%1").arg(divisor));
    m_quality->setToolTip(QString("Playback preview: %1\nArtwork and exports remain unchanged.").arg(divisor == 1 ? QString("Full") : QString("1/%1 resolution").arg(divisor)));
    m_quality->setAccessibleName(m_quality->toolTip());
    for (auto *option : m_quality->menu()->actions()) option->setChecked(option->data().toInt() == divisor);
    QString text = "No shots to review";
    const auto location = m_controller->location(); const auto &project = m_controller->project();
    if (location.shotIndex >= 0 && location.shotIndex < project.shots.size()) {
        const auto &shot = project.shots[location.shotIndex];
        text = QString("%1 / %2 · %3 · %4 · Shot frame %5").arg(frame+1).arg(count).arg(reviewTime(frame, project.fps), shot.number).arg(location.frame+1);
    }
    m_context->setText(text); m_context->setToolTip(text); m_context->setAccessibleName(text);
    m_frame->setToolTip(text); m_strip->refresh();
}
void MovieReviewPanel::keyPressEvent(QKeyEvent *event) {
    if (event->modifiers() & ~Qt::KeypadModifier) { QWidget::keyPressEvent(event); return; }
    if (event->key() == Qt::Key_Space) { if (!event->isAutoRepeat()) m_controller->setPlaying(!m_controller->playing()); event->accept(); }
    else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) { m_controller->setFrame(qBound(0, m_controller->currentFrame() + (event->key() == Qt::Key_Left ? -1 : 1), qMax(0, m_controller->frameCount()-1))); event->accept(); }
    else if (event->key() == Qt::Key_Escape) { m_controller->setMode(ReviewMode::Shot); event->accept(); }
    else QWidget::keyPressEvent(event);
}
}
