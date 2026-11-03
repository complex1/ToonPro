# Symbols

Available from **Panels → Symbols** since **0.65.0**. Version **0.69.0** adds timeline clips and visual frame picking; **0.71.0** adds timed poses for manual lip-sync, and **0.72.0** adds Timeline hierarchy controls. A symbol stores reusable artwork and its internal animation in the movie's library. Each instance is a linked layer placed in a shot. Editing the symbol updates every linked instance across the project; moving an instance or changing its playback affects that instance only.

The library belongs to the current project. It can hold static props, reusable character parts, effects, or animated drawings such as a blink or walk cycle.

## Create and reuse artwork

The panel provides direct **New symbol**, **Convert selected layers**, **Add an instance**, and **Edit symbol artwork** buttons, plus a search field. Hover an icon to see its action. The library item tooltip shows the symbol's duration and the number of linked instances, including uses inside other definitions.

1. Select the artwork in Layers, then choose **Convert selected layers to a symbol** and give it a name. The selected stack becomes one linked instance in its original location; its source artwork moves into the library.
2. Select that library item in another shot and choose **Add an instance to this shot**. The same definition is reused without copying its artwork into a second definition.
3. Select an instance in Layers. Use the move icon in Symbols to open the existing transform controls and move, resize, or rotate it. Existing layer transform and opacity animation remains available.
4. Select the library item and click Edit, or double-click its name, to edit the shared source.

**New symbol** instead creates an empty, one-frame definition and opens its editor. It does not automatically place an instance in the shot. Add an instance when the artwork is ready.

### Conversion constraints

- Select an unlocked layer, a folder, or a continuous range of sibling layers in the same folder. Selected folders include their descendants.
- Include an entire animation track to preserve its drawings and exposure timing. Converting only part of an animation drawing is unavailable. Existing symbols can be inserted into an exposed drawing.
- Put clipped layers and their base artwork inside a folder before converting them. A clipping relationship must not be separated by the conversion. Adjustment layers and multiple independently blending siblings also need a suitable enclosing folder.
- Keep production notes and 3D references outside the symbol. Symbol content is 2D artwork.
- Finish active drawing gestures and resolve pending transforms before library operations. Unsupported conversions leave the existing artwork intact and show an explanation.

## Edit the shared source

The modal editor contains a drawing canvas, Tools, and **Properties**, **Layers**, **Colors**, and **Frames** tabs. Its header provides the symbol's frame count, Undo, Redo, Fit, and a timeline visibility toggle. Increase the frame count before adding a longer animation; it cannot be shortened past existing drawings, keys or source frames used by an instance's pose keys, including retained keys and nested instances.

The editor uses a separate draft. **Save changes** applies the edited definition to the movie as one Undo operation. **Cancel** discards the draft and leaves the existing definition unchanged. Creating a new symbol is already a library operation, so cancelling its first editing session leaves the empty library entry; it can be deleted or undone separately.

Brush and tool organization stays in the main workspace. Symbol editing does not replace the movie's shot selection or turn a definition into an extra movie shot. If the original definition changes while its editor is open, applying the stale draft is refused; reopen the editor before continuing.

In 0.71, Save and Cancel restore canvas keyboard focus after the modal window closes. If the artist activates another window while closing, the editor does not take focus back from it.

## Independent playback

Selecting an instance reveals its playback controls. Frame numbers shown in the interface start at **1**. The instance is hidden before **Start**, after its finite Hold, and outside any exposure of its owning drawing. **First frame** and **Slip** determine ordinary playback; Poses uses timed held choices as described below.

