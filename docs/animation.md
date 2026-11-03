# Drawing, animation, cameras and audio in 0.74.0

The shot timeline combines drawing exposures with property keyframes. Frame-by-frame drawings were introduced in 0.20; 0.21 adds layer motion, opacity and vector path morphs. Version 0.22 adds multiple-key editing, exposure dragging/trimming and selected playback ranges; 0.23 adds canvas motion paths and shared pivot controls; 0.24 adds audio clips and synchronized playback; 0.25 adds MP4 export with mixed audio; 0.26 adds 2D/3D camera animation; 0.27 adds effects and masks; 0.28 adds custom easing curves, curved motion handles and path orientation; 0.29 adds ripple timing, saved layer templates, vector cels and configurable onion skin. Later passes add scene/shot organization and movie exports (0.32), audio finishing (0.33), background saving and previews, reusable Symbols (0.65), cues (0.70), timed Symbol poses (0.71), Timeline track organization (0.72) and animated effects/masks (0.73). See the [November animation review](animation-review-2026-11-02.md) for the original findings and remaining priorities; 0.66 addresses Symbol pivots, safe ripple timing and preview scheduling. Drawing exposures and property keys are independent: a drawing can hold for several frames while its layer or folder moves continuously.

## Start frame-by-frame drawing

1. Open a movie project, then **Panels → Timeline**. **Frame Properties** is a separate dock, also available from Panels. Existing static artwork remains visible throughout the shot.
2. Click **+** in Timeline. Choose **Frame by frame**, name the track and choose **Simple**, **Rough** or **Anime**. Static tracks are also available for backgrounds.
3. Draw with the existing brush tools. The Layers panel shows the current drawing's sublayers and the static stack. Rough contains Rough, Sketch and Clean-up. Anime contains Rough, Sketch, Colors, Shadow, Highlights and Line Art, with Line Art above the color passes.
4. Use **Frame Properties → Drawing → Hold** to expose that drawing for several frames. Changing a held drawing changes every frame that references it; it does not create a new drawing automatically.
5. Choose another frame and click **New blank drawing**, or **Duplicate drawing** for an independent copy. Blank drawings use the track template. Duplicates copy the current drawing's layers, pixels and property channels with independent IDs; copied property keys retain their absolute shot-frame times.

Frames displayed in the UI begin at 1. Project duration and FPS are edited through **File → Project & shot settings**.

## Timeline flow and workspace polish in 0.74.0

The **Anime** workspace keeps drawing controls and references beside the canvas, with Timeline below. **Animatics** brings Shots into the left column and gives Timeline more height. New Project can create an Anime, Storyboard or Animatic drawing structure independently of the workspace. See [starters and workspaces](workspaces.md) for exact holds, layers and layout management.

Timeline's selection/context line identifies the selected drawing, keys, clip or cue and shows the current frame and elapsed time. **Center playhead** scrolls to the current frame without changing zoom, selection or artwork. Narrow panels wrap playback and frame-entry rows and place secondary actions in **More**; their existing controls and shortcuts remain available.

The toolbar's Trash follows the current selection: property/camera/pose keys, audio, cues, Symbol exposure or drawing exposures. A locked selected drawing or position-locked exposure cannot be removed. Removing an exposure keeps its drawing available for reuse. Artwork deletions retain their existing Undo behavior.

Saved workspaces retain the Frame Properties category using a stable identity, including when compact tabs hide their text. Restoring Camera does not clear cel/key selection or activate camera editing. Pending drawing-name/numeric edits and active gestures prevent workspace changes until resolved. This pass changes navigation and layout, without adding a new animation engine or key type.

## Animated effects and masks in 0.73.0

Select an artwork layer or folder and open **Panels → Effects**. Use the diamond beside a setting to enable its animation. This retains the source value, creates a baseline key at frame 1 and, when the playhead is later, adds a key there. Move the playhead and adjust the setting to create or update that frame's key automatically. A setting without an enabled channel still edits the static source. Disabling its diamond removes the channel and restores the source contribution; Undo restores the keys.

**Frame Properties → Animation** has **Transform / Effects / Mask** tabs. Only the chosen category's rows are shown; compact widths use icon tabs with delayed help. Clicking an enabled channel in Timeline selects its category and corresponding Effects page. The original six transform/opacity channels remain unchanged. There are 31 scalar/switch channels in total:

| Category | Numeric channels | Held switches |
| --- | --- | --- |
| Transform | Position X/Y, Scale X/Y, Rotation, Opacity — 6 | None |
| Effects | Blur radius; Shadow blur, X/Y and opacity; Glow radius and opacity; Exposure, Contrast, Saturation and Hue — 11 | Master effects, Blur, Shadow, Glow and Color enabled — 5 |
| Mask | Position X/Y, Scale X/Y, Rotation, Opacity and Feather — 7 | Mask enabled and Inverted — 2 |

Numeric channels use the existing Hold/Line/In/Out/In-Out or custom outgoing-segment easing. Switches always hold; interpolation and curve editing are unavailable for them. Previous/Add/Delete/Next, exact key time, multi-key move/nudge/copy/paste and whole-shot timing edits use the existing property-key workflow. Drawing exposure edits remain independent of property keys.

### Editing mask placement

Create a white mask or one from the current selection, then open **Effects → Mask → Placement**. Position, scale and rotation move the mask independently in the layer's local coordinates; the artwork's layer/folder motion and shot camera still apply afterward. Its saved registration pivot is separate from the artwork animation pivot and is static; this pass does not expose a mask-pivot editor or canvas mask-transform handles. New masks use the artwork's default registration point or the selected area's center. Earlier masks retain their original untransformed appearance.

**Opacity** is mask strength: 100 fully applies the mask, while 0 bypasses its hiding effect. **Feather** softens the mask in mask-source pixels before its local transform. **Invert** reverses its visible coverage without rewriting the source pixels. Enable and Invert can hold different values at different keys. The thumbnail continues to show the painted source, before placement and inversion.

The **Paint** tab edits the same mask image across all frames. Reveal/Hide account for the evaluated placement and inversion at the current frame, while the white/black fill actions explicitly fill the shared source. Moving the playhead does not create a new mask drawing or animate painted pixels. A disabled mask, zero mask strength or non-invertible placement blocks painting with a reason. Mask placement/opacity/feather controls are slider-entry pairs; this pass does not add animated vector mask paths.

### Edit ownership and limits

Each completed numeric or shadow-pad drag is one Undo operation. Exact-entry drafts survive unrelated refreshes and resizing. Escape restores the unfinished gesture; Save, frame/layer navigation and category changes resolve pending edits. Mixed shadow-pad changes can update an animated X value and static Y value together without baking evaluated values into the source. Shadow/glow color edits remain static. Editing keyed effects or mask settings leaves Source mode so the current evaluated result is visible.

Full layer/ancestor locks protect all these edits. Position locks protect layer motion and mask position/scale/rotation, while effect offsets, mask opacity/feather and switches remain editable. Missing masks disable Mask animation with an explanation. Adjustment layers support color-correction and mask channels; blur, shadow and glow channels belong on artwork layers or folders. Notes, 3D references and folders containing them retain their existing property-animation restrictions.

