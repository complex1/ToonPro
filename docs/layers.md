# Layers — 0.20.0

The compact Layers panel shows the topmost artwork first. Its blend control and dedicated opacity row stay above the scrolling stack; the main creation and deletion actions stay below it. The layout takes reference from Clip Studio Paint's separation of the layer list, properties bar and command bar, while using Toon Pro's shared icons and 600 ms tooltips. [Clip Studio Paint layer palette reference](https://help.clip-studio.com/en-us/manual_en/180_layers/Using_layers.htm)

In an animated shot, the list contains the static stack and the current drawing from each animation track. Hidden-time drawings are omitted. Track/drawing container groups are protected: use Timeline to create, duplicate or remove drawings and tracks. Raster sublayers can be added and organized inside a drawing; cross-drawing moves, vector/text/Notes creation inside cels and whole-shot flattening are unavailable. Static layers retain their existing tools. See [frame animation](animation.md).

## Reading and editing a row

Each 36 px row contains visibility, a 28 px checkerboard thumbnail or folder icon, the name and a lock-status column. Raster thumbnails show only that layer's pixels, fitted to its painted content inside the drawing extent. They update during drawing, independently of the layer's visibility, opacity and blend mode. Vector and text layers display type badges. Notes, clipping and alpha lock have small status markers; ordinary raster layers do not have a redundant type badge.

- Click an eye to show or hide its layer without changing the active drawing target. Drag down the eye column to apply the first eye's new visibility to every crossed row. The complete sweep is one undo operation; Escape cancels it.
- Double-click the name, or use Rename in the context menu, to edit it inline.
- A padlock indicates the row's own full lock; a pin indicates its own position lock. Click a displayed padlock to unlock that layer. Locks inherited from folders still apply even when a child has no local lock icon.
- Folder carets expand or collapse indented children. Row colors add a narrow color mark and a subtle tint; they never tint artwork.

## Properties and quick actions

The header edits the single active layer or folder. Choose a blend mode directly; wheel or arrow-key changes immediately update the canvas. Supported modes are **Normal, Multiply, Screen, Overlay, Darken, Lighten, Color Dodge, Color Burn, Hard Light, Soft Light, Difference and Exclusion**.

Drag the opacity slider or type an exact percentage in the adjacent compact field. Both show the same value. Slider movement previews immediately and release commits once; Enter commits typed values and Escape cancels an active edit. Each drag or text-edit session becomes one undo operation. Arrow keys adjust a focused slider or numeric field. Opacity and blend controls are disabled while multiple rows are selected.

The property icons expose full lock, transparent-pixel lock, position lock, clipping, Notes status, Merge Down and the additional-actions menu. Full lock applies to the selected rows; the other property toggles apply to the single active row. The footer provides New Folder, New Raster Layer, New Vector Layer, New Notes Layer, Duplicate and Delete. New Text Layer is in the context menu. Rasterize converts the selected editable vector/text layer to ordinary pixels in one undo step.

In the current local build, the cube beside the blend control creates an editable **3D reference layer**. Open the editor toolbar cube to place Body, Head, Hand, Anime or Pets models, pose them directly on the drawing canvas, then use **Draw above** to create a raster layer for ink. Visibility, opacity, ordering and folder transforms apply to the reference; saving retains its models, poses and scene camera. [3D canvas workflow and limits](3d-reference-workspace.md).

- **Full lock** blocks painting, renaming, appearance edits, deletion and structural movement; a folder's lock applies to its descendants. Visibility and explicit unlocking remain available. An individually locked layer can be copied without altering its original, provided its destination folder is unlocked.
- **Lock transparent pixels** preserves existing pixel coverage while recoloring. It is available on raster layers; turn it off to erase.
- **Position lock** blocks pixel transforms, reordering, reparenting and grouping of the layer or its descendants. Painting and other non-position properties remain available.
- **Notes** excludes a layer or a whole folder's descendants from movie-frame exports and permits drawing in the paper margins below the header. Notes remain visible on production-sheet exports.

## Organizing layers

Shift-click selects a continuous range. Ctrl-click, or the platform's additive-selection modifier on macOS, selects separate rows. The current selected row remains the drawing target; painting is not applied across the selection.

Drag selected rows to reorder them. A blue horizontal line marks the insertion point; the center of a folder row is outlined when the drop will place layers inside it. Folders move with their descendants. The context menu also provides Move Up, Move Down and Move to Top Level.

New Folder wraps selected sibling layers or folders. They must come from the same parent. Adding a raster or Notes layer while a folder is selected places it at the top inside that folder. Duplicate copies selected rows and folder contents with new IDs and preserves sibling order; a single copy appears directly above its source. Delete removes the selected rows and folder contents without a confirmation dialog. Undo restores an accidental deletion. A static shot must retain at least one artwork layer.

Structural edits preserve existing clipping relationships. Moving, deleting or duplicating a clipping base without the affected clipped layers is rejected; select the complete clipping run together. Grouping a single clipped layer removes its clipping flag because its former base would be outside the new folder. The static stack and each animation drawing retain their separate 64-node limits; all operations retain the eight-folder-depth limit and reject cyclic group placement.

The right-click and additional-actions menus include Merge Down, Flatten Image, Rasterize for vector/text layers, and Change Row Color. Row colors are None, Red, Orange, Yellow, Green, Blue and Violet, applied to the selected rows. Duplicate, folder creation, rename, movement, Notes creation and deletion are also available there.

## Merge and flatten

**Merge Down** combines the active raster layer with the next lower sibling in one undoable edit. Both must be visible and unlocked, use Normal blending, share the same effective Notes status, and have no clipping or clipped dependents above the active layer. Folders are not merged by this command. The result keeps the lower layer's identity and name, bakes both opacities into its pixels, uses 100% opacity and turns off alpha lock. Other cases are rejected rather than changing the appearance of the surrounding stack.

**Flatten Image** bakes visible content into a raster artwork layer, retaining a separate raster Notes layer when present. Hidden artwork is omitted from the flattened result; Undo restores the original stack. All layers and folders must be unlocked. Notes must occupy separate top-level layers or folders above the artwork; mixed artwork/Notes folders, interleaved Notes, clipping across the artwork/Notes boundary, and non-Normal top-level Notes blending over artwork are rejected. These guards preserve both production-sheet and movie-frame output. Flattening preserves transparency and does not bake the editor's paper background into pixels.

Merge and flatten render sparse tiles including effect halos, retain off-frame artwork and commit atomically after validation. They remain synchronous CPU operations. They do not change other shots, imported references or drawing guides.

## Rendering rules

- A hidden folder hides its entire subtree. Group children composite in isolation; the group's blend mode and opacity then apply once to the group result.
- A run of clipped layers uses the nearest unclipped sibling below it as its base. Clipping never crosses a group boundary. A group may serve as a base, but a group itself cannot be clipped.
- Clipped layers blend color only inside the base's existing coverage, preserving its partial transparency. The base's blend mode and opacity apply to the complete base-and-clips result. A hidden, missing or export-excluded base makes that clipping run invisible.
- Alpha lock preserves every original alpha value during a stroke, including antialiased edges. It supports the native brush settings and stroke blend modes. An empty alpha-locked layer remains empty.
- Canvas, Navigator, transform previews and exports share the layer blending rules. Previewing a transform does not replace committed artwork until Apply.

## Persistence and limits

Layer hierarchy, pixels, opacity, all three lock states, blend mode, row color, clipping and Notes status persist in project saves and recovery snapshots. Effects, mask resources and adjustment layers were introduced in project format 17 (`TOONPRO17`). Current saves choose a format from the features used; animated effects or mask placement metadata require format 36, including inside Symbols. Legacy projects remain readable.

Row colors and blend modes are strictly validated before save/load. Static shots support 64 layer/folder nodes. Animated shots support 4,096 total nodes, with 64 per drawing and 64 in the static stack; nesting remains limited to eight folders. Vector and text sources retain their editable form and rebuild render tiles when opened; independent vector/text layers support transform/opacity keys, and compatible vector paths support morphs. Drawing containers support raster and vector layers, including editable brush strokes; Text and 3D layers remain outside those containers. See [drawing tools](drawing-tools.md) and [animation limits](animation.md#scope-and-limits). Effects and masks use the workflow below.


## Effects and masks in 0.27.0

Select an artwork layer or folder, then open **Panels → Effects**. The current local build has six tabs: Mask, Blur, Shadow, Glow, Color and Outline; hover for their names. The header switch bypasses the color, lighting and outline effects without deleting their settings. Masks have their own enable switch. Each effect has an enable button, live preview and slider/compact numeric controls. Shadow also has a draggable offset pad. Its pad covers ±128 pixels; exact X/Y controls reach ±512.

The compact layout in 0.39 keeps the layer name, status and category tabs above the scrolling controls. Labels sit beside the slider and exact entry when space permits, wrapping above them in narrower panels. Only the active category determines the required scrolling height; switching between categories restores their individual scroll positions for the current panel session. Smaller icons retain the shared 600 ms help delay, while Shadow's offset pad and the mask thumbnail keep their useful size. Resizing preserves unfinished numeric edits.

- **Mask:** add a white mask to reveal the layer, or create one from the current selection. Enable the paintbrush and choose Hide or Reveal. This dedicated round brush has Size, Strength and Hardness controls, with pen pressure affecting strength. Invert, reveal all, hide all, disable or remove the mask using the direct actions. Painting respects the current selection. Escape cancels an unfinished stroke; Escape when idle exits mask painting. Switching to a drawing tool also exits mask painting.
- **Blur:** softens the layer with a three-pass box approximation of Gaussian blur.
- **Shadow:** adds a colored, offset silhouette with radius and opacity controls.
- **Glow:** adds a colored halo with radius and opacity controls.
- **Color:** adjusts exposure, contrast, saturation and hue. Exposure operates in linear light; the remaining controls operate on unassociated sRGB color. Alpha is preserved.
- **Outline (local 0.76):** adds a smooth outer border around the layer's visible alpha shape, including holes, with width from 0–128 source pixels and an opaque color. The default is 3 pixels, white. Folder outlines follow the combined visible children. [Controls and examples](layer-outline.md).

The header's **Add color adjustment layer** action creates an adjustment above the active layer, or inside the selected folder. It corrects the visible lower siblings in that folder without changing their source pixels. Opacity blends its correction; a mask restricts its area. Adjustment layers expose Mask and Color only, cannot be painted as artwork or alpha-locked/clipped, and do not count as the drawing layer a static shot must retain. Move them in Layers to change which siblings they affect. Layers shows small mask, effects and adjustment indicators without widening rows.

Masks are separate grayscale coverage, not selections or erased source pixels. They stay attached to their layer's source coordinates and follow layer/folder keyframes and the shot camera. The processing order is mask, color, blur, outline, shadow/glow behind the content, then layer opacity, blending and clipping. Folder effects apply to the isolated visible children, including an on-canvas 3D reference if one is inside the folder. Direct effects/masks on Notes and 3D-reference layers are unavailable, and references remain excluded from exports.

As of 0.73.0, effect settings and mask properties can be keyframed. Click a property's diamond in Effects, or use Frame Properties → Animation → Effects / Mask, then change its value at another frame. Blur, shadow/glow size and strength, shadow offset, and exposure/contrast/saturation/hue interpolate; enable switches and mask inversion hold until the next key. Shadow and glow colors remain static. Mask position, scale, rotation, strength and feather animate independently of the layer. The underlying painted mask remains shared across frames; per-frame mask cels and mask-path morphing are not implemented. Destructive pixel transforms move raw pixels beneath the fixed source-local mask; use animated layer/folder transforms to move the whole masked composition. Merge Down bakes compatible raster effects/masks once, including their halos, and removes the consumed settings. Flatten bakes the compatible visible stack, including adjustment layers; existing animation/Notes restrictions still apply. Undo restores the editable sources.

Outline's switch, width and color are static settings; the existing animated effect channels are unchanged. Each slider/pad drag and mask stroke is one Undo action. Exact-entry drafts resolve before Save, export and timeline/layer navigation; Escape or Undo cancels unfinished edits. Saves and recovery retain effects, adjustment settings and mask pixels. Canvas, Navigator, PNG frames/sequences and MP4 share the evaluated compositor; viewport AA remains a display preference.

Radii range from 0–128 source pixels, exposure from −4 to +4 EV, contrast/saturation from −100 to +100, and hue from −180° to +180°. Mask brush diameter is 1–512 pixels. Mask painting is limited to the drawable area; effects may extend beyond it and are cropped by the output frame. A project holds up to 1,024 mask tiles (64 MiB decoded) separately from its artwork tile budget. Rendering uses a 64 MiB output cache, an 8-million-pixel per-region limit and a 256 MiB working-memory guard. Large uncached effects are CPU work and can take longer to preview; extreme scales/regions produce a readable error rather than a partial export. Physical pen performance and production-size shots remain broader acceptance work.
