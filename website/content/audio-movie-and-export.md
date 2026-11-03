---
title: "Audio, movie review and export"
description: "Mix shot audio, review cuts continuously and deliver images, video or a separate sound mix."
category: "Animation"
order: 140
---

Toon Pro edits individual shots and can review or export them in movie order. Each shot keeps its own drawings, camera, animation and audio. Scene and Movie review provide a continuous viewing surface without repeatedly switching the active drawing document as playback crosses cuts.

Use this workflow for dialogue timing, animatic review and checking how completed cuts sit next to one another. A movie remains a series of hard cuts; global editorial audio spanning shots and cross-shot transitions are not currently separate tracks.

## Arrange shots and scenes

Open **Panels → Shots**. **+** creates a blank shot with the current sheet settings and duration. Duplicate copies the shot's artwork, keys, references, guides, cameras and audio into an independent shot.

Double-click Number or Shot to rename. Drag rows or use the up/down actions to set movie order. Assign a **Scene** label to organize related shots, then filter for that scene. Scene labels do not silently rearrange the movie; row order remains the editorial order.

Select a shot row to edit it. Pending transforms keep their Apply / Discard / Cancel decision. The session remembers the editing frame and layer context for each shot. Delete is undoable, and the movie must keep at least one shot.

## Import and place sound

Move the playhead to the intended start and choose **File → Import audio…**, or the Timeline music action. Decoding runs in the background and can be cancelled. A completed import waits for incompatible active editing to finish before entering the shot.

The waveform appears on its own row. Select it for **Frame Properties → Audio**. Drag the body to move Start, or an edge to trim. **Source in** chooses the offset into the original recording; **Duration** controls the visible length. Trimming does not delete source samples.

Start is displayed as a one-based shot frame. Source in is an offset from the sound's beginning. Use the exact fields when a very short clip is too small to trim comfortably. Volume and Mute affect the mix, while rename and remove are available from the clip context menu.

Sound is converted to embedded 48 kHz stereo PCM, so the original file is not required after a successful project save. Supported file decoding depends on the bundled multimedia backend. A failed import explains the problem without replacing existing artwork or audio.

## Fade, automate and crossfade

In **Audio → Mix**, use the waveform fade handles or **Fade in / Fade out** fields. Choose Linear or Equal-power curves. Enable the volume diamond to add gain keys, then edit at the playhead or drag keys in the graph.

Volume automation uses linear interpolation with held values outside the key range. Removing its final key leaves the last gain as static volume; disabling automation retains keys for later use. Volume extends to 200%, so overlapping loud clips can clip at the output range. Listen to the combined result rather than only checking individual waveforms.

Crossfade offers clips with a suitable existing tail/head overlap. Choose the partner and curve to set the two fades without moving their clips. Non-overlapping or nested clips are not automatically rearranged into a crossfade. Crossfades belong within a shot; movie cuts remain hard cuts.

Enable the Timeline speaker action for **audible playhead scrubbing**. Dragging the ruler plays short sound grains to help locate a syllable or impact. Numeric frame entry stays silent. Release stops the scrub sound; ordinary playback uses the shared animation clock.

## Rehearse a shot

Shift-drag the ruler to select an inclusive playback range. Adjust its ends or enter exact range values, then choose whether to play that range or the entire shot. Loop repeats the enabled scope. This temporary rehearsal range does not crop the project or automatically constrain every export.

Use the playback quality control for Full, Half, Quarter or Eighth preview resolution. It changes the work needed to display frames, not project resolution or timing. Heavy pictures may skip displayed frames while the audio clock advances; pausing requests the exact selected frame.

## Review the scene or movie

Timeline's **Shot / Scene / Movie** tabs switch between editing and review. Scene includes shots matching the active editing shot's scene label, in movie order. Movie includes all shots. The global frame readout also identifies the current shot and local frame.

Click a thumbnail to seek to its beginning. The global slider and number reach any frame in the chosen scope. Previous/Next Shot navigate cuts. Play and Loop continue across them with each shot's trimmed, faded and automated sound. Audio stops at its shot boundary.

Double-click a thumbnail or choose **Edit shot** to enter that reviewed drawing. Choosing Shot or pressing Escape returns to the original editing shot and frame. In review, Space toggles playback, arrows step globally, Home/End reach the scope ends and Enter edits the reviewed shot.

Review shows the final artwork frame without the production header, Notes, guides, Light Table or reference-only 3D. Seeking itself does not add Undo steps. A correct-shot thumbnail may briefly appear while a heavy exact frame prepares. Source artwork remains unchanged.

## Choose the delivery format

| File action | Delivers |
| --- | --- |
| Export movie frame PNG | The current evaluated artwork frame, with configured background transparency |
| Export production sheet PNG | The current picture on its production paper, with sheet information and Notes |
| Export animation PNG sequence | Every shot frame in a newly named folder |
| Export video MP4 | Current shot, whole Movie or a Shot queue with H.264 and optional AAC sound |
| Export mixed audio WAV | Current shot or whole-movie stereo PCM mix |

MP4 offers Full shot or Custom range, aspect-linked output dimensions and Draft/Standard/High quality. Dimensions must be even and fit the supported 16–4096 range. Different output proportions use white padding instead of stretching. Transparency composites over white, and project FPS is preserved.

**Include audio** uses the saved mix. Movie ranges use global frames; queued shots can have individual export settings and names. Export resolves pending editor values, pauses playback and works from a frozen snapshot. Progress and cancellation remain available. A failed or cancelled video export preserves an existing destination; completed sequence or queue items can remain.

Keep the `.toonproj` as the editable master. Inspect a short exported section with picture and sound before delivering a long job, especially after changing frame rate, dimensions or audio timing. Exports exclude editor aids even when those aids were useful for making the drawing.