The background-preview path keeps only the latest waiting change and refines the current result after interaction. Playback, reduced previews, nested Symbols, exact paused frames and export evaluate the same channels. Heavy effects can still finish below the requested playback rate; these controls do not establish a new real-time performance guarantee.

Effect/mask channels or nondefault mask placement, pivot, opacity, feather or inversion require **format 36**, including within Symbol definitions. Other projects use their applicable earlier format. Saving stores one source mask image plus its properties/channels; neither preview frames nor transformed copies are saved. Existing combined key, tile, history and project resource limits apply. Effect colors, mask pivot, animated mask drawings and animated vector mask paths are outside this pass.

**0.73 verification:** the app/test build and all 117 affected regression suites pass. Nine native Mac and nine Retina suites each pass 110 checks, including focus, simulated tablet input and narrow/larger-font controls. Evidence is recorded in [the checklist](TODO.md) and [development log](DEVELOPMENT.md). Physical Huion feel and production-shot acceptance remain separate from automated validation.

## Timeline track organization in 0.72.0

Artwork row headers show **Visibility**, **Lock** and **Solo** controls beside the name. Visibility and Lock edit the same layer/folder state used by Layers, with Undo. A locked ancestor still protects its contents; clearing a child's own lock does not unlock that ancestor. Drawing exposure containers remain protected and are edited through the cel workflow.

Click **New timeline folder** to name and create a folder. Right-click an artwork row to create a folder inside an ordinary folder or beside a track. Ordinary artwork folders can contain static artwork, Symbols and animation-track roots. Animation tracks cannot be nested inside another animation track or its drawing containers.

Drag the **row header** to change stacking order. The insertion line shows the destination; dropping into an ordinary folder changes its parent. The preview commits on release as one Undo operation; Escape or lost window focus cancels it. Invalid destinations explain the restriction and leave the hierarchy unchanged. **Move track up/down** and **Move to top level** are available from the row context menu; with the Timeline focused, **Ctrl/Cmd+Shift+Up/Down** moves the selected artwork row among its siblings. Dragging cels, clips or keys in the timing grid retains its existing timing behavior.

Reordering and nesting move the entire layer subtree while retaining drawing IDs, cel exposures, property keys, Symbol pose clocks and audio timing. Layer order and parent folders affect compositing as they do in Layers. The operation refuses cycles, incompatible drawing ownership, clipping-base changes, excessive depth and locked layers/folders. Registered drawing containers cannot be detached; legacy deletion of a folder containing animation tracks is also rejected rather than leaving broken timing references.

Use the folder caret to collapse or expand its Timeline children. Collapse only changes the row display. It is remembered per shot during the current project session, independently of Layers' expansion state, and resets when a project is loaded. Saved folder names, structure and stacking order use the normal project and Undo workflow.

**Solo** isolates one or more chosen artwork rows in Canvas and Navigator. It respects their saved visibility and includes their required ancestor groups and clipping bases. It does not rewrite eye states, mute audio, add Undo entries or alter exports. Hidden artwork remains hidden; use its Visibility control if it should appear. Click the Solo control again or **Clear timeline solo** to restore the normal preview. Solo resets when changing shots or loading a project. Audio, camera, cue and property-only lanes retain their existing controls rather than receiving artwork hierarchy actions.

Nested animation tracks use **format 35**, including nested tracks stored inside Symbol definitions. Other projects keep their applicable earlier format. Ordinary static folders alone do not force a new format. For implementation verification, see the [current checklist](TODO.md) and [development log](DEVELOPMENT.md).

## Multiple-cel timing and drawing reuse in 0.68.0

Click a cel in the Timeline to select it. **Shift-click** extends a range across drawing tracks; **Ctrl/Cmd-click** toggles individual cels. Drag from blank drawing-track space to box-select cels, adding with Shift/Ctrl/Cmd. **Ctrl/Cmd+A** in drawing-track space selects all cels. **Escape** cancels the current drag/marquee; otherwise it clears the selection. Selecting a cel opens the Drawing properties tab.

Drag a selected cel's body to move the whole selection while preserving its gaps and relative track timing. **Alt+Left/Right** nudges it one frame. The preview does not edit the project; release commits once, and Escape restores the original state. A collision with an unselected cel, a locked track/drawing folder or a result outside the shot rejects the entire edit with a reason. A single selected cel retains its edge handles for trimming a hold. Multi-selection uses body movement; it does not trim every edge at once.

The context menu and Drawing properties expose these commands:

| Action | Behavior |
| --- | --- |
| Start | With several cels selected, move all by changing their earliest start. |
| Hold / On 1, 2, 3 | Apply a common hold and arrange selected cels consecutively, starting at the earliest selected cel **on each track**. Gaps between selected cels close; unselected cels stay put and collisions reject the edit. Mixed durations are identified in the context label. |
| Copy / Paste | Ctrl/Cmd+C copies exposure timing. Ctrl/Cmd+V pastes at the playhead into the original tracks in the same shot, preserving relative gaps and offsets. The new exposures reference the existing drawings. |
| Repeat | Append one linked repeat after the end of the entire selected range, preserving gaps and alignment across tracks. The new cels become selected. |
| Reverse | Mirror selected intervals within their span on each track; drawing durations travel with them. |
| Remove | Delete selected exposures while retaining their artwork in the drawing library. |

Each batch creates one Undo entry. Ordinary cel edits do not retime property keys, camera keys, audio or a Symbol's internal clock. Those remain in shot time; use **Ripple time** for whole-shot timing changes. Keyframe and cel clipboards are separate. A project reload clears the internal cel clipboard; pasting never silently replaces an occupied interval.

Use the **Reuse existing drawing** folder icon in the Timeline footer, or its track context-menu entry. The picker lists the current track's drawings with thumbnails, names, compact D001-style labels and usage counts, including unused drawings. Search by name, label or “Unused”; select a drawing, choose Hold and click **Reuse** to expose it at the playhead. Choose an empty interval—overlap is rejected. Cancel changes nothing. Thumbnails prepare in the background for visible entries, without repainting source artwork.

Reused exposures share editable artwork: changing the drawing changes every use. Use the existing **Duplicate drawing** command for independent artwork instead. Library labels reflect list order and may renumber when an unused drawing is deleted; saved links use the drawing's identity, not that label. Reuse is within the original track and shot; project Symbols remain the cross-shot reuse workflow.

The picker's trash action deletes only an **unused** drawing, with Undo and lock protection. Removing exposures, including through whole-shot Delete time, keeps unused drawings in the project so they survive Save/reopen. Explicit unused-drawing deletion removes its layers and attached property keys. Whole-shot ripple still retimes/deletes property keys according to the chosen time range, including keys on retained drawings. Existing project formats already support shared and unused drawings, so no storage-format change is needed.

## Timed Symbol poses and manual lip-sync in 0.71.0

Select a Symbol clip and open **Frame Properties → Symbol → Poses**. Move the playhead inside the clip, click the timed-pose icon and choose a source drawing from the visual picker. **Apply** creates or changes a pose key at that parent frame; Cancel changes nothing. The choice holds until the next key, or the clip's end. Before the first key, the instance holds **First frame**. This works for mouth shapes, expressions and other reusable drawings; no interpolation or automatic speech analysis is performed.