| Control | Behavior |
| --- | --- |
| **Loop** | Repeats the symbol's internal timeline. |
| **Once** | Plays to the end, then holds the final frame. |
| **Still** | Holds the chosen **First frame**. Speed and Slip are disabled. |
| **Ping-pong** | Plays forward and backward repeatedly. |
| **Poses** | Holds explicit source frames until the next pose key. Before the first key, holds **First frame**. Speed is disabled; Slip remains active. |
| **Start** | Frame in the containing shot or symbol at which this instance appears. |
| **First frame** | Internal starting frame; in Poses, the fallback before the first key. Changing it resets Slip in ordinary playback, but preserves Slip and existing pose timing in Poses. It must remain within the definition's duration. |
| **Speed** | Playback percentage: 100 is normal speed, 50 is half speed, and 200 is double speed. Supported values are 1–10,000 percent. Disabled and ignored in Poses and Still; the saved value is retained. |
| **Hold** | Number of parent frames occupied by this clip. Zero retains an empty, reusable instance. |
| **Continue through the shot end** | Uses the shot's end instead of a finite Hold; this is the default for older instances. |
| **Slip** | Moves internal animation or pose timing underneath the clip without moving its Start or Hold. Values are parent frames, before Speed is applied in ordinary playback. Disabled in Still. |

Multiple instances of the same source can therefore start at different times, show different held drawings, or run at different speeds. Their internal playback and the motion of the instance layer are separate: a looping walk cycle can move across the shot using that layer's position keys.

## Timeline clips and visual frame picking in 0.69.0

Each instance has a Symbol clip row in Timeline, including instances inside folders and animation drawings. The bar shows its actual Start and Hold, playback mode and First frame. Loop/Ping-pong cycle ticks appear when zoom leaves enough space. Solid portions indicate the owning drawing's exposures; dashed portions remain editable where that drawing is not exposed. Selecting a clip opens **Frame Properties → Symbol**. The Symbols library's clip-properties icon opens the same inspector.

- Drag the body to move a clip, preserving its Hold and playback phase. Drag either edge to trim. Left trimming preserves the internal frames at surviving parent frames; moving an old through-end clip gives it a finite Hold so its visible length stays constant.
- **Alt-drag** slips the internal animation. **Alt+Left/Right** moves a selected clip one frame; **Alt+Shift+Left/Right** slips it one frame. A drag previews timing and commits one Undo operation on release; Escape cancels the preview.
- **Delete/Backspace**, the Timeline remove button and **Clear clip exposure** set Hold to zero. The layer, its transforms and its linked source stay available. Increase Hold or enable through-end in Symbol properties to expose it again.
- **Start**, **Hold**, **First frame**, **Speed** and **Slip** have compact sliders and exact entry. Editing Start changes its anchor while retaining Hold and Slip. Full, position and ancestor locks protect timing. Unsupported edits report a reason without changing the project.
- Selecting a drawing-owned Symbol visits its nearest exposure when needed. An instance in an unused drawing can still be inspected without activating hidden drawing artwork. Its timing does not create a cel exposure automatically.

The image icon in either Symbols surface opens the **visual frame picker**. Browse paged thumbnails or enter an exact source frame. **Apply** sets First frame and resets Slip to zero in ordinary playback, preserving clip placement, Hold and Speed; optionally choose **Hold selected frame** to use Still playback. In Poses, changing First frame preserves Slip and existing pose keys. Browsing and Cancel leave the instance unchanged. Thumbnail preparation runs in the background; an unavailable or changed source prevents applying a stale choice.

This image picker changes the whole instance's starting frame or fallback. Use the separate timed-pose action below for changes during the shot. Multiple-clip batch selection, per-instance time-remapping curves and automatic lip-sync remain future work. Marker/dialogue cues are available from 0.70.

## Timeline organization in 0.72.0

Symbol artwork rows participate in the shared Timeline hierarchy. Their Visibility and Lock controls edit the same saved state as Layers. Header dragging and Move up/down or Move to top level reorder the whole instance layer while preserving Start, Hold, Slip, motion keys and timed pose keys. Moving into a folder adopts that folder's compositing and inherited restrictions; it does not rewrite the shared Symbol artwork.

