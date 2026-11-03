---
title: "History and Auto Actions"
description: "Navigate recent artwork edits and record repeatable layer, fill and raster-transform workflows."
category: "Workflow"
order: 150
---

History answers “How do I return to an earlier state of this document?” Auto Actions answers “How do I repeat a supported sequence on another target?” They are separate panels with different storage and Undo behavior.

Open either from **Panels → History** or **Panels → Auto Actions**. They are ordinary docks and can join your Layers tab group or float separately. Keep History visible during exploratory painting, and bring Auto Actions forward when a repeated setup is slowing you down.

## Move through artwork history

![History shows the recorded fill and transform operations for the current document.](/assets/screenshots/history.webp)

*History shows the recorded fill and transform operations for the current document.*

Click a state in History to return to it. A marker and bold text identify the current state; later redo states appear dimmed. The saved state is labelled while it remains retained. The panel's Undo and Redo actions move one operation at a time.

Most completed drawing or property gestures create one meaningful entry. A long pen stroke is not a separate history event for every input point; a slider drag becomes one change when finished. This makes it easier to undo a decision rather than reconstruct how many pointer updates produced it.

If you move back and then make a new edit, the old redo branch is removed. Before painting over an earlier state, decide whether you need a separate saved project copy to preserve the alternative. The panel is not a branching version-control system.

History is session-only and resets when the document is loaded. It retains up to 50 commands, subject to memory limits. When older edits are no longer available, the baseline becomes **Oldest retained state**. Repeatedly clicking backward cannot recover states already discarded from that budget.

## Resolve an unfinished operation first

An active stroke should finish before History navigation. A pending transform retains its **Apply / Discard / Cancel** decision rather than being silently committed by a history click. Other editors can also require finishing or cancelling an active draft.

Undo during an unfinished preview often cancels that preview before moving to an earlier committed state. Read the current canvas state before pressing Undo repeatedly. In particular, a transform preview and an already-applied transform are different stages.

Tool and brush organization has a separate history. When Brush Management or the relevant tool organization surface has focus, its Undo restores library changes instead of erasing artwork. Light Table pins and unfinished shading drafts also follow their own saved-aid behavior; the shading panel's step-back edits its draft.

## Record a first action

1. In Auto Actions, click **+** and name the action, such as “Line art setup.”
2. Click **Record**.
3. Add a drawing layer, rename it “Line Art,” and set a supported opacity or blend mode through the ordinary interface.
4. Click **Stop**.
5. Select an appropriate target and click **Play** to reuse the setup.

The action is relative to the target context, not a script tied to the original layer's identity. Selecting another layer during recording stores a relative stack offset. Before playback, place the active layer where that relative relationship makes sense.

Recording again appends to the selected action. Recorded values appear in step labels and tooltips. Uncheck a step to skip it; right-click to move it up or down or remove it. Rename, Duplicate and Delete are available in the action options menu.

## Know what can be recorded

Supported operations include adding drawing or Notes layers, making a folder around the active layer, duplicating or deleting one drawing layer, relative layer selection, renaming, visibility, locks, Notes status, blend mode, opacity, clipping and validated layer-effect settings.

Fill and ordinary raster transforms are also supported. **Fill selection** records the chosen color and uses the target selection when played. Without a selection, that action fills the full drawing area. **Paint bucket** retains click coordinates, tolerance, gap closing and sampling source. A reference layer is represented by relative stack position.

A raster move, resize or rotation is recorded when you choose Apply/Done. It reuses displacement, scale factors and rotation around the target layer or selection bounds. Previewing or cancelling the transform does not record a completed transform step.

Brush strokes, brush settings, gradients, masks, selection creation, perspective/warp, vector or group transforms, animation, exports and 3D edits are not recorded. An unsupported committed artwork edit stops recording with an explanation and preserves the earlier supported steps.

## Example: fill and place a repeated accent

Prepare a selection before pressing Record. Fill it with the desired accent color, choose the basic raster Transform, move or resize it, and Apply. Stop recording. The action now contains the supported fill and final transform, not the act of drawing the selection.

For another accent, choose the target raster layer and make its selection first. Play the action. The fill uses this target area and the transform follows its bounds. If you recorded a bucket click instead, its fixed document coordinates must still make sense in the new drawing.

Preview the action's step labels when moving between differently sized projects. Relative targeting is flexible, but it cannot infer that “the hair layer” moved to another position or that a bucket seed should follow a particular facial feature.

## Understand playback and storage

Before committing, the action evaluates its enabled steps on an isolated copy. If a step fails, the target artwork, selection and history remain unchanged, and the failed step is identified. A successful workflow enters artwork History as one Undo operation.

Actions save locally beside workspace preferences and are available across projects. Deleting an action changes that library; it is not an artwork Undo step. The library is not synchronized across devices automatically, and Photoshop or Clip Studio action-file import is not provided. Keep action names and step sequences simple enough that their intended target remains clear when you return to them later.
