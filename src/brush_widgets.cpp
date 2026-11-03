#include "brush_widgets.h"
#include "brush_engine.h"
#include "ui_theme.h"
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>
#include <cmath>

namespace toon {
namespace {
QImage coloredMask(const QImage &mask) {
    QImage image(mask.size(), QImage::Format_ARGB32_Premultiplied);
    const QColor color(ui::text);
    for (int y = 0; y < mask.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < mask.width(); ++x)
            row[x] = qPremultiply(qRgba(color.red(), color.green(), color.blue(), mask.constScanLine(y)[x]));
    }
    return image;
}
qreal degrees(QPointF offset) { return qRadiansToDegrees(std::atan2(offset.y(), offset.x())); }
}
BrushVisualWidget::BrushVisualWidget(Kind kind, QWidget *parent) : QWidget(parent), m_kind(kind) {
    setFocusPolicy(Qt::StrongFocus); setCursor(Qt::CrossCursor); setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(120); setFixedHeight(kind == Kind::Spacing ? 90 : 140);
    const QStringList names{"Brush tip shape", "Stamp spacing", "Scatter distribution", "Pen pose", "Symmetry center", "Texture scale"};
    const QStringList hints{
        "Drag the outer handle to rotate; drag the square handle to squash. Arrow keys rotate; Shift+Up/Down changes roundness.",
        "Drag horizontally to change stamp spacing. Left/Right adjusts the gap.",
        "Drag vertically to change scatter. Left/Right adjusts stamp count; Up/Down adjusts scatter.",
        "Drag the pen tip to set tilt X/Y; drag the outer ring to set rotation. Edited values enable their overrides. Arrow keys adjust tilt.",
        "Drag to place the symmetry center in the output frame. Arrow keys move it; Shift moves in larger steps.",
        "Drag horizontally to change texture scale. Left/Right makes the pattern smaller/larger."};
    setAccessibleName(names[int(kind)]); setToolTip(hints[int(kind)]);
}
QSize BrushVisualWidget::sizeHint() const { return {210, height()}; }
QRectF BrushVisualWidget::field() const {
    const QRectF available = QRectF(rect()).adjusted(16, 12, -16, -25);
    if (m_kind != Kind::Symmetry) return available;
    const QSizeF size = m_artwork.scaled(available.size(), Qt::KeepAspectRatio);
    return QRectF(available.center() - QPointF(size.width() / 2, size.height() / 2), size);
}
QPointF BrushVisualWidget::anglePoint(qreal angle, qreal radius) const {
    const auto a = qDegreesToRadians(angle); return field().center() + QPointF(std::cos(a), std::sin(a)) * radius;
}
void BrushVisualWidget::setBrush(const BrushSettings &brush) {
    if (m_brush == brush) return;
    m_brush = brush; m_preview = {}; update();
}
void BrushVisualWidget::setArtworkSize(QSizeF size) {
    if (size.isValid() && size != m_artwork) { m_artwork = size; update(); }
}
bool BrushVisualWidget::event(QEvent *event) {
    if (event->type() == QEvent::FocusOut || event->type() == QEvent::Hide ||
        event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate)
        m_dragging = false;
    return QWidget::event(event);
}
void BrushVisualWidget::resizeEvent(QResizeEvent *event) { m_preview = {}; QWidget::resizeEvent(event); }
void BrushVisualWidget::paintEvent(QPaintEvent *) {
    QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(hasFocus() ? ui::focus : ui::border), hasFocus() ? 2 : 1));
    p.setBrush(QColor(ui::base)); p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 6, 6);
    const auto area = field(); const auto center = area.center();
    const qreal radius = qMin(area.width(), area.height()) / 2;
    auto handle = [&](QPointF point, bool square = false) {
        p.setPen(QPen(QColor(ui::base), 2)); p.setBrush(QColor(ui::accent));
        if (square) p.drawRect(QRectF(point - QPointF(5, 5), QSizeF(10, 10))); else p.drawEllipse(point, 5, 5);
    };
    QString caption;
    if (m_kind == Kind::Tip) {
        p.setPen(QPen(QColor(ui::border), 1, Qt::DashLine)); p.setBrush(Qt::NoBrush); p.drawEllipse(center, radius, radius);
        if (m_preview.isNull()) {
            auto b = m_brush; b.size = qMax(1., radius * 1.45); b.pressureSize = false;
            b.sizeDynamics = {}; b.angleDynamics = {}; b.roundnessDynamics = {}; b.pose = {};
            b.texture.enabled = false; b.dual.enabled = false; b.projection = false; b.noise = false; b.wetEdges = false;
            m_preview = coloredMask(brushTipMask(b, {}));
        }
        p.drawImage(center - QPointF(m_preview.width() / 2., m_preview.height() / 2.), m_preview);
        handle(anglePoint(m_brush.angle, radius));
        handle(anglePoint(m_brush.angle + 90, radius * .72 * m_brush.roundness), true);
        caption = QString("%1°  ·  %2%").arg(qRound(m_brush.angle)).arg(qRound(m_brush.roundness * 100));
    } else if (m_kind == Kind::Spacing || m_kind == Kind::Scatter || m_kind == Kind::Texture) {
        if (m_preview.isNull() || m_preview.size() != area.size().toSize()) {
            auto b = m_brush; b.color = QColor(ui::text); b.size = m_kind == Kind::Scatter ? 8 : 20;
            b.symmetry.mode = SymmetryMode::Off; b.pressureSize = false; b.sizeDynamics = {};
            if (m_kind == Kind::Spacing) { b.scatter = 0; b.count = 1; }
            m_preview = brushStrokePreview(b, area.size().toSize(), QColor(ui::base));
        }
        p.save(); p.setClipRect(area); p.drawImage(area.topLeft(), m_preview); p.restore();
        if (m_kind == Kind::Spacing) {
            const qreal x = area.left() + (m_brush.spacing - .01) / 3.99 * area.width();
            p.setPen(QPen(QColor(ui::accent), 1)); p.drawLine(QPointF(x, area.bottom() - 10), QPointF(x, area.bottom()));
            handle(QPointF(x, area.bottom()), true); caption = QString("%1% spacing").arg(qRound(m_brush.spacing * 100));
        } else if (m_kind == Kind::Scatter) {
            handle({area.right(), area.bottom() - m_brush.scatter / 10 * area.height()}, true);
            caption = QString("%1%  ·  %2 stamps%3").arg(qRound(m_brush.scatter * 100)).arg(m_brush.count).arg(m_brush.scatterBothAxes ? " · XY" : "");
        } else caption = QString("%1% scale").arg(qRound(m_brush.texture.scale * 100));
    } else if (m_kind == Kind::Pose) {
        p.setPen(QPen(QColor(ui::border), 1)); p.setBrush(Qt::NoBrush); p.drawEllipse(center, radius, radius);
        p.drawLine(center - QPointF(radius, 0), center + QPointF(radius, 0));
        p.drawLine(center - QPointF(0, radius), center + QPointF(0, radius));
        const auto tip = center + QPointF(m_brush.pose.tiltX, m_brush.pose.tiltY) / 90 * radius * .72;
        p.setPen(QPen(QColor(ui::text), 4, Qt::SolidLine, Qt::RoundCap)); p.drawLine(center, tip); handle(tip);
        handle(anglePoint(m_brush.pose.rotation, radius), true);
        caption = QString("X %1°  Y %2°  ·  %3°").arg(qRound(m_brush.pose.tiltX)).arg(qRound(m_brush.pose.tiltY)).arg(qRound(m_brush.pose.rotation));
    } else if (m_kind == Kind::Symmetry) {
        p.save(); p.setClipRect(area); p.translate(area.topLeft());
        p.scale(area.width() / m_artwork.width(), area.height() / m_artwork.height());
        QPen pen{QColor(ui::accent)}; pen.setWidth(1); pen.setCosmetic(true); p.setPen(pen); p.setBrush(Qt::NoBrush);
        p.drawPath(brushSymmetryGuide(m_brush.symmetry, m_artwork)); p.restore();
        p.setPen(QPen(QColor(ui::border))); p.setBrush(Qt::NoBrush); p.drawRect(area);
        handle(area.topLeft() + QPointF(m_brush.symmetry.center.x() * area.width(), m_brush.symmetry.center.y() * area.height()));
        caption = QString("X %1%  ·  Y %2%").arg(qRound(m_brush.symmetry.center.x() * 100)).arg(qRound(m_brush.symmetry.center.y() * 100));
    }
    p.setPen(QColor(ui::muted)); p.drawText(QRect(4, height() - 23, width() - 8, 18), Qt::AlignCenter, caption);
}
void BrushVisualWidget::mousePressEvent(QMouseEvent *e) {
    if (e->button() != Qt::LeftButton) { QWidget::mousePressEvent(e); return; }
    setFocus(Qt::MouseFocusReason); m_dragging = true; m_start = e->position(); m_startScale = m_brush.texture.scale;
    const auto delta = e->position() - field().center();
    const qreal radius = qMin(field().width(), field().height()) / 2;
    m_angleDrag = std::hypot(delta.x(), delta.y()) > radius * .8;
    if (m_kind == Kind::Pose) {
        // A valid diagonal tilt can lie outside the rotation ring. Prefer the
        // displayed handles over the ring fallback, then keep that drag target.
        const auto tip = field().center() + QPointF(m_brush.pose.tiltX, m_brush.pose.tiltY) / 90 * radius * .72;
        const auto rotation = anglePoint(m_brush.pose.rotation, radius);
        const qreal tipDistance = QLineF(e->position(), tip).length();
        const qreal rotationDistance = QLineF(e->position(), rotation).length();
        if (tipDistance <= 12 && tipDistance <= rotationDistance) m_angleDrag = false;
        else if (rotationDistance <= 12) m_angleDrag = true;
    }
    editAt(e->position()); e->accept();
}
void BrushVisualWidget::mouseMoveEvent(QMouseEvent *e) {
    if (m_dragging && (e->buttons() & Qt::LeftButton)) editAt(e->position());
    else { m_dragging = false; QWidget::mouseMoveEvent(e); }
}
void BrushVisualWidget::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && m_dragging) { editAt(e->position()); m_dragging = false; e->accept(); }
    else QWidget::mouseReleaseEvent(e);
}
void BrushVisualWidget::editAt(QPointF point) {
    const auto area = field(); const auto delta = point - area.center();
    const qreal radius = qMin(area.width(), area.height()) / 2;
    switch (m_kind) {
    case Kind::Tip:
        if (m_angleDrag) m_brush.angle = degrees(delta);
        else { const auto a = qDegreesToRadians(m_brush.angle); m_brush.roundness = qBound(.01, qAbs(-std::sin(a) * delta.x() + std::cos(a) * delta.y()) / (radius * .72), 1.); }
        break;
    case Kind::Spacing: m_brush.spacing = qBound(.01, .01 + (point.x() - area.left()) / area.width() * 3.99, 4.); break;
    case Kind::Scatter: m_brush.scatter = qBound(0., (area.bottom() - point.y()) / area.height() * 10, 10.); break;
    case Kind::Pose:
        if (m_angleDrag) { m_brush.pose.rotation = degrees(delta); m_brush.pose.overrideRotation = true; }
        else { m_brush.pose.tiltX = qBound(-90., delta.x() / (radius * .72) * 90, 90.); m_brush.pose.tiltY = qBound(-90., delta.y() / (radius * .72) * 90, 90.); m_brush.pose.overrideTiltX = m_brush.pose.overrideTiltY = true; }
        break;
    case Kind::Symmetry: m_brush.symmetry.center = {qBound(0., (point.x() - area.left()) / area.width(), 1.), qBound(0., (point.y() - area.top()) / area.height(), 1.)}; break;
    case Kind::Texture: m_brush.texture.scale = qBound(.1, m_startScale * std::pow(2., (point.x() - m_start.x()) / 45.), 10.); break;
    }
    commit();
}
void BrushVisualWidget::keyPressEvent(QKeyEvent *e) {
    const int key = e->key();
    if (key != Qt::Key_Left && key != Qt::Key_Right && key != Qt::Key_Up && key != Qt::Key_Down) { QWidget::keyPressEvent(e); return; }
    const qreal step = (key == Qt::Key_Left || key == Qt::Key_Down) ? -1 : 1;
    const bool vertical = key == Qt::Key_Up || key == Qt::Key_Down;
    switch (m_kind) {
    case Kind::Tip:
        if (e->modifiers() & Qt::ShiftModifier) m_brush.roundness = qBound(.01, m_brush.roundness + step * .01, 1.);
        else m_brush.angle = qBound(-360., m_brush.angle + step, 360.); break;
    case Kind::Spacing: m_brush.spacing = qBound(.01, m_brush.spacing + step * .01, 4.); break;
    case Kind::Scatter:
        if (vertical) m_brush.scatter = qBound(0., m_brush.scatter + step * .1, 10.);
        else m_brush.count = qBound(1, m_brush.count + int(step), 16); break;
    case Kind::Pose:
        if (vertical) { m_brush.pose.tiltY = qBound(-90., m_brush.pose.tiltY - step, 90.); m_brush.pose.overrideTiltY = true; }
        else { m_brush.pose.tiltX = qBound(-90., m_brush.pose.tiltX + step, 90.); m_brush.pose.overrideTiltX = true; } break;
    case Kind::Symmetry: {
        const qreal amount = e->modifiers() & Qt::ShiftModifier ? .05 : .01;
        if (vertical) m_brush.symmetry.center.setY(qBound(0., m_brush.symmetry.center.y() - step * amount, 1.));
        else m_brush.symmetry.center.setX(qBound(0., m_brush.symmetry.center.x() + step * amount, 1.)); break;
    }
    case Kind::Texture: m_brush.texture.scale = qBound(.1, m_brush.texture.scale + step * .05, 10.); break;
    }
    commit(); e->accept();
}
void BrushVisualWidget::commit() { m_preview = {}; update(); emit brushEdited(m_brush); }
} // namespace toon
