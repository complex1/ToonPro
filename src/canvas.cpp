#include "scene3d_controller.h"
#include "reference3d_canvas.h"
#include "reference3d_render.h"
#include "feature_flags.h"
#include "scene3d_cache.h"
#include "drawing_tool_controller.h"
#include "layer_panel.h"
#include "canvas.h"
#include "symbols.h"
#include "hold_shape_tool.h"
#include "keyframe_transform_tool.h"
#include "camera_tool.h"
#include "mask_tool.h"
#include "effect_render.h"
#include "effects_preview_renderer.h"
#include "keyframe_render.h"
#include "animation_render.h"
#include "timeline_solo.h"
#include "guide_tool.h"
#include <QHelpEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QToolTip>
#include "ui_theme.h"
#include "brush_engine.h"
#include "selection_context_bar.h"
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QPainter>
#include <QWheelEvent>
#include <cmath>
#include <QTabletEvent>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QEnterEvent>
#include <QApplication>
#include <QHoverEvent>
#include <QMainWindow>
#include <QWindow>
#include <QScopeGuard>
#include <QLineF>
#include <QHideEvent>
#include <QShowEvent>
#include <QPainterPathStroker>
#include <QDataStream>
#include <QIODevice>
#include <QtMath>
#include <array>
#include <utility>

namespace toon {
namespace {
// A held enable key can activate a filter even when its stored base is off.
// Inspect nested symbol timelines too so compositing never falls back onto the
// GUI thread just because a different frame was inactive.
bool shotHasCompositing(const Project &project, const Shot &shot) {
    QSet<QString> visited;
    std::function<bool(const Shot &)> check = [&](const Shot &content) {
        if (layerStackHasEffects(content.layers)) return true;
        for (const auto &animation : content.layerAnimations)
            for (const auto &channel : animation.channels)
                if (!channel.keys.isEmpty() &&
                    (keyframePropertyIsEffect(channel.property) || keyframePropertyIsMask(channel.property))) return true;
        for (const auto &layer : content.layers) if (layer.symbol && !visited.contains(layer.symbol->symbolId)) {
            visited.insert(layer.symbol->symbolId);
            if (const auto *definition = findSymbol(project, layer.symbol->symbolId))
                if (check(definition->content)) return true;
        }
        return false;
    };
    return check(shot);
}

// Trace the coverage boundary, including holes, without outlining individual pixels.
QPainterPath maskOutline(const QImage &image) {
    QPainterPath path;
    if (image.isNull()) return path;
    // Large textured stamps can contain hundreds of thousands of tiny edges.
    // Bound cursor tracing only; artwork and paint coverage stay full resolution.
    const auto sampled = qMax(image.width(), image.height()) > 384
        ? image.scaled({384,384},Qt::KeepAspectRatio,Qt::SmoothTransformation) : image;
    const auto mask = sampled.convertToFormat(QImage::Format_Grayscale8);
    const int w = mask.width(), h = mask.height(), stride = w + 1;
    int peak = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) peak = qMax(peak, int(mask.constScanLine(y)[x]));
    if (!peak) return path;
    const int threshold = qMax(1, qRound(peak * .2));
    auto inside = [&](int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && mask.constScanLine(y)[x] >= threshold; };
    QVector<quint8> edges((w + 1) * (h + 1), 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) if (inside(x, y)) {
        if (!inside(x, y - 1)) edges[y * stride + x] |= 1; // Right.
        if (!inside(x + 1, y)) edges[y * stride + x + 1] |= 2; // Down.
        if (!inside(x, y + 1)) edges[(y + 1) * stride + x + 1] |= 4; // Left.
        if (!inside(x - 1, y)) edges[(y + 1) * stride + x] |= 8; // Up.
    }
    const int step[] = {1, stride, -1, -stride};
    for (int start = 0; start < edges.size(); ++start) while (edges[start]) {
        int current = start, direction = 0;
        while (!(edges[current] & (1 << direction))) ++direction;
        path.moveTo(start % stride, start / stride);
        do {
            edges[current] &= ~(1 << direction);
            current += step[direction];
            if (current == start) break;
            int next = -1;
            // Keep filled pixels on the right at diagonal corner contacts.
            for (int turn : {1, 0, 3, 2}) if (edges[current] & (1 << ((direction + turn) % 4))) {
                next = (direction + turn) % 4; break;
            }
            if (next < 0) break;
            if (next != direction) path.lineTo(current % stride, current / stride);
            direction = next;
        } while (current != start);
        path.lineTo(current % stride, current / stride); path.closeSubpath();
    }
    path.setFillRule(Qt::OddEvenFill);
    QTransform scale;scale.scale(qreal(image.width())/w,qreal(image.height())/h);
    return scale.map(path.translated(-w / 2.0, -h / 2.0));
}
BrushInput interpolateInput(const BrushInput &a, const BrushInput &b, qreal t) {
    BrushInput value;
    value.pressure = a.pressure + (b.pressure - a.pressure) * t;
    value.tiltX = a.tiltX + (b.tiltX - a.tiltX) * t;
    value.tiltY = a.tiltY + (b.tiltY - a.tiltY) * t;
    value.rotation = a.rotation + std::remainder(b.rotation - a.rotation, 360.0) * t;
    value.wheel = a.wheel + (b.wheel - a.wheel) * t;
    value.speed = a.speed + (b.speed - a.speed) * t;
    return value;
}
void paintDetailedSelectionBoundary(QPainter &painter, const QPainterPath &path) {
    // Qt's antialiased wide-path stroker becomes very expensive when the
    // device scale turns a one-pixel pen into two physical pixels. Keep whole
    // contours together in small batches so their joins stay unchanged.
    QPainterPath contour, batch;
    const auto flushContour = [&] {
        if (contour.elementCount() > 2) {
            const auto start = contour.elementAt(0);
            if (contour.currentPosition() == QPointF(start.x, start.y)) contour.closeSubpath();
        }
        if (contour.elementCount() > 4096) {
            // A single long contour cannot be bounded by subpath batching.
            // Flatten curves at display resolution and rasterize its edges
            // directly; this retains antialiasing without a giant stroke mesh.
            QVector<QLineF> edges;
            const auto device = painter.deviceTransform(), logical = device.inverted();
            for (const auto &polygon : contour.toSubpathPolygons(device))
                for (int i = 1; i < polygon.size(); ++i)
                    edges.append(QLineF(logical.map(polygon[i - 1]), logical.map(polygon[i])));
            painter.drawLines(edges);
        } else {
            if (batch.elementCount() + contour.elementCount() > 256) {
                painter.drawPath(batch); batch = {};
            }
            batch.addPath(contour);
        }
        contour = {};
    };
    for (int i = 0; i < path.elementCount(); ++i) {
        const auto element = path.elementAt(i);
        if (element.isMoveTo()) {
            flushContour(); contour.moveTo(element.x, element.y);
        } else if (element.isLineTo()) contour.lineTo(element.x, element.y);
        else if (element.type == QPainterPath::CurveToElement && i + 2 < path.elementCount()) {
            const auto control = path.elementAt(++i), end = path.elementAt(++i);
            contour.cubicTo(element.x, element.y, control.x, control.y, end.x, end.y);
        }
    }
    flushContour(); painter.drawPath(batch);
}
QVector<QLineF> brushCursorEdges(const QPainter &painter, const QPainterPath &path) {
    // Texture boundaries can contain tens of thousands of short edges. Qt's
    // wide-path stroker builds expensive intersecting meshes even for small
    // batches of these contours. Rasterize their edges directly at display
    // resolution, retaining every contour and hole in the tip shape.
    QVector<QLineF> edges;
    edges.reserve(path.elementCount());
    const auto device = painter.deviceTransform(), logical = device.inverted();
    for (const auto &polygon : path.toSubpathPolygons(device))
        for (int i = 1; i < polygon.size(); ++i)
            edges.append(QLineF(logical.map(polygon[i - 1]), logical.map(polygon[i])));
    return edges;
}
QImage cursorBoundary(const QImage &alpha, qreal radius) {
    // A centered outline is dilation minus erosion of the filled silhouette.
    // Separable extrema keep work proportional to visible pixels rather than
    // contour intersections. Interpolated radii retain half-pixel coverage at
    // 1x and fractional display scales instead of rounding the stroke width.
    const int inner = int(std::floor(radius)), outer = int(std::ceil(radius));
    const qreal fraction = radius - inner;
    QImage minimum(alpha.size(), QImage::Format_Grayscale8);
    QImage maximum(alpha.size(), QImage::Format_Grayscale8);
    QImage result(alpha.size(), QImage::Format_Grayscale8);
    if (minimum.isNull() || maximum.isNull() || result.isNull()) return {};
    const int width = alpha.width(), height = alpha.height();
    const auto interpolate = [fraction](int a, int b) { return uchar(qRound(a + (b - a) * fraction)); };
    for (int y = 0; y < height; ++y) {
        const auto *source = alpha.constScanLine(y);
        auto *low = minimum.scanLine(y), *high = maximum.scanLine(y);
        for (int x = 0; x < width; ++x) {
            int lo = 255, hi = 0;
            for (int dx = -inner; dx <= inner; ++dx) {
                const int value = source[qBound(0, x + dx, width - 1)];
                lo = qMin(lo, value); hi = qMax(hi, value);
            }
            const int a = source[qMax(0, x - outer)], b = source[qMin(width - 1, x + outer)];
            low[x] = interpolate(lo, qMin(lo, qMin(a, b)));
            high[x] = interpolate(hi, qMax(hi, qMax(a, b)));
        }
    }
    for (int y = 0; y < height; ++y) {
        // The caller caps the radius at eight physical pixels.
        std::array<const uchar *, 17> lows{}, highs{};
        for (int dy = -outer; dy <= outer; ++dy) {
            const int row = qBound(0, y + dy, height - 1);
            lows[dy + outer] = minimum.constScanLine(row);
            highs[dy + outer] = maximum.constScanLine(row);
        }
        auto *target = result.scanLine(y);
        for (int x = 0; x < width; ++x) {
            int lo = 255, hi = 0;
            for (int dy = -inner; dy <= inner; ++dy) {
                lo = qMin(lo, int(lows[dy + outer][x]));
                hi = qMax(hi, int(highs[dy + outer][x]));
            }
            const int expanded = interpolate(hi, qMax(hi, qMax(int(highs[0][x]), int(highs[2 * outer][x]))));
            const int contracted = interpolate(lo, qMin(lo, qMin(int(lows[0][x]), int(lows[2 * outer][x]))));
            target[x] = uchar(qMax(0, expanded - contracted));
        }
    }
    return result;
}
}
// Independent overlay animation never invalidates the artwork cache. Qt may
// propagate a transparent child's update to its parent; Canvas then only blits
// its cached viewport, rather than compositing the document again.
class CanvasSelectionOverlay final : public QWidget {
public:
    explicit CanvasSelectionOverlay(Canvas *canvas) : QWidget(canvas), m_canvas(canvas), m_timer(this) {
        setObjectName("selectionOverlay");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        m_timer.setObjectName("selectionAntsTimer");
        m_timer.setInterval(60);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            if (m_canvas->document()->strokeActive()) return;
            m_phase = (m_phase + 1) % 8;
            update(m_antRegion);
        });
    }
    void synchronize() {
        const auto selection = m_canvas->viewportSelectionPath();
        QPainterPath drawable;
        if (m_canvas->hasProject()) {
            drawable.addRect(m_canvas->document()->drawingBounds());
            drawable = m_canvas->artworkViewTransform().map(drawable);
        }
        const qreal ratio = devicePixelRatioF();
        const bool focus = m_canvas->selectionFocusOverlay()
            && (m_canvas->document()->rasterSelection().active || m_canvas->m_selecting);
        if (selection != m_path || drawable != m_drawable || focus != m_focus
            || m_size != size() || m_ratio != ratio) {
            m_path = selection; m_drawable = drawable; m_focus = focus;
            m_size = size(); m_ratio = ratio;
            if (focus && !drawable.isEmpty() && !size().isEmpty()) {
                // One bounded screen-resolution mask, rebuilt only when geometry
                // changes. Odd-even selection holes remain part of the outside.
                const QSize pixels(qCeil(width() * ratio), qCeil(height() * ratio));
                if (m_shade.size() != pixels || m_shade.devicePixelRatio() != ratio) {
                    m_shade = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
                    m_shade.setDevicePixelRatio(ratio);
                }
                m_shade.fill(Qt::transparent);
                if (m_hatch.isNull() || m_hatch.devicePixelRatio() != ratio) {
                    m_hatch = QImage(QSize(qCeil(9 * ratio), qCeil(9 * ratio)), QImage::Format_ARGB32_Premultiplied);
                    m_hatch.setDevicePixelRatio(ratio); m_hatch.fill(QColor(13, 18, 27, 42));
                    QPainter pattern(&m_hatch); pattern.setPen(QPen(QColor(9, 14, 23, 37), 1));
                    for (int x : {-9, 0, 9}) pattern.drawLine(QPointF(x, 9), QPointF(x + 9, 0));
                }
                QPainter shade(&m_shade);
                // Color-range selections can contain thousands of separate
                // islands. Composite their coverage instead of constructing an
                // expensive boolean difference between every contour.
                shade.fillPath(drawable, QBrush(m_hatch));
                shade.setCompositionMode(QPainter::CompositionMode_DestinationOut);
                shade.fillPath(selection, Qt::black);
            } else m_shade = {};
            // A screen-space rectangle is a conservative repaint region; unlike
            // a stroked/flattened polygon it has bounded construction cost even
            // for dense stipple or photographic color-range selections.
            m_antRegion = QRegion(selection.controlPointRect().adjusted(-3, -3, 3, 3).toAlignedRect().intersected(rect()));
            if (selection.elementCount() <= 256 && !m_antRegion.isEmpty()) {
                // Keep ordinary rectangles/lassos confined to their thin border
                // during animation, without applying this geometry work to a
                // potentially enormous set of color-range islands.
                QPainterPathStroker stroker; stroker.setWidth(5);
                QPainterPath viewport; viewport.addRect(QRectF(rect()).adjusted(-2, -2, 2, 2));
                const auto stroke = stroker.createStroke(selection).intersected(viewport);
                m_antRegion = QRegion(stroke.toFillPolygon().toPolygon()).intersected(rect());
            }
            m_antMask = {}; m_antFrame = {}; m_antFramePhase = -1;
            if (selection.elementCount() > 4096 && !m_antRegion.isEmpty()) {
                const QSize pixels(qCeil(width() * ratio), qCeil(height() * ratio));
                m_antMask = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
                m_antMask.setDevicePixelRatio(ratio); m_antMask.fill(Qt::transparent);
                QPainter boundary(&m_antMask); boundary.setRenderHint(QPainter::Antialiasing);
                boundary.setBrush(Qt::NoBrush); boundary.setPen(QPen(Qt::white, 1));
                paintDetailedSelectionBoundary(boundary, selection);
                boundary.end();
                m_antFrame = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
                m_antFrame.setDevicePixelRatio(ratio);
                if (m_antPattern.isNull() || m_antPattern.devicePixelRatio() != ratio) {
                    m_antPattern = QImage(QSize(qCeil(8 * ratio), qCeil(8 * ratio)), QImage::Format_ARGB32_Premultiplied);
                    m_antPattern.setDevicePixelRatio(ratio); m_antPattern.fill(Qt::white);
                    QPainter pattern(&m_antPattern); pattern.setPen(Qt::NoPen); pattern.setBrush(QColor(15, 18, 22));
                    for (int x : {-8, 0, 8})
                        pattern.drawPolygon(QPolygonF{{qreal(x), 8}, {qreal(x + 4), 8}, {qreal(x + 12), 0}, {qreal(x + 8), 0}});
                }
            }
            update();
        } else if (m_canvas->document()->transformActive()) update();
        const bool animate = m_canvas->selectionAnimated() && m_canvas->isVisible() && !selection.isEmpty() && !m_antRegion.isEmpty();
        if (animate && !m_timer.isActive()) m_timer.start();
        else if (!animate) m_timer.stop();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        if (!m_shade.isNull()) { painter.setOpacity(m_canvas->selectionShadeOpacity()); painter.drawImage(QPointF(), m_shade); painter.setOpacity(1); }
        if (!m_antMask.isNull()) {
            // Rasterize the intricate boundary once per selection/view change.
            // Animation thereafter only changes a small repeating pattern over
            // that cached boundary, never strokes thousands of paths again.
            if (m_antFramePhase != m_phase) {
                m_antFrame.fill(Qt::transparent);
                QPainter ants(&m_antFrame);
                ants.drawImage(QPointF(), m_antMask);
                ants.setCompositionMode(QPainter::CompositionMode_SourceIn);
                ants.setBrushOrigin(m_phase, 0);
                ants.fillRect(rect(), QBrush(m_antPattern));
                ants.end(); m_antFramePhase = m_phase;
            }
            painter.drawImage(QPointF(), m_antFrame);
        } else if (!m_path.isEmpty()) {
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(Qt::white, 1)); painter.drawPath(m_path);
            QPen ants(QColor(15, 18, 22), 1);
            ants.setDashPattern({4, 4}); ants.setDashOffset(m_phase);
            painter.setPen(ants); painter.drawPath(m_path);
        }
        m_canvas->paintTransformHandles(painter);
        m_canvas->drawingTools()->paint(painter);
    }
