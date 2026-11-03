---
title: "Perspective guides and assisted drawing"
description: "Build several perspective assistants, snap brush paths and reuse guide sets for backgrounds."
category: "Drawing"
order: 90
---

Perspective guides provide geometric assistance without becoming part of your finished artwork. You can keep several guides in a shot, give them distinct colors, limit them to parts of the drawing and choose which ones assist the brush.

Use them for a city street, a tilted room, parallel hatching or a curved fisheye composition. They constrain the route of a stroke; they do not create a 3D scene, transform existing artwork or automatically scale brush motifs with depth.

## Place the guide geometry

Press **G** with the canvas focused, or choose a guide through **Toolbox → Guides → Tools**. Click **Add** in Properties, then place its required points on the canvas. The placement indicator shows what remains. Escape or right-click cancels an unfinished guide.

| Guide | Points to place | A suitable use |
| --- | --- | --- |
| Perspective | Four corners around a convex plane | A wall or tabletop plane |
| Vanishing point | One point | Lines converging down a corridor |
| Two-point perspective | Two vanishing points, then a grid anchor | Buildings seen at an angle |
| Parallel ruler | Two distinct points | Repeated parallel edges or hatching |
| Three-point perspective | Three vanishing points, then an anchor | Looking up or down at tall structures |
| Fisheye perspective | Two opposite axis endpoints, then a radius handle | A curved wide-angle composition |

Repeat the Add workflow for another guide. A shot can hold different types together. A simple room might use a perspective plane for the floor plus a separate vanishing-point assistant for a distant corridor.

The grid anchor positions the displayed planes for two- and three-point guides. Keep it away from the horizon or lines joining vanishing points. Perspective corners must form a convex shape. Invalid geometry is rejected rather than creating an unusable assistant.

## Refine placement directly

Click the guide's line, handle or list row to select it. Drag a point to reshape it, or drag its cross-shaped center handle to move the complete guide. Shift constrains a normal point drag to the artwork's horizontal or vertical axis. Escape restores an unfinished drag; a completed drag is one Undo step.

For a fisheye guide, the first two points establish the center, axis and half-width. The third point controls the perpendicular half-height. Hold Shift while placing or moving that radius handle for a circular boundary. Unequal radii create an ellipse. Moving along the main axis does not alter the perpendicular radius.

Very distant vanishing points may require zooming out to place comfortably. View navigation is separate from guide geometry: zooming or rotating the working view does not change where the saved points lie in artwork coordinates.

## Separate visibility, locking and snapping

The guide list has three distinct controls: visibility, handle lock and snapping. Visibility hides the grid. Handle lock prevents moving its geometry. Snapping decides whether it can assist brush strokes.

Hiding a guide does **not** turn snapping off. This is useful once the structure is established and the grid becomes distracting, but can be confusing if you expect freehand drawing. Turn off its snapping icon or the shared **All guides → Snap to guides** switch when you want an unconstrained stroke.

New projects start with global snapping off. Enable it, then return to **Brush** or **Eraser**. After a short initial movement, the closest eligible direction is selected and kept for that stroke. Begin a new stroke to choose another direction. A dashed path preview shows the current constraint.

Pressure, texture and other brush effects still operate. A scattered brush can paint beside the guide even though its center path follows it. Three-point guides offer three straight directions; fisheye chooses an eligible ellipse-family route or a center axis within its boundary.

## Control visual density and working areas

**Selected guide** contains the name, density, custom color, opacity and area settings. **All guides** controls shared display, snapping and handle visibility. Start with a low grid density if too many lines obscure the drawing; more grid lines do not inherently make brush assistance more accurate.

Enable **Custom color** when a guide needs a distinct identity. Otherwise it uses the shared color and opacity. Choose subdued guide colors that remain readable against the paper and your sketch.

**Limit to area** provides a rectangle with direct handles and exact bounds. It clips the displayed grid and governs whether a stroke is eligible for that guide. It is not a paint selection: marks are not erased simply because they travel outside the rectangle. The stroke resumes freehand outside the allowed area and can resume its chosen guide direction when returning.

## Save and reuse a construction setup

Guide geometry and shared settings are saved with the shot and support Undo. The selected guide and handles remain editor aids; guides do not appear in production-sheet or movie-frame exports.

**Save guide set** writes a native `.toonguides` file. **Add guide set** appends its guides with new identities and adopts the imported shared settings. Existing artwork is unchanged, but review the shared snapping switch after importing because the set can change it.

For repeated shots of the same room, save the construction setup before adding shot-specific details. Reuse it as a starting point, then move the anchors to fit the new composition. Guide files use Toon Pro's own format; Krita assistant files are not interchangeable. If a brush pattern must follow a mapped surface rather than a line, use the separate **Patterns and tones → Perspective plane** controls described in [Brushes and dynamics](brushes-and-dynamics.md).
