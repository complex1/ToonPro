#include "scene3d_controller.h"
#include "canvas.h"
#include "camera_tool.h"
#include "mask_tool.h"
#include "keyframe_render.h"
#include "keyframe_transform_tool.h"
#include "animation.h"
#include "drawing_tool_controller.h"
#include "guide_tool.h"
#include "layer_panel.h"
#include <QSet>

namespace toon {
namespace {
QString editReason(const QVector<RasterLayer> &layers, int index) {
    if (index < 0 || index >= layers.size()) return "Choose a drawing layer.";
    if (layers[index].group) return "Choose a drawing layer inside the folder.";
    const int selected = index;
    QSet<QString> visited;
    while (index >= 0) {
        const auto &layer = layers[index];
        if (visited.contains(layer.id)) return "This layer's folder structure is invalid.";
        visited.insert(layer.id);
        if (layer.locked) return index == selected ? "Unlock the layer to edit it." : "Unlock the parent folder to edit this layer.";
        if (!layer.visible) return index == selected ? "Show the layer to edit it." : "Show the parent folder to edit this layer.";
        if (layer.parentId.isEmpty()) return {};
        int parent = -1;
        for (int i = 0; i < layers.size(); ++i) if (layers[i].id == layer.parentId) { parent = i; break; }
        if (parent < 0) return "This layer's parent folder is missing.";
        index = parent;
    }
    return {};
}
bool selectionTool(Canvas::Tool tool) {
    using T = Canvas::Tool;
    return tool == T::RectangleSelect || tool == T::LassoSelect || tool == T::EllipseSelect
        || tool == T::PolygonSelect || tool == T::MagicWand || tool == T::ColorRange;
}
QString samplingReason(const Canvas *canvas) {
    const auto &settings = canvas->drawingTools()->fillSettings;
    if (settings.source != RasterSampleSource::ReferenceLayer) return {};
    for (const auto &layer : canvas->shot().layers)
        if (layer.id == settings.referenceLayerId)
            return layerPresentAtFrame(canvas->shot(), layer.id, canvas->document()->currentFrame())
                ? QString() : QString("The reference layer belongs to another frame. Choose an exposed layer to sample.");
    return "Choose a reference layer in Tool Properties before sampling.";
}
}

QString Canvas::toolBlockReason() const {
    const auto tool = pointerTool();
    if (!hasProject()) return "Create or open a project to use the canvas.";
    const auto soloReason=[this] {
        return timelineSoloIncludesLayer(shot().layers[m_document->activeLayer()].id) ? QString()
            : QString("This layer is outside Solo. Turn off Solo or choose an isolated layer to edit.");
    };
    if (m_maskTool->enabled() && !m_space) return soloReason().isEmpty() ? m_maskTool->unavailableReason() : soloReason();
    if (m_cameraTool->enabled() && !m_space) return m_animationPlayback ? QString("Pause playback to edit the camera.") : QString();
    if(m_keyframeTransformTool->enabled()&&!m_space)return soloReason().isEmpty() ? m_keyframeTransformTool->unavailableReason() : soloReason();
    if (tool == Tool::Hand) return {};
    if (m_animationPlayback) return "Pause playback to edit a drawing.";
    if (!m_toolAvailable) return "Choose a tool from the Tools panel.";
    if (tool == Tool::Eyedropper) return samplingReason(this);
    if (m_document->effectsEditActive()) return "Finish the effect adjustment first.";
    if (m_document->layerPropertyEditActive()) return "Finish the layer property adjustment first.";
    if (m_document->transformActive() && tool != Tool::Transform && !(m_document->stampPlacementActive() && tool == Tool::Brush))
        return "Apply or cancel the transform before using this tool.";
    if (m_document->externalEditActive() && !m_drawingTools->active() && !m_scene3D->editing())
        return "Finish or cancel the current edit first.";

    if (tool == Tool::SceneObject || tool == Tool::SceneCamera || tool == Tool::SceneRig) {
        if (tool != Tool::SceneCamera && !soloReason().isEmpty()) return soloReason();
        if (!m_scene3D->scene()) return "Add a model from Panels → 3D Library first.";
        if (!m_scene3D->canEdit()) return "Show and unlock the 3D layer and its position.";
        if (tool == Tool::SceneRig) {
            if(m_scene3D->skinBindingBusy())return "Binding the model to its skeleton…";
            if (!m_scene3D->scene()->visible) return "Show the 3D scene before editing its skeletons.";
            const auto *object = m_scene3D->selectedObject();
            if (!object) return "Select a model in Tool Properties to edit its skeleton.";
            if (!object->visible || object->locked) return "Show and unlock the model to edit its skeleton.";
            if(m_scene3D->rigEditMode()==Scene3DController::RigEditMode::Pose){
                if(!m_scene3D->selectedSkin())return "Bind the model in Weights before posing it.";
                if(!m_scene3D->rigVisible())return "Show the skeleton in Tool Properties to pose it on the canvas.";
            }else if(m_scene3D->rigEditMode()==Scene3DController::RigEditMode::Weights) {
                if(!m_scene3D->selectedSkin())return "Bind the model in Tool Properties before painting weights.";
                if(!m_scene3D->selectedJoint())return "Select a joint in the hierarchy to paint its influence.";
                if(!m_scene3D->skinPreviewRotation().isNull())return "Reset the bend check before painting weights.";
            } else {
                if(m_scene3D->selectedSkin())return "Choose Weights to paint influence, or unbind to edit rest joints.";
                if (!m_scene3D->rigVisible()) return "Show the skeleton in Tool Properties to edit joints on the canvas.";
            }
        }
        return {};
    }
    if (selectionTool(tool))
        return tool == Tool::MagicWand || tool == Tool::ColorRange ? samplingReason(this) : QString();
    if (tool == Tool::Guides) {
        if (!m_guides->creating && !m_guides->settings().visible)
            return "Show guides in Tool Properties to edit them.";
        return {};
    }
    if (!soloReason().isEmpty()) return soloReason();
    const auto &layers = shot().layers;
    const int active = m_document->activeLayer();
    const auto &layer = layers[active];
    if(layer.kind==LayerKind::Symbol)return "Edit this symbol in the Symbols panel to draw inside it, or use its Transform control to move the instance.";
    if(layer.adjustment)return "Use Effects to edit this adjustment layer or its mask; choose a drawing layer to paint.";
    if(tool!=Tool::VectorNode&&tool!=Tool::VectorWidth&&tool!=Tool::Text){
        const auto animationReason=keyframeRasterEditReason(m_document);if(!animationReason.isEmpty())return animationReason;
    }
    const auto owner = animationOwner(shot(), layer.id);
    if (!owner.trackId.isEmpty() && ((owner.drawingId.isEmpty() && tool != Tool::AdvancedTransform)
        || !layerPresentAtFrame(shot(), layer.id, m_document->currentFrame())))
        return "This frame is empty. Add a drawing in the Timeline to start painting.";
    const auto *tools = m_drawingTools;
    QStringList transformLayerIds;
    if (tool == Tool::AdvancedTransform) transformLayerIds = resolvedTransformLayerIds();
    else if (tool == Tool::Transform) transformLayerIds.append(layer.id);
    if ((tool == Tool::Transform || tool == Tool::AdvancedTransform)
        && tool == m_transformFailureTool && tools->selectionBoundary == m_transformFailureBoundary
        && tools->warpMode == m_transformFailureWarp && transformLayerIds == m_transformFailureLayerIds
        && !m_transformFailure.isEmpty())
        return m_transformFailure;
    if (tool == Tool::AdvancedTransform) {
        if (tools->warpMode && m_document->rasterSelection().active
            && m_document->rasterSelection().path.elementCount() > 4096)
            return "This selection is too detailed for mesh warp. Use Perspective Transform or deselect it first.";
        const bool boundary = tools->selectionBoundary;
        if (boundary) return !m_document->rasterSelection().active || m_document->rasterSelection().path.isEmpty()
            ? "Select an area before transforming its boundary." : QString();
        const auto &ids = transformLayerIds;
        const auto exposed = activeLayerIds(shot(), m_document->currentFrame());
        bool content = false;
        for (int i = 0; i < layers.size(); ++i) {
            if (layers[i].group || !exposed.contains(layers[i].id)) continue;
            bool selected = ids.contains(layers[i].id);
            for (const auto &id : ids) selected |= layerIsDescendant(layers, i, id);
            if (!selected) continue;
            if (!timelineSoloIncludesLayer(layers[i].id)) return "The transform includes layers outside Solo. Turn off Solo or select only isolated layers.";
            if (layers[i].kind == LayerKind::Symbol) return "Transform linked instances in the Symbols panel, or break them apart first.";
            if (layers[i].kind == LayerKind::Scene3D) return "Use the 3D Object or Camera tool to edit the reference scene.";
            const auto reason = editReason(layers, i);
            if (!reason.isEmpty()) return reason;
            if (layerPositionLocked(layers, i)) return "Unlock layer and folder positions before transforming artwork.";
            if (layers[i].alphaLocked) return "Turn off alpha lock before transforming artwork.";
            if (layerHasEditableObjects(layers[i])) {
                if (tools->warpMode) return "Mesh Warp needs raster artwork. Rasterize the layer or use Perspective Transform.";
                if (m_document->rasterSelection().active)
                    return "Clear the pixel selection before transforming vector or text artwork.";
            }
            content |= !layers[i].tiles.isEmpty();
        }
        return content ? QString() : "The selected layers contain no artwork to transform.";
    }

    // These tools can create a matching layer. Check the destination folder,
    // rather than blocking on the unrelated current layer's type or self lock.
    if ((tool == Tool::Text && layer.kind != LayerKind::Text)
        || (tool == Tool::VectorPen && layer.kind != LayerKind::Vector)) {
        if (!owner.trackId.isEmpty() && tool == Tool::Text) return "Animation drawings support raster and vector layers. Add text to a static layer.";
        if (layers.size() >= (shot().animationTracks.isEmpty() ? maxDrawingLayers : maxAnimationLayers))
            return "The shot has reached its stored layer limit.";
        QString parent = layer.group ? layer.id : layer.parentId;
        QSet<QString> visited;
        while (!parent.isEmpty()) {
            if (visited.contains(parent)) return "This layer's folder structure is invalid.";
            visited.insert(parent);
            const RasterLayer *folder = nullptr;
            for (const auto &candidate : layers) if (candidate.id == parent) { folder = &candidate; break; }
            if (!folder) return "The destination folder is missing.";
            if (folder->locked) return "Unlock the destination folder before drawing.";
            if (!folder->visible) return "Show the destination folder before drawing.";
            parent = folder->parentId;
        }
        return {};
    }
    const auto reason = editReason(layers, active);
    if (!reason.isEmpty()) return reason;
    if (layer.kind == LayerKind::Scene3D) return "Choose a drawing layer to paint, or use the 3D Object and Camera tools.";
    const bool shape = tool == Tool::Line || tool == Tool::Rectangle || tool == Tool::Ellipse
        || tool == Tool::Polygon || tool == Tool::Curve || tool == Tool::Arc;
    const bool pixelSelectionApplies = tool == Tool::Brush || tool == Tool::Bucket || tool == Tool::Gradient
        || tool == Tool::Retouch || (layer.kind == LayerKind::Raster && (tool == Tool::Eraser || shape));
    if (pixelSelectionApplies && m_document->rasterSelection().active && m_document->rasterSelection().path.isEmpty())
        return "The selection is empty. Clear it or select an area to paint.";
    const bool vectorEdit = tool == Tool::VectorNode || tool == Tool::VectorSimplify
        || tool == Tool::VectorWidth || tool == Tool::VectorEraser;
    if (vectorEdit) {
        if (layer.kind != LayerKind::Vector) return "Choose a vector layer for this tool.";
        if (layer.objects.isEmpty()) return "Draw a vector path on this layer first.";
        if (layerPositionLocked(layers, active) && (tool == Tool::VectorSimplify
            || (tool == Tool::VectorNode && tools->pathEditMode != DrawingToolController::PathEditMode::Width)))
            return "Unlock the layer or folder position to edit path geometry.";
        return {};
    }
    if (tool == Tool::Bucket || tool == Tool::Gradient || tool == Tool::Retouch) {
        if (layer.kind != LayerKind::Raster) return "This tool needs a raster layer. Choose one or rasterize this layer.";
        return tool == Tool::Bucket ? samplingReason(this) : QString();
    }
    if (tool == Tool::Transform) {
        if (layer.kind != LayerKind::Raster) return "Use Path Edit or Perspective Transform for vector or text artwork.";
        if (layerPositionLocked(layers, active)) return "Unlock the layer or folder position before transforming.";
        if (layer.alphaLocked) return "Turn off alpha lock before transforming artwork.";
        if (layer.tiles.isEmpty()) return "This layer has no artwork to transform.";
    }
    if (tool == Tool::Eraser && layer.kind == LayerKind::Raster && layer.alphaLocked)
        return "Turn off alpha lock to erase this layer.";
    if (layer.kind == LayerKind::Text && tool != Tool::Text)
        return "Choose a raster or vector layer to draw. Use the Text tool to edit text.";
    if (tool == Tool::Brush && layer.kind == LayerKind::Vector && layer.objects.size() >= 1024)
        return "This vector layer is full. Add another vector layer to keep drawing.";
    return {};
}

QString Canvas::pointerBlockReason() const {
    if (!hasProject()) return toolBlockReason();
    if (m_dragging || m_space || (m_tool == Tool::Hand && !m_maskTool->enabled())) return {};
    if (m_maskTool->enabled()) return toolBlockReason();
    // An accepted brush gesture retains its layer/tool eligibility until it
    // ends. Document mutation guards prevent locks/layers changing underneath
    // it; do not traverse animation ownership again for every cursor outline.
    if (m_document->strokeActive() && (m_tool == Tool::Brush || m_tool == Tool::Eraser)) return {};
    if (hasProject() && m_toolAvailable && (m_pointerModifiers & Qt::AltModifier)
        && (m_tool == Tool::Brush || m_tool == Tool::Eraser || m_tool == Tool::Retouch))
        return samplingReason(this);
    return toolBlockReason();
}
QString Canvas::interactionHint() const {
    if(!m_pointerInside)return {};
    const auto reason=pointerBlockReason();
    if(!reason.isEmpty())return reason;
    if(m_document->stampPlacementActive()&&!m_space&&!m_dragging)
        return QStringLiteral("Move, resize or rotate the stamp. Enter places it; Esc discards it.");
    if(m_tool==Tool::Arc&&!m_space&&!m_dragging)
        return m_drawingTools->waitingForArcBend()
            ? QStringLiteral("Move to bend the arc; tap or press Enter to finish. Esc cancels.")
            : QStringLiteral("Drag the endpoints, lift, then place the bend.");
    return {};
}
bool Canvas::rejectBlockedAction() {
    const auto reason = pointerBlockReason();
    if (reason.isEmpty()) return false;
    refreshPointerCursor();
    emit actionBlocked(reason);
    return true;
}
bool Canvas::prepareToolTransform(const QStringList &layerIds) {
    // Transform preparation already examines actual selected pixels. Retain its
    // failure until context changes instead of repeating that work on hover.
    QString failure;
    const auto connection = connect(m_document, &Document::warning, this,
        [&failure](const QString &message) { failure = message; });
    const auto resolvedIds = m_tool == Tool::AdvancedTransform ? resolvedTransformLayerIds(layerIds)
        : hasProject() ? QStringList{shot().layers[m_document->activeLayer()].id} : QStringList();
    const bool ready = m_tool == Tool::AdvancedTransform
        ? m_drawingTools->transform()->begin(m_document, resolvedIds, m_drawingTools->selectionBoundary)
        : m_document->beginTransform();
    disconnect(connection);
    m_transformFailure = ready ? QString() : failure;
    m_transformFailureLayerIds = resolvedIds;
    m_transformFailureTool = m_tool;
    m_transformFailureBoundary = m_drawingTools->selectionBoundary;
    m_transformFailureWarp = m_drawingTools->warpMode;
    refreshPointerCursor();
    return ready;
}
QStringList Canvas::resolvedTransformLayerIds(const QStringList &requested) const {
    if (!hasProject() || m_drawingTools->selectionBoundary) return {};
    auto ids = requested;
    if (ids.isEmpty()) if (auto *panel = window()->findChild<LayerPanel *>()) ids = panel->selectedLayerIds();
    if (ids.isEmpty()) ids.append(shot().layers[m_document->activeLayer()].id);
    ids.removeDuplicates(); ids.sort();
    return ids;
}
void Canvas::refreshLayerSelectionContext() {
    m_transformFailure.clear();
    refreshPointerCursor();
}
} // namespace toon
