---
title: "Layers, masks and compositing"
description: "Organize drawings, protect finished work, combine layers and apply reversible effects."
category: "Drawing"
order: 40
---

Layers separate decisions that you may want to change later. Keep line art, flat colors and shading on distinct layers; use folders when several layers should move, fade or receive an effect together. The topmost row is the topmost artwork in the stack.

In an animated shot, Layers shows the static stack and the currently exposed drawing from each animation track. Other-time drawings are not visible in the list. Use Timeline to create or remove drawings; use Layers to work inside the current drawing.

## Read and organize the stack

Each row has an eye, thumbnail or folder icon, name and lock information. The thumbnail shows that layer's own content. Vector, text, masks, effects and other special states use small badges instead of making every row larger.

Click the eye to change visibility. Drag down the eye column to give the crossed rows the same visibility state in one operation. Double-click a name to rename it. Folder carets collapse or expand their children without deleting them.

Shift-click selects a range; Ctrl/Cmd-click adds or removes individual rows. Selection does not make a brush paint on several layers at once. The active row remains the drawing target.

Drag selected rows to reorder them. Watch the insertion line, or the outlined folder destination, before releasing. A folder moves with its contents. **New Folder** can wrap selected siblings that share a parent. The context menu also provides **Move Up**, **Move Down** and **Move to Top Level**.

Use the footer for common creation, duplication and deletion actions. Delete is immediately undoable rather than asking for confirmation each time. Removing a folder also removes its contents. **Change Row Color** adds an organizational marker; it never tints the artwork.

## Choose the right protection

| Control | Use it when | What remains possible |
| --- | --- | --- |
| Full lock | A finished layer should not be altered | Visibility changes and explicit unlocking |
| Lock transparent pixels | Recoloring inside existing raster paint | Painting changes color while retaining coverage |
| Position lock | The drawing must remain registered | Painting and suitable appearance changes |
| Notes | Annotations belong on the sheet only | Notes remain editable and appear in sheet export |

A folder's locks and visibility affect its children. If a child looks unlocked but refuses an edit, inspect its parents. An empty alpha-locked layer stays empty because there are no painted pixels to recolor. Disable transparent-pixel lock before erasing or performing an incompatible pixel transform.

## Blend and clip

The header changes the active layer's blend mode and opacity. **Normal** is the ordinary starting point. **Multiply** is useful to test for darkening shade layers; **Screen** for lightening effects. The result depends on the underlying colors, so judge it on the canvas rather than assuming a mode is suitable from its name.

Layer opacity changes the whole composited layer. Brush opacity belongs to individual strokes and has a different purpose. Slider drags preview live and become one Undo step on release.

A clipped layer uses the nearest unclipped sibling below it as its base. It contributes only inside that base's coverage. Keep the complete base-and-clipped-layer run together when moving or grouping it; actions that would break the relationship are rejected. Clipping does not cross folder boundaries. A hidden base hides its clipped contribution too.

Folders composite their children as a group, then apply the folder's own blending and opacity. This is useful when a character's line and color layers need to fade together.

## Mask without erasing source paint

Open **Panels → Effects → Mask**. Add a white mask to reveal everything, or create a mask from the current selection. Enable mask painting and choose **Hide** or **Reveal**. Size, Strength and Hardness control its dedicated brush; pen pressure affects strength.

A mask hides coverage without deleting the underlying drawing. Use the direct actions to invert, reveal all, hide all, disable or remove it. The mask has its own enable switch, independent of the Effects header bypass. Press Escape when idle to leave mask painting, or choose a drawing tool.

**Placement** moves, scales or rotates the mask independently in layer-local coordinates. Its Opacity is strength: zero bypasses the hiding effect, while 100 applies it fully. Feather softens the source mask. The thumbnail shows the mask's painted source, not its transformed appearance.

## Apply effects and adjustment layers

The Effects tabs provide **Blur**, **Shadow**, **Glow** and **Color**, alongside Mask. Enable a category, then adjust it while watching the canvas. Shadow includes a direct offset pad. Color offers Exposure, Contrast, Saturation and Hue.

**Add color adjustment layer** corrects visible lower siblings in the same folder. Move it to change which layers it affects. Its mask restricts the correction area. An adjustment is not a paintable raster layer and exposes Mask and Color rather than every effect category.

Effects are evaluated with masks, layer blending and animation for display and export. Heavy previews refine after interaction; wait for the settled image before judging fine edges. Diamonds beside supported settings enable animation, described in [Keyframes, cameras and effects](keyframes-cameras-and-effects.md).

## Merge deliberately

**Merge Down** combines compatible visible raster siblings. **Flatten Image** bakes a compatible visible stack. These commands have restrictions around locks, clipping, Notes and animation to avoid silently changing the result. Read the reported reason if a command is unavailable.

Flattening omits hidden artwork and removes separate editability from the result. Undo restores the earlier stack while that history remains available. Save a separate project copy before a major destructive consolidation if you need to retain the layered version beyond the current session.