private:
    Canvas *m_canvas;
    QTimer m_timer;
    QPainterPath m_path, m_drawable;
    QImage m_shade, m_hatch, m_antMask, m_antFrame, m_antPattern;
    QRegion m_antRegion;
    QSize m_size;
    qreal m_ratio = 0;
    int m_phase = 0;
    int m_antFramePhase = -1;
    bool m_focus = false;
};
Canvas::Canvas(QWidget *parent) : QWidget(parent), m_document(new Document(this)) {
    m_effectsPreview = new EffectsPreviewRenderer(this);
    setProperty("effectsPreviewPending", false);
    setProperty("effectsPreviewPublishedRevision", qulonglong(0));
    setProperty("effectsPreviewSourceScale", 1.0);
    setProperty("effectsPreviewExact", true);
    setProperty("effectsPreviewFrame", -1);
    connect(m_effectsPreview, &EffectsPreviewRenderer::completed, this, [this](const EffectsPreviewResult &result) {
        if (result.cancelled || result.context != m_effectsPreviewContext ||
            result.revision < m_effectsPreviewPublishedRevision || !hasProject()) return;
        const bool exact = result.quality == EffectsPreviewQuality::Exact && result.sourceScale == 1;
        if (result.quality == EffectsPreviewQuality::Exact && result.revision != m_effectsPreviewRevision && !(result.playback && m_animationPlayback)) return;
        if (!result.image.isNull() && result.error.isEmpty()) {
            m_artworkCache = result.image;
            m_effectsPreviewPublishedRevision = result.revision;
            ++m_artworkRenderCount;
            m_artworkPaintedPixelCount += quint64(result.image.width()) * result.image.height();
            setProperty("effectsPreviewPublishedRevision", qulonglong(result.revision));
            setProperty("effectsPreviewSourceScale", result.sourceScale);
            setProperty("effectsPreviewExact", exact);
            setProperty("effectsPreviewFrame", result.frame);
            setPlaybackPreviewFallbackReason(result.playback ? result.playbackFallbackReason : QString());
        }
        // Obsolete drafts may advance the visible preview, but may not report
        // a stale failure or declare the newest authoritative render finished.
        if (result.revision == m_effectsPreviewRevision && !m_artworkCacheDirty && !m_strokeArtworkUpdatePending) {
            if (result.error != m_animationRenderError) {
                m_animationRenderError = result.error;
                if (!result.error.isEmpty()) emit m_document->warning(result.error);
            }
            if (exact || result.playback) {
                setEffectsPreviewPending(false);
                if (!result.playback && exact && !shotHasCompositing(project(), shot())) m_effectsPreviewActive = false;
            }
        }
        update();
    });
    m_drawingTools = new DrawingToolController(this);
    m_guides = new GuideTool(this);
    m_scene3D = new Scene3DController(this);
    m_reference3D = new Reference3DCanvasController(m_document,this);
    m_reference3D->setLayerVisibilityFilter([this](const QString &id){return timelineSoloIncludesLayer(id);});
    connect(m_reference3D,&Reference3DCanvasController::previewChanged,this,[this]{invalidateArtworkCache();update();});
    connect(m_reference3D,&Reference3DCanvasController::warning,m_document,&Document::warning);
    m_keyframeTransformTool = new KeyframeTransformTool(this);
    m_cameraTool = new CameraTool(this);
    m_maskTool = new MaskTool(this);
    m_holdShape = new HoldShapeTool(this);
    m_selectionOverlay = new CanvasSelectionOverlay(this);
    m_selectionBar = new SelectionContextBar(this);
    connect(m_document, &Document::strokeUpdated, this, [this] {
        if (!m_effectsStrokeWasActive) {
            m_effectsStrokeWasActive = true;
            invalidateEffectsPreviewContext();
        }
        // Keep synchronous pen delivery limited to painting. Broad document
        // notifications also rebuild timeline/effect controls and thumbnails,
        // and cause unrelated tools to request full-canvas repaints.
        scheduleStrokeArtworkUpdate();
    });
    connect(m_document, &Document::shotChanged, this, [this] {
        synchronizeTimelineSolo(true);
        invalidateEffectsPreviewContext();
        m_artworkCache = {};
        m_airbrushTimer.stop(); m_strokeFilter.reset();
        resetStrokeConstraint();
        m_holdShape->reset();
        m_tablet = false; m_dragging = false; m_space = false; m_input = {};
        cancelOverlayDrag();
        m_guides->cancel(); m_guides->endSnap(); m_guides->select({});
        m_scene3D->reset();
        m_cameraTool->setEnabled(false); m_keyframeTransformTool->setEnabled(false); m_maskTool->setEnabled(false);
        m_onionCache = {}; m_animationDisplayKey.clear();
        m_strokeLayerId.clear(); m_strokeTileRevisions.clear();
        invalidateArtworkCache(); refreshPointerCursor(); updateSelectionUi();
        emit viewChanged();
    });
    connect(m_document, &Document::changed, this, [this] {
        synchronizeTimelineSolo();
        const bool editingEffects = m_document->effectsEditActive() || m_document->keyframeEditActive();
        const bool stroking = m_document->strokeActive();
        // Values within one edit can publish increasingly recent previews.
        // Ending/cancelling an edit, undoing, or changing other artwork starts
        // a new context, so no previous draft can be installed afterwards.
        // Retouch emits changed per dab instead of strokeUpdated. Keep those
        // samples in one context too, so a running draft can make progress.
        if (stroking ? !m_effectsStrokeWasActive : (!editingEffects || !m_effectsEditWasActive))
            invalidateEffectsPreviewContext();
        m_effectsEditWasActive = editingEffects;
        m_effectsStrokeWasActive = stroking;
        if (!m_document->strokeActive()) m_onionCache = {};
        m_transformFailure.clear();
        if (m_scene3D->changingRestSkeleton()) {
            // Rest joints are a screen overlay; the mesh and artwork cache are unchanged.
            refreshPointerCursor();
        } else if (m_document->strokeActive()) {
            scheduleStrokeArtworkUpdate();
            return;
        } else {
            m_airbrushTimer.stop();
            resetStrokeConstraint();
            m_holdShape->reset();
            m_strokeLayerId.clear(); m_strokeTileRevisions.clear();
            invalidateArtworkCache(); refreshPointerCursor();
        }
        emit documentChanged();
    });
    connect(m_document, &Document::loaded, this, [this] { synchronizeTimelineSolo(true); });
    connect(m_document, &Document::frameChanged, this, [this] {
        // Playback advances faster than heavy frames can render. Keep this
        // playback context alive and replace only pending work; a completed
        // frame must reach the screen before the next one begins.
        if (!m_animationPlayback) invalidateEffectsPreviewContext();
        const auto key = animationDisplayKey(displayShot(), m_document->currentFrame(),
            m_onionSkin ? m_document->activeAnimationTrackId() : QString(), m_onionSettings);
        if (key != m_animationDisplayKey) { m_onionCache = {}; invalidateArtworkCache(); emit frameViewChanged(); }
        m_strokeLayerId.clear(); m_strokeTileRevisions.clear();
        refreshPointerCursor(); updateSelectionUi();
    });
    connect(m_document, &Document::animationSelectionChanged, this, [this] {
        if (m_onionSkin && animationDisplayKey(displayShot(), m_document->currentFrame(),
            m_document->activeAnimationTrackId(), m_onionSettings) != m_animationDisplayKey) { m_onionCache = {}; invalidateArtworkCache(); }
    });
    const auto contextChanged = [this] { m_transformFailure.clear(); refreshPointerCursor(); };
    connect(m_document, &Document::rasterSelectionChanged, this, contextChanged);
    connect(m_document, &Document::selectionChanged, this, contextChanged);
    connect(m_document, &Document::selectionChanged, this, [this] {
        // Frame-by-frame animation remaps the active cel on every exposure.
        // During playback this selection is not an artwork/view change.
        if (!m_animationPlayback) invalidateEffectsPreviewContext();
    });
    connect(m_drawingTools, &DrawingToolController::changed, this, &Canvas::refreshPointerCursor);
    connect(m_scene3D, &Scene3DController::changed, this, [this] {
        if (m_tool == Tool::SceneRig) refreshPointerCursor();
    });
    connect(this, &Canvas::toolStateApplied, this, &Canvas::refreshPointerCursor);
    connect(this, &Canvas::guideCreationChanged, this, &Canvas::refreshPointerCursor);
    connect(m_document, &Document::transformChanged, this, [this] {
        invalidateEffectsPreviewContext();
        if (!m_document->transformActive()) {
            m_transformDrag = TransformDrag::None; m_stampContact = false;
            m_stampTransformTimer.stop(); m_pendingStampTransform.reset();
        }
        invalidateArtworkCache(); refreshPointerCursor(); emit documentChanged();
    });
    connect(m_document, &Document::transformApplyRequested, this, &Canvas::flushStampTransform);
    connect(this, &Canvas::viewChanged, this, [this] { m_onionCache = {}; invalidateEffectsPreviewContext(); invalidateArtworkCache(); updateSelectionUi(); });
    m_airbrushTimer.setTimerType(Qt::PreciseTimer);
    m_straightStrokeTimer.setSingleShot(true);
    m_straightStrokeTimer.setInterval(16);
    m_straightStrokeTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_straightStrokeTimer, &QTimer::timeout, this, &Canvas::flushStraightStroke);
    m_stampTransformTimer.setSingleShot(true); m_stampTransformTimer.setInterval(16);
    m_stampTransformTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_stampTransformTimer, &QTimer::timeout, this, &Canvas::flushStampTransform);
    connect(&m_airbrushTimer, &QTimer::timeout, this, [this] {
        if (!m_document->strokeActive() || !m_strokeFilter.active()) { m_airbrushTimer.stop(); return; }
        const auto cursorBefore = brushCursorDamageRect();
        m_document->continueStroke(m_paintedTip, m_lastPaintInput);
        m_document->airbrushTick();
        update(cursorBefore.united(brushCursorDamageRect()).adjusted(-3, -3, 3, 3).toAlignedRect());
    });
    setObjectName("canvas");
    // The viewport cache covers every pixel with an opaque canvas background.
    // Avoid clearing/painting the parent behind it on every pen repaint.
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_TabletTracking);
    qApp->installEventFilter(this);
    setCursor(Qt::ArrowCursor);
    setAccessibleName("Production sheet canvas");
}

Canvas::~Canvas() {
    for (const auto &connection : std::as_const(m_externalViewGestures)) disconnect(connection);
    m_externalViewGestures.clear();
    m_reference3D->finishInteraction();m_reference3D->disconnect();
    m_effectsPreview->invalidate();
    m_effectsPreview->disconnect(this);
    // Preview cancellation and history destruction emit signals. Stop their
    // UI callbacks before consulting a window whose children are being deleted.
    m_document->history()->disconnect();
    m_document->disconnect();
    m_keyframeTransformTool->disconnect(); m_keyframeTransformTool->cancel();
    m_maskTool->disconnect(); m_maskTool->cancel();
    m_cameraTool->disconnect(); m_cameraTool->cancel();
    m_scene3D->disconnect();
    m_scene3D->cancelEdit();
    m_drawingTools->disconnect();
    m_drawingTools->cancel();
    // Cancelling another tool can refresh selection controls, which still
    // consult the reference controller while the canvas is being torn down.
    delete m_reference3D;m_reference3D=nullptr;
}

void Canvas::setProject(const Project &p, int shotIndex) {
    invalidateEffectsPreviewContext();
    m_artworkCache = {};
    m_effectsEditWasActive = m_effectsStrokeWasActive = false;
    m_maskTool->setEnabled(false);
    m_cameraTool->setEnabled(false); m_cameraView = true;
    m_keyframeTransformTool->setEnabled(false);
    m_scene3D->reset();
    m_drawingTools->cancel();
    m_guides->cancel(); m_guides->endSnap(); m_guides->select({});
    m_airbrushTimer.stop(); m_strokeFilter.reset(); m_tablet = false; m_dragging = false; m_input = {};
    cancelOverlayDrag();
    m_document->load(p);
    if (shotIndex > 0 && shotIndex < p.shots.size()) m_document->selectShot(p.shots[shotIndex].id);
    m_rotation = 0;
    if (m_canvasMirrored) { m_canvasMirrored = false; emit canvasMirroredChanged(false); }
    fitSheet();
    emit documentChanged();
}

QRectF Canvas::paperRect() const { return hasProject() ? sheetGeometry(project(), shot()).paper : QRectF(); }
QTransform Canvas::viewTransform() const {
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.rotate(m_rotation);
    t.scale(m_canvasMirrored ? -m_zoom : m_zoom, m_zoom);
    t.translate(-m_center.x(), -m_center.y());
    return t;
}
QPolygonF Canvas::visiblePolygon() const { return viewTransform().inverted().map(QPolygonF(QRectF(rect()))); }

QTransform Canvas::cameraTransform() const {
    return hasProject() && m_cameraView
        ? shotCameraTransform(shot(), m_document->currentFrame(), QSizeF(project().width, project().height)) : QTransform();
}
qreal Canvas::artworkZoom() const {
    return m_zoom * (hasProject() && m_cameraView ? evaluateShotCamera(shot(), m_document->currentFrame()).zoom : 1.);
}
qreal Canvas::artworkRotation() const {
    return m_rotation - (hasProject() && m_cameraView ? evaluateShotCamera(shot(), m_document->currentFrame()).rotation : 0.);
}
void Canvas::setCameraView(bool enabled) {
    if (m_cameraView == enabled) return;
    finishStroke(); m_maskTool->finish(); m_cameraTool->finish();
    if (enabled) m_cameraTool->setEnabled(false);
    m_cameraView = enabled;
    invalidateEffectsPreviewContext();
    m_onionCache = {}; invalidateArtworkCache(); refreshPointerCursor();
    emit cameraViewChanged(); emit frameViewChanged();
}

void Canvas::fitSheet() {
    if (!hasProject()) return;
    m_maskTool->finish();
    if (m_document->strokeActive()) finishStroke();
    cancelOverlayDrag();
    const auto paper = paperRect();
    QTransform rotate;
    rotate.rotate(m_rotation);
    const auto bounds = rotate.mapRect(paper);
    m_zoom = qBound(.01, qMin((width() - 80.0) / bounds.width(), (height() - 80.0) / bounds.height()), 16.0);
    m_center = paper.center();
    m_fit = true;
    update();
    emit viewChanged();
}
void Canvas::zoomAt(qreal zoomValue, QPointF position) {
    if (!hasProject()) return;
    m_maskTool->finish();
    if (m_document->strokeActive()) finishStroke();
    cancelOverlayDrag();
    const auto before = viewTransform().inverted().map(position);
    m_zoom = qBound(.01, zoomValue, 16.0);
    const auto after = viewTransform().inverted().map(position);
    m_center += before - after;
    m_fit = false;
    update();
    emit viewChanged();
}
void Canvas::setZoom(qreal value) { zoomAt(value, rect().center()); }
void Canvas::setRotation(qreal degrees) {
    if (!hasProject()) return;
    m_maskTool->finish();
    if (m_document->strokeActive()) finishStroke();
    cancelOverlayDrag();
    m_rotation = std::remainder(degrees, 360.0);
    m_fit = false;
    update();
    emit viewChanged();
}
void Canvas::setCanvasMirrored(bool enabled) {
    if (!hasProject() || m_canvasMirrored == enabled) return;
    // A view change during contact would splice two coordinate systems into
    // one gesture. Keep pending previews, but wait for the current drag to end.
    if (m_tablet || m_dragging || m_selecting || m_transformDrag != TransformDrag::None
        || m_document->strokeActive() || m_guides->dragging() || m_drawingTools->dragging()
        || m_maskTool->dragging() || m_cameraTool->dragging() || m_keyframeTransformTool->dragging()
        || !m_externalViewGestures.isEmpty()
        || m_reference3D->interactionActive()) return;
    m_canvasMirrored = enabled;
    refreshPointerCursor();
    emit canvasMirroredChanged(enabled);
    emit viewChanged();
    update();
}
void Canvas::setExternalViewGestureActive(QObject *owner, bool active) {
    if (!owner) return;
    if (active) {
        if (m_externalViewGestures.contains(owner)) return;
        m_externalViewGestures.insert(owner, connect(owner, &QObject::destroyed, this, [this, owner] {
            m_externalViewGestures.remove(owner);
        }));
    } else if (const auto found = m_externalViewGestures.find(owner); found != m_externalViewGestures.end()) {
        disconnect(found.value()); m_externalViewGestures.erase(found);
    }
}
void Canvas::centerOn(QPointF point) {
    if (!hasProject()) return;
    m_maskTool->finish();
    if (m_document->strokeActive()) finishStroke();
    cancelOverlayDrag();
    m_center = point;
    m_fit = false;
    update();
    emit viewChanged();
}

