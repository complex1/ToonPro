# Cel coloring and tracing

Open **Panels → Cel Paint Check**, **Panels → Cel Shading**, or **References → Light Table**. Version 0.61 introduced these workflows; version 0.62 adds project storage for light-table pins and unfinished shading. Accepted repairs change ordinary raster tiles, and applied shading creates raster layers. Saved drawing aids stay separate from that artwork and its exports.

The 0.62 persistence pass is verified across 12 affected regression suites and four workflow suites on native macOS and at offscreen 2× scale. Restored Light Table and shading previews were visually reviewed at both scales. See the [development log](DEVELOPMENT.md) for the evidence and remaining physical-tablet acceptance scope.

## Cel Paint Check

1. In **Drawings**, check the raster paint layers to inspect. The list includes static layers and the layers inside other drawings in the current shot.
2. Choose a character and lighting palette such as **Hana · Night** as the approved palette, or leave palette checking off. Explicit navigation in References → Color Models also selects the corresponding approved palette here. The Paint Check chooser remains independently usable. Adjust tolerance, the repairable hole size and the stray-pixel size when needed.
3. Click **Check**. The **Problems** tab lists suspected enclosed transparent holes, small disconnected islands, possible edge-reaching fill leaks and colors outside the palette. Previous/Next jumps between issues and their drawing exposures. The canvas highlights the region; the panel shows the isolated drawing.
4. Check only the problems you want to repair. **Preview** fills small holes with the current foreground, erases checked stray/leak regions and replaces off-palette pixels with the nearest approved RGB color. Use **Show original** to compare each drawing.
5. **Apply** commits the batch as one Undo operation. **Cancel** discards the proposed repairs. Save, Close and timeline transitions resolve an outstanding repair preview first; cancelling that decision keeps the current context.

These are inspection heuristics, not proof that a drawing is wrong. Intentional negative space, detached detail and backgrounds touching the frame can be flagged. Leak detection identifies flat regions connected to the artwork-frame edge; it does not infer an intended character silhouette. Checks examine raw raster layers inside the artwork frame, not composited effects, overscan or vector paths. Nearly transparent/partially transparent edges and local antialias mixtures are excluded from flat-color palette checks. Palette matching compares RGB; it is not an alpha-channel conformance check.

Analysis runs in the background. Limits are 64 selected layers, 16 million pixels per drawing, 32 million pixels per batch and 1,024 reported issues. Large holes remain available for inspection; automatic hole repairs are limited to the selected Fill limit, up to 4,096 pixels. A bounded total of issue pixels can be prepared for repair. Editing a source invalidates its results. Apply checks every target before changing any, and refuses locked, hidden, alpha-locked or property-animated destinations.

## Shadow and highlight boundaries

1. Choose a **transparent raster outline layer** in Cel Shading. It can be locked because the tool reads it without painting on it.
2. Enable the boundary tool, choose **Shadow** or **Highlight**, and draw colored guide lines in **Boundary** mode. The guide tint helps distinguish the regions and is separate from the final paint color.
3. Choose the fill color, switch to **Fill**, and click inside a region closed by the outline and guides. The existing character outline remains unchanged.
4. Choose whether the guide disappears at its centerline or takes the adjoining region color across its guide width. Preview the result before applying.
5. **Apply** creates separate **Shadows** and **Highlights** raster layers below the outline, inside the same drawing folder. One Undo removes the result. Cancel discards the draft.

Regions reaching the frame edge are rejected, so an open boundary cannot accidentally flood the drawing. Each unfinished draft belongs to a specific shot, outline layer and frame. Save keeps its guides, fills, operation order, Shadow/Highlight colors, guide colors, line width, Boundary/Fill mode and boundary finish. Switching drawings, frames or outline sources retains separate drafts; return to the same target to continue. Reopening restores the saved shot/frame and rebuilds its shading preview. Apply clears the draft after creating the shade layers; Cancel clears only the current draft. The panel's step-back action removes its latest guide/fill operation independently of artwork Undo.

Source artwork changes refresh the preview instead of deleting the draft. If an outline is missing or the destination is no longer usable, the draft remains stored and Apply is blocked with a reason. Review the refreshed preview before applying it. Limits are 16 million canvas pixels, 256 guides and 64 filled regions per draft, with up to 64 drafts per project and one million path elements across them. Locked or hidden destination folders and playback prevent editing. Transformed outlines require source-drawing editing. Guides can be drawn with mouse or pen; hiding the panel or pressing Escape exits its canvas interaction. Finish an active pen stroke before saving or changing drawings.

## Light Table / Shift-and-Trace

- Pin the current layer, choose a drawing or folder from any shot, or import an image. Each pin is an explicitly labelled snapshot, so later drawing changes do not silently move the reference.
- Enable **Move/rotate/scale** to manipulate the selected snapshot with its canvas handles. Compact position, scale, angle and opacity controls provide precise entry. Reset restores its placement.
- Use visibility checks to combine references. Previous/Next enables solo viewing and flips through the pins without changing the drawing being edited.
- Exit pin editing to draw through the references. Middle-button/Space navigation remains available. Escape or hiding the panel exits pin manipulation.
- **Refresh** updates a document pin from its source. Removing a pin leaves the source unchanged.

Pins are embedded in the project together with placement, uniform scale, rotation, opacity, individual visibility, the selected pin, solo mode and overall visibility. They survive Save/reopen without requiring the imported image file. A deleted source drawing leaves its snapshot usable; refreshing from that source requires it to exist again. Opening a different project loads that project's pins.

