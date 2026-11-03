---
title: "Symbols and manual lip-sync"
description: "Reuse linked artwork across shots, edit shared sources and schedule mouth or expression poses."
category: "Animation"
order: 130
---

A Symbol stores reusable artwork and its own internal animation. An **instance** is a linked layer placed in a shot. Edit the shared definition to update every linked instance; move or retime one instance to change only that placement.

Symbols suit a reusable prop, blink, walk cycle, mouth set or effects element. They belong to the movie project and can be reused across its shots. Ordinary drawing reuse stays inside one drawing track; Symbols provide the broader shared-source workflow.

## Create a definition from artwork

Open **Panels → Symbols**. Select an unlocked layer, folder or continuous set of sibling layers in Layers, then choose **Convert selected layers to a symbol** and name it. The selection becomes a linked instance in the same location, with its source artwork moved into the library.

Use a folder to enclose related clipping bases and clipped layers before converting. Include a complete animation track to retain its drawings and timing. Converting only part of a drawing container is unavailable. Production Notes and 3D references should remain outside the Symbol because its contents are 2D artwork.

Alternatively, **New symbol** creates an empty one-frame definition and opens its editor. It does not automatically place an instance in the shot. Choose the definition and **Add an instance to this shot** after creating the artwork.

Give reusable assets descriptive names, such as “Hana mouth set” or “Lantern flicker.” The library tooltip identifies duration and linked uses, helping you judge the scope of a source edit before opening it.

## Edit the shared source

![The Symbol editor opens reusable content with its own drawing timeline.](/assets/screenshots/symbol-editor.webp)

*The Symbol editor opens reusable content with its own drawing timeline.*

Double-click the library entry or choose **Edit symbol artwork**. The separate editor has a canvas, Tools and Properties, Layers, Colors and Frames tabs. Its header provides frame count, Undo, Redo, Fit and a timeline toggle.

The editor works on a draft. **Save changes** applies the complete source edit to the movie as one Undo operation; **Cancel** leaves the earlier definition unchanged. If you just created an empty Symbol, cancelling its first editing session leaves that library entry available until you delete or undo its creation.

Increase the internal frame count before adding a longer sequence. It cannot be shortened past artwork, keys or source frames referenced by timed poses. A stale editor draft is rejected if its original definition changed meanwhile, so reopen instead of overwriting newer source work.

## Position and play an instance

Select the instance in Layers. Use the Symbols move action or animated layer transform controls to place it. Its own layer can have position, scale, rotation, opacity, masks and effects, independently of the internal animation.

The playback modes are:

| Mode | What the instance displays |
| --- | --- |
| Loop | Repeats the internal sequence |
| Once | Plays to the end, then holds the last frame |
| Still | Holds First frame |
| Ping-pong | Alternates forward and backward playback |
| Poses | Holds explicit source frames chosen at parent times |

**Start** places the clip in the parent timeline. **Hold** sets its visible length; zero retains an empty instance. **First frame** chooses the internal starting frame. **Speed** changes ordinary playback rate. **Slip** moves the internal schedule beneath the clip without moving the clip itself. Still disables Slip and Speed; Poses ignores Speed while retaining the saved value.

A looping walk can therefore move across the shot with outer position keys while its internal legs repeat. Two instances can use one source with different starts or speeds.

## Edit the timeline clip

Each instance has a clip row, including instances inside animation drawings. Solid portions intersect the owning drawing's exposure; dashed portions remain editable where that drawing is not exposed. Editing the clip does not create a missing drawing exposure.

Drag the body to move, edges to trim, or Alt-drag to slip. Alt+Left/Right nudges placement; Alt+Shift+Left/Right nudges Slip. Left trimming preserves the source frames at surviving parent times. Escape cancels a timing preview.

Delete or **Clear clip exposure** sets Hold to zero rather than deleting the source definition. Restore Hold or choose **Continue through the shot end** to show it again. The image icon opens a paged visual frame picker for First frame, with **Hold selected frame** available for a Still instance. This picker changes the fallback or start; the timed-pose action below creates changes during the shot.

## Create manual lip-sync

1. Make a Symbol with mouth drawings on known internal frames. Include a resting mouth and the shapes useful for your character's performance.
2. Add its instance to the shot and place it over the face.
3. Import dialogue audio and enable audible scrubbing if useful.
4. Select the clip and open **Frame Properties → Symbol → Poses**.
5. Move the parent playhead inside the clip, choose **Choose a timed pose at the playhead**, select a source thumbnail and Apply.
6. Move to the next mouth change and choose another pose.

Each choice holds until the next key. Before the first key, First frame supplies the fallback; after the final key, the pose holds to the clip's end. There is no morph between mouth drawings and no automatic speech or phoneme recognition.

The Poses lane shows diamonds and held intervals. Double-click it or press Enter on its selection to open the timed picker. Drag, nudge, copy, paste or delete pose keys using the usual Timeline conventions. The **Pose** field changes the source drawing and **Key frame** retimes an exact key.

Moving the clip carries its schedule; Slip shifts the schedule beneath it. Trimming can retain hidden keys for later extension. Switching to another playback mode preserves pose keys. Dialogue cues and the waveform help you make timing decisions but do not generate them.

## Separate a special case

**Make selected instance unique** duplicates the definition and links only that instance to the copy. Use it when a shared mouth set needs one shot-specific drawing. Nested Symbols inside the copy remain linked to their existing definitions.

**Break apart at current frame** replaces an exposed instance with editable layers from the frame currently displayed. Internal timing freezes at that frame; outer instance motion and compositing remain on the wrapper folder. The original library definition stays available. This is a deliberate conversion to a current-frame drawing, not an extraction of the whole animated sequence.