The clip's **Poses** lane shows diamonds and held intervals. Double-click or Enter opens the picker. Select keys with Shift/Ctrl/Cmd or a marquee; drag, nudge, copy/paste or delete them with the existing timeline commands. **Pose** changes the source frame; **Key frame** retimes an exact key, with Previous/Next and Delete beside them. Key operations stay separate from layer/path/camera animation and cues. Each completed edit or batch is one atomic Undo operation; occupied destinations, locks, stale sources and invalid ranges reject the edit without partial changes.

Pose keys follow the unscaled clip clock `parent frame - Start + Slip`. Speed is disabled and ignored in Poses, while its saved playback value is retained. Moving the clip carries the pose schedule; trimming preserves surviving poses and retains hidden keys. Slip moves the schedule underneath the fixed clip. Changing First frame in Poses preserves Slip and every key. Previous/Next and Properties can inspect an existing hidden key whose parent frame remains inside the shot; new pose choices require a frame inside the clip. Deliberate key moves/pastes can keep keys outside the clip without making hidden artwork visible.

Each instance has its own keys, including nested instances, while source artwork stays shared. Switching playback modes retains the keys. Source editing cannot shorten a definition past a referenced pose. Whole-shot ripple retimes surviving held values and adds boundary keys where needed, while rejecting unrepresentable visibility splits atomically. Playback at every preview ratio, exact pause and exports evaluate the same source poses.

