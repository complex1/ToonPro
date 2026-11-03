#include "document.h"
#include "keyframe_render.h"
#include "vector_brush.h"

#include <cmath>

namespace toon {
namespace {
bool validStampInput(QPointF point, const BrushInput &input) {
    return std::isfinite(point.x()) && std::isfinite(point.y())
        && qAbs(point.x()) <= 32768 && qAbs(point.y()) <= 32768
        && std::isfinite(input.pressure) && input.pressure >= 0 && input.pressure <= 1
        && std::isfinite(input.tiltX) && qAbs(input.tiltX) <= 90
        && std::isfinite(input.tiltY) && qAbs(input.tiltY) <= 90
        && std::isfinite(input.rotation) && qAbs(input.rotation) <= 720
        && std::isfinite(input.wheel) && input.wheel >= 0 && input.wheel <= 1
        && std::isfinite(input.speed) && input.speed >= 0 && input.speed <= 1000000;
}

QRectF stampFootprint(const EditableObject &object) {
    const auto &source = *object.brushStroke;
    const auto &sample = source.samples.first();
    QRectF bounds;
    const int copies = brushDabCount(source.settings, sample.input, 0, source.seed);
    for (int copy = 0; copy < copies; ++copy) {
        const auto seed = source.seed ^ (quint32(copy) * 0x9e3779b9U);
        const auto dab = evaluateBrushDab(source.settings, sample.input, sample.point, 0, 0, 0,
                                           seed, {}, BrushDabPurpose::Painting, copy, source.seed);
        if (dab.mask.isNull() || dab.flow <= 0 || dab.opacity <= 0) continue;
        const auto &mask = dab.untexturedMask.isNull() ? dab.mask : dab.untexturedMask;
        const QRectF rectangle(-mask.width() * .5, -mask.height() * .5, mask.width(), mask.height());
        for (const auto &instance : brushSymmetryInstances(source.settings.symmetry,
                sample.point + dab.offset, source.canvasSize)) {
            const auto footprint = instance.transform.mapRect(rectangle).translated(instance.point);
            bounds = bounds.isEmpty() ? footprint : bounds.united(footprint);
        }
    }
    return bounds.isEmpty() ? QRectF() : bounds.adjusted(-2, -2, 2, 2);
}

bool supportedStampBounds(QRectF bounds) {
    return !bounds.isEmpty() && std::isfinite(bounds.left()) && std::isfinite(bounds.top())
        && std::isfinite(bounds.right()) && std::isfinite(bounds.bottom())
        && bounds.left() >= -32768 && bounds.top() >= -32768
        && bounds.right() <= 32768 && bounds.bottom() <= 32768;
}
} // namespace

bool Document::beginStampPlacement(QPointF point, BrushInput input, BrushSettings settings) {
    if (!m_open || editingGestureActive() || layerPropertyEditActive()) return false;
    const auto animationReason = keyframeRasterEditReason(this);
    if (!animationReason.isEmpty()) { emit warning(animationReason); return false; }
    if (!validStampInput(point, input) || !validateBrushSettings(settings) || !validateBrushTipImages(settings)) {
        emit warning("The stamp brush or pen input is invalid."); return false;
    }
    const auto &shot = m_project.shots[activeShotIndex()];
    const auto &layer = shot.layers[m_active];
    if (!layerPresentAtFrame(shot, layer.id, m_currentFrame)) {
        emit warning("Choose a drawing exposed at the current frame."); return false;
    }
    const bool vector = layer.kind == LayerKind::Vector && layerCanEdit(shot.layers, m_active);
    if (!layerCanPaint(shot.layers, m_active) && !vector) {
        emit warning("Choose a visible, unlocked raster or vector drawing layer to place a stamp."); return false;
    }
    if (vector && layer.objects.size() >= 1024) {
        emit warning("A vector layer supports up to 1024 editable strokes."); return false;
    }
    settings.size = qBound(1., settings.size, maxBrushSize);
    settings.spacing = qBound(.01, settings.spacing, 4.);
    input = resolvedBrushInput(settings, input);
    const auto seed = settings.lockRandomSeed ? settings.randomSeed : m_randomSeed;
    // Match ordinary stroke contact: per-stroke color variation resolves once,
    // and every subsequent transform keeps this same impression and seed.
    resolveBrushStrokeColor(settings, input, seed);
    auto object = EditableObject::create();
    object.stroke = settings.color; object.strokeWidth = settings.size;
    object.nodes = {BezierNode::corner(point), BezierNode::corner(point)};
    VectorBrushStroke source;
    source.settings = settings; source.seed = seed;
    source.samples = {{0, point, input, 0, 0, 0}};
    source.canvasSize = QSizeF(m_project.width, m_project.height);
    source.alphaLocked = layer.alphaLocked;
    source.selection = m_selection.path; source.selectionActive = m_selection.active;
    source.selectionFeather = m_selection.featherRadius;
    source.sourceGeometry = editableGeometrySignature(object);
    object.brushStroke = std::move(source);
    const auto bounds = stampFootprint(object);
    if (!supportedStampBounds(bounds)) {
        emit warning(bounds.isEmpty() ? "Increase the stamp size, opacity or pen pressure to place a visible stamp."
                                     : "The stamp exceeds the supported drawing coordinates.");
        return false;
    }
    const auto validation = validateEditableObject(object);
    if (!validation.isEmpty()) { emit warning(validation); return false; }
    const int otherTiles = projectTileCount(m_project, layer.id);
    m_stampPlacement = StampPlacement{object, m_selection, vector, otherTiles};
    m_transformBounds = bounds; m_transformParameters = {};
    QString error; RasterLayer rendered;
    if (!renderStampPlacement({}, false, &rendered, nullptr, &error)) {
        m_stampPlacement.reset(); m_transformBounds = {};
        emit warning(error); return false;
    }
    RasterLayerPreview preview;
    preview.layerId = layer.id; preview.stationary = std::move(rendered);
    // The candidate is already composited with the brush's blend/alpha rules.
    // The generic SourceOver moving-pixels preview would change those rules.
    preview.moving = RasterLayer::create(); preview.sourceUnchangedAtIdentity = false;
    m_transformPreview = std::move(preview); m_transformRasterized.reset(); m_transformRows.clear();
    if (!settings.lockRandomSeed) m_randomSeed += 0x9e3779b9U;
    m_strokeSeed = seed;
    emit transformChanged();
    return true;
}

bool Document::renderStampPlacement(const QTransform &transform, bool clipped, RasterLayer *output,
                                    EditableObject *placedObject, QString *error) const {
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!m_stampPlacement || !output) return fail("There is no stamp waiting to be placed.");
    auto object = m_stampPlacement->source;
    object.transform = transform;
    // Selections remain fixed in the artwork while choosing the placement.
    // Store the inverse-mapped mask in an editable vector capture so its replay
    // produces the same committed pixels without moving the document selection.
    if (m_stampPlacement->selection.active) {
        bool invertible = false; const auto inverse = transform.inverted(&invertible);
        if (!invertible) return fail("The stamp transform must remain invertible.");
        object.brushStroke->selection = inverse.map(m_stampPlacement->selection.path);
    }
    const auto validation = validateEditableObject(object);
    if (!validation.isEmpty()) return fail(validation);
    QRectF bounds = drawingBounds();
    if (!clipped) bounds = bounds.united(transform.mapRect(m_transformBounds));
    auto layer = m_project.shots[activeShotIndex()].layers[m_active];
    if (!replayVectorBrushStroke(layer, object, bounds, maxProjectTiles - m_stampPlacement->otherTiles, error)) return false;
    *output = std::move(layer);
    if (placedObject) *placedObject = std::move(object);
    return true;
}

