---
title: "Shortcuts and troubleshooting"
description: "Find the right control quickly and diagnose blocked drawing, unexpected timing or slow previews."
category: "Workflow"
order: 160
---

Shortcuts depend on focus. A canvas shortcut acts on drawing tools, while the same key can mean something different in Timeline or a text field. Click or tap the intended surface before using a command. On macOS, platform commands generally use Command; on Windows, Control. The menu and tooltip show the application's platform-specific form.

## Everyday canvas controls

| Shortcut or gesture | Action with canvas focus |
| --- | --- |
| B / E | Brush / Eraser |
| I / F / D | Eyedropper / Paint bucket / Gradient |
| M / L | Rectangle selection / Lasso |
| T / G / H | Transform / Perspective guides / Hand |
| Space-drag or middle-button drag | Pan the working view |
| Wheel | Zoom around the pointer |
| Cmd/Ctrl+0 / Cmd/Ctrl+1 | Fit sheet / Actual size |
| Cmd/Ctrl+S | Save project |
| Cmd/Ctrl+Z | Undo, or cancel an applicable pending edit |
| Cmd/Ctrl+A / Cmd/Ctrl+D | Select all pixels / Deselect |
| Cmd/Ctrl+Shift+I | Invert selection |
| Cmd/Ctrl+T | Transform pixels |
| Cmd/Ctrl+C / X / V | Copy / Cut / Paste into new layer |
| Cmd/Ctrl+Shift+V | Paste into active raster layer |
| Cmd/Ctrl+J | Duplicate selected pixels |
| Enter / Escape | Complete or cancel the current supported operation |
| Tab | Enter or leave Drawing mode |

Enter inside a numeric field commits the number rather than pressing a dialog's main action. Escape inside an unfinished number restores that edit first. Tab inside panels and fields remains ordinary keyboard navigation; it does not hide the workspace while you are typing.

For artwork text, **Cmd/Ctrl+Enter** commits the inline editor. **Escape** cancels it. Double-click an existing text object to edit its content again. Fonts are not embedded in projects, so another computer may substitute a font if the chosen family is missing.

## Brush modifier gestures

Hold **Command or Control before contact** for a horizontal or vertical brush stroke. The first clear movement chooses an artwork-aligned axis, which remains fixed through pen-up. Hold **Shift before contact** for an adjustable straight line at any angle. Its endpoint follows the pen and commits on release.

The modifier is captured at contact. Releasing it halfway through does not change the current gesture. Command/Control takes priority if combined with Shift. These gestures intentionally bypass normal stabilization and guide snapping, so check for a held modifier if freehand curves unexpectedly become straight.

With **Hold to shape** enabled, pause while keeping the pen down for about 0.7 seconds after a suitable line or shape. Lift to see fitted choices. Choose **Original** to retain the freehand mark, or turn off the wand toggle beside the brush wrench when you do not want fitting.

## Timeline and review controls

| Shortcut | Context and action |
| --- | --- |
| Space | Play/pause in Timeline or Scene/Movie review |
| Left/Right | Step frames in the focused Timeline or review |
| Home/End | Reach the current shot or review-scope bounds |
| Shift-click / Ctrl/Cmd-click | Extend or toggle a suitable cel/key selection |
| Alt+Left/Right | Nudge the selected timing item one frame |
| Cmd/Ctrl+C / V | Copy/paste the selected timing domain |
| Delete/Backspace | Remove the selected supported timeline item |
| Cmd/Ctrl+wheel | Zoom Timeline around the pointer |
| Cmd/Ctrl+Shift+Up/Down | Move a selected artwork row among siblings |
| M | Add a marker with Timeline grid focus |
| Enter | Edit the reviewed shot, or use the selected pose lane's picker |
| Escape | Cancel a timing gesture or leave Scene/Movie review |

The Timeline clipboard is separate from artwork pixels. Cel exposures, property keys, timed poses and clips have different selection domains. Check the selection/context line before pressing Delete or Paste.

## The brush does not draw

Look at the bottom status message while the pointer is on the canvas. Then check these in order:

1. Is playback paused and the shot in its editing view?
2. Is an editable raster or vector artwork layer selected, rather than a 3D reference, adjustment or linked Symbol instance?
3. Is the layer and every parent visible and unlocked?
4. Does the current animation interval contain a drawing?
5. Is an empty active pixel selection blocking coverage? Try **Select → Deselect**.
6. Is transparent-pixel lock enabled on an empty raster layer?
7. Does a pending transform, mask edit, shading tool or Light Table handle own the canvas interaction?

Bucket, gradient and retouch require raster paint. A linked Symbol must be edited through its source editor or deliberately broken apart. A moved animated layer may require **Source artwork** before destructive painting. The forbidden cursor is a context explanation, not proof that the tablet stopped sending input.

## A stroke is delayed, angular or unexpectedly varied

Test a simple Round Ink stroke at a modest size with low Smoothing. Turn off guide snapping and Hold to shape for the comparison, and ensure Shift/Command/Control are released. If the simple brush is responsive, add texture, size, scatter and symmetry back one at a time to identify the expensive setting.

Large tips, close spacing and many particles create more painting work. Connected patterns have different controls: ordinary stamp spacing and scatter do not govern Ribbon or Repeat. Tight corners can compress motifs even when input delivery is smooth. Use wider curves when the source design needs room.

For variation within one continuous stroke, check per-impression jitter or **Apply per tip**, rather than once-per-stroke variation. Colored tips need the explicit image-color options for tint or HSV changes. Gray source art may not visibly respond to saturation alone.

If the problem happens with every simple brush, compare tablet and mouse input and record the app version, brush, size, canvas dimensions and smoothing setting. Note whether lag occurs during contact, after pen-up or only while an effect preview refines. A reproducible short example is more useful than changing all tablet and brush settings simultaneously. Hardware tilt and rotation require driver support; their absence is different from missing position samples.

## The preview or exported picture is unexpected

Check whether Timeline Solo, Source artwork, camera layout view, a transient selection, Notes or reference overlays explain the difference. Solo affects preview only. Source artwork bypasses layer animation for editing. References and Light Table never become movie paint. The top AA switch changes display smoothing only.

For heavy animated effects, reduce playback quality to Quarter or Eighth and wait for the exact paused frame before judging detail. This does not lower export quality. A preview can display fewer frames than the requested FPS while sound remains synchronized.

If a fill leaves a gap, inspect its sampling source, tolerance and Close gaps settings. If a recolor changes nothing, verify that From matches the painted palette and To contains different approved colors. If a transform seems lost after switching tools, use its explicit Apply decision; Escape discards a preview.

Save before large experiments and keep milestone project copies. History is limited and session-only. For an error report, include the exact bottom message or export-dialog error and a small reproducible project when possible; do not assume a missing control can be repaired by repeatedly clicking it.
