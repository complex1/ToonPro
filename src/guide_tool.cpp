#include "guide_tool.h"
#include "canvas.h"
#include "guide_geometry.h"
#include "ui_theme.h"
#include <QPainter>
#include <QPainterPathStroker>
#include <QUuid>
#include <cmath>
#include <limits>

namespace toon {
namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QPointF bounded(QPointF point) { return {qBound(-1000000., point.x(), 1000000.), qBound(-1000000., point.y(), 1000000.)}; }
QPointF center(const DrawingGuide &guide) {
    if (guide.type == GuideType::Fisheye && guide.points.size() == 3) return (guide.points[0] + guide.points[1]) / 2;
    if (guide.type == GuideType::ThreePoint && guide.points.size() == 4) return guide.points.last();
    QPointF result; for (const auto point : guide.points) result += point;
    return guide.points.isEmpty() ? result : result / guide.points.size();
}
void alignFisheyeHeight(DrawingGuide &guide, bool circle = false) {
    if (guide.type != GuideType::Fisheye || guide.points.size() != 3) return;
    const auto axis = guide.points[1] - guide.points[0]; const auto length = std::hypot(axis.x(), axis.y());
    if (length < 1e-6) return;
    const QPointF normal(-axis.y() / length, axis.x() / length);
    const auto middle = center(guide);
    auto radius = QPointF::dotProduct(guide.points[2] - middle, normal);
    if (circle) radius = std::copysign(length / 2, radius);
    guide.points[2] = middle + normal * radius;
}
qreal detailTolerance(const QTransform &mapping, qreal ratio = 1) {
    return .4 / qMax(.001, std::hypot(mapping.m11(), mapping.m12()) * ratio);
}
void paintPairedPath(QPainter &painter, const QPainterPath &path, bool curves) {
    if (curves) {
        // Join neighboring chords into each ellipse/visible arc. Independent
        // translucent line caps otherwise leave dark dots at every junction.
        QPolygonF strip;
        const auto flush = [&] { if (strip.size() > 1) painter.drawPolyline(strip); strip.clear(); };
        for (int i = 1; i < path.elementCount(); i += 2) {
            const auto from = path.elementAt(i - 1), to = path.elementAt(i);
            const QPointF a(from.x, from.y), b(to.x, to.y);
            if (!strip.isEmpty() && strip.last() != a) flush();
            if (strip.isEmpty()) strip.append(a);
            strip.append(b);
            if (QLineF(strip.first(), strip.last()).length() < 1e-6) flush();
        }
        flush(); return;
    }
    // Straight families remain independent segments for fast rasterization.
    QVector<QLineF> lines; lines.reserve(path.elementCount() / 2);
    for (int i = 1; i < path.elementCount(); i += 2) {
        const auto a = path.elementAt(i - 1), b = path.elementAt(i);
        lines.append(QLineF(a.x, a.y, b.x, b.y));
    }
    painter.drawLines(lines);
}
void paintGuideLines(QPainter &painter, const DrawingGuide &guide, const GuideSettings &global,
                     const QTransform &mapping, QRectF bounds) {
    QColor color = guide.customColor ? guide.color : global.color;
    color.setAlphaF(color.alphaF() * (guide.customColor ? guide.opacity : global.opacity));
    // Guides are physical-pixel hairlines. This also avoids Qt's expensive
    // antialiased wide-stroke path for dense grids on high-DPI displays.
    const auto ratio = painter.device()->devicePixelRatioF();
    painter.setPen(QPen(color, 1 / ratio)); painter.setBrush(Qt::NoBrush);
    const auto path = mapping.map(guidePath(guide, bounds, detailTolerance(mapping, ratio)));
    paintPairedPath(painter, path, guide.type == GuideType::Fisheye);
}
}
GuideTool::GuideTool(Canvas *canvas) : QObject(canvas), m_canvas(canvas) {
    connect(canvas->document(), &Document::changed, this, &GuideTool::synchronize);
}
GuideSettings GuideTool::settings() const { return m_canvas->hasProject() ? m_canvas->shot().guideSettings : GuideSettings{}; }
QVector<DrawingGuide> GuideTool::guides() const {
    if (!m_canvas->hasProject()) return {};
    auto result = m_canvas->shot().guides;
    if (m_preview) for (auto &guide : result) if (guide.id == m_preview->id) guide = *m_preview;
    return result;
}
void GuideTool::synchronize() {
    // History may change artwork without changing assistants. It must still
    // discard an unfinished guide edit rather than commit over the redo branch.
    if (dragging() || creating) { cancel(); m_canvas->updateSelectionUi(); }
    const auto current = m_canvas->hasProject() ? m_canvas->shot().guides : QVector<DrawingGuide>{};
    const auto currentSettings = settings();
    if (m_cached == current && m_settings == currentSettings) return;
    m_cached = current; m_settings = currentSettings;
    // External edits/undo invalidate a pending drag, never a committed drawing.
    m_preview.reset(); m_dragHandle = -1;
    bool exists = false; for (const auto &guide : current) exists |= guide.id == selectedId;
    if (!exists && !selectedId.isEmpty()) { selectedId.clear(); emit m_canvas->guideSelectionChanged(); }
    emit m_canvas->guidesChanged(); m_canvas->update();
}
void GuideTool::select(const QString &id) {
    bool exists = id.isEmpty(); for (const auto &guide : guides()) exists |= guide.id == id;
    if (!exists || selectedId == id) return;
    selectedId = id; emit m_canvas->guideSelectionChanged(); m_canvas->update();
}
void GuideTool::beginCreation(GuideType type) {
    if (!m_canvas->hasProject() || m_canvas->document()->editingGestureActive() || guidePointCount(type) == 0) return;
    cancel(); creationType = type; creating = true; m_pending.clear(); m_hoverModifiers = Qt::NoModifier;
    emit m_canvas->guideCreationChanged(); m_canvas->updateSelectionUi(); m_canvas->update();
}
void GuideTool::cancelDrag() { m_preview.reset(); m_dragHandle = -1; m_canvas->update(); }
void GuideTool::cancel() {
    const bool changed = creating; creating = false; m_pending.clear(); m_preview.reset(); m_dragHandle = -1;
    if (changed) emit m_canvas->guideCreationChanged();
    m_canvas->update();
}
void GuideTool::commit(const QVector<DrawingGuide> &guides, const GuideSettings &settings, const QString &label) {
    if (!m_canvas->hasProject() || m_canvas->document()->editingGestureActive()) return;
    if (guides == m_canvas->shot().guides && settings == m_canvas->shot().guideSettings) return;
    const auto error = validateGuides(guides, settings);
    if (!error.isEmpty()) { emit m_canvas->document()->warning(error); return; }
    auto project = m_canvas->project(); project.shots[m_canvas->document()->activeShotIndex()].guides = guides; project.shots[m_canvas->document()->activeShotIndex()].guideSettings = settings;
    m_canvas->document()->replaceProject(project, label);
}
void GuideTool::appendGuide(DrawingGuide guide) {
    auto list = guides(); if (list.size() >= maxDrawingGuides) { emit m_canvas->document()->warning("A shot supports up to 64 drawing guides."); return; }
    const bool fresh = guide.id.isEmpty();
    guide.id = newId(); guide.name = guideTypeName(guide.type) + QString(" %1").arg(list.size() + 1);
    if (fresh) guide.area = QRectF(0, 0, m_canvas->project().width, m_canvas->project().height);
    list.append(guide); commit(list, settings(), "Add drawing guide"); select(guide.id);
}
void GuideTool::addDefault(GuideType type) {
    if (!m_canvas->hasProject()) return;
    const qreal w = m_canvas->project().width, h = m_canvas->project().height;
    DrawingGuide guide; guide.type = type;
    switch (type) {
    case GuideType::Perspective: guide.points = {{w*.2,h*.3},{w*.65,h*.2},{w*.85,h*.8},{w*.25,h*.8}}; break;
    case GuideType::VanishingPoint: guide.points = {{w*.5,h*.35}}; break;
    case GuideType::TwoPoint: guide.points = {{-w*.2,h*.3},{w*1.2,h*.3},{w*.5,h*.7}}; break;
    case GuideType::ParallelRuler: guide.points = {{w*.3,h*.5},{w*.7,h*.5}}; break;
    case GuideType::ThreePoint: guide.points = {{-w*.2,h*.25},{w*1.2,h*.25},{w*.5,-h*.85},{w*.5,h*.75}}; break;
    case GuideType::Fisheye: guide.points = {{w*.15,h*.5},{w*.85,h*.5},{w*.5,h*.15}}; break;
    }
    appendGuide(guide);
}
void GuideTool::updateGuide(const DrawingGuide &guide) {
    auto list = guides(); for (auto &item : list) if (item.id == guide.id) { item = guide; commit(list, settings(), "Edit drawing guide"); return; }
}
void GuideTool::removeSelected() {
    auto list = guides(); for (int i = 0; i < list.size(); ++i) if (list[i].id == selectedId) {
        list.removeAt(i); commit(list, settings(), "Delete drawing guide"); return;
    }
}
void GuideTool::duplicateSelected() {
    for (auto guide : guides()) if (guide.id == selectedId) {
        for (auto &point : guide.points) point = bounded(point + QPointF(24,24));
        guide.area.translate(24,24); guide.locked = false; appendGuide(guide); return;
    }
}
void GuideTool::setSettings(const GuideSettings &value) { commit(guides(), value, "Guide display and snapping"); }
void GuideTool::replaceSet(QVector<DrawingGuide> loaded, const GuideSettings &value, bool append) {
    auto result = append ? guides() : QVector<DrawingGuide>{};
    for (auto &guide : loaded) { guide.id = newId(); result.append(guide); }
    commit(result, value, append ? "Import drawing guides" : "Replace drawing guides");
    if (!loaded.isEmpty()) select(loaded.first().id);
}
QTransform GuideTool::transform() const { return m_canvas->artworkViewTransform(); }
QRectF GuideTool::visibleBounds() const {
    const auto viewport = transform().inverted().mapRect(QRectF(m_canvas->rect()));
    const auto geometry = sheetGeometry(m_canvas->project(), m_canvas->shot());
    return viewport.intersected(geometry.drawing.translated(-geometry.output.topLeft()));
}
QPointF GuideTool::moveHandle(const DrawingGuide &guide) const {
    auto position = transform().map(center(guide));
    if (guide.type == GuideType::VanishingPoint || guide.type == GuideType::ThreePoint) position += QPointF(24,24);
    return position;
}
int GuideTool::handleAt(const DrawingGuide &guide, QPointF position) const {
    if (!settings().handles) return -1;
    const auto mapping = transform();
    int handle = QLineF(position, moveHandle(guide)).length() <= 12 ? -2 : -1;
    for (int i = 0; i < guide.points.size(); ++i)
        if (QLineF(position, mapping.map(guide.points[i])).length() <= 11) handle = i;
    if (guide.limited && guide.id == selectedId) {
        if (QLineF(position, mapping.map(guide.area.topLeft())).length() <= 11) handle = guide.points.size();
        if (QLineF(position, mapping.map(guide.area.bottomRight())).length() <= 11) handle = guide.points.size() + 1;
    }
    return handle;
}
QString GuideTool::toolTipAt(QPointF position) const {
    if (creating || dragging() || !settings().visible) return {};
    auto list = guides();
    for (int i = 0; i < list.size(); ++i) if (list[i].id == selectedId) { list.append(list.takeAt(i)); break; }
    for (int i = list.size() - 1; i >= 0; --i) {
        const auto &guide = list[i]; if (!guide.visible) continue;
        const auto handle = handleAt(guide, position); if (handle == -1) continue;
        if (guide.locked) return guide.name + " — locked";
        if (handle == -2) return "Move " + guide.name + " (Shift: constrain direction)";
        if (handle >= guide.points.size()) return "Resize guide area (Shift: constrain direction)";
        if (guide.type == GuideType::VanishingPoint || (guide.type == GuideType::TwoPoint && handle < 2) || (guide.type == GuideType::ThreePoint && handle < 3))
            return "Move vanishing point (Shift: constrain direction)";
        if (guide.type == GuideType::TwoPoint || guide.type == GuideType::ThreePoint) return "Move grid anchor (Shift: constrain direction)";
        if (guide.type == GuideType::Fisheye) return handle == 2 ? "Set fisheye height (Shift: circular boundary)" : "Move fisheye axis endpoint";
        return "Move guide handle (Shift: constrain direction)";
    }
    return {};
}
const QImage &GuideTool::cachedLines(LineCache &cache, const QVector<DrawingGuide> &guides,
                                   const GuideSettings &global, const QTransform &mapping,
                                   QRectF bounds, bool selected) const {
    const auto ratio = m_canvas->devicePixelRatioF();
    const QSize pixels(qCeil(m_canvas->width() * ratio), qCeil(m_canvas->height() * ratio));
    if (cache.image.size() != pixels || cache.image.devicePixelRatio() != ratio ||
        cache.guides != guides || cache.settings != global || cache.mapping != mapping || cache.bounds != bounds) {
        cache.image = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
        cache.image.setDevicePixelRatio(ratio); cache.image.fill(Qt::transparent);
        QPainter painter(&cache.image); painter.setRenderHint(QPainter::Antialiasing);
        for (const auto &guide : guides) paintGuideLines(painter, guide, global, mapping, bounds);
        cache.guides = guides; cache.settings = global; cache.mapping = mapping; cache.bounds = bounds;
        if (!selected) ++m_cacheRenderCount;
    }
    return cache.image;
}
void GuideTool::paint(QPainter &painter) const {
    if (!m_canvas->hasProject()) return;
    const auto global = settings(); const bool editing = m_canvas->tool() == Canvas::Tool::Guides;
    const auto mapping = transform(); const auto bounds = visibleBounds();
    const auto list = guides();
    painter.save(); painter.setRenderHint(QPainter::Antialiasing);
    // Static assistants reuse a viewport image during cursor motion and brush
    // dabs. While editing, only the selected assistant is drawn live; moving its
    // handles does not rebuild either the other assistants or artwork pixels.
    if (global.visible) {
        QVector<DrawingGuide> background;
        for (const auto &guide : list)
            if (guide.visible && !(editing && guide.id == selectedId)) background.append(guide);
        if (!background.isEmpty()) painter.drawImage(QPointF(), cachedLines(m_backgroundCache, background, global, mapping, bounds));
    }
    if (global.visible) for (const auto &guide : list) {
        if (!guide.visible) continue;
        if (editing && guide.id == selectedId) {
            if (dragging()) {
                // Avoid the high-DPI antialiased stroke cost during pointer
                // movement; restore the crisp cached final rendering on release.
                painter.setRenderHint(QPainter::Antialiasing, false);
                paintGuideLines(painter, guide, global, mapping, bounds);
                painter.setRenderHint(QPainter::Antialiasing, true);
            } else painter.drawImage(QPointF(), cachedLines(m_selectedCache, {guide}, global, mapping, bounds, true));
        }
        if (!editing || !global.handles) continue;
        const bool selected = guide.id == selectedId;
        painter.setPen(QPen(QColor(selected ? ui::focus : ui::muted), 1.5)); painter.setBrush(QColor(ui::base));
        for (auto point : guide.points) {
            const auto location = mapping.map(point);
            if (guide.locked) painter.drawRect(QRectF(location - QPointF(4,4), QSizeF(8,8)));
            else painter.drawEllipse(location, selected ? 5 : 4, selected ? 5 : 4);
        }
        if (selected && guide.limited) {
            painter.setPen(QPen(QColor(ui::focus), 1, Qt::DashLine)); painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(mapping.map(QPolygonF(guide.area)));
            painter.setBrush(QColor(ui::panel));
            for (auto point : {guide.area.topLeft(), guide.area.bottomRight()}) painter.drawRect(QRectF(mapping.map(point) - QPointF(5,5), QSizeF(10,10)));
        }
        const auto move = moveHandle(guide);
        painter.setPen(QPen(QColor(selected ? ui::focus : ui::muted), 1)); painter.setBrush(QColor(ui::panel));
        painter.drawRoundedRect(QRectF(move - QPointF(8,8), QSizeF(16,16)), 3,3);
        painter.drawLine(move - QPointF(5,0), move + QPointF(5,0)); painter.drawLine(move - QPointF(0,5), move + QPointF(0,5));
    }
    if (creating && editing) {
        if ((creationType == GuideType::Fisheye || creationType == GuideType::ThreePoint) && m_pending.size() + 1 == guidePointCount(creationType)) {
            DrawingGuide preview; preview.type = creationType; preview.points = m_pending;
            preview.points.append(m_canvas->artworkPoint(m_hover)); alignFisheyeHeight(preview, m_hoverModifiers.testFlag(Qt::ShiftModifier));
            if (validGuideGeometry(preview)) paintGuideLines(painter, preview, global, mapping, bounds);
        }
        painter.setPen(QPen(QColor(ui::focus), 1.5, Qt::DashLine)); painter.setBrush(Qt::NoBrush);
        QPainterPath path;
        if (!m_pending.isEmpty()) { path.moveTo(mapping.map(m_pending.first())); for (int i=1;i<m_pending.size();++i) path.lineTo(mapping.map(m_pending[i])); path.lineTo(m_hover); }
        painter.drawPath(path);
        for (auto point : m_pending) painter.drawEllipse(mapping.map(point), 5,5);
        const auto message = QString("%1 · %2/%3").arg(guideTypeName(creationType)).arg(m_pending.size()+1).arg(guidePointCount(creationType));
        painter.setPen(QColor(ui::text)); painter.drawText(m_hover + QPointF(14,-14), message);
    }
    if (m_snapLine) {
        painter.setPen(QPen(QColor(ui::focus), 1, Qt::DashLine)); painter.setBrush(Qt::NoBrush);
        QPainterPath clip;
        clip.addRect(m_snapLine->area ? bounds.intersected(*m_snapLine->area) : bounds);
        painter.setClipPath(mapping.map(clip), Qt::IntersectClip);
        paintPairedPath(painter, mapping.map(guideConstraintPath(m_snapLine->constraint, bounds,
            detailTolerance(mapping, painter.device()->devicePixelRatioF()))), m_snapLine->constraint.kind == GuideConstraint::Kind::Ellipse);
    }
    painter.restore();
}
void GuideTool::press(QPointF position, Qt::KeyboardModifiers modifiers) {
    m_hover = position; m_hoverModifiers = modifiers;
    if (creating) {
        m_pending.append(bounded(m_canvas->artworkPoint(position)));
        if (m_pending.size() == guidePointCount(creationType)) {
            DrawingGuide guide; guide.type = creationType; guide.points = m_pending;
            alignFisheyeHeight(guide, modifiers.testFlag(Qt::ShiftModifier));
            if (!validGuideGeometry(guide)) {
                m_pending.removeLast();
                const auto message = creationType == GuideType::Fisheye ? "Place distinct axis endpoints and a height handle away from the axis."
                    : creationType == GuideType::ThreePoint ? "Keep three vanishing points off one line, and the grid anchor off their connecting lines."
                    : "Place distinct handles around a convex perspective plane; keep the two-point anchor off the horizon.";
                emit m_canvas->document()->warning(message);
            }
            else { cancel(); appendGuide(guide); }
        }
        emit m_canvas->guideCreationChanged(); m_canvas->update(); return;
    }
    if (!settings().visible) return;
    auto list = guides(); const auto mapping = transform();
    // Selected handles take precedence when assistants overlap.
    for (int i=0;i<list.size();++i) if (list[i].id == selectedId) { list.append(list.takeAt(i)); break; }
    for (int i=list.size()-1;i>=0;--i) {
        const auto &guide = list[i]; if (!guide.visible) continue;
        const int handle = handleAt(guide, position);
        QPainterPathStroker stroker; stroker.setWidth(10);
        if (handle != -1 || stroker.createStroke(mapping.map(guidePath(guide,visibleBounds(), detailTolerance(mapping)))).contains(position)) {
            select(guide.id);
            if (handle != -1 && guide.locked)
                emit m_canvas->document()->warning("Unlock this guide in Tool Properties to move its handles.");
            if (handle != -1 && !guide.locked) { m_before=guide; m_preview=guide; m_dragHandle=handle; m_dragStart=m_canvas->artworkPoint(position); }
            m_canvas->updateSelectionUi(); return;
        }
    }
    select({});
}
void GuideTool::move(QPointF position, Qt::KeyboardModifiers modifiers) {
    m_hover=position; m_hoverModifiers = modifiers; if (creating) { m_canvas->update(); return; }
    if (!dragging()) return;
    auto guide=m_before; auto point=bounded(m_canvas->artworkPoint(position));
    if ((modifiers & Qt::ShiftModifier) && !(guide.type == GuideType::Fisheye && m_dragHandle == 2)) {
        const auto delta=point-m_dragStart;
        if (qAbs(delta.x()) > qAbs(delta.y())) point.setY(m_dragStart.y()); else point.setX(m_dragStart.x());
    }
    if (m_dragHandle == -2) { const auto delta=point-m_dragStart; for(auto &p:guide.points) p=bounded(p+delta); guide.area.translate(delta); }
    else if (m_dragHandle < guide.points.size()) {
        guide.points[m_dragHandle]=point;
        if (guide.type == GuideType::Fisheye) {
            if (m_dragHandle < 2) {
                const auto oldAxis = m_before.points[1] - m_before.points[0], newAxis = guide.points[1] - guide.points[0];
                const auto oldLength = std::hypot(oldAxis.x(), oldAxis.y()), newLength = std::hypot(newAxis.x(), newAxis.y());
                if (oldLength > 1e-6 && newLength > 1e-6) {
                    const auto radius = QPointF::dotProduct(m_before.points[2] - center(m_before), QPointF(-oldAxis.y(), oldAxis.x()) / oldLength);
                    guide.points[2] = center(guide) + QPointF(-newAxis.y(), newAxis.x()) * (radius / newLength);
                }
            }
            alignFisheyeHeight(guide, m_dragHandle == 2 && modifiers.testFlag(Qt::ShiftModifier));
        }
    }
    else {
        const auto other=m_dragHandle == guide.points.size() ? guide.area.bottomRight() : guide.area.topLeft();
        guide.area=QRectF(point,other).normalized();
        if (guide.area.width()<1 || guide.area.height()<1) return;
    }
    if (validGuideGeometry(guide) && validateGuides({guide},settings()).isEmpty()) { m_preview=guide; m_canvas->update(); }
}
void GuideTool::release(QPointF position, Qt::KeyboardModifiers modifiers) {
    if (!dragging()) return;
    move(position,modifiers); const auto result=m_preview;
    m_preview.reset(); m_dragHandle=-1;
    if (result) updateGuide(*result);
    m_canvas->updateSelectionUi(); m_canvas->update();
}
void GuideTool::beginSnap(QPointF point) {
    endSnap(); m_snapStart=point; m_lastStrokePoint=point;
    const auto global=settings(); if (!global.snapping) return;
    for (const auto &guide : guides()) if (guide.snapping) {
        const auto area = guide.limited ? std::optional<QRectF>(guide.area) : std::nullopt;
        for (const auto &constraint : guideSnapConstraints(guide, point)) m_snapCandidates.append({constraint, area});
    }
}
std::optional<QPointF> GuideTool::snap(QPointF point) {
    m_snapActive = false;
    if (m_snapCandidates.isEmpty()) return point;
    if (!m_snapLine) {
        if (QLineF(m_snapStart,point).length()*m_canvas->artworkZoom() < 3) return std::nullopt;
        qreal distance=std::numeric_limits<qreal>::max();
        for (const auto &candidate : m_snapCandidates) {
            if (candidate.area && (!candidate.area->contains(point) || !candidate.area->contains(projectOntoGuideConstraint(point, candidate.constraint)))) continue;
            const qreal error = guideConstraintDistance(point, candidate.constraint);
            if (error < distance) { distance = error; m_snapLine = candidate; }
        }
    }
    if (!m_snapLine) return point;
    const auto projected = projectOntoGuideConstraint(point, m_snapLine->constraint);
    if (m_snapLine->area && (!m_snapLine->area->contains(point) || !m_snapLine->area->contains(projected))) return point;
    m_snapActive = true; return projected;
}
QVector<QPointF> GuideTool::strokePoints(QPointF point) {
    QVector<QPointF> result;
    if (m_snapActive && m_snapLine && m_snapLine->constraint.kind == GuideConstraint::Kind::Ellipse) {
        const auto projected = projectOntoGuideConstraint(point, m_snapLine->constraint);
        if (!m_snapLine->area || (m_snapLine->area->contains(point) && m_snapLine->area->contains(projected))) {
            if (guideConstraintDistance(m_lastStrokePoint, m_snapLine->constraint) < .01) {
                result = guideConstraintSamples(m_snapLine->constraint, m_lastStrokePoint, projected,
                    qMin(.2, detailTolerance(transform(), m_canvas->devicePixelRatioF())));
                // A curved arc can leave a rectangular limit even if both ends
                // are inside. Release that portion to normal freehand drawing.
                if (m_snapLine->area) for (const auto p : result) if (!m_snapLine->area->contains(p)) { result.clear(); break; }
            }
            if (result.isEmpty()) result.append(projected);
        }
    }
    if (result.isEmpty()) result.append(point);
    m_lastStrokePoint = result.last(); return result;
}
void GuideTool::endSnap() { m_snapCandidates.clear(); m_snapLine.reset(); m_snapActive = false; }
} // namespace toon
