---
title: "Keyframes, cameras and animated effects"
description: "Animate existing artwork, shape its motion and keyframe compositing without repainting each frame."
category: "Animation"
order: 120
---

Keyframes store property values at selected times. Toon Pro evaluates the frames between them using the chosen interpolation. A drawing exposure decides whether artwork exists at a frame; property keys decide how that artwork is placed or rendered there. The two schedules can be edited independently.

## Animate an existing layer

Select a raster, vector, text layer or suitable folder. Open **Frame Properties → Animation → Transform**. Position X/Y, Scale X/Y, Rotation and Opacity each have a diamond that enables their channel.

1. Visit the intended starting frame and enable the property diamond. A baseline is retained at frame 1, with a key at the current frame when it is later.
2. Move the playhead to the next pose.
3. Change the enabled property's value. The edit creates or updates a key there.
4. Play or scrub between the keys to inspect the transition.

An unenabled property remains a static edit. Animated opacity multiplies ordinary layer opacity. A folder's animation moves its children together, which is useful for a character assembled from separate drawing layers.

Use the animated transform icon for canvas handles. Drag inside to move, a corner to scale or the rotation handle to turn the artwork. These handles create the necessary channels automatically. Finish one gesture for one Undo step; Escape restores a pending edit.

## Edit keys and easing

The selected layer's enabled channels appear beneath its Timeline row. Click a key to select and visit it. Shift and Ctrl/Cmd build a multi-key selection; a blank-lane drag creates a marquee. Drag a selected key to move the batch, or use Alt+Left/Right for one-frame nudges.

Copy/Paste preserves values, relative timing and interpolation on the original channels in the same shot. It refuses occupied destinations rather than silently replacing keys. **Key frame** in the inspector provides exact retiming, while Previous/Add/Delete/Next are direct actions.

Interpolation belongs to the selected key's outgoing segment. **Hold** maintains the value until the next key. **Line** changes it evenly. **In**, **Out** and **In/Out** ease acceleration and deceleration. Outside the key range, endpoint values hold.

For numeric layer properties and compatible path morphs, drag the easing graph handles for a custom **Curve**. The horizontal axis represents time and the vertical axis progress. Choosing a preset replaces the custom curve. This is a segment editor, not a multi-channel speed graph; overshoot and custom camera easing are not provided.

## Shape the motion path and pivot

Enable **Motion Path** to see the layer pivot's trajectory, including parent motion. Squares mark keyed positions and a diamond marks the current frame. Drag a marker to change its location without changing its key time. Use Timeline to retime it.

Select a position marker and enable **Curve** to expose spatial handles. Edit the incoming or outgoing direction, then inspect the next key's handles to shape the whole segment. **Follow path** turns artwork toward its travel direction; **Rotation offset** adjusts which side faces forward.

**Pivot** sets the common rotation and scaling center. Drag its crosshair or enter coordinates. Moving the pivot preserves the current pose by adjusting position keys, but other poses with different scale or rotation can change. Review the entire move afterward. Reset centers the pivot on the source artwork.

## Paint the source safely

The brush icon toggles **Source artwork**. It displays original layer positions and path poses so destructive drawing operations remain predictable. Leave Source to see the animated result. Playback and export always evaluate animation.

If painting is blocked on a moved animated layer, switch to Source artwork rather than painting into a misleading evaluated position. Source mode does not remove the shot camera; choose camera layout view separately when you need to see the artwork without shot framing.

For vector morphing, select a path with Path Edit and enable **Path morph**. At another frame, edit compatible nodes, handles, width or color to create a pose. The path must retain node correspondence, count and open/closed state. Simplifying or erasing it requires disabling its path animation first; unrelated drawings are not automatically matched.

## Animate the shot camera

Open **Frame Properties → Camera → 2D**. Enable its diamond, compose the first pose, move the playhead and change Position, Zoom or Rotation. Each key stores the complete camera pose.

The framing icon shows a rectangle over source artwork. Drag inside to move, corners to adjust zoom, or the outer round handle to rotate. **Camera view** returns to the evaluated picture. This camera changes the exported framing, unlike Hand or Navigator navigation, which only changes your working view.

Camera rotation turns the view, so artwork appears to turn oppositely. Provide enough background overscan for the move; the camera does not enlarge the drawable area. Starting playback returns to Camera view.

The **3D** camera tab animates the reference-scene camera where rendering is supported. It remains reference-only and is excluded from output. A 2D camera cannot reveal 3D geometry beyond the finite reference image; change the 3D camera itself for that view.

## Animate effects and masks

Select the layer or folder and open **Panels → Effects**. Enable a diamond beside a supported setting, then change that value at another frame. **Frame Properties → Animation** groups the same channels under **Transform**, **Effects** and **Mask**.

Blur radius, shadow and glow size/strength, shadow offsets and color correction values can interpolate. Enable switches and mask inversion use held values. Shadow and glow colors remain static.

Create a mask before enabling its channels. **Effects → Mask → Placement** can animate position, scale, rotation, strength and feather independently of the artwork. The painted mask source remains shared across frames; painting at a later frame does not create a new mask cel. Its thumbnail shows source paint before placement.

For a reveal, start with a mask covering the artwork, key its horizontal position, then move it aside at a later frame. Inspect the actual camera view and output edge. Use Opacity to vary mask strength, not to repaint its pixels.

## Preview heavy shots

Timeline's **1/1** control offers Full, Half, Quarter and Eighth playback resolution. Reduced quality helps rehearse heavy effects; pausing requests the exact frame and exports remain full quality. Some unsupported combinations fall back to full quality with a reason. Audio and playhead timing can continue while expensive pictures display fewer frames, so judge final detail after the paused frame finishes or from an export.