Poses mode or retained keys require **format 34**, unless nested animation tracks require format 35; projects without either feature keep their applicable earlier format. Dialogue cues and audible scrubbing are timing references for manual pose placement. Automatic lip-sync, phoneme recognition and subtitle import/export remain future work. The native Symbol-editor focus repair is also included in 0.71; see [Symbols](symbols.md#071-verification-status) for that release's verification and [pose timing](symbols.md#pose-timing-and-rendering) for limits and exact trim/slip behavior.

## Markers and dialogue cues in 0.70.0

Use the Timeline footer’s pin button to add a **Marker**, **Key pose**, **Breakdown**, **In-between** or **Dialogue** cue at the playhead. With the timeline grid focused, **M** adds a marker. These are shot timing notes; they do not create drawings, animation keys or mouth poses. New markers last one frame; a new dialogue range starts at one second, clipped to the remaining shot.

Cues appear in Markers and Dialogue rows above the artwork tracks. Overlapping ranges receive separate rows. Click a cue to select it and jump to its start; double-click to open **Frame Properties → Cues**. Drag a cue to move it, drag a range edge to trim, or use **Alt + Left/Right** for a one-frame move. Gestures preview until release; Escape cancels. Delete removes only the selected cue. Fit Selection includes its full range. Empty shots retain their previous timeline layout.

The Cues tab provides a searchable shot list, previous/next navigation, kind tabs, name, color, Start and Hold. Dialogue adds a speaker and multiline text; other types have multiline notes. Search matches name, kind, speaker and text. Frames display from 1. Text drafts survive ordinary playhead refreshes and resolve before timeline transitions and Save; Escape discards an unfinished edit. Each committed edit has one Undo step. Cue selection stays separate from cels, keys, audio and Symbol clips.

Cues require project format 33, or a newer format when another feature needs it. Duplicating a shot makes independent cues. Whole-shot Insert time extends a cue when the insertion is inside its range; insertion at its start moves it. Delete time shortens or removes affected cues. Move time can split a cue into separate ranges with the same text, and adjacent pieces rejoin. Invalid ranges or limits reject the entire edit. Cues stay outside Symbol definitions, rendered artwork and exports.

Limits: 1,024 cues per shot and 16,384 per project; names and speakers up to 100 characters, notes/dialogue up to 4,096. The 0.70 cue milestone does not generate poses; manual timed Symbol poses are available in 0.71 above. Automatic lip-sync and subtitle import/export remain future work. Historical verification for 0.70 is recorded in the development log.

## Symbol clips and visual frame picking in 0.69.0

Symbol instances have individual timeline rows showing their actual Start and Hold, including instances inside folders and animation drawings. Solid bar portions intersect the drawing's exposures; dashed portions retain editable timing outside them. Selecting a clip opens **Frame Properties → Symbol**, with Start/Hold/First frame/Speed/Slip and Loop/Once/Still/Ping-pong controls; 0.71 adds Poses. The existing Symbols library links to this inspector and the frame picker.

Drag a clip body to move it; drag an edge to trim. Trimming the start retains the internal frames at surviving parent frames. **Alt-drag** slips the internal animation without moving the clip. **Alt+Left/Right** moves one parent frame, and **Alt+Shift+Left/Right** slips one parent frame. Each completed drag adds one Undo entry; Escape cancels its preview. Finite clips remain inside the shot. Full, position and ancestor locks apply.

**Delete/Backspace**, the remove button and **Clear clip exposure** set Hold to zero while retaining the layer, source link and motion channels. Restore its exposure through Hold or **Continue through the shot end**. Changing Start numerically preserves Hold and Slip. Older through-end clips retain their original behavior until edited; moving one materializes its visible length as a finite Hold. Selecting an unexposed drawing-owned instance does not create exposures or activate hidden artwork.

Use the image icon to choose an internal frame from paged thumbnails or exact entry. **Apply** sets First frame, resets Slip in ordinary playback and optionally uses **Hold selected frame** for Still playback. In Poses, changing First frame preserves Slip and pose keys. Clip timing and Speed stay unchanged; Cancel leaves everything unchanged. This image picker chooses the whole clip's starting frame or fallback. The separate timed-pose action in 0.71 creates pose keys. Automatic lip-sync and batch clip edits remain future work.

Finite/empty clip timing or nonzero Slip requires at least project format 32; original Symbol settings can retain format 31 when no newer feature requires more. Historical 0.69 verification passed all 82 affected regressions, four native Symbol/Timeline suites (60 checks) and six Retina suites (84 checks). Broader native workspace/editor focus rechecks were inconclusive then; the drawing-workspace check passed in 0.70 and native Symbol-editor focus is repaired in 0.71. See [Symbols](symbols.md) for current workflow, limits and verification.

## Animate an existing layer

1. Draw on a raster or vector layer, then select that layer in Layers. An existing folder or text layer can also receive transform/opacity keys. **Timeline → + → Keyframes** creates a normal folder and drawing layer and opens the Animation tab; no drawing-type conversion is required.
2. Open **Frame Properties → Animation → Transform**. Enable the diamond beside Position X/Y, Scale X/Y, Rotation or Opacity. Enabling a channel creates a baseline key at frame 1 and a key at the playhead if it is later.
3. Move the playhead. Change an enabled property's slider or exact value: it creates or updates that frame's key and previews the transition. Other properties remain unchanged. Position is an offset from source artwork, scale is a percentage, and animated opacity multiplies the layer's ordinary opacity.
4. Use the panel's move icon for animated canvas handles. Drag inside the bounds to move, a corner to scale, or the rotation handle to rotate. Shift constrains a scaling drag to equal proportions. These handles create the required channels automatically. Each drag is one Undo action; Escape cancels an unfinished drag.
5. Select a property row to inspect its keys. Previous/next key buttons navigate that channel; the diamond adds a key at its current evaluated value. Delete removes the current key. The **Key frame** control retimes it; an occupied destination is rejected.

The selected layer's enabled channels appear as diamond lanes beneath its track in Timeline. Choose another layer in Layers to see its channels, keeping the timeline compact.

- Click a key to visit and select it. Shift-click extends a selection across frames and lanes; Ctrl/Cmd-click toggles individual keys. Drag empty key-lane space to box-select, with Shift/Ctrl/Cmd to add. Ctrl/Cmd+A selects all keys in the displayed layer's lanes.
- Drag any selected key to move the selection together. Alt+Left/Right nudges selected keys by one frame. Moving into an unselected key is rejected; keys can occupy slots vacated by other selected keys.
- Ctrl/Cmd+C copies selected values and interpolation. Move the playhead and Ctrl/Cmd+V pastes, anchoring the earliest copied key there while retaining relative timing. Paste uses the original layer/path channels within the same shot and never replaces existing keys. This internal timeline clipboard is separate from artwork-pixel copying.
- Delete/Backspace deletes selected keys. The context menu exposes Add, Copy, Paste and Delete. Each batch operation creates one Undo entry; Escape cancels an unfinished gesture; when no gesture is active, it clears key selection. Changing the active layer clears key selection.
- Key drags preview without modifying source data. Save/navigation resolves an unfinished drag; Undo cancels it before changing document history. Full and position locks remain enforced.

Choose **Hold**, **Line**, **In**, **Out** or **In/Out** for the selected key's outgoing interpolation. These mean Hold, Linear, Ease in, Ease out and Ease in/out. The first and last values hold outside their key range. Disabling a property's diamond removes that channel's keys and restores its source/default contribution; Undo restores the channel.

An animated folder moves its children together. A drawing's own property keys use shot time, not time relative to its exposure. Selecting or dragging a key outside that drawing's exposed interval keeps the playhead on the current drawing so its lanes remain available. Moving an exposure does not retime its property keys. Full locks protect key edits; position locks protect motion and path keys while opacity remains editable. Notes and 3D reference layers, and folders containing them, are excluded from property animation.

## Camera animation

Open **Frame Properties → Camera**, or select a camera row at the bottom of Timeline. The **2D / 3D** tabs distinguish shot framing from the reference scene's camera. Selecting a camera row preserves the selected drawing layer. Each camera key stores its complete pose; its diamond enables animation for that camera. With animation off, the fields adjust a static camera pose.

For a 2D move, enable the diamond, compose the starting frame, move the playhead, then change **Position X/Y**, **Zoom** or **Rotation**. A pose key is created at that frame automatically. Position is an offset from the artwork center; positive zoom magnifies the artwork. Rotation turns the camera, so the viewed artwork turns in the opposite direction. Angles interpolate continuously, allowing deliberate full turns. Existing Hold/Line/In/Out/In-Out controls set outgoing easing, with endpoint holds outside the key range.

The framing icon opens a layout view with the camera rectangle over the original artwork: drag inside to move, drag a corner to change zoom uniformly, or drag the outer round handle to rotate. Shift gives finer movement/zoom and 15-degree rotation snapping. The **Camera view** icon returns to evaluated shot framing. This is separate from Navigator/Hand zoom, pan and rotation, which never create camera keys. Starting playback returns to Camera view. Selecting a drawing tool ends the framing handles; the chosen camera/layout view remains available for drawing.

Brushes, selections, guides and editing handles use the current camera mapping, so input remains under the pointer in Camera view. The layer **Source artwork** mode removes layer motion/path evaluation but retains shot-camera framing; choose layout view separately to see artwork without the shot camera. Onion skin uses neighboring drawings through the current camera. Camera framing handles and layout navigation never appear in exports. Camera movement does not expand the raster drawing area: brush and fill edits still use the original sheet margins and overscan. Increase the project’s drawable extent before painting a larger background.

The **3D** tab animates the existing shared reference-scene camera: target X/Y/Z, orbit, elevation, distance and the applicable perspective lens or orthographic height. Projection type and lock stay in the existing 3D Tool Properties and are static. Its direct-edit icon selects that scene and opens the existing 3D Camera tool. Both the existing 3D controls and this tab edit the evaluated pose and write keys while animation is enabled. Camera/layer/folder locks still apply.

The 3D camera renders its reference image first; the 2D shot camera then frames that image with the artwork. A 2D pan cannot reveal 3D geometry outside that finite reference image—move the 3D camera to reveal another part of a city. Both cameras share shot time. While the renderer prepares a new camera pose, the reference is temporarily absent rather than displaying an older pose against the current drawing. Reference-only 3D remains excluded from movie and production-sheet exports.

Use Previous/Add/Delete/Next and the one-based Key frame field for exact edits. Timeline camera rows support key selection, dragging, nudging, copy/paste and deletion using the existing key shortcuts; selection stays within the chosen camera. Collisions reject the entire edit. Disabling animation, or deleting its final key, retains the currently displayed pose as a static camera; Undo restores the keys. Numeric and canvas gestures create one Undo action, Escape cancels a draft, and Save/frame/tool changes resolve pending edits.

PNG frames, production sheets, PNG sequences and MP4 use the evaluated shot camera independently of workspace view settings. Saved camera poses/keys use project format 16; existing projects load without changing their framing. Cameras permit up to 16,384 combined pose keys per shot. 2D position is bounded to ±32,768 pixels, zoom to 1–10,000%, and rotation to ±36,000 degrees; large views retain existing render/tile limits.

## Animate vector paths

Select a vector path with **Path Edit**, then enable **Path morph** in the Animation tab. Go to another frame and use Path Edit to move nodes or Bézier handles, transform the path, or change stroke/fill color, width, local width factors or opacity. Compatible edits create a pose key automatically. Original brush resources, captured dabs and randomness stay on the source stroke; keys store only the editable shape and style. Sampled brushes therefore keep their brush character while the path changes.

Morph keys require the same source path, node count, node correspondence and open/closed state. Stroke/fill presence and the original brush source also stay fixed. Adding/deleting nodes, simplifying, erasing, deleting or rasterizing an animated path requires disabling its path animation first. This is interpolation between matching shapes, not automatic matching between unrelated drawings.

Path-object transforms interpolate translation, rotation, scale and shear; rotation takes the shortest angular route. A transition between ordinary and reflected object transforms requires **Hold** on the preceding path key to avoid crossing a collapsed transform. For an intentional spinning motion with multiple turns, animate the layer's Rotation value.

## Edit original artwork

The brush icon in **Frame Properties → Animation** toggles **Source artwork**. It shows drawings at their original positions and original path poses so painting, filling and pixel editing remain predictable. Leave Source to see the animated result; choosing an animated transform or editing a property exits Source automatically. Playback and exports evaluate animation.

Destructive drawing operations on a layer with a nonidentity animated transform require Source mode and show a bottom-bar reason when blocked. An already animated path cannot be changed in Source mode: leave Source to edit its current pose, or disable its path animation to edit the original. This mode is transient and does not change saved artwork or keyframes.

Copying or duplicating selected pixels from a morphing path layer also requires Source mode, so the copied pixels come from the original artwork.

Duplicating ordinary layers/groups copies their channels with fresh layer/object identities. Deleting a layer or its last drawing exposure removes its channels; Undo restores both. Merging directly animated layers and flattening a shot with property keys are blocked to preserve their animation.

## Motion controls

Open **Frame Properties → Animation** and use the three icon modes beneath the layer name: Transform, Motion Path and Pivot. Clicking the active mode again turns its canvas controls off. Selecting Source artwork or starting playback also turns the motion controls off.

**Motion Path** shows the selected layer's pivot trajectory, including motion inherited from animated folders. Squares mark position-key times, and the current frame has a diamond. Frame labels are one-based. Drag a marker to change its X/Y position while retaining its key time and the current playhead. Dragging the current-frame diamond creates or updates position keys there. Missing X/Y channels receive a baseline at frame 1; when only one coordinate has a key at the dragged time, the other receives a matching key. Existing keys retain their outgoing interpolation. Use the timeline to retime keys and the existing interpolation controls to change their easing.

The path is a bounded sampled preview of the evaluated trajectory. Hold transitions appear as jumps rather than connected travel. Exact key markers remain separately editable even when a long path needs fewer curve samples. It is an editor overlay and never appears in exports. Bézier handles, path orientation and the easing graph are available as described below.

**Pivot** shows a draggable crosshair and source-coordinate X/Y controls. The reset icon centers it on the source artwork. The pivot is shared by the entire layer's animation. Moving it offsets the position keys by a constant amount to keep the current frame's artwork in place; other frames with different rotation/scale can change. Review those poses after moving the pivot. The source pixels and vector geometry stay unchanged. An otherwise unanimated layer receives a neutral position channel to retain a custom pivot in the existing format-14 project data.

Each canvas or numeric gesture is one Undo action. Escape cancels an unfinished gesture; Save and tool/frame transitions finish it. Locks and unsupported layer types use the existing blocked-action feedback. Canvas dragging requires an invertible parent transform at the edited key; dragging the pivot also requires nonzero layer scale. Ordinary canvas pan/zoom/rotation remain working-view controls.

## Exposure timing and navigation

- Click or drag the ruler or empty track space to scrub, or use current-frame slider and exact entry. Track labels stay visible while the grid scrolls.
- Previous/next frame and drawing icons provide direct navigation. With the timeline grid focused, Left/Right step frames, Up/Down select rows, Home/End reach shot bounds, and Space toggles playback. Space over the canvas retains its Hand-tool behavior.
- Drag the body of a drawing block to move it, or either edge to trim/extend its hold. The Drawing tab also contains name, exposure Start and Hold. During dragging, overlapping blocks show an invalid preview; release rejects the change. Escape cancels. One timing gesture makes one Undo entry. Overlapping exposures and out-of-shot edits are rejected; later drawings are not shifted.
- Creating a drawing inside a hold splits it at the playhead; the new drawing inherits the remaining hold. At the first frame of an existing exposure, New/Duplicate creates after that exposure when there is room. At a gap it creates a new exposure there. Collisions do not replace other drawings.
- Removing an exposure leaves a blank interval. Delete track is in the track context menu. Both actions support Undo. A static top-level raster/vector layer or compatible folder can be converted through that menu while preserving its artwork as the first drawing.
- Changing frames clears transient pixel selection and follows the same sublayer role where available. A pending destructive transform requires Apply, Discard or Cancel before switching. Blank intervals require a new drawing before painting.

## Playback and output

Play uses project FPS. Shift-drag the ruler to select an inclusive playback range, then drag its endpoints to adjust it. The range toggle switches between that range and the entire shot; range settings provide exact Start/End values and a reset. Loop repeats the enabled range; with Loop off, playback stops at its final frame. Starting outside the range or at its end begins at the first frame. Scrubbing can still visit any shot frame.

Timing follows elapsed time and may skip displayed frames on heavy artwork. Playback and range changes do not edit the document. Pause to draw. The range resets on project reload and does not restrict still or PNG-sequence export.

Onion skin defaults to one previous and one next distinct drawing from the selected exposure track, tinted pink and blue. Its settings support 0–8 per direction, custom colors and opacity. Held/reused copies of the current drawing do not create duplicate ghosts. It is a drawing-exposure aid; it does not add before/after ghosts for property keys. Static backgrounds and editor references do not become ghosts. Onion skin is hidden during playback and excluded from exports.

Each ghost uses its neighboring exposure's frame when evaluating property transforms. Animated exports retain output resolution and filter fractional movement across tile boundaries, independently of the canvas AA display preference. Eyedropper sampling evaluates the same animated artwork.

## Advanced motion controls in 0.28.0

In **Frame Properties → Animation**, select a property and visit a key that has a following key. Drag either circular handle in the easing graph to shape acceleration for that outgoing segment. The horizontal axis is time and the vertical axis is progress between the two key values. Arrow keys move the selected handle precisely; Shift uses a larger step and Tab switches handles. **Curve** identifies custom easing. The five original preset tabs remain available; choosing one replaces the custom curve. Reset removes the custom curve and restores its stored preset. The graph also controls compatible vector-path morph timing.

To bend a trajectory, enable **Motion Path**, select a position-key square on the canvas, then enable **Curve**. Drag the incoming/outgoing circles attached to the selected key. Select the next key to edit the segment's other end. Turning Curve off straightens that outgoing segment. Normal key dragging still moves its position without moving the playhead. For a collapsed handle, Alt-drag its key to edit the outgoing handle or Alt+Shift-drag for incoming.

Curved paths pair Position X/Y key times and easing. The first conversion retains existing positions at the union of X/Y key times and uses the X timing where both axes had different easing. Subsequent retiming, deletion and copying include both axes. Adding a position key splits the current curve and timing before applying the new value, preserving the existing trajectory when the value is unchanged. Disabling either position channel removes both paired channels; Undo restores them.

**Follow path** turns the artwork toward its travel direction. **Rotation offset** controls how it faces relative to that direction. It works with straight and curved position animation, preserves a deterministic heading through stationary stretches, and inherits folder/camera transforms. It does not change source pixels or vector paths.

Curve drags preview live, commit once, cancel with Escape/Undo and resolve on Save/navigation. Curves, handles and orientation are saved with the project and evaluated during playback, PNG sequences and MP4 export. The graph edits one outgoing segment at a time, with bounded progress and ordered time handles; overshoot, a multi-channel value/speed graph and custom camera easing remain future work. Camera keys retain their existing easing presets.

## Audio

Use the Timeline's music icon or **File → Import audio** to import at the current frame. Decoding runs asynchronously with progress and cancellation. A completed import waits for active artwork edits, playback or dialogs to finish before entering the shot; it can still be cancelled while waiting. Each import creates a waveform row; clips can overlap for dialogue, music and effects. Selecting audio opens **Frame Properties → Audio** while preserving the selected artwork layer.

Drag a clip's body to move its starting frame. Drag its left or right edge to trim within the original sound; no source samples are deleted. Frame Properties offers name, Start, Source in, Duration, Volume and Mute. Start uses one-based shot frames; Source in is a zero-based frame offset into the source. Duration displays whole frames, rounding a partial last frame upward. Exact source samples remain intact until a timing field changes. Rename, mute and remove are also available in the clip context menu. Each edit is undoable; Escape/Undo cancels an unfinished draft, and Save/navigation resolves it.

Playback mixes audible clips, including overlaps, against the same clock as the animation. Play, pause, seeking, looping and the inclusive playback range use the shared transport. Clips may extend beyond the shot; playback ends at the shot/range boundary without deleting their sound. The optional speaker control enables audible ruler scrubbing; numeric/programmatic navigation remains silent. Fades, crossfades, gain automation and scrubbing are described under Audio finishing below. Time stretching, recording and beat detection remain outside the implemented scope.

Imported sound is converted to 48 kHz stereo, signed 16-bit PCM, cached for waveform drawing and embedded in the format-15 project. The original file is no longer required. Clips are limited to 128 MiB of decoded audio per source, 256 MiB across distinct sources in a project and 64 clips per shot; the existing 300 MiB total project-container limit still applies. Volume ranges from 0–200%; overlapping sounds are summed and clipped at the output range. Frame-rate changes affect frame-based placement while source audio keeps its original speed. A supported output device uses its audio clock; unavailable, changed or stalled output shows a warning and continues visual playback silently.

The importer uses the bundled [Qt Multimedia decoder](https://doc.qt.io/qt-6.10/qaudiodecoder.html), and playback streams mixed PCM through [QAudioSink](https://doc.qt.io/qt-6.10/qaudiosink.html). Codec availability depends on the installed backend. PNG exports remain silent images. MP4 includes the timeline mix when enabled; mixed audio can also be exported as WAV for a shot or the movie, as described below.

## Image exports

**Export movie frame PNG** and **Export production sheet PNG** use the current animated frame, including property/path evaluation. **Export animation PNG sequence** writes every shot frame into a newly named folder beneath the chosen parent folder. Movie output uses configured dimensions and white/transparent background; it excludes paper margins, headers, Notes, onion skin and reference-only 3D. Export has progress/cancellation and preserves existing files. Completed frames remain if cancelled.

## Video export

Open **File → Export video MP4**. Export starts with **Full shot** selected; **Custom range** uses inclusive, one-based Start/End frames. The range icon explicitly copies the current playback range. Choosing an export range does not move the playhead or change the shot.

Width/Height use the shared slider and exact-entry fields, with an aspect lock. MP4 dimensions are even numbers from 16 to 4096; odd input values round to the nearest supported size. Larger projects default to a fitted size within this limit. Artwork fits the requested rectangle without cropping or stretching, with white padding if its aspect ratio differs. MP4 uses H.264 video and optional AAC audio, with Draft/Standard/High quality. The project FPS is preserved; there is no independent export-speed control.

**Include audio** exports audible clips with their saved placement, source trims, volume and mute, including overlaps and silent gaps. A range export begins sound at its first included frame. Transparency is composited over white for MP4. Production headers, margins, Notes, drawing guides, onion skin and reference-only 3D stay outside movie output.

Export resolves pending editor values, pauses playback and renders a frozen project snapshot in a worker. The window reports progress and supports cancellation; it waits for the encoder to stop before closing. Successful completion publishes the final file atomically. A failed or cancelled export leaves an existing destination unchanged, and an existing file requires one replacement confirmation. Encoder or destination errors stay in the dialog for correction and retry. Resolving an unfinished edit keeps its normal Undo action; exporting adds no history action or project-format change.

The app uses the Qt Multimedia FFmpeg backend already required for audio import. No external FFmpeg command is used by the application. H.264/AAC encoder availability is checked at runtime. macOS is the current verification platform; Windows encoding and packaged runtime deployment still require platform acceptance.

The video frame count and source audio sample range are exact. AAC encodes sound in blocks and can add a short padded tail: the tested backend added 13.3 ms to a five-second mix, within one 1,024-sample block (21.3 ms at 48 kHz). Some players therefore report a slightly longer container duration than the video. Sound placement within the shot is unchanged.

## Scope and limits

The editor works on the selected shot in a movie. It includes raster/vector drawings, static backgrounds, holds, templates, transform/opacity and effect/mask channels, compatible vector path morphs, five interpolation presets plus custom easing, curved motion paths, orientation, audio import/mixing, playback, drawing onion skin, PNG sequences, MP4 export, 2D/3D camera keys, timed Symbol poses, Undo and versioned save/reopen/recovery. Movies with multiple shots or named scenes require at least format 22; later features select newer formats as needed. Original Symbols use 31, finite/empty clip timing or nonzero Slip uses 32, cues use 33, Poses mode or retained pose keys uses 34, nested animation tracks use 35, and animated effect/mask channels or extended mask properties use 36. Earlier supported formats remain readable. Source artwork is stored once; drawing holds do not duplicate pixels, and path keys do not duplicate sampled brush resources. Playhead, playback range, key/audio/cel/Symbol selection, timeline clipboards, playback, onion state, Timeline Solo/collapse state and Source mode are transient.

Frame-by-frame drawings support raster and vector layers, and can contain linked Symbol instances. Vector brush strokes remain editable using Path Edit and correction tools; compatible paths can morph within the drawing’s exposure. Text and 3D layers remain outside drawing containers. Independent text layers can use property animation. Rig/model-transform animation, 3D shape keys, multi-channel speed/value graphs, custom camera curves, automatic matching of unrelated vector topology, continuous cross-shot editor playback and per-track ripple editing remain later work. Ordered movie export and scene/shot management are implemented.

Per shot: up to 16 exposure tracks, 256 drawings, 16,384 exposures and 4,096 stored layer/group nodes; each drawing and the static stack allow 64 nodes. Property animation permits up to 1,024 target layers, 16,384 scalar/path keys combined and 262,144 stored path-key nodes per shot, within the layer limits. Existing project bounds still apply: 1,024 stored raster tiles, conservative 64 MiB history budgeting, 50 history commands, bounded resources and background snapshot saving/recovery. This remains a short-shot workflow.


## Effects on animated artwork

**Panels → Effects** edits the selected artwork layer or folder. Persistent masks, blur, shadow, glow and color correction are evaluated with the drawing exposure, layer/folder keys and camera for each frame. An adjustment layer corrects lower siblings in its folder. Version 0.73 adds numeric effect channels and independent mask placement, opacity, feather and held enable/inversion channels through the workflow above. Shadow/glow colors and the painted mask image remain shared static source data. PNG sequences and MP4 use the same evaluated compositor. See [Effects and masks](layers.md#effects-and-masks-in-0270) for controls, processing order and limits.


## Timeline improvements in 0.29.0

### Ripple time

Open the **Ripple time** icon (split) on the timeline’s lower action row. Choose **Insert**, **Delete** or **Move**, then set Start frame and Frames. An enabled playback range supplies the initial interval; otherwise it starts at the playhead. Controls show one-based frame numbers. Apply makes one undoable edit across the entire shot; Cancel leaves it unchanged. Ordinary dragging, trimming and exposure deletion keep their existing behavior.

- **Insert** creates blank drawing/silent audio time before Start, splitting a hold or audio clip when the cut crosses it. Later content shifts right and the shot becomes longer. Insert at the boundary after the last frame appends time.
- **Delete** removes the specified interval and closes the gap. Keys inside the interval are removed; later keys shift left. Drawings remain in the reuse library even if their last exposure is deleted (0.68). At least one shot frame must remain.
- **Move** cuts the interval and inserts it before the destination, using the original timeline’s frame numbers. The boundary after the final frame moves it to the end. Destinations inside the moved range are rejected; its own start/end boundaries are no-ops.

Drawing exposures, absolute-time layer/path keys, 2D/3D camera keys and audio all follow the same operation. Static artwork remains visible throughout. **Symbols in 0.66:** instance starts shift with their surviving exposed frames and keys. Shared/nested definitions are unchanged. A cut requiring different clock offsets within one ordinary animated instance, a negative start, or an unrepresentable visibility boundary is rejected atomically with a Symbol-specific explanation. Still-frame instances support interior cuts. Moving an exposed animated instance is supported when one start shift preserves all surviving internal frames; reused drawings with incompatible shifts are rejected. Deleting all of a standalone instance's visible tail leaves it hidden at shot end. This is safe timing integration, not arbitrary Symbol time remapping. Existing easing and spatial handles travel with their keys; interpolation is evaluated between the resulting keys. For continuous property/path/camera animation, ripple does not generate boundary keys or freeze the in-between pose when a cut crosses an interpolated segment. If all position keys are deleted, Follow path resets; if all camera keys disappear, the saved base camera is used.

Affected full/position-locked layers or folders reject the edit with a readable message. Split drawings reuse the same editable artwork rather than copying it. Audio splits retain the source asset and use sample-accurate trims at timeline boundaries; at fractional FPS, a relocated piece can lose one rounding sample to stay inside its interval. Only audio within the original shot is rearranged; source tails beyond the original shot end stay excluded. Undo restores the original timing, resources and trims. Existing exposure/audio/key limits also apply to split results.

**Finite Symbol clips in 0.69:** ripple includes their Hold and Slip. Fully removed timing retains an empty instance. A result requiring one instance to split into separate visible intervals or use incompatible internal clocks is rejected before any change. Shared Symbol definitions are not retimed by a parent-shot operation.

**Timed Symbol poses in 0.71:** surviving segments retain their held source frame, with boundary pose keys added when a cut would otherwise change the held value. Hidden keys are retained or retimed with their surviving segments. This allows Poses to preserve different held choices without needing one continuous playback phase. Unrepresentable visibility splits and key limits still reject the entire operation. Ripple leaves unused drawings available for reuse and does not retime their inactive Symbol clocks.

### Drawing templates and vector cels

In **Timeline → + → Frame by frame**, choose a built-in or saved template. Edit its top-to-bottom layer list: rename layers, choose Raster/Vector, add/remove and reorder. New blank drawings use this recipe; duplicate drawings copy the current content and editable sources. An existing compatible raster/vector layer or folder can also be converted to a frame track.

Enter a preset name and click Save to reuse a recipe across projects. Saving an existing name updates that recipe. Delete removes the saved recipe while keeping the current draft and existing drawings. Templates contain layer names, order and types, without artwork or brush resources. The global `drawing-templates.json` library sits beside workspace preferences; malformed, unsupported or externally modified libraries are preserved and made read-only. Up to 128 presets are supported, each with at most 63 drawable layers plus its drawing folder.

Vector cels support existing brushes and path editing. Save/reopen and duplication preserve sampled brush sources, pressure, nodes and supported path animation. A recipe’s role mapping is a hint: rasterizing one existing vector cel layer does not change future blank drawings’ recipe.

### Onion skin settings

The **Onion skin settings** icon opens separate Previous/Next counts and color swatches plus shared opacity. Counts are 0–8 distinct drawings per direction, skipping held/reused copies of the current drawing and repeated neighbors. Nearest drawings are chosen across gaps; each ghost uses its own frame’s layer/path pose through the current camera. The main onion button enables the overlay; it is suppressed during playback and Source artwork editing.

Changes preview live when the onion overlay is enabled. Reset previews the defaults (one drawing each way, pink/cyan, 25% opacity). Cancel restores the opening settings; OK remembers them across restarts. These preferences never change project history, artwork or exported frames.


## Shots and movie exports in 0.32.0

Open **Panels → Shots**. Click + for a blank shot using the current sheet settings and duration, or Duplicate to retain its drawings, keyframes, references, guides, audio and cameras. Double-click Number or Shot to rename. Drag one row or use the up/down icons to change movie order. Delete supports artwork Undo; the final shot cannot be deleted.

Select a row to work on that shot. Pending transforms offer Apply / Discard / Cancel; canceling keeps the old shot and preview. Layer selection and playhead position are remembered per shot during the session. The drawing tools, Layers, references, cameras, timeline and project settings follow the active shot. Undo/Redo reveals the shot it changes. Selection boundaries and transient tool drags do not carry into another shot.

Use the editable **Scene** field to assign selected shots to a named scene; the filter helps browse them. The row order is the movie order, including when scene names repeat. Scene assignment does not silently rearrange cuts. Select several rows and use Export to queue them in movie order.

**File → Export video MP4** has Current shot, Movie and Shot queue choices. Movie export joins shots with hard cuts at the project's frame rate; each cut uses that shot's local animation time and audio. A movie range uses global frame numbers. In Shot queue, select an entry to set its own range, dimensions, quality and audio. Files are named with a sequence position, cut number and shot name. Existing files are skipped unless replacement is explicitly approved; cancellation or failure preserves already completed files and leaves remaining jobs untouched. PNG and production-sheet exports continue to use the current shot.

Movie playback remains shot-based in the editor; whole-movie assembly is reviewed through movie export. Cross-shot transitions and an editorial track spanning shots are later work. Project-wide resource limits still apply across all shots.

## Audio finishing in 0.33.0

Select an audio clip and open **Frame Properties → Audio**. Volume remains directly available. **Clip** holds placement and source trim; **Mix** shows a waveform with editable fade handles and volume-key diamonds. Drag a fade handle or use Fade in/Fade out for precise frame lengths, including fractional frames. Choose Linear or Equal-power fades. One drag is one Undo step; Escape cancels its draft.

Enable the volume diamond to animate gain. Add a key at the playhead, double-click the graph, or drag an existing key. The Volume control edits the selected key or creates a key at the playhead while automation is enabled. Arrow keys move a selected key or change its gain; Delete removes it. Removing the final key disables automation and retains that key's gain as static volume. Disabling automation keeps its keys for later reuse. Volume is 0–200%, with linear interpolation and held values outside the first/last key.

Crossfade lists clips with an existing tail/head overlap. Choose the partner and curve; the outgoing fade-out and incoming fade-in cover that overlap without moving either clip. Opposite-end fades and volume keys remain intact. Nested clips and non-overlapping clips are not offered. Crossfades are within a shot; movie cuts remain hard cuts.

Trim and ripple operations retain fade/key positions in source-sample coordinates. Editing a fade explicitly anchors that fade to its current visible edge. Source audio files and embedded PCM remain unchanged. The live player, MP4 export and WAV export use the same gain/fade mixer.

The Timeline speaker icon enables **audible playhead scrubbing**, remembered in the workspace. Dragging the ruler previews short 80 ms grains, with 2 ms edge ramps and coalesced seek requests. Release, Escape, hiding the timeline, tool/shot changes and playback stop the scrub output immediately. Numeric frame entry and ordinary programmatic navigation stay silent. Device support is checked when scrubbing starts; unavailable output reports an explanation.

**File → Export mixed audio WAV** exports the current shot or whole movie, using Full or Custom range. The playback-range icon copies the active shot's rehearsal range. WAV is 48 kHz stereo, signed 16-bit PCM; fades, volume keys, trims, mute and overlaps are included. Export runs in the background with progress/cancellation and atomic publication; cancellation preserves existing destinations. The standard RIFF size limit is checked before writing. Audio finishing uses optional project format 23; older audio clips retain their existing format unless newer features require an upgrade.


## Timeline navigation and compact properties in 0.66.0

The magnifier row below Timeline provides zoom as a slider and precise percentage (100 means the original 20 pixels per frame). Ctrl/Cmd + wheel zooms around the pointer; changing the slider preserves the visible playhead position, or the viewport centre when the playhead is offscreen. Fit Shot displays the entire shot and remains fitted through resizing. Fit Selection uses selected property/path/camera keys first, then the selected audio clip, enabled playback range, current exposure or current frame. It changes neither selection nor timing and creates no Undo entry. Zoom is local workspace state, not saved project data.

At low zoom, ruler/grid intervals become coarser and keyframes use their painted hit targets. Very short cels/audio clips drag to move; zoom in or use Frame Properties for precise trimming. Navigation is ignored during an unfinished timeline drag/scrub. The playhead remains readable when frames are narrower than a pixel.

Frame Properties uses compact Drawing / Animation / Audio / Camera tabs, showing names when they fit and icons with named help at narrow widths. Channel diamonds and clickable labels remain directly available beside sliders; rows wrap for narrow docks or larger fonts. Precise entry, cancellation, one-gesture Undo and canvas tools retain their existing behavior.

### Symbol registration

Creating a Symbol's first motion channel samples the tight artwork bounds at its current internal frame, or its first playback frame when before its start. Nested content and transformed descendants of a selected group contribute to those bounds. The saved pivot stays fixed afterwards; changing the internal drawing does not move it automatically. Existing saved/custom pivots are preserved. Reset Pivot resamples the bounds while preserving the current pose, and can be undone.

### Effects playback

Canvas and Navigator each permit one running effects frame and one replaceable newest waiting frame during playback. Completed frames publish in request order, even across loops. This prevents continuously cancelled work from starving the canvas; slow compositions can still skip preview frames or trail the playhead. Pausing, changing view or editing invalidates old work and requests the exact current state. Source pixels, settings-preview coalescing and export quality are unchanged. Version 0.67 adds the playback quality choices below; no cached playback range is introduced.

## Playback preview quality in 0.67.0

Use the **1/1** button in the Timeline footer to choose **Full, Half (1/2), Quarter (1/4), or Eighth (1/8)** resolution. The ratio reduces artwork dimensions in each direction during playback. Quarter is a useful starting point for heavy effects; Eighth trades more detail for faster motion previews. The application remembers the choice across sessions. It is a view preference, independent of the project, timing, selection and Undo.

Canvas and Navigator use the same requested quality. During playback, the existing status row shows the preview ratio. Pausing requests the full-quality selected frame in the background; the last completed preview stays visible while it finishes. Drawing, PNG/movie exports and saved source artwork retain full quality. The paper, production header, camera registration and workspace view keep their original geometry at every ratio.

Reduced previews support raster/vector/text drawings, frame exposures, groups, masks, effects, animated transforms, compatible vector morphs and nested Symbols. A composition containing a 3D reference, an unfinished pixel transform, onion overlays, an unavailable Symbol link or a transform too small to reduce safely uses full-quality rendering with a reason in the status row. Unsupported content is not silently omitted. These fallbacks and complex scenes can still play slowly.

Prepared reduced sources are reused through a bounded worker cache (64 MiB per worker, including retained source data). Oversized scenes still render at the chosen scale but do not retain that preparation. This is not a pre-rendered frame cache.

On the generated Retina 4K blur/glow scene with an animated Symbol and visible Navigator, native macOS measured approximately **1 / 4.7 / 17.7 / 24 displayed FPS** for Full / Half / Quarter / Eighth at a requested 24 FPS. Paused full-quality rendering took about 1.16 seconds for that scene and matched the exact reference in both views. These measurements describe that fixture; production-shot and physical Huion acceptance remain separate checks.


## Scene and Movie review in 0.75.0

Use Timeline’s **Shot / Scene / Movie** tabs. Shot retains the drawing/exposure/keyframe editor. Scene reviews every shot with the active editing shot’s scene label, in movie order. Movie reviews every shot in the Shots panel’s order. Scene frames count within that scene; Movie frames count across the whole movie. UI frame numbers start at 1. The readout also shows elapsed time, shot number and local shot frame.

- Click a thumbnail to seek to that shot’s beginning. The global slider and exact-entry field seek anywhere in the selected scope. Previous/Next Shot navigate cuts; Previous returns to the current shot’s beginning before going to the earlier shot.
- Play/Pause and Loop use one continuous sound clock across hard cuts. Each shot’s existing audio retains source trim, overlaps, mute, gain, fades and volume keys, and stops at its cut. Starting partway through a shot or looping does not accumulate fractional-FPS sample-rounding drift. Audio-device failure uses the existing silent clock fallback and reports a warning.
- Double-click a thumbnail or choose **Edit shot** to return to its local drawing timeline. Choosing Shot or pressing Escape returns to the original editing shot/frame. Space plays/pauses, arrows step global frames, Home/End navigate the scope and Enter in the preview/strip edits the reviewed shot. Pen taps focus the preview/strip without painting.
- Entering review resolves pending edits through the existing drawing transition guard. Playback and seeking themselves do not select document shots, change artwork or add Undo steps. Returning to editing, changing tools, modifying/loading a project or changing the document frame invalidates the review snapshot. Hiding the canvas stops sound/playback. Switching/restoring workspaces or entering Drawing Mode exits review and returns focus to the original drawing.

The central canvas shows only the final artwork frame: no production header, margins, Notes, light-table pins, guides or reference-only 3D. Shot-local drawings, Symbols/poses, camera, animated effects and masks use the existing compositor. Thumbnail generation is lazy and bounded (128 renderer entries, 64 strip entries, up to 32 queued requests); current/next cuts receive priority. The review renderer allows one running and one replaceable pending frame. A correct-shot thumbnail can appear briefly while a costly frame prepares; stale results from another cut/seek cannot replace the new view.

Full/Half/Quarter/Eighth controls playback source resolution; pausing requests full source quality. The displayed image fits the viewport and is bounded to 4096 physical pixels per side. Artwork, exports and the saved project format are unchanged. Review mode, position, loop and thumbnails are temporary workspace state.

This is shot-audio review with hard cuts. Global music/dialogue tracks spanning cuts, cross-shot transitions, arbitrary Scene/Movie in/out ranges, a full pre-rendered playback cache and automatic speech analysis are not part of this pass. Cold, very short effects-heavy shots can miss display frames even though audio/playhead stay on time; use reduced preview resolution for rehearsal. Physical Huion and production-movie listening/performance remain acceptance checks.