bool Document::previewStampPlacement(const TransformParameters &parameters, const QTransform &transform) {
    RasterLayer rendered; QString error;
    if (!renderStampPlacement(transform, false, &rendered, nullptr, &error)) { emit warning(error); return false; }
    m_transformPreview->stationary = std::move(rendered);
    m_transformPreview->transform = transform;
    m_transformParameters = parameters; m_transformRasterized.reset();
    emit transformChanged(); return true;
}

bool Document::rasterizeStampPlacement(QString *error) const {
    RasterLayer rendered; EditableObject object;
    if (!renderStampPlacement(transformMatrix(), true, &rendered, &object, error)) return false;
    const auto &original = m_project.shots[activeShotIndex()].layers[m_active];
    // A fully clipped or transparent placement is a no-op, including on vector
    // layers: there is no invisible object unexpectedly left outside the sheet.
    if (m_stampPlacement->vectorLayer && rendered.tiles != original.tiles) rendered.objects.append(std::move(object));
    auto project = m_project;
    project.shots[activeShotIndex()].layers[m_active] = std::move(rendered);
    const auto validation = project.validate();
    if (!validation.isEmpty()) { if (error) *error = validation; return false; }
    m_transformRasterized = std::move(project); return true;
}

bool Document::applyStampPlacement() {
    QString error;
    if (!rasterizeTransform(&error)) { emit warning(error); return false; }
    const auto before = m_project;
    const auto after = *m_transformRasterized;
    const auto &oldLayer = before.shots[activeShotIndex()].layers[m_active];
    const auto &newLayer = after.shots[activeShotIndex()].layers[m_active];
    if (oldLayer.tiles == newLayer.tiles && oldLayer.objects == newLayer.objects) { cancelTransform(); return true; }
    // Selection is intentionally unchanged. ProjectCommand resets the pending
    // transform as it installs the completed document and records this one edit.
    return commit(before, after, "Place stamp");
}
} // namespace toon