Pins never modify source artwork or exports. Up to eight snapshots share a 64 MiB pixel budget, each fitted to 2,048 pixels per side; source images retain their original resolution. 3D and adjustment-only sources are not supported. Cached viewport overlays avoid resampling all pins on every pen redraw. Finish a placement gesture or snapshot import before saving.

## Saving and Undo

Adding, moving, changing or removing saved drawing aids marks the project unsaved. Returning all aids to their saved state clears that change and removes obsolete recovery snapshots when the artwork is also unchanged. Save writes them with the artwork; Close uses the normal Save/Discard/Cancel decision for unsaved project changes. Unfinished shading no longer requires Apply before Save or a frame change. Cel Paint Check repair previews retain their separate Apply/Discard/Cancel decision.

Artwork Undo/Redo preserves the current pins and shading drafts. Applied shading remains one ordinary artwork Undo step; undoing it removes the generated shade layers without recreating the cleared working draft. Use the shading panel's own step-back or Cancel actions to change unfinished work.

Projects containing pins, drafts or nondefault light-table visibility/solo settings require at least format 29. Character Color Variants raise the format to 30 while retaining these saved aids. Embedded PNG snapshots have checksums and dimension/pixel limits; drafts have bounded geometry and validated operation lists. Older projects open with empty drawing aids. Projects without newer saved features retain their existing compatible format. Drawing aids are not included in image or movie exports.

## Character lighting palettes in 0.63.0

Color Models supplies Day, Night, Sunset and Interior palettes for newly created characters. Lighting palettes start as copies of Day and are edited independently; they do not automatically transform lighting or recolor existing drawings. Older characters can add missing lighting palettes explicitly. Palette definitions are shared across shots and saved in the project; the current selection is remembered within the open session and returns to Day on reopen.

Paint Check compares against the chosen variant's colors, using the same existing RGB/alpha limits described above. Editing its palette invalidates an earlier check, so rerun inspection before preparing repairs. Changing the approved palette resolves an outstanding repair preview through the existing workflow. See [Character Color Models](drawing-tools.md#character-color-models) for creation, duplication, shared-role behavior and limits. The 0.63 integration checks pass for selected-variant inspection, palette changes, saved colors and existing repair guards; native and offscreen 2× workflow checks also pass.

## Previewed character recoloring in 0.64.0

1. In **References → Color Models**, choose a character and use the **Recolor drawings…** paint-bucket button. The dialog starts with Day as **From** and the selected lighting palette as **To**; when Day is selected, the first additional palette is suggested.
2. Choose the palette already used in the drawing and its replacement. Standard lighting palettes initially contain identical colors, so define their colors before expecting a visible change.
3. Check the raster drawings to update. **Current shot / All shots** controls the list; animation drawings on other frames are included. Only the active drawing starts checked. Hidden or locked layers/ancestors and notes are unavailable. Alpha and position locks are compatible with recoloring because it changes neither coverage nor position.
4. Click **Preview**. Preparation runs off the interface thread and can be cancelled. Select any prepared drawing and compare **Before / After**, with pan, zoom and fit. The view shows raw source paint before layer opacity, masks, effects, transforms or other layers; it does not change the canvas artwork.
5. **Apply** commits the entire checked batch as one Undo. Cancel or Escape leaves the artwork untouched. Changing the settings, checked drawings, source pixels or palettes invalidates the old preview. Applying rechecks every source before changing any of them. Outstanding Cel Paint Check repairs are resolved before opening recoloring, and playback stops.

Colors map through the character's shared role IDs. Pixels matching a source RGB color receive that role's target RGB, retaining their original alpha regardless of palette opacity. Premultiplied edge matching handles the quantization of translucent pixels without changing coverage. Narrow opaque antialiased boundaries are also remapped when both endpoint colors are identifiable in the original neighboring pixels, including across tile seams. Unrelated colors, broad gradients, unsupported mixtures and ambiguous edge matches remain unchanged. If two roles have the same source RGB but different target RGB values, preview reports those roles rather than choosing one. Replacements do not cascade through other mappings.

Full-resolution stored tiles are recolored, including painted overscan outside the artwork frame. A batch supports at most **64 drawings** and **64 × 1024² stored tile pixels**. Preview images are bounded to **2048 pixels per side**, with **16 × 1024² pixels per batch for each of Before and After**; resolution adapts to the selected count. Preview downscaling never reduces the resolution of applied pixels; larger batches must be split. No-match or identical-palette results create no Undo edit. The usual history memory budget applies, so a large batch can evict older history while retaining its own Undo step.

The result is an ordinary raster edit saved with the project, without a new file-format revision. Palette definitions are unchanged. This does not create a live link between palette swatches and painted pixels; later palette edits require another explicit recolor operation.

## Workflow references

The inspection and colored-boundary design is informed by OpenToonz's [painted-drawing checks](https://opentoonz.readthedocs.io/en/latest/painting_animation_levels.html#checking-painted-drawings) and [Autopaint for Lines](https://opentoonz.readthedocs.io/en/latest/painting_animation_levels.html#using-the-autopaint-for-lines-option). Toon Pro's implementation operates on its own raster layers and does not add Toonz indexed-color level formats.

The movable reference workflow follows the source-preserving model described in [Clip Studio Paint's light-table documentation](https://help.clip-studio.com/en-us/manual_en/600_animation/Using_light_table_layers.htm). Toon Pro saves embedded snapshots; Refresh is an explicit update from an available source drawing.
