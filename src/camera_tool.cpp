#include "camera_tool.h"
#include "canvas.h"
#include "mask_tool.h"
#include "keyframe_transform_tool.h"
#include "ui_theme.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScopeGuard>
#include <QTabletEvent>
#include <cmath>

namespace toon {
CameraTool::CameraTool(Canvas *canvas) : QObject(canvas), m_canvas(canvas) {
    canvas->installEventFilter(this);
    connect(canvas, &Canvas::toolChanged, this, [this] { setEnabled(false); });
    connect(canvas->document(), &Document::loaded, this, [this] {
        m_drag = 0; m_tablet = false; m_enabled = false; emit changed();
    });
    connect(canvas->document(), &Document::frameChanged, this, [this] {
        if (dragging() && m_frame != m_canvas->document()->currentFrame()) cancel();
        m_canvas->update();
    });
    connect(canvas->document(), &Document::changed, this, [this] {
        if (dragging() && !m_canvas->document()->cameraEditActive()) {
            m_drag = 0; m_tablet = false; emit changed();
        }
        m_canvas->update();
    });
}
void CameraTool::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    finish();
    if (enabled && (!m_canvas->hasProject() || m_canvas->animationPlayback())) return;
    if (enabled) {
        m_canvas->maskTool()->setEnabled(false);
        m_canvas->keyframeTransformTool()->setEnabled(false);
        m_canvas->setCameraView(false);
    }
    m_enabled = enabled;
    m_canvas->refreshPointerCursor(); m_canvas->update(); emit changed();
}
QPolygonF CameraTool::box() const {
    if (!m_canvas->hasProject()) return {};
    const auto &project = m_canvas->project();
    const auto camera = shotCameraTransform(m_canvas->shot(), m_canvas->document()->currentFrame(), QSizeF(project.width, project.height));
    const auto world = camera.inverted().map(QPolygonF(QRectF(0, 0, project.width, project.height)));
    return m_canvas->artworkViewTransform().map(world);
}
QPointF CameraTool::rotationHandle() const {
    const auto frame = box(); if (frame.size() < 4) return {};
    const auto top = (frame[0] + frame[1]) / 2;
    const auto center = (frame[0] + frame[2]) / 2;
    auto normal = top - center; const qreal length = std::hypot(normal.x(), normal.y());
    return top + (length > 0 ? normal * (28 / length) : QPointF(0, -28));
}
void CameraTool::paint(QPainter &painter) const {
    if (!m_canvas->hasProject() || m_canvas->cameraView()) return;
    const auto frame = box(); if (frame.size() < 4) return;
    painter.save(); painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush); painter.setPen(QPen(QColor(15, 15, 15, 210), 4)); painter.drawPolygon(frame);
    painter.setPen(QPen(QColor(ui::accent), 2)); painter.drawPolygon(frame);
    const auto top = (frame[0] + frame[1]) / 2;
    painter.setPen(QColor(ui::text)); painter.drawText(top + QPointF(8, -9), "Camera");
    if (m_enabled && !m_canvas->animationPlayback()) {
        painter.setPen(QPen(QColor(ui::accent), 2)); painter.setBrush(QColor(ui::raised));
        for (int i = 0; i < 4; ++i) painter.drawRect(QRectF(frame[i] - QPointF(5, 5), QSizeF(10, 10)));
        const auto rotation = rotationHandle(); painter.drawLine(top, rotation); painter.drawEllipse(rotation, 5, 5);
    }
    painter.restore();
}
bool CameraTool::press(QPointF position) {
    if (!m_canvas->hasProject() || m_canvas->animationPlayback()) return true;
    m_canvas->setFocus();
    const auto frame = box(); if (frame.size() < 4) return true;
    int drag = 0;
    if (QLineF(position, rotationHandle()).length() <= 12) drag = 3;
    else for (int i = 0; i < 4; ++i) if (QLineF(position, frame[i]).length() <= 12) { drag = 2; break; }
    if (!drag && frame.containsPoint(position, Qt::OddEvenFill)) drag = 1;
    if (!drag) return true;
    auto *document = m_canvas->document();
    if (!document->beginCameraEdit({}, "Frame shot camera")) return true;
    m_drag = drag; m_frame = document->currentFrame();
    m_before = evaluateShotCamera(m_canvas->shot(), m_frame);
    m_start = m_canvas->artworkPoint(position);
    m_center = QPointF(m_canvas->project().width / 2., m_canvas->project().height / 2.) + m_before.position;
    const auto delta = m_start - m_center;
    m_startRadius = std::hypot(delta.x(), delta.y());
    m_lastAngle = std::atan2(delta.y(), delta.x()); m_rotationDelta = 0;
    emit changed(); return true;
}
void CameraTool::move(QPointF position, bool fine) {
    if (!dragging()) return;
    auto *document = m_canvas->document();
    if (!document->cameraEditActive() || m_frame != document->currentFrame()) { cancel(); return; }
    const auto point = m_canvas->artworkPoint(position);
    auto camera = m_before;
    if (m_drag == 1) {
        camera.position += (point - m_start) * (fine ? .1 : 1.);
        camera.position.setX(qBound(-32768., camera.position.x(), 32768.));
        camera.position.setY(qBound(-32768., camera.position.y(), 32768.));
    } else if (m_drag == 2) {
        const auto delta = point - m_center;
        const auto radius = qMax(.001, std::hypot(delta.x(), delta.y()));
        const qreal ratio = m_startRadius / radius;
        camera.zoom = qBound(.01, m_before.zoom * (fine ? std::pow(ratio, .1) : ratio), 100.);
    } else {
        const auto delta = point - m_center;
        const qreal angle = std::atan2(delta.y(), delta.x());
        m_rotationDelta += std::remainder(angle - m_lastAngle, 2 * M_PI) * 180 / M_PI; m_lastAngle = angle;
        camera.rotation = m_before.rotation + m_rotationDelta;
        if (fine) camera.rotation = qRound(camera.rotation / 15) * 15;
        camera.rotation = qBound(-36000., camera.rotation, 36000.);
    }
    // Exact return restores the original key set rather than adding a hold key.
    if (QLineF(point, m_start).length() < .0001 && (m_drag != 3 || qAbs(m_rotationDelta) < .0001)) camera = m_before;
    document->previewShotCamera(camera);
}
void CameraTool::finish() {
    m_tablet = false; if (!dragging()) return;
    m_drag = 0; m_canvas->document()->commitCameraEdit(); emit changed();
}
void CameraTool::cancel() {
    m_tablet = false; if (!dragging()) return;
    m_drag = 0; m_canvas->document()->cancelCameraEdit(); emit changed(); m_canvas->update();
}
bool CameraTool::eventFilter(QObject *watched, QEvent *event) {
    if (!m_enabled || watched != m_canvas) return false;
    if (event->type() == QEvent::FocusOut) { finish(); return false; }
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) { if (dragging()) cancel(); else setEnabled(false); return true; }
        return false;
    }
    if (m_canvas->m_space || m_canvas->m_dragging) return false;
    const auto *tablet = event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove
        || event->type() == QEvent::TabletRelease ? static_cast<QTabletEvent *>(event) : nullptr;
    // This filter consumes pen events before Canvas::tabletEvent runs.
    const auto syncCursor = qScopeGuard([this, tablet] {
        if (tablet) m_canvas->syncTabletCursor(tablet->position());
    });
    if (event->type() == QEvent::MouseButtonPress) {
        auto *e = static_cast<QMouseEvent *>(event); if (e->button() != Qt::LeftButton) return false;
        if (m_tablet || e->source() != Qt::MouseEventNotSynthesized) return true;
        return press(e->position());
    }
    if (event->type() == QEvent::MouseMove) {
        auto *e = static_cast<QMouseEvent *>(event);
        if (m_tablet || e->source() != Qt::MouseEventNotSynthesized) return true;
        if (dragging()) { if (e->buttons() & Qt::LeftButton) move(e->position(), e->modifiers() & Qt::ShiftModifier); else finish(); }
        return true;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *e = static_cast<QMouseEvent *>(event); if (e->button() != Qt::LeftButton) return false;
        if (m_tablet || e->source() != Qt::MouseEventNotSynthesized) return true;
        move(e->position(), e->modifiers() & Qt::ShiftModifier); finish(); return true;
    }
    if (event->type() == QEvent::TabletPress) {
        auto *e = static_cast<QTabletEvent *>(event); if (e->button() == Qt::LeftButton) { m_tablet = true; press(e->position()); }
        event->accept(); return true;
    }
    if (event->type() == QEvent::TabletMove) {
        auto *e = static_cast<QTabletEvent *>(event);
        if (m_tablet) { if (e->buttons() & Qt::LeftButton) move(e->position(), e->modifiers() & Qt::ShiftModifier); else finish(); }
        event->accept(); return true;
    }
    if (event->type() == QEvent::TabletRelease) {
        auto *e = static_cast<QTabletEvent *>(event);
        if (m_tablet) { move(e->position(), e->modifiers() & Qt::ShiftModifier); finish(); }
        event->accept(); return true;
    }
    return false;
}
} // namespace toon
