---
title: "Frame-by-frame drawing and timing"
description: "Create cels, control holds, reuse drawings and organize an animation shot without losing timing."
category: "Animation"
order: 110
---

Frame-by-frame animation separates a drawing from its **exposure**, the interval when that drawing is visible. Holding a drawing for two frames uses one drawing twice; it does not create two independent images. Editing shared artwork changes every exposure that refers to it.

The shot can combine static backgrounds, several drawing tracks and animated properties. A held drawing can move through position keys while its line art stays unchanged. Start with exposure timing first, then add motion only where the shot needs it.

## Create the drawing track

1. Open **Panels → Timeline** and **Panels → Frame Properties**, or choose the Anime workspace.
2. Click Timeline's **+**, choose **Frame by frame**, and name the track.
3. Choose **Simple**, **Rough**, **Anime** or a saved drawing template.
4. Draw on the active sublayer shown in Layers.
5. In **Frame Properties → Drawing**, set **Hold** to the number of frames the drawing should remain visible.

Rough includes Rough, Sketch and Clean-up. Anime includes Rough, Sketch, Colors, Shadow, Highlights and Line Art. You can edit the template's ordered layer recipe, choose raster or vector types, and save a named recipe for later projects. A template contains structure, not artwork or brush resources.

Frames shown in the interface begin at 1. Change FPS and shot duration through **File → Project & shot settings…**. Blank intervals require a drawing before you can paint. Pause playback to edit.

## Create the next drawing

Move the playhead and choose **New blank drawing** for a clean template, or **Duplicate drawing** for an independent copy of the current artwork. Duplicate is useful when only a small detail changes; it does not keep the copied paint linked to the original.

Creating a drawing inside a held interval splits that hold at the playhead. At the first frame of an existing exposure, the new drawing is created after that exposure when there is room. At a gap, it starts there. The operation rejects collisions instead of overwriting another drawing.

Use Drawing's name, Start and Hold controls for precise edits. Drag a block's body to move its exposure; drag either edge to trim or extend one selected hold. A preview commits on release and Escape cancels it. Later drawings do not move automatically when you make an ordinary trim.

## Time a sequence on ones, twos or threes

Click a cel to select it. Shift-click extends a range, Ctrl/Cmd-click toggles individual cels, and dragging from blank drawing-track space creates a marquee. Use **On 1**, **On 2** or **On 3** to assign common exposure lengths.

For multiple selections, Hold and the On buttons arrange selected cels consecutively from the earliest selected start on each track. Existing gaps between selected cels close. Unselected cels stay put, and a resulting collision rejects the complete operation. Review the selection before changing a large batch.

Drag any selected body to move the batch while preserving its relative gaps and track alignment. **Alt+Left/Right** nudges by one frame. Multi-selection does not trim every selected edge at once. **Reverse** mirrors selected intervals within their per-track span, carrying their durations with them.

**Repeat** appends a linked repeat after the complete selected range. Copy and Paste also copy exposure timing and reuse the existing drawings in their original tracks. Use Duplicate drawing when you need a new independently editable drawing instead.

## Reuse artwork deliberately

Choose **Reuse existing drawing** from the Timeline footer or track context menu. Its picker lists the current track's drawings with thumbnails, names, compact labels and usage counts. Search for a name or “Unused,” choose a Hold, and click **Reuse** in an empty interval.

Reusing a drawing shares its artwork. Removing an exposure leaves the drawing in the library so it can be reused later. The picker's trash action deletes only an unused drawing, with Undo. Labels such as D001 identify list order and may renumber; the actual saved links use stable drawing identities.

Reuse belongs to the same track and shot. Use [Symbols](symbols-and-lip-sync.md) for linked artwork across shots or for a reusable independently timed animation.

## See neighboring drawings

Enable **Onion skin**. Its settings allow up to eight distinct drawings before and after, separate colors and shared opacity. Held or repeated uses of the current drawing do not create duplicate ghosts. Static backgrounds do not become ghosts either.

Use one previous and one next drawing for a clean starting view. Increase the count when checking a motion arc, then reduce it again for line cleanup. Each ghost uses its neighboring drawing's evaluated pose through the current camera. Onion skin is hidden during playback and omitted from exports.

## Keep the Timeline readable

Artwork headers provide Visibility, Lock and Solo. Visibility and Lock match the saved Layers state. **Solo** only isolates the workspace preview and does not change exports or mute audio. **Clear timeline solo** restores the normal preview.

Create a **New timeline folder** to group related artwork tracks. Drag a row header to reorder the complete subtree; drag within the time grid to change timing. Folder carets collapse rows without changing the project. Protected drawing containers cannot be detached from their track.

Use zoom, **Fit Shot**, **Fit Selection** and **Center playhead** instead of squeezing very short cels. Ctrl/Cmd+wheel zooms around the pointer. In the focused grid, arrows navigate, Home/End reach shot bounds and Space plays or pauses.

## Retiming the whole shot

Ordinary cel edits do not move camera keys, property keys or audio. Use **Ripple time** when the entire shot must gain, lose or move a time interval. Choose Insert, Delete or Move, enter the range and review before Apply.

Ripple affects exposures, keys and sound together. Insert can split a hold into linked uses of the same drawing. Delete closes the interval while retaining unused drawings for reuse. Some Symbol arrangements cannot represent a split internal clock or visibility interval; those edits are rejected intact with a reason. If the operation is blocked, make a smaller timing change or revise that clip explicitly instead of assuming some tracks changed and others did not.