void Canvas::invalidateArtworkCache() {
    m_strokeArtworkUpdatePending = false;
    m_artworkCacheDirty = true; m_artworkDirtyRegion = {}; update();
    if (needsAsyncEffectsPreview()) setEffectsPreviewPending(true);
}
bool Canvas::needsAsyncEffectsPreview() const {
    if(!hasProject())return false;
    if(m_animationPlayback && m_playbackPreviewDivisor > 1)return true;
    return m_effectsPreviewActive || shotHasCompositing(project(), shot());
}
void Canvas::setEffectsPreviewPending(bool pending) {
    if (m_effectsPreviewPending == pending) return;
    m_effectsPreviewPending = pending;
    setProperty("effectsPreviewPending", pending);
    emit effectsPreviewPendingChanged(pending);
}
void Canvas::setPlaybackPreviewFallbackReason(const QString &reason) {
    if(m_playbackPreviewFallbackReason == reason)return;
    m_playbackPreviewFallbackReason = reason;
    setProperty("playbackPreviewFallbackReason", reason);
    emit playbackPreviewStatusChanged();
}
void Canvas::setPlaybackPreviewDivisor(int divisor) {
    if((divisor != 1 && divisor != 2 && divisor != 4 && divisor != 8) || divisor == m_playbackPreviewDivisor)return;
    m_playbackPreviewDivisor = divisor;
    setPlaybackPreviewFallbackReason({});
    if(m_animationPlayback){invalidateEffectsPreviewContext();invalidateArtworkCache();}
    emit playbackPreviewChanged();
}
void Canvas::invalidateEffectsPreviewContext() {
    ++m_effectsPreviewContext;
    m_effectsPreviewPublishedRevision = 0;
    m_effectsPreview->invalidate();
    if (needsAsyncEffectsPreview()) {
        m_artworkCacheDirty = true;
        m_artworkDirtyRegion = {};
        setEffectsPreviewPending(true);
        update();
    }
}
EffectsPreviewSnapshot Canvas::effectsPreviewSnapshot(const QWidget *target, const QTransform &view, bool miniature,
    QImage &background, QByteArray &backgroundKey, std::optional<SheetGeometry> &backgroundGeometry) const {
    EffectsPreviewSnapshot snapshot;
    // Only sheet metadata is needed alongside the one display-shot snapshot.
    // Do not retain other shots, reference images or the movie's audio library.
    snapshot.project.movie = project().movie;
    snapshot.project.width = project().width; snapshot.project.height = project().height;
    snapshot.project.fps = project().fps; snapshot.project.transparent = project().transparent;
    snapshot.project.symbols = project().symbols;
    snapshot.shot = displayShot();
    snapshot.shot.references.clear(); snapshot.shot.audioClips.clear();
    snapshot.frame = m_document->currentFrame();
    snapshot.playback = m_animationPlayback;
    snapshot.playbackDivisor = m_animationPlayback ? m_playbackPreviewDivisor : 1;
    snapshot.devicePixelRatio = target->devicePixelRatioF();
    snapshot.pixelSize = QSize(qCeil(target->width() * snapshot.devicePixelRatio), qCeil(target->height() * snapshot.devicePixelRatio));
    snapshot.dotsPerMeterX = qRound(target->logicalDpiX() / .0254);
    snapshot.dotsPerMeterY = qRound(target->logicalDpiY() / .0254);
    snapshot.view = view;
    if (!miniature) snapshot.viewportClip = QRegion(target->rect());
    snapshot.surround = QColor(miniature ? ui::base : ui::canvas);
    snapshot.symmetryColor = QColor(ui::accent);
    snapshot.smoothDisplay = m_canvasSmoothing; snapshot.applyCamera = m_cameraView;
    snapshot.drawPaperShadow = !miniature;
    // CoreText can rasterize the same glyph differently across thread-local
    // font engines. Keep the inexpensive sheet furniture on its GUI thread,
    // cached independently of every effect value and artwork change.
    QByteArray key;
    QDataStream data(&key, QIODevice::WriteOnly);
    const auto &sheet = snapshot.shot.sheet;
    data << snapshot.pixelSize << snapshot.devicePixelRatio << snapshot.dotsPerMeterX << snapshot.dotsPerMeterY
         << snapshot.view << snapshot.viewportClip << snapshot.surround << snapshot.drawPaperShadow
         << snapshot.project.movie << snapshot.project.width << snapshot.project.height << snapshot.project.fps
         << snapshot.shot.number << snapshot.shot.name << snapshot.shot.durationFrames
         << sheet.left << sheet.right << sheet.top << sheet.bottom << sheet.header << sheet.overscan << sheet.safeArea << sheet.showHeader << sheet.showGuides;
    if (key == backgroundKey && !background.isNull() && backgroundGeometry) {
        snapshot.preparedBackground = background;
        snapshot.preparedGeometry = backgroundGeometry;
    } else if (prepareEffectsPreviewBackground(snapshot)) {
        background = snapshot.preparedBackground;
        backgroundGeometry = snapshot.preparedGeometry;
        backgroundKey = std::move(key);
    }
    const auto *preview = m_drawingTools->transform()->active()
        ? m_drawingTools->transform()->preview() : m_document->transformPreview();
    if (preview) snapshot.transformPreview = *preview;
    if (!miniature) {
        snapshot.symmetry = brush.symmetry;
        snapshot.onionEnabled = m_onionSkin && !m_animationPlayback && !m_document->keyframeSourceEditing();
        snapshot.onionSettings = m_onionSettings;
        snapshot.onionTrackId = m_document->activeAnimationTrackId();
    }
    // Capture the display images under this canvas's transient 3D scope. The
    // worker receives values and never accesses the controller or GPU cache.
    const Scene3DPreviewScope sceneView(m_scene3D->previewBaseKey(), m_scene3D->previewDisplayKey());
    for (const auto &layer : snapshot.shot.layers) if (layer.scene3d) {
        auto scene = *layer.scene3d;
        scene.camera = evaluateSceneCamera(snapshot.shot, layer.id, snapshot.frame);
        snapshot.scenePreviews.insert(layer.id, scene3DPreview(scene, QSize(project().width, project().height)));
    }
    m_reference3D->prepareLayerPreviews({project().width,project().height});
    snapshot.referencePreviews=m_reference3D->preparedLayerPreviews();
    return snapshot;
}
void Canvas::scheduleStrokeArtworkUpdate() {
    if (m_strokeArtworkUpdatePending) return;
    m_strokeArtworkUpdatePending = true;
    // Preserve every brush sample, but discover display damage once for a
    // delivered batch rather than scanning the layer after every sub-sample.
    QTimer::singleShot(0, this, [this] {
        if (!m_strokeArtworkUpdatePending) return;
        m_strokeArtworkUpdatePending = false;
        invalidateStrokeArtworkCache();
    });
}
void Canvas::invalidateStrokeArtworkCache() {
    // A dab can change filter halos and folder composites beyond its source
    // tile. The effect compositor caches completed output tiles; do not leave
    // neighboring screen regions stale while a stroke is in progress.
    if (needsAsyncEffectsPreview()) { invalidateArtworkCache(); return; }
    const auto &layer = shot().layers.at(m_document->activeLayer());
    ++m_strokeTileScanCount;
    const bool tracked = m_strokeLayerId == layer.id;
    QMap<TileKey, qint64> revisions;
    QRegion changed;
    const auto view = artworkViewTransform();
    const auto dirtyTile = [&](const TileKey &key) {
        // Filtered display samples the neighboring edge pixel. Expand in both
        // artwork and screen space so zoom, rotation and Retina retain it.
        const QRectF tile(key.first * tileSize, key.second * tileSize, tileSize, tileSize);
        changed += view.mapRect(tile.adjusted(-1, -1, 1, 1)).adjusted(-2, -2, 2, 2).toAlignedRect();
    };
    for (auto it = layer.tiles.cbegin(); it != layer.tiles.cend(); ++it) {
        const auto revision = it.value().cacheKey();
        revisions.insert(it.key(), revision);
        if (tracked && m_strokeTileRevisions.value(it.key()) != revision) dirtyTile(it.key());
    }
    if (tracked) for (auto it = m_strokeTileRevisions.cbegin(); it != m_strokeTileRevisions.cend(); ++it)
        if (!revisions.contains(it.key())) dirtyTile(it.key());
    m_strokeLayerId = layer.id;
    // Keep only image identities: retaining tile images here would make every
    // subsequent paint dab detach and copy the complete 256-pixel tile.
    m_strokeTileRevisions = std::move(revisions);
    // Qt's clipped nearest-neighbor sampler can choose an adjacent source
    // pixel at fractional zoom/rotation. Keep pixel-display mode exact.
    if (!tracked || !m_canvasSmoothing) {
        invalidateArtworkCache(); return;
    }
    changed &= rect();
    if (!m_artworkCacheDirty) m_artworkDirtyRegion += changed;
    if (!changed.isEmpty()) update(changed);
}
void Canvas::setCanvasSmoothing(bool enabled) {
    if (m_canvasSmoothing == enabled) return;
    m_canvasSmoothing = enabled;
    invalidateEffectsPreviewContext();
    m_onionCache = {};
    invalidateArtworkCache();
    emit canvasSmoothingChanged(enabled);
}
void Canvas::prepareTimelineTransition() {
    invalidateEffectsPreviewContext();
    m_maskTool->finish(); m_cameraTool->finish();
    cancelOverlayDrag();
    m_drawingTools->cancel();
    m_guides->cancel(); m_guides->endSnap();
    m_tablet = false; m_dragging = false; m_space = false;
}
void Canvas::setAnimationPlayback(bool playing) {
    if (m_animationPlayback == playing) return;
    m_animationPlayback = playing;
    setPlaybackPreviewFallbackReason({});
    invalidateEffectsPreviewContext();
    if (m_onionSkin) invalidateArtworkCache();
    refreshPointerCursor(); updateSelectionUi();
    emit animationPlaybackChanged(playing);
}
void Canvas::setOnionSkin(bool enabled) {
    if (m_onionSkin == enabled) return;
    m_onionSkin = enabled; m_onionCache = {};
    invalidateEffectsPreviewContext();
    invalidateArtworkCache();
}
void Canvas::setOnionSkinSettings(const OnionSkinSettings &settings) {
    const auto valid = validOnionSkinSettings(settings) ? settings : OnionSkinSettings{};
    if (m_onionSettings == valid) return;
    m_onionSettings = valid; m_onionCache = {};
    invalidateEffectsPreviewContext();
    if (m_onionSkin) invalidateArtworkCache();
    emit onionSkinSettingsChanged();
}
void Canvas::paintOnionSkin(QPainter &painter) {
    if(m_document->keyframeSourceEditing())return;
    if (!m_onionSkin || m_animationPlayback || m_onionSettings.opacity <= 0
        || m_document->activeAnimationTrackId().isEmpty()) return;
    const auto size = m_artworkCache.size();
    const auto ratio = devicePixelRatioF();
    if (m_onionCache.isNull() || m_onionCache.size() != size || m_onionCache.devicePixelRatio() != ratio) {
        m_onionCache = QImage(size, QImage::Format_ARGB32_Premultiplied);
        m_onionCache.setDevicePixelRatio(ratio); m_onionCache.fill(Qt::transparent);
        const auto displayedShot = displayShot();
        const auto geometry = sheetGeometry(project(), displayedShot);
        const auto ghostBounds = cameraTransform().inverted().mapRect(
            geometry.paper.adjusted(0, geometry.header.height(), 0, 0).translated(-geometry.output.topLeft()));
        QPainter combined(&m_onionCache);
        for (int direction : {-1, 1}) {
            const auto samples = onionSkinSamples(displayedShot, m_document->currentFrame(), m_document->activeAnimationTrackId(),
                direction, direction < 0 ? m_onionSettings.previousCount : m_onionSettings.nextCount);
            // Draw distant ghosts first, so the nearest remains easy to distinguish.
            for (auto sample = samples.crbegin(); sample != samples.crend(); ++sample) {
                const auto layers = onionSkinLayersForSample(project(), displayedShot, m_document->currentFrame(),
                    m_document->activeAnimationTrackId(), *sample, ghostBounds);
                if (layers.isEmpty()) continue;
                // Bound ghost allocation to the visible viewport, even at 8K.
                QImage ghost(size, QImage::Format_ARGB32_Premultiplied);
                ghost.setDevicePixelRatio(ratio); ghost.fill(Qt::transparent);
                QPainter p(&ghost);
                p.setTransform(viewTransform());
                p.setClipRect(geometry.paper.adjusted(0, geometry.header.height(), 0, 0));
                p.translate(geometry.output.topLeft());
                p.setTransform(cameraTransform(), true);
                paintLayers(p, layers, false, nullptr, m_canvasSmoothing);
                p.resetTransform(); p.setClipping(false);
                p.setCompositionMode(QPainter::CompositionMode_SourceIn);
                p.fillRect(rect(), direction < 0 ? m_onionSettings.previousColor : m_onionSettings.nextColor);
                p.end();
                combined.setOpacity(m_onionSettings.opacity); combined.drawImage(QPointF(), ghost);
            }
        }
    }
    painter.save(); painter.resetTransform(); painter.drawImage(QPointF(), m_onionCache); painter.restore();
}
void Canvas::ensureArtworkCache() {
    // A paint may arrive before the queued damage discovery. Always compose
    // the latest delivered samples, including an immediate screenshot/render.
    if (m_strokeArtworkUpdatePending) {
        m_strokeArtworkUpdatePending = false;
        invalidateStrokeArtworkCache();
    }
    const qreal ratio = devicePixelRatioF();
    const QSize pixels(qCeil(width() * ratio), qCeil(height() * ratio));
    const int dotsX = qRound(logicalDpiX() / .0254), dotsY = qRound(logicalDpiY() / .0254);
    if (needsAsyncEffectsPreview()) {
        const bool changedView = m_effectsPreviewPixelSize != pixels || m_effectsPreviewRatio != ratio ||
            m_effectsPreviewDotsX != dotsX || m_effectsPreviewDotsY != dotsY;
        if (!m_artworkCacheDirty && m_artworkDirtyRegion.isEmpty() && !changedView && m_cachedSymmetry == brush.symmetry) return;
        if (pixels.isEmpty()) return;
        auto snapshot = effectsPreviewSnapshot(this, viewTransform(), false,
            m_effectsPreviewBackground, m_effectsPreviewBackgroundKey, m_effectsPreviewBackgroundGeometry);
        snapshot.context = m_effectsPreviewContext;
        snapshot.revision = ++m_effectsPreviewRevision;
        snapshot.quality = m_document->effectsEditActive() || m_document->keyframeEditActive() || m_document->strokeActive()
            ? EffectsPreviewQuality::Draft : EffectsPreviewQuality::Exact;
        m_effectsPreviewActive = true;
        m_effectsPreviewPixelSize = pixels; m_effectsPreviewRatio = ratio;
        m_effectsPreviewDotsX = dotsX; m_effectsPreviewDotsY = dotsY;
        m_cachedSymmetry = brush.symmetry;
        m_artworkCacheDirty = false; m_artworkDirtyRegion = {};
        m_animationDisplayKey = animationDisplayKey(snapshot.shot, snapshot.frame,
            m_onionSkin ? m_document->activeAnimationTrackId() : QString(), m_onionSettings);
        setEffectsPreviewPending(true);
        m_effectsPreview->request(std::move(snapshot));
        return;
    }
    if (m_effectsPreviewPending) {
        m_effectsPreview->invalidate();
        setEffectsPreviewPending(false);
    }
    if(hasProject())m_reference3D->prepareLayerPreviews({project().width,project().height});
    const auto referenceImages=m_reference3D->preparedLayerPreviews();
    const ReferenceLayerPreviewScope referenceScope(referenceImages);
    const Scene3DPreviewScope sceneView(m_scene3D->previewBaseKey(),m_scene3D->previewDisplayKey());
    const bool full = m_artworkCacheDirty || m_artworkCache.size() != pixels || m_artworkCache.devicePixelRatio() != ratio
        || m_artworkCache.dotsPerMeterX() != dotsX || m_artworkCache.dotsPerMeterY() != dotsY
        || m_cachedSymmetry != brush.symmetry;
    if (!full && m_artworkDirtyRegion.isEmpty()) return;
    if (pixels.isEmpty()) return;
    if (m_artworkCache.size() != pixels || m_artworkCache.devicePixelRatio() != ratio) {
        m_artworkCache = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
        m_artworkCache.setDevicePixelRatio(ratio);
    }
    m_artworkCache.setDotsPerMeterX(dotsX); m_artworkCache.setDotsPerMeterY(dotsY);
    const QRegion dirty = full ? QRegion(rect()) : m_artworkDirtyRegion;
    m_artworkDirtyRegion = {};
    for (const auto &area : dirty)
        m_artworkPaintedPixelCount += qCeil(area.width() * ratio) * quint64(qCeil(area.height() * ratio));
    m_artworkCacheDirty = false; m_cachedSymmetry = brush.symmetry; ++m_artworkRenderCount;
    QPainter p(&m_artworkCache);
    p.setClipRegion(dirty);
    p.fillRect(rect(), QColor(ui::canvas));
    if (!hasProject()) {
        p.setPen(QColor(ui::muted));
        p.drawText(rect(), Qt::AlignCenter, "Create or open a movie project to begin.");
        return;
    }
    p.save();
    p.setTransform(viewTransform());
    p.fillRect(paperRect().translated(8, 10), QColor(0, 0, 0, 65));
    paintSheet(p, project(), displayShot(), true,
        m_drawingTools->transform()->active() ? m_drawingTools->transform()->preview() : m_document->transformPreview(),
        m_canvasSmoothing, true, m_document->currentFrame(), m_cameraView);
    const auto renderError = keyframeRenderError();
    if (renderError != m_animationRenderError) {
        m_animationRenderError = renderError;
        if (!renderError.isEmpty()) QTimer::singleShot(0, this, [this, renderError] {
            if (m_animationRenderError == renderError) emit m_document->warning(renderError);
        });
    }
    paintOnionSkin(p);
    m_animationDisplayKey = animationDisplayKey(displayShot(), m_document->currentFrame(),
        m_onionSkin ? m_document->activeAnimationTrackId() : QString(), m_onionSettings);
    if (brush.symmetry.mode != SymmetryMode::Off) {
        p.setTransform(artworkViewTransform());
        QColor guideColor(ui::accent); guideColor.setAlpha(170);
        QPen guide(guideColor, 1, Qt::DashLine); guide.setCosmetic(true);
        p.setPen(guide); p.setBrush(Qt::NoBrush);
        p.drawPath(brushSymmetryGuide(brush.symmetry, QSizeF(project().width, project().height)));
    }
    p.restore();
}
void Canvas::clearBrushCursorRaster() {
    m_brushCursorRaster = {}; m_brushCursorRasterPath = {}; m_brushCursorRasterRatio = 0;
}
bool Canvas::paintDetailedBrushCursor(QPainter &painter, const QPainterPath &path) {
    const qreal ratio = devicePixelRatioF();
    if (!std::isfinite(ratio) || ratio <= 0 || ratio > 5) { clearBrushCursorRaster(); return false; }
    const int darkRadius = qCeil(1.5 * ratio), lightRadius = qCeil(.5 * ratio);
    QTransform scale; scale.scale(ratio, ratio);
    auto physicalPath = scale.map(path);
    // Keep a filter halo outside the viewport. Clipping the filled mask at the
    // visible edge would otherwise invent an outline across that edge.
    const int halo = darkRadius + 2;
    const QRect viewport(0, 0, qCeil(width() * ratio), qCeil(height() * ratio));
    const auto pathBounds = physicalPath.boundingRect();
    const QRect fullBounds = pathBounds.adjusted(-halo,-halo,halo,halo).toAlignedRect();
    const auto affordable = [&](QSize size) {
        const qint64 pixels=qint64(size.width())*size.height();
        return pixels<=4*1024*1024 && pixels*(4*darkRadius+4*lightRadius+4)<=64*1024*1024;
    };
    // Cache translation-independent coverage, including offscreen portions.
    // Fractional pen movement moves this image rather than rebuilding its
    // mask and two boundary filters. Artwork coordinates remain untouched.
    const bool reusable=affordable(fullBounds.size());
    const QRect clipped=fullBounds.intersected(viewport.adjusted(-halo,-halo,halo,halo));
    if(clipped.isEmpty()) { clearBrushCursorRaster();return true; }
    const QPointF origin=reusable?pathBounds.topLeft()-QPointF(halo,halo):QPointF(clipped.topLeft());
    const QSize rasterSize=reusable?QSize(qCeil(qRound64(pathBounds.width()*4096)/4096.)+2*halo,qCeil(qRound64(pathBounds.height()*4096)/4096.)+2*halo):clipped.size();
    if(!affordable(rasterSize)){clearBrushCursorRaster();return false;}
    physicalPath.translate(-origin.x(),-origin.y());
    if(reusable) {
        // Remove cancellation noise from subtracting a moving large origin.
        // This affects only the display mask, by <= 1/8192 physical pixel.
        for(int i=0;i<physicalPath.elementCount();++i) {
            const auto e=physicalPath.elementAt(i);
            physicalPath.setElementPositionAt(i,qRound64(e.x*4096)/4096.,qRound64(e.y*4096)/4096.);
        }
    }
    if (m_brushCursorRaster.isNull() || m_brushCursorRaster.size() != rasterSize
        || m_brushCursorRasterRatio != ratio || m_brushCursorRasterPath != physicalPath) {
        QImage filled(rasterSize, QImage::Format_ARGB32_Premultiplied);
        QImage alpha(rasterSize, QImage::Format_Grayscale8);
        if (filled.isNull() || alpha.isNull()) { clearBrushCursorRaster(); return false; }
        filled.fill(Qt::transparent);
        {
            QPainter maskPainter(&filled); maskPainter.setRenderHint(QPainter::Antialiasing);
            maskPainter.setPen(Qt::NoPen); maskPainter.setBrush(Qt::white);
            maskPainter.drawPath(physicalPath);
        }
        for (int y = 0; y < filled.height(); ++y) {
            const auto *source = reinterpret_cast<const QRgb *>(filled.constScanLine(y));
            auto *target = alpha.scanLine(y);
            for (int x = 0; x < filled.width(); ++x) target[x] = uchar(qAlpha(source[x]));
        }
        filled = {};
        const auto dark = cursorBoundary(alpha, 1.5 * ratio);
        const auto light = cursorBoundary(alpha, .5 * ratio);
        QImage overlay(rasterSize, QImage::Format_ARGB32_Premultiplied);
        if (dark.isNull() || light.isNull() || overlay.isNull()) { clearBrushCursorRaster(); return false; }
        for (int y = 0; y < overlay.height(); ++y) {
            const auto *darkRow = dark.constScanLine(y), *lightRow = light.constScanLine(y);
            auto *target = reinterpret_cast<QRgb *>(overlay.scanLine(y));
            for (int x = 0; x < overlay.width(); ++x) {
                const int darkAlpha = (darkRow[x] * 220 + 127) / 255;
                const int lightAlpha = (lightRow[x] * 240 + 127) / 255;
                const auto base = qPremultiply(qRgba(20, 24, 30, darkAlpha));
                const auto over = [lightAlpha](int channel) { return lightAlpha + (channel * (255 - lightAlpha) + 127) / 255; };
                target[x] = qRgba(over(qRed(base)), over(qGreen(base)), over(qBlue(base)), over(darkAlpha));
            }
        }
        overlay.setDevicePixelRatio(ratio);
        m_brushCursorRaster = std::move(overlay); m_brushCursorRasterPath = std::move(physicalPath);
        m_brushCursorRasterRatio = ratio; ++m_brushCursorRasterBuildCount;
    }
    // Interpolate only the small display overlay at its exact fractional
    // position. Shape/pressure/zoom changes still rebuild the coverage.
    painter.save();painter.setRenderHint(QPainter::SmoothPixmapTransform,true);
    painter.drawImage(origin / ratio, m_brushCursorRaster);painter.restore();
    return true;
}
void Canvas::paintEvent(QPaintEvent *) {
    static const bool profile = qEnvironmentVariableIsSet("TOON_PROFILE_CANVAS");
    QElapsedTimer profileTimer;
    if(profile)profileTimer.start();
    const auto profilePaint=qScopeGuard([&]{if(profile)setProperty("profilePaintNs",property("profilePaintNs").toULongLong()+profileTimer.nsecsElapsed());});
    ensureArtworkCache();
    if(profile)setProperty("profileCacheNs",property("profileCacheNs").toULongLong()+profileTimer.nsecsElapsed());
    QPainter p(this);
    // The previous image remains visible while a newer effect value renders.
    // A first render/new document has no image yet, so paint a clean surround.
    p.fillRect(rect(), QColor(ui::canvas));
    p.drawImage(QPointF(), m_artworkCache);
    m_guides->paint(p);
    m_scene3D->paintHandles(p);
    if(hasProject())m_reference3D->paintOverlay(p,artworkViewTransform(),{project().width,project().height});
    m_keyframeTransformTool->paint(p);
    m_cameraTool->paint(p);
    QElapsedTimer cursorTimer;if(profile)cursorTimer.start();
    const auto profileCursor=qScopeGuard([&]{if(profile)setProperty("profileCursorNs",property("profileCursorNs").toULongLong()+cursorTimer.nsecsElapsed());});
    const auto cursorPath = brushCursorPath();
    if (!cursorPath.isEmpty()) {
        // Screen-space strokes stay readable on both paper and dark artwork.
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(Qt::NoBrush);
        const bool detailed = cursorPath.elementCount() > 256;
        if (detailed && paintDetailedBrushCursor(p, cursorPath)) return;
        if (!detailed) clearBrushCursorRaster();
        const auto edges = detailed ? brushCursorEdges(p, cursorPath) : QVector<QLineF>{};
        const auto paintOutline = [&] {
            if (detailed) p.drawLines(edges);
            else p.drawPath(cursorPath);
        };
        p.setPen(QPen(QColor(20, 24, 30, 220), 3));
        paintOutline();
        p.setPen(QPen(QColor(255, 255, 255, 240), 1));
        paintOutline();
    } else clearBrushCursorRaster();
}
bool Canvas::event(QEvent *event) {
    if (event->type() == QEvent::ToolTip && m_tool == Tool::Guides && hasProject()) {
        const auto *help = static_cast<QHelpEvent *>(event);
        const auto text = m_guides->toolTipAt(help->pos());
        if (text.isEmpty()) { QToolTip::hideText(); event->ignore(); }
        else { QToolTip::showText(help->globalPos(), text, this); event->accept(); }
        return true;
    }
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::DevicePixelRatioChange) {
        invalidateEffectsPreviewContext();
        invalidateArtworkCache(); updateSelectionUi();
    }
    return handled;
}
void Canvas::resizeEvent(QResizeEvent *) {
    invalidateEffectsPreviewContext();
    m_selectionOverlay->setGeometry(rect());
    invalidateArtworkCache();
    if (m_fit) fitSheet();
    else emit viewChanged();
}
void Canvas::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (needsAsyncEffectsPreview()) { invalidateEffectsPreviewContext(); invalidateArtworkCache(); }
    m_selectionOverlay->setGeometry(rect()); updateSelectionUi();
}
void Canvas::hideEvent(QHideEvent *event) {
    ++m_effectsPreviewContext;
    m_effectsPreview->invalidate();
    if (m_effectsPreviewActive) m_artworkCacheDirty = true;
    setEffectsPreviewPending(false);
    m_guides->cancelDrag();
    clearBrushCursorRaster();
    QWidget::hideEvent(event); m_selectionOverlay->synchronize();
}
void Canvas::setSelectionCombine(SelectionCombine mode) {
    if (mode != SelectionCombine::Replace && mode != SelectionCombine::Add
        && mode != SelectionCombine::Subtract && mode != SelectionCombine::Intersect) return;
    if (m_selectionCombine == mode) return;
    m_selectionCombine = mode; emit selectionCombineChanged();
}
bool Canvas::selectionGestureActive() const {
    return (m_reference3D && m_reference3D->interactionActive()) || m_stampContact || m_scene3D->editing() || m_drawingTools->active() || m_selecting || m_dragging || m_transformDrag != TransformDrag::None || m_document->strokeActive() || m_guides->dragging() || m_guides->creating;
}
void Canvas::setSelectionFocusOverlay(bool enabled) {
    if (m_selectionFocusOverlay == enabled) return;
    m_selectionFocusOverlay = enabled; updateSelectionUi();
}
void Canvas::setSelectionShadeOpacity(qreal value) { m_selectionShadeOpacity=qBound(0.,value,1.); updateSelectionUi(); }
void Canvas::setSelectionAnimated(bool value) { m_selectionAnimated=value; updateSelectionUi(); }
void Canvas::setSelectionBarVisible(bool visible) {
    if (m_selectionBarVisible == visible) return;
    m_selectionBarVisible = visible; emit selectionBarVisibilityChanged(visible); updateSelectionUi();
}
void Canvas::updateSelectionUi() {
    m_selectionOverlay->synchronize();
    m_selectionOverlay->update();
    emit selectionUiChanged();
}
QRectF Canvas::selectionViewportBounds() const {
    if (!hasProject()) return {};
    if (m_document->stampPlacementActive()) return transformBox().boundingRect();
    const auto selection = viewportSelectionPath();
    if (!selection.isEmpty()) return selection.boundingRect();
    if (m_document->rasterSelection().active)
        return artworkViewTransform().mapRect(m_document->drawingBounds());
    return transformBox().boundingRect();
}
bool Canvas::resolvePendingTransform() {
    m_reference3D->finishInteraction();
    flushStampTransform();
    auto *advanced = m_drawingTools->transform();
    const bool rasterActive = m_document->transformActive();
    if (!rasterActive && !advanced->active()) return true;
    bool changed = rasterActive && (m_document->stampPlacementActive() || !m_document->transformMatrix().isIdentity());
    if (advanced->active()) {
        changed |= advanced->modified();
    }
    if (!changed) {
        advanced->cancel(); m_document->cancelTransform();
        return true;
    }
    QMessageBox message(QMessageBox::Question, tr("Pending transform"),
        m_document->stampPlacementActive() ? tr("Place this stamp before continuing?") : tr("Apply the transform before continuing?"), QMessageBox::NoButton, window());
    message.setObjectName("pendingTransformDialog");
    auto *apply = message.addButton(tr("Apply"), QMessageBox::AcceptRole);
    auto *discard = message.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    auto *cancel = message.addButton(QMessageBox::Cancel);
    apply->setObjectName("applyPendingTransform"); discard->setObjectName("discardPendingTransform");
    cancel->setObjectName("cancelPendingTransform");
    message.setDefaultButton(apply); message.setEscapeButton(cancel);
    message.exec();
    if (message.clickedButton() == apply) {
        const bool applied = advanced->active() ? advanced->apply() : m_document->applyTransform();
        if (applied) cancelOverlayDrag();
        return applied;
    }
    if (message.clickedButton() == discard) {
        advanced->cancel(); m_document->cancelTransform(); cancelOverlayDrag();
        return true;
    }
    return false;
}
void Canvas::setTool(Tool tool) {
    if (!features::legacy3DEditor && (tool == Tool::SceneObject || tool == Tool::SceneCamera || tool == Tool::SceneRig)) {
        emit actionBlocked("Legacy 3D editing is retired.");
        return;
    }
    emit m_document->effectsEditResolutionRequested();
    m_document->commitEffectsEdit();
    m_maskTool->setEnabled(false);
    if (m_toolRouter && m_toolRouter(tool)) return;
    if (tool != m_tool && !resolvePendingTransform()) return;
    m_cameraTool->setEnabled(false);
    m_keyframeTransformTool->setEnabled(false);
    finishStroke();
    if(tool!=m_tool){m_scene3D->endEdit();m_drawingTools->cancel();}
    m_guides->cancel();
    cancelOverlayDrag();
    if (tool != m_tool) m_document->cancelTransform();
    QStringList selectedLayers;
    if(auto *panel=window()->findChild<LayerPanel*>())selectedLayers=panel->selectedLayerIds();
    bool transformSymbol=false;
    if(tool==Tool::Transform && hasProject()) {
        const auto &layer=shot().layers[m_document->activeLayer()];
        transformSymbol=layer.symbol.has_value()&&selectedLayers.size()<=1;
        if(!transformSymbol&&(layer.kind!=LayerKind::Raster||layer.group||selectedLayers.size()>1))tool=Tool::AdvancedTransform;
    }
    m_transformFailure.clear();
    if (m_tool != tool) invalidateEffectsPreviewContext();
    m_tool = tool;
    if (toolBlockReason().isEmpty()) {
        if (tool == Tool::AdvancedTransform && hasProject() && !m_drawingTools->transform()->active()) prepareToolTransform(selectedLayers);
        if (tool == Tool::Transform && hasProject() && !m_document->transformActive()) prepareToolTransform();
    }
    refreshPointerCursor();
    emit toolChanged();
    if(transformSymbol){
        m_document->setKeyframeSourceEditing(false);
        m_keyframeTransformTool->setMode(KeyframeTransformTool::Mode::Transform);
        m_keyframeTransformTool->setEnabled(true);
    }
}
void Canvas::setToolAvailable(bool available) {
    if (m_toolAvailable == available) return;
    if (!available && !resolvePendingTransform()) return;
    finishStroke(); m_drawingTools->cancel(); m_guides->cancel(); cancelOverlayDrag();
    m_document->cancelTransform(); m_toolAvailable = available;
    if (!available) { m_tablet = false; m_dragging = false; }
    refreshPointerCursor(); emit toolAvailabilityChanged();
}
GuideType Canvas::guideCreationType() const { return m_guides->creationType; }
quint64 Canvas::guideCacheRenderCount() const { return m_guides->cacheRenderCount(); }
void Canvas::setGuideCreationType(GuideType type) {
    if (guidePointCount(type) < 1) return;
    if (m_guides->creating) m_guides->beginCreation(type);
    else { m_guides->creationType = type; emit guideCreationChanged(); }
}
void Canvas::beginGuideCreation(GuideType type) { setTool(Tool::Guides); if (m_tool != Tool::Guides) return; m_guides->beginCreation(type); setFocus(); }
void Canvas::cancelGuideCreation() { m_guides->cancel(); updateSelectionUi(); }
bool Canvas::guideCreating() const { return m_guides->creating; }
QString Canvas::selectedGuideId() const { return m_guides->selectedId; }
void Canvas::selectGuide(const QString &id) { m_guides->select(id); }
QVector<DrawingGuide> Canvas::drawingGuides() const { return m_guides->guides(); }
GuideSettings Canvas::guideSettings() const { return m_guides->settings(); }
void Canvas::addDefaultGuide(GuideType type) { m_guides->addDefault(type); }
void Canvas::updateGuide(const DrawingGuide &guide) { m_guides->updateGuide(guide); }
void Canvas::removeSelectedGuide() { m_guides->removeSelected(); }
void Canvas::duplicateSelectedGuide() { m_guides->duplicateSelected(); }
void Canvas::setGuideSettings(const GuideSettings &settings) { m_guides->setSettings(settings); }
void Canvas::replaceGuideSet(const QVector<DrawingGuide> &guides, const GuideSettings &settings, bool append) { m_guides->replaceSet(guides, settings, append); }
void Canvas::setBrushSize(qreal size) {
    if (!std::isfinite(size)) return;
    brush.size = qBound(1.0, size, maxBrushSize);
    update();
    emit brushChanged();
}
void Canvas::setBrushFlow(qreal flow) {
    if (!std::isfinite(flow)) return;
    brush.opacity = qBound(0.0, flow, 1.0); update(); emit brushChanged();
}
void Canvas::setBrushSpacing(qreal spacing) {
    if (!std::isfinite(spacing)) return;
    brush.spacing = qBound(.01, spacing, 4.0); update(); emit brushChanged();
}
void Canvas::setBrushColor(const QColor &color) {
    if (!color.isValid()) return;
    brush.color = color; emit brushChanged();
}
void Canvas::setBrushSettings(const BrushSettings &settings) {
    if (!validateBrushSettings(settings) || !validateBrushTipImages(settings)) return;
    finishStroke();
    brush = settings; update(); emit brushChanged();
}
void Canvas::applyBrushSettings(const BrushSettings &settings, qreal smoothing) {
    if (!validateBrushSettings(settings) || !validateBrushTipImages(settings) || !std::isfinite(smoothing)) return;
    finishStroke();
    const auto color = brush.color;
    const auto background = brush.backgroundColor;
    const auto texture = brush.texture;
    const bool protect = brush.protectTexture;
    brush = settings; brush.color = color; brush.backgroundColor = background;
    if (protect) { brush.texture = texture; brush.protectTexture = true; }
    brush.size = qBound(1.0, brush.size, maxBrushSize);
    brush.opacity = qBound(0.0, brush.opacity, 1.0);
    brush.spacing = qBound(.01, brush.spacing, 4.0);
    m_stabilization = qBound(0.0, smoothing, 100.0);
    update(); emit brushChanged();
}
void Canvas::setPressureSize(bool enabled) {
    brush.pressureSize = enabled;
    brush.sizeDynamics.control = enabled ? BrushControl::Pressure : BrushControl::Off;
    update();
    emit brushChanged();
}
void Canvas::setStabilization(qreal amount) {
    if (!std::isfinite(amount)) return;
    // Snapshot strength at the beginning of each stroke, just like brush size.
    m_stabilization = qBound(0.0, amount, 100.0);
    emit brushChanged();
}
void Canvas::setStabilizationMode(StrokeFilterMode mode) {
    if ((mode != StrokeFilterMode::Smooth && mode != StrokeFilterMode::Stabilizer) || m_stabilizationMode == mode) return;
    m_stabilizationMode = mode; emit brushChanged();
}
void Canvas::setStabilizationDeadZone(qreal pixels) {
    if (!std::isfinite(pixels)) return;
    pixels = qBound(0.0, pixels, 20.0);
    if (m_stabilizationDeadZone == pixels) return;
    m_stabilizationDeadZone = pixels; emit brushChanged();
}
void Canvas::setStabilizationFinishAtEndpoint(bool enabled) {
    if (m_stabilizationFinishAtEndpoint == enabled) return;
    m_stabilizationFinishAtEndpoint = enabled; emit brushChanged();
}
QPainterPath Canvas::brushCursorPath() const {
    if(m_maskTool->enabled())return m_maskTool->cursorPath();
    if(m_keyframeTransformTool->enabled() || m_cameraTool->enabled())return {};
    if (!m_toolAvailable || !pointerBlockReason().isEmpty()) return {};
    if(m_tool==Tool::SceneRig && m_scene3D->rigEditMode()==Scene3DController::RigEditMode::Weights) {
        QPainterPath path;
        if(m_pointerInside && !m_dragging && !m_space && m_scene3D->canPaintWeights())
            path.addEllipse(m_pointerPosition,m_scene3D->weightBrushRadius(),m_scene3D->weightBrushRadius());
        return path;
    }
    if ((m_pointerModifiers & Qt::AltModifier) && (m_tool == Tool::Brush || m_tool == Tool::Eraser || m_tool == Tool::Retouch)) return {};
    if(m_drawingTools) {auto cursor=m_drawingTools->cursorPath();if(!cursor.isEmpty())return cursor;}
    if (!hasProject() || !m_pointerInside || m_dragging || m_space || m_document->transformActive()
        || (m_tool != Tool::Brush && m_tool != Tool::Eraser)) return {};
    const bool drawing = m_document->strokeActive() && m_strokeFilter.active();
    const QPointF position = drawing ? artworkViewTransform().map(m_paintedTip)
        : m_pointerPosition;
    const auto &settings = drawing ? m_strokeBrush : brush;
    BrushInput input = m_input;
    input.pressure = drawing ? m_lastPaintInput.pressure : 1;
    input.speed = drawing ? m_lastPaintInput.speed : 0;
    const quint64 dabIndex = drawing ? m_document->brushCursorIndex() : 0;
    const quint32 seed = drawing ? m_document->brushStrokeSeed() : settings.lockRandomSeed ? settings.randomSeed : 1;
    const qreal direction = drawing ? m_document->brushDirection() : m_pointerDirection;
    const qreal initialDirection = drawing ? m_document->brushInitialDirection() : m_initialDirection;
    const QPointF artworkPosition = drawing ? m_paintedTip : artworkPoint(m_pointerPosition);
    const QPointF patternPosition = settings.texture.enabled && !settings.texture.eachTip ? artworkPosition : QPointF();
    const bool simple = settings.tipMask.isNull() && settings.colorTip.isNull() && settings.angle == 0 && settings.roundness == 1
        && settings.sizeDynamics.jitter == 0 && settings.sizeDynamics.minimum == .05
        && settings.sizeDynamics.response == BrushDynamic{}.response
        && (settings.sizeDynamics.control == BrushControl::Off || settings.sizeDynamics.control == BrushControl::Pressure)
        && settings.angleDynamics == BrushDynamic{} && settings.roundnessDynamics == BrushDynamic{0, .01, BrushControl::Off, 25}
        && !settings.projection && !settings.texture.enabled && !settings.dual.enabled
        && !settings.pose.overrideTiltX && !settings.pose.overrideTiltY && !settings.pose.overrideRotation
        && !settings.noise && !settings.wetEdges;
    QPainterPath outline;
    if (simple) {
        const qreal pressure = settings.pose.overridePressure ? settings.pose.pressure : input.pressure;
        const bool pressureSize = settings.pressureSize || settings.sizeDynamics.control == BrushControl::Pressure;
        const qreal diameter = qBound(.5, settings.size * (pressureSize ? qMax(.05, pressure) : 1)
            * brushSpeedFactor(settings.speedDynamics,input.speed,settings.speedDynamics.sizeAmount),maxBrushSize) * artworkZoom();
        outline.addEllipse(QRectF(position - QPointF(diameter / 2, diameter / 2), QSizeF(diameter, diameter)));
        return outline;
    } else {
        // A static sampled silhouette does not change when the stroke advances
        // or turns. Only key inputs that actually affect mask coverage; color,
        // flow and particle offsets do not alter the cursor's tip silhouette.
        const auto usesIndex = [](const BrushDynamic &dynamic) {
            return (dynamic.jitter != 0 && !dynamic.jitterPerStroke) || dynamic.control == BrushControl::Fade;
        };
        const bool texture = settings.texture.enabled;
        const bool varyingIndex = !settings.tipFrames.isEmpty() || settings.noise || settings.dual.enabled
            || usesIndex(settings.sizeDynamics) || usesIndex(settings.angleDynamics) || usesIndex(settings.roundnessDynamics)
            || (texture && (settings.texture.eachTip || usesIndex(settings.texture.dynamics)));
        const bool varyingSeed = !settings.tipFrames.isEmpty() || settings.noise || settings.dual.enabled
            || settings.sizeDynamics.jitter != 0 || settings.angleDynamics.jitter != 0 || settings.roundnessDynamics.jitter != 0
            || (texture && (settings.texture.eachTip || settings.texture.dynamics.jitter != 0));
        const auto directionKey = settings.tipOrder == BrushTipOrder::Direction || settings.dual.enabled || settings.sizeDynamics.control == BrushControl::Direction
            || settings.angleDynamics.control == BrushControl::Direction ? direction : 0;
        const auto initialKey = settings.sizeDynamics.control == BrushControl::InitialDirection
            || settings.angleDynamics.control == BrushControl::InitialDirection ? initialDirection : 0;
        const auto indexKey = varyingIndex ? dabIndex : 0;
        const auto seedKey = varyingSeed ? seed : 1;
        auto outlineInput=resolvedBrushInput(settings,input);
        if(!settings.dual.enabled) {
            const auto uses=[&](BrushControl control) {
                return settings.sizeDynamics.control==control || settings.angleDynamics.control==control
                    || settings.roundnessDynamics.control==control || (texture&&settings.texture.dynamics.control==control);
            };
            if(!settings.pressureSize&&!uses(BrushControl::Pressure)&&settings.tipOrder!=BrushTipOrder::Pressure)outlineInput.pressure=1;
            if(!settings.projection&&!uses(BrushControl::Tilt))outlineInput.tiltX=outlineInput.tiltY=0;
            if(!uses(BrushControl::Wheel))outlineInput.wheel=1;
            if(!settings.speedDynamics.enabled||settings.speedDynamics.sizeAmount==0)outlineInput.speed=0;
        }
        if (!m_outlineValid || m_outlineSettings != settings || m_outlineInput != outlineInput
            || m_outlineDirection != directionKey || m_outlineInitialDirection != initialKey
            || m_outlineDabIndex != indexKey || m_outlineSeed != seedKey || m_outlinePosition != patternPosition) {
            if (settings.tipFrames.isEmpty()) m_tipOutline = maskOutline(brushTipMask(settings, input, direction, initialDirection, dabIndex, seed, patternPosition));
            else {
                auto tipSettings = settings; tipSettings.scatter = 0;
                tipSettings.clusterSpray.enabled = false;
                tipSettings.colorDynamics.applyToColorTip = false;tipSettings.colorDynamics.tintAmount = 0;
                const auto dab = evaluateBrushDab(tipSettings, input, patternPosition, direction, initialDirection, dabIndex, seed);
                m_tipOutline = maskOutline(dab.mask).translated(dab.offset);
            }
            ++m_brushOutlineBuildCount;
            m_outlineSettings = settings; m_outlineInput = outlineInput;
            m_outlineDirection = directionKey; m_outlineInitialDirection = initialKey;
            m_outlineDabIndex = indexKey; m_outlineSeed = seedKey; m_outlinePosition = patternPosition;
            m_outlineValid = true;
        }
        outline = m_tipOutline;
    }
    // Use the actual linear view transform, including reflections and camera
    // rotation, so an asymmetric tip still follows the pixels it will paint.
    const auto view = artworkViewTransform();
    QTransform t(view.m11(), view.m12(), view.m21(), view.m22(), position.x(), position.y());
    return t.map(outline);
}
QRectF Canvas::brushCursorRect() const { return brushCursorPath().boundingRect(); }
QRectF Canvas::brushCursorDamageRect() const {
    if ((m_tool != Tool::Brush && m_tool != Tool::Eraser) || m_maskTool->enabled()
        || m_keyframeTransformTool->enabled() || m_cameraTool->enabled()) return brushCursorRect();
    if (!hasProject() || !m_pointerInside || m_dragging || m_space) return {};
    const bool drawing = m_document->strokeActive() && m_strokeFilter.active();
    const auto position = drawing ? artworkViewTransform().map(m_paintedTip) : m_pointerPosition;
    const auto &settings = drawing ? m_strokeBrush : brush;
    // Every tip effect is clipped to the rotated, padded mask. This envelope
    // covers it at any pressure/angle, including an additional view rotation.
    // Trace the precise stamp silhouette only when a frame is actually painted.
    const qreal radius = (qBound(1.0, settings.size, maxBrushSize) * (settings.tipFrames.isEmpty() ? 1. : 1.5) + 4) * artworkZoom() + 3;
    return QRectF(position - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius));
}
void Canvas::refreshPointerCursor() {
    if (!hasProject()) setCursor(Qt::ForbiddenCursor);
    else if (m_dragging) setCursor(Qt::ClosedHandCursor);
    else if (m_space || (m_tool == Tool::Hand && !m_keyframeTransformTool->enabled() && !m_maskTool->enabled())) setCursor(Qt::OpenHandCursor);
    else if(m_reference3D&&m_reference3D->editable())setCursor(m_reference3D->interactionActive()?Qt::ClosedHandCursor:Qt::ArrowCursor);
    else if (!pointerBlockReason().isEmpty()) setCursor(Qt::ForbiddenCursor);
    else if (m_maskTool->enabled()) setCursor(m_pointerInside?Qt::BlankCursor:Qt::ArrowCursor);
    else if (m_cameraTool->enabled()) setCursor(Qt::SizeAllCursor);
    else if (m_keyframeTransformTool->enabled()) setCursor(m_keyframeTransformTool->mode()==KeyframeTransformTool::Mode::Pivot?Qt::CrossCursor:Qt::SizeAllCursor);
    else if ((m_pointerModifiers & Qt::AltModifier) && (m_tool == Tool::Brush || m_tool == Tool::Eraser || m_tool == Tool::Retouch)) setCursor(Qt::CrossCursor);
    else if (m_tool == Tool::RectangleSelect || m_tool == Tool::LassoSelect || m_tool == Tool::Guides) setCursor(Qt::CrossCursor);
    else if (m_tool == Tool::Transform || m_tool == Tool::AdvancedTransform || m_document->transformActive()) setCursor(Qt::SizeAllCursor);
    else if (m_tool == Tool::SceneRig && m_scene3D->rigEditMode()==Scene3DController::RigEditMode::Weights)
        setCursor(m_pointerInside?Qt::BlankCursor:Qt::ArrowCursor);
    else if (m_tool == Tool::SceneObject || m_tool == Tool::SceneRig) setCursor(Qt::SizeAllCursor);
    else if (m_tool == Tool::SceneCamera) setCursor(Qt::OpenHandCursor);
    else if (m_drawingTools->handlesTool() && m_tool!=Tool::Retouch && m_tool!=Tool::VectorEraser && m_tool!=Tool::VectorSimplify && m_tool!=Tool::VectorWidth && m_tool!=Tool::VectorNudge && m_tool!=Tool::Brush && m_tool!=Tool::Eraser) setCursor(Qt::CrossCursor);
    // Hide the native pointer only for tools that draw a replacement brush outline.
    else if (m_tool == Tool::Brush || m_tool == Tool::Eraser || m_tool == Tool::Retouch
        || m_tool == Tool::VectorEraser || m_tool == Tool::VectorSimplify || m_tool == Tool::VectorWidth || m_tool == Tool::VectorNudge)
        setCursor(m_pointerInside ? Qt::BlankCursor : Qt::ArrowCursor);
    else setCursor(Qt::ArrowCursor);
    const auto hint = interactionHint();
    if (m_interactionHint != hint) { m_interactionHint = hint; emit interactionHintChanged(hint); }
    updateSelectionUi();
    update();
}
void Canvas::trackPointer(QPointF position, qreal pressure) {
    const auto before = brushCursorDamageRect();
    const bool wasInside = m_pointerInside;
    m_pointerPosition = position;
    m_pointerPressure = qBound(0.0, pressure, 1.0);
    m_pointerInside = QRectF(rect()).contains(position);
    if (wasInside != m_pointerInside || m_interactionHint != interactionHint()) refreshPointerCursor();
    else update(before.united(brushCursorDamageRect()).adjusted(-3, -3, 3, 3).toAlignedRect());
}
void Canvas::syncTabletCursor(QPointF position) {
    if (!isVisible() || !QRectF(rect()).contains(position) || QApplication::activePopupWidget()
        || (QApplication::activeModalWidget() && QApplication::activeModalWidget() != window())
        || (QWidget::mouseGrabber() && QWidget::mouseGrabber() != this)) return;
    auto *top = window();
    // Accepted tablet events do not synthesize the mouse hover that normally
    // clears QMainWindow's dock-separator cursor. Clear only that stale hover;
    // sending a mouse move here could accidentally continue a dock resize.
    if (auto *mainWindow = qobject_cast<QMainWindow *>(top);
        mainWindow && (mainWindow->cursor().shape() == Qt::SplitHCursor || mainWindow->cursor().shape() == Qt::SplitVCursor)) {
        const auto local = mapTo(mainWindow, position);
        QHoverEvent hover(QEvent::HoverMove, local, mapToGlobal(position), local, Qt::NoModifier);
        QApplication::sendEvent(mainWindow, &hover);
    }
    // QWidget's cached mouse owner can still be the separator. Address the
    // native window directly, without touching application busy overrides.
    if (auto *handle = top->windowHandle()) {
        if (handle->cursor().shape() != cursor().shape()) handle->setCursor(cursor());
        m_tabletCursorOwned = true;
    }
}
void Canvas::handOffTabletCursor(QWidget *receiver, QPointF position) {
    if (!m_tabletCursorOwned || !receiver || !receiver->isVisible() || receiver == this
        || isAncestorOf(receiver) || QApplication::activePopupWidget()
        || (QApplication::activeModalWidget() && QApplication::activeModalWidget() != receiver->window())
        || (QWidget::mouseGrabber() && QWidget::mouseGrabber() != receiver)) return;
    // Pen-only motion also needs the reverse handoff. A stale native blank
    // cursor must not follow the artist into a field, color picker or panel.
    // Clear ownership before dispatching hover to avoid event-filter recursion.
    m_tabletCursorOwned = false;
    if (m_pointerInside) { m_pointerInside = false; refreshPointerCursor(); }
    auto *top = receiver->window();
    if (auto *mainWindow = qobject_cast<QMainWindow *>(top);
        mainWindow && (mainWindow->cursor().shape() == Qt::SplitHCursor || mainWindow->cursor().shape() == Qt::SplitVCursor)) {
        const auto local = receiver->mapTo(mainWindow, position);
        QHoverEvent hover(QEvent::HoverMove, local, receiver->mapToGlobal(position), local, Qt::NoModifier);
        QApplication::sendEvent(mainWindow, &hover);
    }
    if (auto *handle = top->windowHandle(); handle && handle->cursor().shape() != receiver->cursor().shape())
        handle->setCursor(receiver->cursor());
}
void Canvas::finishInterruptedTabletGesture() {
    if (!m_tablet) return;
    // Resolve at the last contact position. A hover packet is never ink and
    // must not extend a selection, transform, or pan after a missing release.
    if (!m_dragging) {
        if (m_tool == Tool::SceneObject || m_tool == Tool::SceneCamera || m_tool == Tool::SceneRig) m_scene3D->pointerRelease();
        else if (m_drawingTools->dragging()) m_drawingTools->interruptGesture();
        else if (m_tool == Tool::Guides) m_guides->cancelDrag();
        else if (m_selecting) finishSelection(m_pointerPosition);
    }
    m_transformDrag = TransformDrag::None;
    finishStroke();
    m_tablet = false; m_dragging = false;
    refreshPointerCursor(); updateSelectionUi();
}
bool Canvas::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::TabletLeaveProximity) {
        finishInterruptedTabletGesture();
        m_maskTool->finishTabletContact();
        if (m_pointerInside) {
            m_pointerInside = false;
            refreshPointerCursor();
        }
    } else if (watched != this && (event->type() == QEvent::TabletMove
                                  || event->type() == QEvent::TabletPress || event->type() == QEvent::TabletRelease)) {
        const auto *tablet = static_cast<QTabletEvent *>(event);
        auto *receiver = qobject_cast<QWidget *>(watched);
        // Native QWindow packets precede the translated QWidget event. They
        // must not finish/cancel contact before the canvas receives its final
        // point. Only a real panel in this workspace resolves a lost release;
        // floating docks keep their owner through the QWidget parent chain.
        const bool endingContact = (event->type() == QEvent::TabletMove && !(tablet->buttons() & Qt::LeftButton))
            || ((event->type() == QEvent::TabletPress || event->type() == QEvent::TabletRelease)
                && tablet->button() == Qt::LeftButton);
        bool ownPanel = false;
        if (endingContact) {
            for (auto *owner = receiver; owner; owner = owner->parentWidget())
                if (owner == window()) { ownPanel = true; break; }
        }
        if (ownPanel) {
            finishInterruptedTabletGesture();
            m_maskTool->finishTabletContact();
        }
        handOffTabletCursor(receiver, tablet->position());
    }
    return QWidget::eventFilter(watched, event);
}
void Canvas::enterEvent(QEnterEvent *event) {
    trackPointer(event->position());
    QWidget::enterEvent(event);
}
void Canvas::leaveEvent(QEvent *event) {
    m_pointerInside = false;
    refreshPointerCursor();
    QWidget::leaveEvent(event);
}
QPointF Canvas::artworkPoint(QPointF position) const {
    return artworkViewTransform().inverted().map(position);
}
QTransform Canvas::artworkViewTransform() const {
    auto transform = viewTransform();
    const auto origin = sheetGeometry(project(), shot()).output.topLeft();
    transform.translate(origin.x(), origin.y());
    return cameraTransform() * transform;
}
void Canvas::cancelOverlayDrag() {
    m_guides->cancelDrag();
    m_selecting = false; m_selectionPreview = {}; m_lassoPoints.clear();
    m_transformDrag = TransformDrag::None;
    updateSelectionUi();
}
void Canvas::beginSelection(QPointF position, Qt::KeyboardModifiers modifiers) {
    if (m_document->editingGestureActive()) return;
    m_selecting = true; m_selectionOrigin = artworkPoint(position);
    const bool add = modifiers.testFlag(Qt::ShiftModifier), subtract = modifiers.testFlag(Qt::AltModifier);
    m_gestureCombine = add && subtract ? SelectionCombine::Intersect : add ? SelectionCombine::Add
        : subtract ? SelectionCombine::Subtract : m_selectionCombine;
    m_selectionPreview = {}; m_lassoPoints = {m_selectionOrigin};
    if (m_tool == Tool::LassoSelect) m_selectionPreview.moveTo(m_selectionOrigin);
    updateSelectionUi();
}
void Canvas::updateSelection(QPointF position) {
    if (!m_selecting) return;
    const auto point = artworkPoint(position);
    m_selectionPreview = {};
    if (m_tool == Tool::RectangleSelect) {
        m_selectionPreview.addRect(QRectF(m_selectionOrigin, point).normalized());
    } else {
        if (QLineF(m_lassoPoints.last(), point).length() * artworkZoom() >= 1) {
            // Keep a bounded history without dropping the beginning of a long lasso.
            if (m_lassoPoints.size() >= 4096) {
                QVector<QPointF> reduced;
                for (int i = 0; i < m_lassoPoints.size(); i += 2) reduced.append(m_lassoPoints[i]);
                reduced.append(m_lassoPoints.last()); m_lassoPoints = std::move(reduced);
            }
            m_lassoPoints.append(point);
        }
        m_selectionPreview.moveTo(m_lassoPoints.first());
        for (int i = 1; i < m_lassoPoints.size(); ++i) m_selectionPreview.lineTo(m_lassoPoints[i]);
        m_selectionPreview.closeSubpath();
    }
    updateSelectionUi();
}
void Canvas::finishSelection(QPointF position) {
    if (!m_selecting) return;
    updateSelection(position);
    const auto path = m_selectionPreview;
    const auto screenPath = artworkViewTransform().map(path);
    const auto bounds = screenPath.boundingRect();
    qreal area = 0;
    for (const auto &polygon : screenPath.simplified().toFillPolygons()) {
        qreal twiceArea = 0;
        for (int i = 0; i < polygon.size(); ++i) {
            const auto a = polygon[i], b = polygon[(i + 1) % polygon.size()];
            twiceArea += a.x() * b.y() - b.x() * a.y();
        }
        area += std::abs(twiceArea) / 2;
    }
    cancelOverlayDrag();
    // A click or a line leaves the previous selection intact.
    if (bounds.width() >= 2 && bounds.height() >= 2 && area >= 4)
        m_document->combineRasterSelection(path, m_gestureCombine);
}
QPolygonF Canvas::transformBox() const {
    if (!m_document->transformActive()) return {};
    const auto bounds = m_document->transformBounds();
    const QPolygonF corners{bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()};
    return artworkViewTransform().map(m_document->transformMatrix().map(corners));
}
QPointF Canvas::rotationHandle() const {
    const auto box = transformBox();
    if (box.size() != 4) return {};
    const auto middle = (box[0] + box[1]) / 2;
    const auto center = (box[0] + box[2]) / 2;
    auto direction = middle - center;
    const qreal length = std::hypot(direction.x(), direction.y());
    if (length > .00001) direction /= length;
    else direction = QPointF(0, -1);
    return middle + direction * 28;
}
QPainterPath Canvas::viewportSelectionPath() const {
    if (!hasProject()) return {};
    QPainterPath selection;
    if (m_selecting) {
        selection = m_selectionPreview;
        if (m_document->rasterSelection().active) {
            const auto &previous = m_document->rasterSelection().path;
            // Complex masks combine once on release using the document's
            // bounded raster fallback. Preview only the incoming gesture while
            // dragging rather than intersecting thousands of islands per move.
            if (previous.elementCount() + selection.elementCount() <= 4096) {
                switch (m_gestureCombine) {
                case SelectionCombine::Add: selection = previous.united(selection); break;
                case SelectionCombine::Subtract: selection = previous.subtracted(selection); break;
                case SelectionCombine::Intersect: selection = previous.intersected(selection); break;
                case SelectionCombine::Replace: break;
                }
            }
        }
        if (!m_document->drawingBounds().contains(selection.controlPointRect())) {
            QPainterPath extent; extent.addRect(m_document->drawingBounds());
            selection = selection.intersected(extent);
        }
    }
    else if (m_document->rasterSelection().active) {
        selection = m_document->rasterSelection().path;
        if (m_document->transformActive() && !m_document->stampPlacementActive()) selection = m_document->transformMatrix().map(selection);
    }
    if(auto preview=m_drawingTools->selectionPreview())selection=*preview;
    if(m_drawingTools->transform()->active() && m_document->rasterSelection().active)selection=m_drawingTools->transform()->selectionPath();
    return artworkViewTransform().map(selection);
}
void Canvas::paintTransformHandles(QPainter &painter) const {
    painter.save(); painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    const auto box = transformBox();
    if (box.size() == 4) {
        // Avoid duplicating an identical rectangular boundary, but keep a thin
        // box linking the handles for lassos or selections containing blank space.
        painter.setPen(QPen(QColor(ui::focus), 1));
        QPainterPath rectangle; rectangle.addRect(m_document->transformBounds());
        const bool sameBoundary = !m_document->stampPlacementActive() && m_document->rasterSelection().active && m_document->rasterSelection().path == rectangle;
        if (!sameBoundary) painter.drawPolygon(box);
        painter.drawLine((box[0] + box[1]) / 2, rotationHandle());
        painter.setPen(QPen(QColor(ui::base), 1)); painter.setBrush(QColor(ui::focus));
        for (const auto &corner : box) painter.drawRect(QRectF(corner - QPointF(4, 4), QSizeF(8, 8)));
        painter.drawEllipse(rotationHandle(), 5, 5);
    }
    painter.restore();
}
void Canvas::beginTransformDrag(QPointF position) {
    flushStampTransform();
    if (!m_document->transformActive() && !prepareToolTransform()) return;
    const auto box = transformBox();
    m_transformDrag = TransformDrag::None;
    if (QLineF(position, rotationHandle()).length() <= 11) m_transformDrag = TransformDrag::Rotate;
    else {
        for (int i = 0; i < box.size(); ++i) if (QLineF(position, box[i]).length() <= 11) {
            m_transformDrag = TransformDrag::Scale; m_resizeCorner = i; break;
        }
        if (m_transformDrag == TransformDrag::None && box.containsPoint(position, Qt::OddEvenFill))
            m_transformDrag = TransformDrag::Move;
    }
    m_transformStart = artworkPoint(position);
    m_transformStartParameters = m_document->transformParameters();
    refreshPointerCursor();
}
void Canvas::updateTransformDrag(QPointF position, Qt::KeyboardModifiers modifiers) {
    if (!m_document->transformActive() || m_transformDrag == TransformDrag::None) return;
    auto parameters = m_transformStartParameters;
    const auto point = artworkPoint(position);
    const auto bounds = m_document->transformBounds();
    const auto center = bounds.center() + parameters.translation;
    if (m_transformDrag == TransformDrag::Move) parameters.translation += point - m_transformStart;
    else if (m_transformDrag == TransformDrag::Rotate) {
        const auto start = m_transformStart - center, current = point - center;
        if (std::hypot(current.x(), current.y()) < .001) return;
        const qreal delta = (std::atan2(current.y(), current.x()) - std::atan2(start.y(), start.x())) * 180 / 3.141592653589793;
        parameters.rotation = std::remainder(parameters.rotation + delta, 360.0);
    } else {
        QTransform unrotate; unrotate.rotate(-parameters.rotation);
        const QPolygonF corners{bounds.topLeft(), bounds.topRight(), bounds.bottomRight(), bounds.bottomLeft()};
        const auto offset = corners[m_resizeCorner] - bounds.center();
        const QPointF start(offset.x() * parameters.scaleX, offset.y() * parameters.scaleY);
        // Grabbing anywhere inside a handle must not resize the artwork before moving.
        const auto local = unrotate.map(point - center) + start - unrotate.map(m_transformStart - center);
        if (modifiers.testFlag(Qt::ShiftModifier)) {
            parameters.scaleX = qBound(.01, local.x() / offset.x(), 16.0);
            parameters.scaleY = qBound(.01, local.y() / offset.y(), 16.0);
        } else {
            const qreal ratio = QPointF::dotProduct(local, start) / QPointF::dotProduct(start, start);
            const qreal minimum = qMax(.01 / parameters.scaleX, .01 / parameters.scaleY);
            const qreal maximum = qMin(16.0 / parameters.scaleX, 16.0 / parameters.scaleY);
            const qreal limited = qBound(minimum, ratio, maximum);
            parameters.scaleX *= limited; parameters.scaleY *= limited;
        }
    }
    if (m_document->stampPlacementActive()) {
        m_pendingStampTransform = parameters;
        if (!m_stampTransformTimer.isActive()) m_stampTransformTimer.start();
    } else m_document->previewTransform(parameters);
}
void Canvas::flushStampTransform() {
    m_stampTransformTimer.stop();
    const auto parameters = m_pendingStampTransform; m_pendingStampTransform.reset();
    if (parameters && m_document->stampPlacementActive()) m_document->previewTransform(*parameters);
}
bool Canvas::stampPlacementAvailable() const {
    return m_toolAvailable && m_tool == Tool::Brush && brush.connected.mode == BrushConnectedMode::Off
        && (!brush.tipMask.isNull() || !brush.colorTip.isNull())
        && !m_maskTool->enabled() && !m_cameraTool->enabled() && !m_keyframeTransformTool->enabled();
}
void Canvas::setStampPlacementEnabled(bool enabled) {
    if (m_stampPlacementEnabled == enabled) return;
    m_stampPlacementEnabled = enabled; emit stampPlacementEnabledChanged(enabled);
}
bool Canvas::prepareStampPress(QPointF position, Qt::KeyboardModifiers modifiers) {
    if (!m_document->stampPlacementActive() || m_tool != Tool::Brush || m_space
        || m_pointerEraser || modifiers.testFlag(Qt::AltModifier)) return true;
    flushStampTransform();
    const auto box = transformBox();
    bool handle = QLineF(position, rotationHandle()).length() <= 11;
    for (const auto &corner : box) handle |= QLineF(position, corner).length() <= 11;
    // A new contact outside the handles accepts this stamp and places the next.
    if (!handle && !box.containsPoint(position, Qt::OddEvenFill)) return m_document->applyTransform();
    return true;
}
void Canvas::resetStrokeConstraint() {
    m_straightStrokeTimer.stop();
    m_straightStrokeDirty = false;
    m_strokeConstraint = StrokeConstraint::None;
}
bool Canvas::holdShapePending() const { return m_holdShape->pending(); }
void Canvas::setHoldShapeEnabled(bool enabled) {
    if (m_holdShapeEnabled == enabled) return;
    if (m_document->strokeActive()) finishStroke();
    m_holdShapeEnabled = enabled; emit holdShapeEnabledChanged(enabled);
}
void Canvas::endPaintContact() {
    if (m_stampContact) {
        m_stampContact = false; updateSelectionUi(); refreshPointerCursor(); return;
    }
    if (!m_holdShape->release()) { finishStroke(); return; }
    m_strokeFilter.reset(); m_airbrushTimer.stop(); m_guides->endSnap();
    refreshPointerCursor();
}
void Canvas::flushStraightStroke() {
    m_straightStrokeTimer.stop();
    if (!m_straightStrokeDirty || m_strokeConstraint != StrokeConstraint::Straight) return;
    m_straightStrokeDirty = false;
    if (m_document->strokeActive()) m_document->previewStrokeLine(m_paintedTip, m_lastPaintInput);
}
BrushInput Canvas::timedBrushInput(QPointF point, BrushInput input, ulong timestamp, bool beginning) {
    input.speed = 0;
    // Event timestamps preserve pen velocity even when rendering/UI work delays
    // delivery. The monotonic fallback covers synthetic events without timestamps.
    static const QElapsedTimer fallbackClock = [] { QElapsedTimer timer; timer.start(); return timer; }();
    const qreal milliseconds = timestamp ? qreal(timestamp) : fallbackClock.nsecsElapsed() / 1000000.;
    if (beginning) m_brushSpeedTracker.reset(point, milliseconds);
    else if (m_document->strokeActive() && m_strokeBrush.speedDynamics.enabled)
        input.speed = m_brushSpeedTracker.update(point, milliseconds);
    return input;
}
void Canvas::beginPaintStroke(QPointF point, const BrushInput &input, bool eraser, Qt::KeyboardModifiers modifiers) {
    resetStrokeConstraint();
    if (!eraser && m_stampPlacementEnabled && stampPlacementAvailable()) {
        m_strokeFilter.reset(); m_holdShape->reset(); m_airbrushTimer.stop(); m_guides->endSnap();
        m_stampContact = true;
        if (!m_document->beginStampPlacement(point, input, brush)) m_stampContact = false;
        updateSelectionUi(); return;
    }
    // Latch at contact: releasing a modifier mid-drag must not bend the line.
    // Both flags work on macOS too, where Qt maps Command to ControlModifier.
    if (modifiers & (Qt::ControlModifier | Qt::MetaModifier)) m_strokeConstraint = StrokeConstraint::AxisPending;
    else if (modifiers & Qt::ShiftModifier) m_strokeConstraint = StrokeConstraint::Straight;
    if (m_strokeConstraint == StrokeConstraint::None) point = m_guides->beginSnap(point);
    m_strokeOrigin = point;
    m_input = m_previousInput = input; m_strokeBrush = brush; m_hasDirection = false;
    m_pointerDirection = m_initialDirection = 0;
    StrokeFilterSettings stabilization;
    stabilization.mode = m_stabilizationMode;
    stabilization.strength = m_strokeConstraint == StrokeConstraint::None && brush.smoothingEnabled ? m_stabilization : 0;
    stabilization.pixelScale = 1 / artworkZoom();
    stabilization.deadZone = m_stabilizationMode == StrokeFilterMode::Stabilizer ? m_stabilizationDeadZone : 0;
    stabilization.finishAtEndpoint = m_stabilizationFinishAtEndpoint;
    const auto sample = m_strokeFilter.begin(point, input.pressure, stabilization);
    m_paintedTip = sample.point; m_lastPaintInput = input;
    if (!m_strokeFilter.active() || !m_document->beginStroke(sample.point, input, brush, eraser)) {
        m_strokeFilter.reset();
        resetStrokeConstraint();
        return;
    }
    const bool airbrush = brush.airbrush && brush.connected.mode == BrushConnectedMode::Off;
    if (airbrush && m_strokeConstraint != StrokeConstraint::Straight)
        m_airbrushTimer.start(qMax(1, qRound(1000 / brush.airbrushRate)));
    m_holdShape->begin(point, input, m_strokeConstraint == StrokeConstraint::None && !airbrush && !m_guides->settings().snapping);
}
void Canvas::applyStrokeSamples(const QVector<StrokeSample> &samples) {
    int index = 0;
    for (const auto &sample : samples) {
        auto input = interpolateInput(m_previousInput, m_input, qreal(++index) / samples.size());
        input.pressure = sample.pressure;
        const auto points = m_strokeConstraint == StrokeConstraint::None
            ? m_guides->strokePoints(sample.point) : QVector<QPointF>{sample.point};
        const auto previous = m_lastPaintInput;
        for (int i = 0; i < points.size(); ++i) {
            m_lastPaintInput = interpolateInput(previous, input, qreal(i + 1) / points.size());
            m_paintedTip = points[i];
            m_document->continueStroke(m_paintedTip, m_lastPaintInput);
            if (!m_document->strokeActive()) break;
        }
        if (!m_document->strokeActive()) { m_strokeFilter.reset(); break; }
    }
    if (samples.isEmpty()) {
        // A stationary stabilized pen can still change pressure/tilt. Keep
        // cursor and airbrush input current without moving off the curved tip.
        m_lastPaintInput = m_input;
        m_lastPaintInput.pressure = m_strokeFilter.tip().pressure;
    }
    m_previousInput = m_input;
}
void Canvas::continuePaintStroke(QPointF point, const BrushInput &input) {
    if (!m_document->strokeActive() || !m_strokeFilter.active()) return;
    const auto cursorBefore = brushCursorDamageRect();
    if (m_holdShape->move(point, input)) return;
    if (m_strokeConstraint == StrokeConstraint::AxisPending) {
        const auto delta = point - m_strokeOrigin;
        // Ignore contact jitter; choose in artwork axes, independent of zoom.
        if (std::hypot(delta.x(), delta.y()) * artworkZoom() < 3) return;
        m_strokeConstraint = qAbs(delta.x()) >= qAbs(delta.y())
            ? StrokeConstraint::Horizontal : StrokeConstraint::Vertical;
    }
    if (m_strokeConstraint == StrokeConstraint::Horizontal) point.setY(m_strokeOrigin.y());
    else if (m_strokeConstraint == StrokeConstraint::Vertical) point.setX(m_strokeOrigin.x());
    if (m_strokeConstraint == StrokeConstraint::Straight) {
        if (!std::isfinite(point.x()) || !std::isfinite(point.y()) || !std::isfinite(input.pressure)) return;
        m_strokeFilter.advance(point, input.pressure);
        if (!m_strokeFilter.active()) { m_document->cancelStroke(); refreshPointerCursor(); return; }
        if (point != m_paintedTip || input != m_lastPaintInput) {
            m_paintedTip = point; m_input = m_previousInput = m_lastPaintInput = input;
            m_straightStrokeDirty = true;
            // Coalesce endpoint changes, not pen samples of ordinary strokes.
            if (!m_straightStrokeTimer.isActive()) m_straightStrokeTimer.start();
        }
        update(cursorBefore.united(brushCursorDamageRect()).adjusted(-3, -3, 3, 3).toAlignedRect());
        return;
    }
    const auto snapped = m_strokeConstraint == StrokeConstraint::None ? m_guides->snap(point) : std::optional<QPointF>(point);
    if (!snapped) {
        // Choosing a guide direction holds position, but pressure and pose
        // still respond while the pen rests inside that initial dead zone.
        m_input = m_lastPaintInput = m_previousInput = input;
        m_strokeFilter.advance(m_strokeFilter.raw().point, input.pressure);
        if (!m_strokeFilter.active()) { m_document->cancelStroke(); refreshPointerCursor(); return; }
        update(cursorBefore.united(brushCursorDamageRect()).adjusted(-3, -3, 3, 3).toAlignedRect());
        return;
    }
    point = *snapped;
    const auto delta = point - m_strokeFilter.raw().point;
    if (std::hypot(delta.x(), delta.y()) > .01) {
        m_pointerDirection = std::atan2(delta.y(), delta.x()) * 180 / 3.141592653589793;
        if (!m_hasDirection) { m_initialDirection = m_pointerDirection; m_hasDirection = true; }
    }
    m_input = input;
    const auto samples = m_strokeFilter.advance(point, input.pressure);
    if (!m_strokeFilter.active()) { m_document->cancelStroke(); refreshPointerCursor(); return; }
    applyStrokeSamples(samples);
    update(cursorBefore.united(brushCursorDamageRect()).adjusted(-3, -3, 3, 3).toAlignedRect());
}
void Canvas::finishStroke() {
    flushStampTransform();
    const bool stampContact = m_stampContact; m_stampContact = false;
    m_drawingTools->finishGesture();
    m_airbrushTimer.stop();
    flushStraightStroke();
    if (m_document->strokeActive() && m_strokeFilter.active() && m_strokeConstraint != StrokeConstraint::Straight && !m_holdShape->fitted()) {
        const auto raw = m_strokeFilter.raw();
        const auto tail = m_strokeFilter.finish(raw.point, raw.pressure);
        if (m_strokeFilter.failed()) {
            m_document->cancelStroke();
            emit m_document->warning("This stroke exceeds the stabilizer's completion limit. Use Smooth mode or a lower strength for tight, repeated loops.");
        } else applyStrokeSamples(tail);
    }
    m_strokeFilter.reset();
    m_holdShape->reset();
    resetStrokeConstraint();
    m_document->endStroke();
    m_guides->endSnap();
    refreshPointerCursor();
    if (stampContact) updateSelectionUi();
}
void Canvas::mousePressEvent(QMouseEvent *event) {
    if (m_tablet || event->source() != Qt::MouseEventNotSynthesized) return;
    m_tabletCursorOwned = false;
    if (m_holdShape->pending()) finishStroke();
    setFocus();
    m_input = {};
    m_pointerEraser = false;
    m_pointerModifiers = event->modifiers();
    trackPointer(event->position());
    if (hasProject() && (event->button() == Qt::MiddleButton || (event->button() == Qt::LeftButton && (m_tool == Tool::Hand || m_space)))) {
        finishStroke(); cancelOverlayDrag();
        m_dragging = true; m_last = event->position(); refreshPointerCursor();
    } else if(hasProject() && m_tool!=Tool::Hand && m_reference3D->mousePress(event,artworkViewTransform(),{project().width,project().height})){
        event->accept();return;
    } else if (event->button() == Qt::RightButton && m_tool == Tool::Guides) {
        cancelGuideCreation();
    } else if (event->button() == Qt::LeftButton) {
        if (!prepareStampPress(event->position(), event->modifiers())) { event->accept(); return; }
        if (rejectBlockedAction()) { event->accept(); return; }
        if (m_scene3D->pointerPress(artworkPoint(event->position()),event->modifiers())) {}
        else if (m_drawingTools->press(event->position(),event->modifiers())) {}
        else if (m_tool == Tool::Guides) m_guides->press(event->position(), event->modifiers());
        else if (m_tool == Tool::Transform || m_document->transformActive()) beginTransformDrag(event->position());
        else if (m_tool == Tool::RectangleSelect || m_tool == Tool::LassoSelect)
            beginSelection(event->position(), event->modifiers());
        else beginPaintStroke(artworkPoint(event->position()), timedBrushInput(artworkPoint(event->position()), {}, event->timestamp(), true), m_tool == Tool::Eraser, event->modifiers());
    }
    event->accept();
}
void Canvas::mouseMoveEvent(QMouseEvent *event) {
    if (m_tablet || event->source() != Qt::MouseEventNotSynthesized) return;
    m_tabletCursorOwned = false;
    if (!m_document->strokeActive()) m_input = {};
    if (m_pointerEraser) { m_pointerEraser = false; refreshPointerCursor(); }
    if (m_pointerModifiers != event->modifiers()) { m_pointerModifiers = event->modifiers(); refreshPointerCursor(); }
    trackPointer(event->position());
    if (!hasProject()) return;
    if(!m_dragging && m_reference3D->mouseMove(event,artworkViewTransform(),{project().width,project().height})){event->accept();return;}
    if (m_stampContact && !(event->buttons() & Qt::LeftButton)) endPaintContact();
    if (m_dragging) {
        const auto inverse = viewTransform().inverted();
        centerOn(m_center + inverse.map(m_last) - inverse.map(event->position()));
        m_last = event->position();
    } else if (!m_toolAvailable) return;
    else if (m_tool == Tool::SceneObject || m_tool == Tool::SceneCamera || m_tool == Tool::SceneRig) {
        if (!(event->buttons() & Qt::LeftButton)) m_scene3D->pointerRelease();
        else m_scene3D->pointerMove(artworkPoint(event->position()),event->modifiers());
    }
    else if (m_drawingTools->dragging() && !(event->buttons() & Qt::LeftButton)) { m_drawingTools->interruptGesture(); }
    else if (m_drawingTools->move(event->position(),event->modifiers())) {}
    else if (m_tool == Tool::Guides) {
        if (m_guides->dragging() && !(event->buttons() & Qt::LeftButton)) m_guides->cancelDrag();
        m_guides->move(event->position(), event->modifiers());
    }
    else if (m_selecting) updateSelection(event->position());
    else if (m_transformDrag != TransformDrag::None) updateTransformDrag(event->position(), event->modifiers());
    else if (m_document->strokeActive()) continuePaintStroke(artworkPoint(event->position()), timedBrushInput(artworkPoint(event->position()), {}, event->timestamp()));
}
void Canvas::mouseReleaseEvent(QMouseEvent *event) {
    if(hasProject()&&!m_dragging&&m_reference3D->mouseRelease(event,artworkViewTransform(),{project().width,project().height})){event->accept();return;}
    if (m_tablet || event->source() != Qt::MouseEventNotSynthesized) return;
    trackPointer(event->position());
    if (!hasProject()) return;
    if (m_toolAvailable && event->button() == Qt::LeftButton && !m_dragging) {
        if (m_tool == Tool::SceneObject || m_tool == Tool::SceneCamera || m_tool == Tool::SceneRig) m_scene3D->pointerRelease();
        else if (m_drawingTools->release(event->position(),event->modifiers())) {}
        else if (m_tool == Tool::Guides) m_guides->release(event->position(), event->modifiers());
        else if (m_selecting) finishSelection(event->position());
        else if (m_transformDrag != TransformDrag::None) {
            updateTransformDrag(event->position(), event->modifiers()); flushStampTransform(); m_transformDrag = TransformDrag::None; updateSelectionUi();
        } else if (m_stampContact) {
            endPaintContact();
        } else if (m_document->strokeActive()) {
            continuePaintStroke(artworkPoint(event->position()), timedBrushInput(artworkPoint(event->position()), {}, event->timestamp()));
            endPaintContact();
        }
    }
    m_dragging = false;
    refreshPointerCursor();
}
void Canvas::wheelEvent(QWheelEvent *event) {
    if(hasProject()&&m_tool!=Tool::Hand&&!m_space&&m_reference3D->wheel(event,artworkViewTransform(),{project().width,project().height})){event->accept();return;}
    if (m_document->strokeActive()) { event->accept(); return; }
    const qreal delta = event->angleDelta().y() ? event->angleDelta().y() : event->pixelDelta().y();
    zoomAt(m_zoom * std::pow(1.0015, delta), event->position());
    event->accept();
}
void Canvas::tabletEvent(QTabletEvent *event) {
    if(hasProject()&&m_tool!=Tool::Hand&&!m_space&&m_reference3D->tablet(event,artworkViewTransform(),{project().width,project().height})){event->accept();return;}
    event->accept(); // Prevent a second, synthesized mouse stroke.
    const auto syncCursor = qScopeGuard([this, event] { syncTabletCursor(event->position()); });
    if (m_tablet && ((event->type() == QEvent::TabletMove && !(event->buttons() & Qt::LeftButton))
        || (event->type() == QEvent::TabletPress && event->button() == Qt::LeftButton)))
        finishInterruptedTabletGesture();
    const bool eraser = event->pointerType() == QPointingDevice::PointerType::Eraser;
    if (m_pointerEraser != eraser) { m_pointerEraser = eraser; refreshPointerCursor(); }
    if (m_pointerModifiers != event->modifiers()) { m_pointerModifiers = event->modifiers(); refreshPointerCursor(); }
    if (!hasProject()) {
        trackPointer(event->position());
        if (event->type() == QEvent::TabletPress && event->button() == Qt::LeftButton) rejectBlockedAction();
        return;
    }
    if (m_tool == Tool::Guides && event->type() == QEvent::TabletPress && event->button() == Qt::RightButton) {
        cancelGuideCreation(); return;
    }
    BrushInput input;
    input.pressure = event->pressure();
    const auto view = artworkViewTransform();
    const auto scale = artworkZoom();
    const auto inverseOrientation = QTransform(view.m11() / scale, view.m12() / scale,
        view.m21() / scale, view.m22() / scale, 0, 0).inverted();
    const auto tilt = inverseOrientation.map(QPointF(event->xTilt(), event->yTilt()));
    input.tiltX = qBound(-90.0, tilt.x(), 90.0); input.tiltY = qBound(-90.0, tilt.y(), 90.0);
    if (event->pointingDevice()->capabilities().testFlag(QInputDevice::Capability::Rotation)) {
        const auto radians = qDegreesToRadians(event->rotation());
        const auto direction = inverseOrientation.map(QPointF(std::cos(radians), std::sin(radians)));
        input.rotation = qRadiansToDegrees(std::atan2(direction.y(), direction.x()));
    }
    input.wheel = event->pointingDevice()->capabilities().testFlag(QInputDevice::Capability::TangentialPressure)
        ? qBound(0.0, (event->tangentialPressure() + 1) / 2, 1.0) : 1;
    input = timedBrushInput(artworkPoint(event->position()), input, event->timestamp(), event->type() == QEvent::TabletPress);
    if (!m_document->strokeActive()) m_input = input;
    const qreal lastContactPressure = m_pointerPressure;
    trackPointer(event->position(), event->pressure());
    if (event->type() == QEvent::TabletMove && !m_tablet && m_tool == Tool::Guides) m_guides->move(event->position(), event->modifiers());
    if (event->type() == QEvent::TabletMove && !m_tablet && m_toolAvailable && m_drawingTools->waitingForArcBend())
        m_drawingTools->move(event->position(), event->modifiers(), input.pressure);
    if (event->type() == QEvent::TabletPress && event->button() == Qt::LeftButton) {
        if (m_holdShape->pending()) finishStroke();
        if(m_tool==Tool::Hand||m_space){setFocus();m_tablet=true;cancelOverlayDrag();m_dragging=true;m_last=event->position();refreshPointerCursor();return;}
        if (!prepareStampPress(event->position(), event->modifiers())) return;
        if (rejectBlockedAction()) return;
        setFocus(); m_tablet = true;
        if (m_tool == Tool::Hand || m_space) {
            cancelOverlayDrag(); m_dragging = true; m_last = event->position(); refreshPointerCursor();
        } else if (m_scene3D->pointerPress(artworkPoint(event->position()),event->modifiers())) {}
        else if (m_drawingTools->press(event->position(),event->modifiers(),input.pressure)) {}
        else if (m_tool == Tool::Guides) m_guides->press(event->position(), event->modifiers());
        else if (m_tool == Tool::Transform || m_document->transformActive()) beginTransformDrag(event->position());
        else if (m_tool == Tool::RectangleSelect || m_tool == Tool::LassoSelect)
            beginSelection(event->position(), event->modifiers());
        else beginPaintStroke(artworkPoint(event->position()), input,
            m_tool == Tool::Eraser || event->pointerType() == QPointingDevice::PointerType::Eraser, event->modifiers());
    } else if (event->type() == QEvent::TabletMove && m_tablet) {
        if (m_dragging) {
            const auto inverse = viewTransform().inverted();
            centerOn(m_center + inverse.map(m_last) - inverse.map(event->position()));
            m_last = event->position();
        } else if (m_tool == Tool::SceneObject || m_tool == Tool::SceneCamera || m_tool == Tool::SceneRig) m_scene3D->pointerMove(artworkPoint(event->position()),event->modifiers());
        else if (m_drawingTools->move(event->position(),event->modifiers(),input.pressure)) {}
        else if (m_tool == Tool::Guides) m_guides->move(event->position(), event->modifiers());
        else if (m_selecting) updateSelection(event->position());
        else if (m_transformDrag != TransformDrag::None) updateTransformDrag(event->position(), event->modifiers());
        else continuePaintStroke(artworkPoint(event->position()), input);
    } else if (event->type() == QEvent::TabletRelease && event->button() == Qt::LeftButton && m_tablet) {
        // Pen release commonly reports zero pressure. Preserve the last contact
        // pressure while including any final position delivered only on release.
        if (!m_dragging && (m_tool == Tool::SceneObject || m_tool == Tool::SceneCamera || m_tool == Tool::SceneRig)) m_scene3D->pointerRelease();
        else if (!m_dragging && m_drawingTools->release(event->position(),event->modifiers(),lastContactPressure>0?lastContactPressure:1)) {}
        else if (!m_dragging && m_tool == Tool::Guides) m_guides->release(event->position(), event->modifiers());
        else if (!m_dragging && m_selecting) finishSelection(event->position());
        else if (!m_dragging && m_transformDrag != TransformDrag::None) {
            updateTransformDrag(event->position(), event->modifiers()); flushStampTransform(); m_transformDrag = TransformDrag::None; updateSelectionUi();
        } else if (!m_dragging && m_strokeFilter.active()) {
            input.pressure = m_strokeFilter.raw().pressure;
            continuePaintStroke(artworkPoint(event->position()), input);
        }
        endPaintContact(); m_tablet = false; m_dragging = false;
        refreshPointerCursor();
    }
}
void Canvas::keyPressEvent(QKeyEvent *event) {
    if(m_reference3D->keyPress(event)){event->accept();return;}
    if (event->key() == Qt::Key_Alt) { m_pointerModifiers |= Qt::AltModifier; refreshPointerCursor(); }
    if(event->key()==Qt::Key_Escape && m_scene3D->editing()){m_scene3D->cancelEdit();event->accept();return;}
    if((event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace)&&m_tool==Tool::SceneRig){m_scene3D->removeSelectedJoint();event->accept();return;}
    if((event->key()==Qt::Key_Delete||event->key()==Qt::Key_Backspace)&&m_tool==Tool::SceneObject){m_scene3D->removeSelected();event->accept();return;}
    if(m_toolAvailable && m_drawingTools->key(event->key(),event->modifiers())){refreshPointerCursor();event->accept();return;}
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        if (!m_document->strokeActive()) { m_space = true; refreshPointerCursor(); }
        event->accept();
    } else if (event->key() == Qt::Key_Escape) {
        const bool hadGesture = selectionGestureActive() || m_document->transformActive();
        m_airbrushTimer.stop(); m_strokeFilter.reset(); m_guides->cancel(); m_guides->endSnap(); cancelOverlayDrag();
        resetStrokeConstraint();
        m_holdShape->reset();
        m_document->cancelStroke(); m_document->cancelTransform();
        m_tablet = false; m_dragging = false;
        if (!hadGesture) m_document->clearRasterSelection();
        refreshPointerCursor(); event->accept();
    } else if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && m_toolAvailable && m_tool == Tool::Guides) {
        removeSelectedGuide(); event->accept();
    } else if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_document->transformActive()) {
        m_transformDrag = TransformDrag::None; m_document->applyTransform(); event->accept();
    } else QWidget::keyPressEvent(event);
}
void Canvas::keyReleaseEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Alt) { m_pointerModifiers &= ~Qt::AltModifier; refreshPointerCursor(); }
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_space = false; refreshPointerCursor();
        event->accept();
    } else QWidget::keyReleaseEvent(event);
}
void Canvas::focusOutEvent(QFocusEvent *event) {
    m_reference3D->finishInteraction();
    m_scene3D->pointerRelease();
    finishStroke(); cancelOverlayDrag(); m_dragging = false; m_space = false; m_tablet = false;
    m_pointerModifiers = Qt::NoModifier;
    m_pointerEraser = false;
    m_pointerInside = false;
    refreshPointerCursor();
    QWidget::focusOutEvent(event);
}

