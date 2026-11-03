---
title: "Select, fill and transform artwork"
description: "Isolate paint, build clean fills and reposition pixels while keeping changes reviewable."
category: "Drawing"
order: 50
---

A selection limits where a painting operation can act. A transform changes the position or shape of artwork. They often work together, but you can also transform a selection boundary without moving any pixels.

The moving dashed border marks the selection. The shaded area outside it is a viewing aid, not a dark overlay painted into the document. You can turn that shading off through **View → Shade outside selection** and adjust its strength in selection Properties.

## Choose how to select

- **Rectangle** and **Ellipse selection** use a drag.
- **Lasso** follows your freehand outline and closes on release.
- **Polygonal lasso** uses successive points; press Enter to finish.
- **Magic wand** selects a connected region matching the clicked color.
- **Color range** selects matching colors throughout the sampled area, including separate islands.

Wand and Color range provide tolerance and sampling-source controls. The source can be the active layer, all visible artwork, or a chosen reference artwork layer. A reference artwork layer is part of Layers; it is not an imported image in the References viewer.

The four modes are **New**, **Add**, **Subtract** and **Intersect**. Their buttons are in Properties and the floating selection bar. For supported shape gestures, hold Shift when starting to add, Alt/Option to subtract, or both to intersect. The mode is captured when the gesture starts.

**Select → Select all pixels**, **Invert selection** and **Deselect** provide the common global actions. A selection that is active but contains no area blocks painting; it is different from no selection. If nothing paints after a complex selection, Deselect is a good first check.

## Refine and save a useful area

**Feather** gives the edge graded coverage. Use zero for a hard edge and increase it for a soft painted transition. The dashed outline describes the selection geometry rather than every partly covered pixel. Fill, gradient, eraser, retouch and clipboard operations use the feathered coverage.

The floating bar's More menu includes **Expand selection**, **Contract selection**, **Fill with foreground** and **Clear selected pixels**. Clear requires a suitable unlocked raster target with transparent-pixel lock off. Filling uses the foreground color and opacity independently of brush settings.

To reuse an area, give it a name in selection Properties and save it. Named selections keep their geometry and feather in the project. The current unsaved selection is temporary and clears on reopening. A shot can store up to 64 named selections.

Drag the floating toolbar's grip to pin it out of your way. **More → Reset position** returns it to following the selection. If it is hidden, restore **View → Selection task bar**. The bar hides during drawing gestures to avoid intercepting your pen.

## Fill beneath line art

Select a Colors raster layer below the line layer and choose **Paint bucket**. Set sampling to the visible composite or the intended outline layer, while leaving Colors as the destination. Click inside the region.

Tolerance controls which nearby colors count as a match. **Close gaps** can seal small breaks in detected boundaries. It cannot infer a complete silhouette from a widely open sketch. Adjacent antialiased edges receive limited recovery to reduce thin unpainted seams, but broad blurred outlines may still need cleanup.

Use a selection to contain a fill that must not reach another area. For finished flat colors, [Cel Paint Check](cel-paint-and-light-table.md) can inspect suspected holes or leaks without replacing the bucket workflow.

**Linear Gradient** and **Radial Gradient** paint directly into a raster layer. Edit their color-stop strip, then drag to set direction or radius. Their result respects the selection; it is painted pixels rather than a permanently editable gradient layer.

## Move, resize or rotate pixels

1. Select an unlocked raster layer and optionally select the pixels to move. With no selection, Transform uses its painted pixels.
2. Choose **Transform**, press **T**, or use **Select → Transform pixels**.
3. Drag inside the box to move it. Drag a corner to resize proportionally; hold Shift to allow independent width and height. Use the round handle above it to rotate.
4. Use the displacement, scale and rotation rows for exact values. The width link maintains proportions for numeric scaling.
5. Press Enter or **Apply** to commit. Escape or **Cancel** restores the original.

You may move the preview beyond the drawable area and bring it back before applying. Pixels are cropped to the valid drawing extent only on Apply. Undo restores a committed result. Save and export use committed artwork, so resolve a pending transform first.

Changing tools after a modified transform asks **Apply / Discard / Cancel**. Cancel keeps the preview and current tool. Discard restores the original before switching. Space-drag can still pan the view without resolving the transformation.

## Transform layers, perspective or only the boundary

Use **Select → Transform selected layers / perspective / warp** for several selected layers, a folder or editable object layers. Perspective Transform offers four corner handles; Warp uses a nine-handle mesh. Flip and skew controls are available in the transform properties. Invalid folded meshes or crossed perspective corners are rejected.

**Transform selection boundary** changes the outline alone. This is useful when the selected region needs to move but the drawing should remain fixed. Editable vector or text geometry can use matrix transforms; mesh warp requires explicit rasterization. A pixel selection cannot cut an editable object into independently transformed pieces.

For clipboard work, **Copy** preserves transparency, **Paste into new layer** creates a positioned image layer, and **Duplicate selected pixels** copies into a new layer without replacing the clipboard. Creating the layer and committing its following positioning are separate Undo steps. Repeated applied raster rescaling can lose detail, so make several adjustments within one preview before committing.