Folder carets collapse the Timeline children, including nested Symbol clips and their pose lanes. Expansion is workspace state rather than project artwork. Solo temporarily isolates visible Symbol artwork in Canvas and Navigator, retaining required ancestors/clipping bases; saved eye states, audio and exports remain unchanged. **Clear timeline solo** restores the normal preview. Timeline audio, camera and cue rows do not receive artwork hierarchy actions.

Ordinary folders may contain animation tracks, both in shots and inside Symbol definitions. That structure requires project **format 35**. Without nested animation tracks, the existing feature-based formats remain applicable. Registered drawing containers retain their ownership protections, and hierarchy edits cannot move artwork between unrelated animation drawings.

Current 0.72 build and interaction verification is recorded in the [checklist](TODO.md) and [development log](DEVELOPMENT.md); the 0.71 results below are historical.

## Timed poses and manual lip-sync in 0.71.0

1. Create a Symbol containing the mouth or expression drawings on its internal frames, then place an instance in the shot. The same workflow works for hands, eyes and other reusable poses.
2. Select the clip and open **Frame Properties → Symbol → Poses**. Move the parent playhead to a frame inside the clip.
3. Click **Choose a timed pose at the playhead**, choose a thumbnail or exact source frame, then **Apply**. The action can also switch an ordinary instance to Poses. Cancel changes nothing.
4. Move to another parent frame and choose the next pose. The current pose holds through the frame before the next key; after the final key it holds to the clip's end. Before the first key, **First frame** supplies the fallback. There is no interpolation between drawings.

The **Poses** lane below the clip shows key diamonds and held intervals labelled with source frame numbers. Double-click the lane or press Enter on its selection to open the timed picker. Use Shift/Ctrl/Cmd selection, marquee selection, drag or Alt+Left/Right nudges, Copy/Paste and Delete for pose keys. Pasting preserves relative timing on the original instance in the same shot and refuses occupied destinations or a changed source definition. Pose keys remain separate from motion/path/camera keys, cels and cues.

The inspector's **Pose** row changes or creates the held source frame at the playhead; **Key frame** retimes an exact key. Previous/Next visits keys and Delete removes the exact key at the playhead. An existing key outside the trimmed clip can still be reached and edited in Properties if its parent frame remains inside the shot. New pose choices require a frame inside the clip; deliberate key moves/pastes may retain keys outside it. Hidden keys do not expose the drawing or extend the clip automatically. Keys outside the shot remain stored and become accessible again when clip placement or Slip brings them back.

Each completed pose edit, key batch or picker Apply is one atomic Undo operation; invalid collisions, locks, limits and stale contexts leave the project unchanged. Unfinished numeric text survives ordinary refreshes and is resolved before changing timeline context. Instances of the same definition keep independent pose schedules while sharing source artwork. Switching to Loop/Once/Still/Ping-pong retains pose keys for a later return to Poses. Deleting all pose keys leaves Poses using First frame.

### Pose timing and rendering

Pose keys use an **unscaled clip clock**: with zero-based internal frame values, `clock = parent frame - Start + Slip`. The interface displays parent and source frames from 1. Speed neither stretches the keys nor changes their evaluation in Poses; its saved ordinary-playback value is preserved.

- Moving a clip carries its pose schedule with it. Changing Start numerically also moves the schedule while retaining Hold and Slip.
- Left trimming changes Start and Slip together, preserving the pose at surviving parent frames. Right trimming changes only the visible interval. Neither trim deletes hidden pose keys.
- Increasing Slip advances the schedule under the fixed clip: its keys appear earlier in parent time. Decreasing Slip delays it.
- Changing First frame in Poses changes only the fallback; it does not reset Slip, retime keys or repaint source drawings.
- Whole-shot Insert/Delete/Move maps pose timing with surviving time segments and adds held-value boundary keys where needed. Unrepresentable visibility splits, locked targets and key limits reject the whole operation. Shared definition timing is unchanged, and unused drawings remain reusable.