Navigator::Navigator(Canvas *canvas, QWidget *parent) : QWidget(parent), m_canvas(canvas) {
    m_effectsPreview = new EffectsPreviewRenderer(this);
    setProperty("effectsPreviewPending", false);
    setProperty("effectsPreviewPublishedRevision", qulonglong(0));
    setProperty("effectsPreviewSourceScale", 1.0);
    setProperty("effectsPreviewExact", true);
    setProperty("effectsPreviewFrame", -1);
    connect(m_effectsPreview, &EffectsPreviewRenderer::completed, this, [this](const EffectsPreviewResult &result) {
        if (result.cancelled || result.context != m_previewContext || result.revision < m_publishedRevision ||
            !m_canvas->hasProject() || !isVisible()) return;
        const bool exact = result.quality == EffectsPreviewQuality::Exact && result.sourceScale == 1;
        if (result.quality == EffectsPreviewQuality::Exact && result.revision != m_previewRevision && !(result.playback && m_canvas->animationPlayback())) return;
        if (!result.image.isNull() && result.error.isEmpty()) {
            m_preview = result.image; m_publishedRevision = result.revision;
            setProperty("effectsPreviewPublishedRevision", qulonglong(result.revision));
            setProperty("effectsPreviewSourceScale", result.sourceScale);
            setProperty("effectsPreviewExact", exact);
            setProperty("effectsPreviewFrame", result.frame);
        }
        if ((exact || result.playback) && result.revision == m_previewRevision && !m_previewDirty && !m_strokePreviewQueued) {
            setProperty("effectsPreviewPending", false);
            if (!result.playback && exact && !shotHasCompositing(m_canvas->project(), m_canvas->shot())) m_previewActive = false;
        }
        update();
    });
    connect(canvas->scene3D(), &Scene3DController::previewChanged, this, [this] { invalidatePreview(); });
    setMinimumSize(200, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::OpenHandCursor);
    setAccessibleName("Canvas navigator: drag to pan");
    connect(canvas, &Canvas::viewChanged, this, qOverload<>(&Navigator::update));
    connect(canvas, &Canvas::documentChanged, this, [this] {
        const bool editing = m_canvas->document()->effectsEditActive() || m_canvas->document()->keyframeEditActive();
        invalidatePreview(!editing || !m_effectsEditWasActive);
        m_effectsEditWasActive = editing;
        if (!m_canvas->document()->strokeActive()) m_strokePreviewActive = false;
    });
    const auto queueStrokePreview = [this] {
        // Normal drawing needs no additional work here. An effects miniature
        // consumes at most one copied snapshot per delivered display batch,
        // never one per tablet sample, and does all compositing in its worker.
        if (!isVisible() || !m_previewActive || m_strokePreviewQueued || !m_canvas->document()->strokeActive()) return;
        m_strokePreviewQueued = true;
        if (!property("effectsPreviewPending").toBool()) setProperty("effectsPreviewPending", true);
        QTimer::singleShot(0, this, [this] {
            m_strokePreviewQueued = false;
            if (!isVisible() || !m_canvas->document()->strokeActive()) return;
            invalidatePreview(!m_strokePreviewActive);
            m_strokePreviewActive = true;
        });
    };
    connect(canvas->document(), &Document::strokeUpdated, this, queueStrokePreview);
    connect(canvas->document(), &Document::changed, this, queueStrokePreview);
    connect(canvas, &Canvas::frameViewChanged, this, [this] { invalidatePreview(!m_canvas->animationPlayback()); });
    connect(canvas, &Canvas::timelineSoloChanged, this, [this] { invalidatePreview(true); });
    connect(canvas, &Canvas::animationPlaybackChanged, this, [this] { invalidatePreview(true); });
    connect(canvas, &Canvas::playbackPreviewChanged, this, [this] { if(m_canvas->animationPlayback())invalidatePreview(true); });
    connect(canvas, &Canvas::canvasSmoothingChanged, this, [this] { invalidatePreview(true); });
    connect(canvas, &Canvas::toolChanged, this, [this] { invalidatePreview(true); });
    connect(canvas->document(), &Document::selectionChanged, this, [this] {
        if (!m_canvas->animationPlayback()) invalidatePreview(true);
    });
    connect(canvas->document(), &Document::shotChanged, this, [this] {
        m_preview = {}; m_effectsEditWasActive = m_strokePreviewActive = false; invalidatePreview(true);
    });
}
void Navigator::invalidatePreview(bool contextChanged) {
    if (contextChanged) {
        ++m_previewContext; m_publishedRevision = 0;
        m_effectsPreview->invalidate();
    }
    m_previewDirty = true;
    if (m_canvas->needsAsyncEffectsPreview()) m_previewActive = true;
    if (isVisible() && m_canvas->hasProject() && m_previewActive)
        setProperty("effectsPreviewPending", true);
    update();
}
bool Navigator::event(QEvent *event) {
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::DevicePixelRatioChange) invalidatePreview(true);
    return handled;
}
void Navigator::resizeEvent(QResizeEvent *) { invalidatePreview(true); }
void Navigator::showEvent(QShowEvent *event) { QWidget::showEvent(event); invalidatePreview(true); }
void Navigator::hideEvent(QHideEvent *event) {
    ++m_previewContext; m_effectsPreview->invalidate(); m_previewDirty = true;
    setProperty("effectsPreviewPending", false);
    QWidget::hideEvent(event);
}
QTransform Navigator::miniatureTransform() const {
    if (!m_canvas->hasProject()) return {};
    const auto paper = m_canvas->paperRect();
    const qreal scale = qMin((width() - 24.0) / paper.width(), (height() - 24.0) / paper.height());
    QTransform t;
    t.translate(width() / 2.0, height() / 2.0);
    t.scale(scale, scale);
    t.translate(-paper.center().x(), -paper.center().y());
    return t;
}
void Navigator::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor(ui::base));
    if (!m_canvas->hasProject()) { setProperty("effectsPreviewPending", false); return; }
    if (m_previewActive || m_canvas->needsAsyncEffectsPreview()) {
        if (m_previewDirty) {
            auto snapshot = m_canvas->effectsPreviewSnapshot(this, miniatureTransform(), true,
                m_previewBackground, m_previewBackgroundKey, m_previewBackgroundGeometry);
            snapshot.context = m_previewContext; snapshot.revision = ++m_previewRevision;
            snapshot.quality = m_canvas->document()->effectsEditActive() || m_canvas->document()->keyframeEditActive() || m_canvas->document()->strokeActive()
                ? EffectsPreviewQuality::Draft : EffectsPreviewQuality::Exact;
            m_previewActive = true; m_previewDirty = false;
            setProperty("effectsPreviewPending", true);
            m_effectsPreview->request(std::move(snapshot));
        }
        p.drawImage(QPointF(), m_preview);
        p.setTransform(miniatureTransform());
    } else {
        m_canvas->reference3D()->prepareLayerPreviews({m_canvas->project().width,m_canvas->project().height});
        const auto references=m_canvas->reference3D()->preparedLayerPreviews();
        const ReferenceLayerPreviewScope nativeScope(references);
        const Scene3DPreviewScope sceneView(m_canvas->scene3D()->previewBaseKey(),m_canvas->scene3D()->previewDisplayKey());
        p.setTransform(miniatureTransform());
        paintSheet(p, m_canvas->project(), m_canvas->displayShot(), true,
            m_canvas->drawingTools()->transform()->active() ? m_canvas->drawingTools()->transform()->preview() : m_canvas->document()->transformPreview(),
            m_canvas->canvasSmoothing(), true, m_canvas->document()->currentFrame(), m_canvas->cameraView());
        m_previewDirty = false;
        setProperty("effectsPreviewPending", false);
    }
    QPen outline(QColor(ui::accent), 2);
    outline.setCosmetic(true);
    p.setPen(outline);
    QColor viewportTint(ui::accent); viewportTint.setAlpha(22); p.setBrush(viewportTint);
    p.drawPolygon(m_canvas->visiblePolygon());
}
void Navigator::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !m_canvas->hasProject()) return;
    const auto point = miniatureTransform().inverted().map(event->position());
    m_offset = m_canvas->visiblePolygon().containsPoint(point, Qt::OddEvenFill)
        ? point - m_canvas->center() : QPointF();
    m_dragging = true;
    setCursor(Qt::ClosedHandCursor);
    m_canvas->centerOn(point - m_offset);
}
void Navigator::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging) m_canvas->centerOn(miniatureTransform().inverted().map(event->position()) - m_offset);
}
void Navigator::mouseReleaseEvent(QMouseEvent *) { m_dragging = false; setCursor(Qt::OpenHandCursor); }
} // namespace toon

