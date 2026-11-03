---
title: "Brushes, stamps and dynamics"
description: "Choose and organize brushes, shape their pen response, and build varied or connected marks."
category: "Drawing"
order: 30
---

Toon Pro's brushes share one native engine. Ink pens, soft paint, image stamps, sprays and connected patterns start with different settings, but remain editable. The installed library contains original Toon Pro designs; familiar brush-family names do not promise identical results to another application's engine.

## Start with the five everyday controls

Choose a brush from **Toolbox → Drawing → Tools**, or activate one through **Brush Management**. **Properties** keeps Size, Opacity, Flow, Hardness and Smoothing available.

**Size** describes a round tip's diameter or a sampled tip's longest side. **Opacity** caps the accumulated contribution of one stroke. **Flow** controls each impression's contribution within that cap, so repeated passes during a low-flow stroke build toward the stroke opacity. **Hardness** changes suitable tip edges. **Smoothing** steadies small movements but introduces additional pen lag and can soften tight corners.

The pen icon enables pressure-controlled size. For responsive line work, start with a simple pen, moderate size and low smoothing; increase smoothing only as much as helps your hand. The pointer follows the tip's coverage rather than always showing a circle. Hovering shows a representative tip; scatter, flow and dynamics still affect the actual paint.

## Make an independent brush

![Brush Management groups brush previews, filters and editable metadata.](/assets/screenshots/brush-management.webp)

*Brush Management groups brush previews, filters and editable metadata.*

Right-click a tool in Tools and choose **Duplicate**, then rename it. Adjusting the copy preserves the original. Adding the same existing tool to another group shares its settings; duplication creates independence.

Open **Panels → Brush Management**, or **Tools → ⋯ → Manage brushes…**, to browse all brush definitions. Use tag, search, category and type filters together. Selecting a row for organization does not activate it: double-click or choose **Use** when ready to draw.

**New brush** offers Pen, Ink, Soft brush, Spray, Stamp, Colored stamp, Dynamic stamp and Custom starting points. **Current brush** copies the present configuration. Assign a category for its main family and several tags for overlapping purposes, such as “foliage,” “night scene” and “rough.” Multi-selection can add or remove a tag, assign a category or add brushes to a group.

Delete removes the chosen definitions from all placements. Existing artwork remains intact. The management panel's organization Undo restores deleted or reorganized tools; it is separate from painting Undo.

**Import** accepts stamp images and supported Photoshop ABR files. Open **Show brush import details** to inspect transferred settings and skipped features. Supported sampled or computed tips and selected geometry/dynamics become native Toon Pro settings. Photoshop textures, dual-brush definitions and proprietary rendering are not reproduced exactly. Keep the original pack and compare a few strokes before depending on an imported brush for a finished pass.

## Adjust advanced settings

Click the **wrench** in Properties. **Advanced brush settings** uses category tabs for tip shape, dynamics, scatter, texture, color, transfer and other behaviors. Changes affect subsequent strokes immediately. Close keeps committed changes; Escape inside a number cancels that number, while Escape outside an active numeric edit closes the dialog.

In **Tip shape**, rotate or squash the preview and adjust spacing. Low spacing makes impressions overlap; high spacing separates them. Very tight spacing with a large textured tip costs more work because many large impressions overlap.

**Shape dynamics** controls size, angle and roundness. **Transfer** controls opacity and flow. Choose the input for each channel, then shape its five-point response graph. **Soft** reaches stronger output with lighter input; **Firm** requires stronger input. **Linear** preserves the input and **Invert** reverses it. This changes the brush's response, not your tablet's system calibration.

Jitter adds random variation. Its adjacent mode chooses variation between impressions or once for the entire stroke. Once-per-stroke jitter holds the random component, while pressure or another live input can still change. **Behavior → Randomness** offers **New each stroke** and **Fixed seed**. A fixed seed repeats random choices for matching input; it does not make different paths identical.

Tilt, barrel rotation and stylus-wheel control require compatible pen and driver data. They should not be confused with canvas rotation or the mouse wheel. **Transfer → Speed response** can change size and opacity using pen movement speed, with a reference speed and signed response amounts.

## Place one stamp or draw a trail

A normal **Stamp** uses the foreground color. Transparent images supply their alpha; opaque images use dark-on-white coverage. A **Colored stamp** retains the source colors and transparency, including an opaque background present in the image.

Enable **Adjust stamp** to place one impression and then move, resize or rotate it with the bounding box. Enter or the checkmark places it; Escape discards it. Only the new impression moves. Clicking outside confirms it and begins the next placement. Turn Adjust stamp off for continuous trails.

For several images, create **Dynamic stamp** and add 2–16 tips. Choose **Foreground** for masks or **Image colors** to preserve colors. In **Tip shape**, use Ordered, Random or Shuffle; Shuffle visits every tip without immediate repeats. Pressure and Direction are alternative selection modes. Set a root anchor near a tree or grass base to keep that point aligned to the drawn route.

Size, Rotation and Saturation variation can change between impressions within one stroke. **Color dynamics → Vary image colors** explicitly enables color variation for colored tips; **Foreground tint** shifts them toward the selected hue and saturation while retaining brightness detail. Gray source colors may show little saturation variation.

## Connected marks, architecture and tones

**Tip shape → Ribbon / Repeat** maps imagery along a curve. Draw to build the strip; pen-down alone does not place a complete motif. Width follows Size and supported dynamics, while Repeat length controls the motif's length. Ordinary stamp spacing and scatter are retained for Stamps mode rather than shaping connected strips. Adjust stamp is unavailable here.

Use broad curves for rope, ivy, cables and architectural repeats. Tight inside turns can compress or overlap motifs; the engine does not invent special corner windows or roof pieces. Slower input should not be used to deliberately pile up more connected motifs.

**Patterns and tones** offers Dots, Lines, Crosshatch, Diamond, Noise, Fade dots and Image. Frequency is cycles per 100 artwork pixels; Density controls coverage. Angle and offsets place the grid. Image tiles can use an explicit **Perspective plane** with four corners. **Fit to artwork** places that plane over the frame; it is not automatically linked to a perspective guide or 3D camera.

Large tips, fine spacing, heavy texture, particles and symmetry multiply rendering work. Save a smaller working variant when necessary, and inspect final marks at output resolution. See [Troubleshooting](shortcuts-and-troubleshooting.md) for a focused pen-lag check and [Vector drawing](vector-drawing.md) for editable brush paths.

## Retouch an existing raster pass

The **Retouch** group contains Smudge, Mixer, Blur, Sharpen, Dodge and Burn as separate tools. Select the raster layer containing the paint, set Size and Strength, and work with short strokes so each correction is easy to review or undo. Selection and feathering limit their coverage; layer and parent locks still apply.

Smudge carries nearby paint along the drag. Mixer combines carried paint with some foreground color. Blur and Sharpen modify local detail, while Dodge and Burn move color toward lighter or darker values without replacing its transparency. These are native image-processing brushes, not a physical pigment or wet-reservoir simulation. Pen pressure affects diameter and strength. Pen-up commits the stroke; Escape cancels an unfinished correction. Use a duplicate layer when comparing a larger retouch pass against its original.