Canvas, reduced playback previews, exact pause, nested Symbols, exports and current-frame Break apart evaluate the same held source frame. Pose edits invalidate prepared previews. Dialogue cues and audible scrubbing provide timing references; neither automatically analyzes speech or generates mouth poses.

## Library management and detaching

The overflow menu provides **Rename symbol**, **Duplicate symbol**, **Delete unused symbol**, **Make selected instance unique**, and **Break apart at current frame**.

**Duplicate symbol** creates another definition without replacing existing instances. **Make selected instance unique** duplicates its definition and links only the selected instance to the new copy. The copy's own artwork and timing can then be edited independently. Any nested symbol references inside that copy still point to their existing shared definitions.

**Break apart at current frame** replaces the instance with a folder containing editable layers from the currently displayed internal frame. Internal exposure timing and motion are frozen at that frame; the former instance's outer motion, effects, mask, and opacity remain on the wrapper folder. Nested instances remain linked and are held at their currently evaluated frame. The original library definition remains available. Remove clipping from the instance before breaking it apart, and move to a frame where it is visible.

A definition can be deleted only when no shot or other definition references it. Deleting or breaking apart an instance does not delete its library source. Library and instance changes participate in the movie's normal Undo/Redo history.

## Content, saving, and limits

Definitions support raster, vector, text, folders, nested symbol instances, masks, effects, drawing exposures, and compatible property/path keyframes. Source artwork and sampled-brush resources are retained; an instance does not become an independent painted copy unless it is broken apart.

- The library supports up to **128 definitions** and **four symbol levels**, including the outermost definition. Circular references are rejected.
- Each definition supports up to **1,000,000 frames** and follows the existing static/animated layer limits.
- Pose keys are limited to **4,096 per instance** and **65,536 per project**, including nested definitions. Stored clip-clock keys must be unique and ordered within **−2,000,000 to +2,000,000**; source poses must remain within the referenced definition's duration. Retained hidden or inactive-mode keys receive the same validation.
- Definition artwork counts toward the same project tile, mask, editable-object, and sampled-brush resource budgets as shot artwork. Adding more linked instances does not duplicate those canonical source resources.
- **Format 35** is used when any animation-track root is nested in an ordinary folder, including tracks inside Symbol definitions. Otherwise **format 34** is used when an instance uses Poses mode or retains pose keys, including nested definitions and keys retained under other playback modes. Projects without these features keep their applicable earlier format: cues use 33, finite/empty Hold or nonzero Slip uses 32, and original Symbol settings use 31. Definitions remain stored once. Legacy instances reopen through the containing shot's end with zero Slip; earlier supported formats remain readable.
- The library is saved inside the project and reused across its shots. A separate global symbol library, symbol-file exchange, and cross-project import are not part of this version.
- Audio, 3D scenes, cameras, production references, drawing guides, and saved selections remain outside definitions. Interactive button states, scripts, events, and Animate/ToonSquid file import are not implemented.

## Workflow references