namespace toon {
Shot Canvas::displayShot() const {
    auto value=shot();
    if (!m_timelineSoloLayers.isEmpty()) { refreshTimelineSoloPreview(); value.layers=m_timelineSoloPreview; }
    if(m_document->keyframeSourceEditing())value.layerAnimations.clear();return value;
}
void Canvas::refreshTimelineSoloPreview() const {
    if (!hasProject() || m_timelineSoloLayers.isEmpty()) return;
    if (m_timelineSoloSource.constData()==shot().layers.constData()) return;
    // Retain the source's implicitly shared vector as the cache identity. This
    // keeps old addresses alive and avoids copying every layer on every frame.
    m_timelineSoloSource=shot().layers;
    m_timelineSoloKeptLayers=timelineSoloKeptLayerIds(m_timelineSoloSource,m_timelineSoloLayers);
    m_timelineSoloPreview=timelineSoloPreviewLayers(m_timelineSoloSource,m_timelineSoloKeptLayers);
}
bool Canvas::timelineSoloIncludesLayer(const QString &layerId) const {
    if (m_timelineSoloLayers.isEmpty()) return true;
    refreshTimelineSoloPreview(); return m_timelineSoloKeptLayers.contains(layerId);
}
bool Canvas::setTimelineSoloLayers(const QSet<QString> &layerIds) {
    if (layerIds==m_timelineSoloLayers) return true;
    if (!hasProject() || m_document->editingGestureActive() || m_document->layerPropertyEditActive()
        || selectionGestureActive() || m_drawingTools->active() || m_scene3D->editing()) return false;
    QSet<QString> available; for (const auto &layer:shot().layers) available.insert(layer.id);
    for (const auto &id:layerIds) if (!available.contains(id)) return false;
    m_timelineSoloLayers=layerIds;
    m_timelineSoloSource.clear(); m_timelineSoloPreview.clear(); m_timelineSoloKeptLayers.clear();
    m_onionCache={}; m_animationDisplayKey.clear();
    invalidateEffectsPreviewContext(); invalidateArtworkCache(); refreshPointerCursor(); updateSelectionUi();
    emit timelineSoloChanged(); return true;
}
void Canvas::synchronizeTimelineSolo(bool reset) {
    if (m_timelineSoloLayers.isEmpty()) return;
    if (!reset && m_document->strokeActive()) return;
    auto next=m_timelineSoloLayers;
    if (reset || !hasProject()) next.clear();
    else {
        QSet<QString> available; for (const auto &layer:shot().layers) available.insert(layer.id);
        next.intersect(available);
    }
    if (next==m_timelineSoloLayers) return;
    m_timelineSoloLayers=next;
    m_timelineSoloSource.clear(); m_timelineSoloPreview.clear(); m_timelineSoloKeptLayers.clear();
    m_onionCache={}; m_animationDisplayKey.clear();
    invalidateEffectsPreviewContext(); invalidateArtworkCache(); refreshPointerCursor();
    emit timelineSoloChanged();
}
}
