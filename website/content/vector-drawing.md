---
title: "Editable vector brush drawing"
description: "Keep the appearance of raster brushes while editing paths, widths and captured strokes later."
category: "Drawing"
order: 60
---

A vector layer in Toon Pro lets you draw with the same brush tools used on raster layers while retaining an editable path. A textured or sampled stroke keeps its brush resources and recorded input. You can adjust its geometry, width, color and opacity later without first selecting its pixels.

This does not turn a bitmap stamp into unlimited-resolution SVG artwork. The stroke keeps an editable centerline while its tip and textures still render as pixels. For flat colors made with bucket, gradient or retouch brushes, use a raster destination alongside the vector line art.

## Draw the first editable stroke

1. In **Layers**, choose **New Vector Layer** using its pen icon.
2. Select a brush in **Tools** and draw normally. Pressure, stabilization, sampled tips, color, selection capture and supported symmetry retain their normal brush behavior.
3. Choose **Toolbox → Vector Editing → Path Edit**.
4. Click the stroke to select it. Use the Properties tabs to choose Transform, Nodes or Width editing.

Picking tests painted coverage for sampled brushes, including transparent holes, within the normal selection tolerance. If a large textured stroke is difficult to pick, zoom in near a clear painted portion. Selecting empty space inside an open stamp does not necessarily select that stamp.

## Reposition a whole stroke

In **Transform**, drag inside the selected stroke's box to move it. Use edge or corner handles to resize and the handle above the box to rotate. Shift constrains appropriate scaling or rotation gestures.

The stroke width, color and opacity controls change the selected object, not the active brush preset. This lets you correct an existing line while keeping the brush ready for the next mark. A completed slider drag becomes one Undo operation; Escape restores an unfinished change.

A position lock protects geometry while allowing suitable color, width and opacity changes. Full locks and hidden parent folders still block editing. Read the bottom status message when a control or gesture is unavailable.

## Edit the path's anchors

Choose **Nodes** to reveal anchors and incoming or outgoing curve handles. Drag an anchor to alter the route and a handle to adjust curvature. Alt-drag an anchor to create a curve control; Shift mirrors the opposite handle. Ctrl-click a selected path to insert an anchor.

Delete removes the selected node when the remaining path is valid; otherwise it can remove the object. Work in small changes and use Undo if you remove more than intended. For a bend that looks uneven, adjusting the neighboring handles can preserve the stroke's flow better than adding many new nodes.

Shape tools can also create editable paths on vector layers. Line, Rectangle and Ellipse use drags, Polygon uses points, and Curve uses four points for a cubic segment. **Arc** uses a different gesture: drag the start and end, lift, then position the third point to preview the bend and click to commit. Enter also commits; Escape cancels.

## Refine line weight

Choose **Width** in Path Edit. Drag a width handle across the path to change local thickness. Click the selected path to add a width control; Reset removes the local width profile.

For a broader correction, **Width Brush** thickens or thins nearby sections using its radius and strength. This is useful for building a stronger underside on a jaw or simplifying an overly uniform contour. The correction applies to the editable stroke rather than painting another dark line over it.

Captured symmetry copies remain linked to their source stroke. Correcting a mirrored or radial copy updates the shared source and the other copies. Independent scattered stamp positions are not separate correction-brush paths.

## Simplify without losing the design

In Nodes, **Simplify selected path** or **Simplify layer** reduces unnecessary anchors within a tolerance. **Simplify Brush** limits the correction to the swept region. Begin gently around expressive curves; a useful hand-drawn contour may legitimately need several anchors.

Simplification preserves necessary curve and width detail, so an already simple path may not visibly change. Do not increase tolerance merely to force a smaller node count if the silhouette begins to change. Each completed correction stroke supports Undo, and Escape cancels a pending gesture.

## Erase editable lines

The ordinary Eraser on a vector layer removes editable strokes. **Vector Eraser** adds whole-stroke and segment modes. Segment mode removes intersected Bézier segments; it is not a pixel-perfect cut at every visible brush edge.

Use whole-stroke mode to remove a wrong construction line. Use segment mode when you want to remove a defined path section, then inspect the remaining shape. A textured stroke's visual footprint can be wider than its centerline, which is another reason to zoom in for precise cleanup.

## Combine vector and raster work

A practical anime drawing can use vector Line Art above raster Colors and Shadow. Keep editable line corrections on the vector layer; fill color regions on the raster layers using the line art as a sampling reference. Rasterize from Layers only when a later operation genuinely needs pixels, such as mesh warp.

Compatible paths can also morph with keys. That requires the same node count, correspondence and open/closed state across poses. Adding nodes, simplifying or erasing an already animated path requires disabling its path animation first. See [Keyframes, cameras and effects](keyframes-cameras-and-effects.md).

Saved editable strokes embed the resources they need. Later preset changes or removal of the original imported image do not replace the existing stroke's brush. Very large geometric edits can spread captured impressions apart, and dense complex strokes still cost time to replay; inspect important changes at the final viewing scale.