The shared-source/instance workflow draws on [ToonSquid's Symbol Layer](https://toonsquid.com/handbook/symbols/symbol_layer/) and [Creating a Symbol](https://toonsquid.com/handbook/symbols/create_symbol/), together with [Adobe Animate's symbol overview](https://helpx.adobe.com/animate/desktop/multimedia-and-video/symbols.html). These are workflow references, not claims of file compatibility or complete feature parity.


## Historical verification — 0.65.0, 2 November 2026

- The full application build succeeds. All **50 affected regression suites** pass; the log is `build/symbols-regression.log`.
- After the final icon and narrow-tab fixes, the library, editor and workspace suites pass at both **1× and 2× offscreen scale**, with **20 Qt results at each scale**. These include keyboard entry, canvas focus restoration, drawing/transform events, shared Save/Cancel, effects workers and light-table snapshots.
- The final 2× workspace/editor captures were visually reviewed. Symbol badges load correctly and all four playback modes remain directly accessible in narrow panels. Captures are `build/symbols-final-2x-workspace.png` and `build/symbols-final-2x-editor.png`.
- Native Cocoa verification is complete after unlocking the desktop: **all three UI suites pass, 20 Qt results, zero failures or skips**. The workspace check now shows and activates the real parent window, clicks the actual **Save changes** button, and verifies that both Save and Cancel return focus to the drawing canvas. The strengthened workspace suite also passes at offscreen 1× and 2×. Logs: `build/symbols-native-symbols_panel.log`, `build/symbols-native-symbol_editor.log`, and `build/symbols-native-symbols_workspace.log`; native editor/workspace captures were visually reviewed.
- At that release the app bundle reported **0.65.0**. That follow-up changed test coverage and verification records; no application fix was needed then. Physical Huion input was not tested in that pass. The later native focus repair is described in the 0.71 status below.


## Animation integration in 0.66.0

A new motion channel uses the current Symbol artwork centre as its default pivot, including nested content. Before playback begins, its first internal frame supplies the registration bounds. Once saved, the pivot stays fixed; existing manual pivots are retained. Reset Pivot resamples artwork bounds while preserving the current pose and supports Undo.

Whole-shot ripple now shifts an instance's start with its keys and surviving cel exposures. Shared definitions and their nested timing remain unchanged. Cuts that require splitting/reordering one continuous internal clock, an unsupported visibility boundary or a negative start are rejected before changing the project. Still-frame instances permit interior cuts. Reused drawing exposures must all be representable by the same instance start. See [timeline navigation and timing](animation.md) for details.

In 0.69, ripple also accounts for finite Hold and Slip. Removing all visible timing retains an empty instance. Ordinary animated playback still rejects operations requiring disjoint clips or different internal clocks. In 0.71 Poses can preserve differing held values through boundary keys, while unrepresentable visibility splits remain rejected. Shared source artwork is unchanged.

## Historical 0.69 verification

All **82 affected regression suites passed**. Four native Symbol/Timeline suites passed **60 Qt checks**, and all six offscreen Retina suites passed **84 checks**. Broader native drawing-workspace and Symbol-editor focus checks were inconclusive after losing window activation while Chrome was in front; their Retina equivalents passed. The current focus status is below.


## Historical 0.70 integration

Shot markers/dialogue cues stay outside Symbol definitions, including conversion and isolated editing. All 85 affected regressions passed. Native drawing-workspace focus passed, while the older native Symbol-editor Cancel-focus and subsequent activation assertions failed (5 pass / 2 fail). All seven offscreen Retina Symbol-workspace cases passed. The focus defect and manual timed poses are addressed in 0.71.

## 0.71 verification status

For 0.71, the app/test build and all **90 affected regression suites passed**, including five new pose suites covering timing/storage/ripple, controller/properties, visual picking, timeline gestures and rendering. Eight native Mac suites and the same eight offscreen at 2× each pass **106 Qt checks**, with no failures or skips. Save/Cancel restore canvas focus and closing respects another window's focus. Loaded thumbnails, held-pose labels, ordinary/Poses action visibility and 240px larger-text properties were reviewed. Evidence: `build/animation-071-regression-final.log`, `build/animation-071-{native,retina}-results.json` and `build/animation-071-validation.json`. That release updated the development app to **0.71.0** at `build/ToonPro.app`; its standalone ZIP was unchanged.

Physical Huion feel and Windows execution remain separate acceptance checks. See [the development log](DEVELOPMENT.md) for final verification evidence.


## Animated effects and masks in 0.73.0

Symbol instances and artwork inside definitions can use the Effects and Mask animation channels. Nested playback evaluates those channels at the Symbol's local frame; the same evaluation is used for previews and exports. Breaking apart a Symbol freezes the evaluated effect/mask state along with its current artwork. Animated compositing channels or non-default mask placement/strength/feather/inversion metadata require project format 36, including when only a nested definition uses them. Shadow/glow colors and the painted mask source remain static.
