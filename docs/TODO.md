# Toon Pro development checklist

Updated 4 November 2026. Current implementation: **0.75.0** Scene/Movie playback, shot thumbnails and a global playhead with continuous shot audio. The app builds and all **130 regression suites pass**. Nine native Mac suites and nine Retina suites each pass **140 checks**, with no failures/skips. Results are recorded in [the development log](DEVELOPMENT.md). Earlier release results below are historical.

## Website and illustrated documentation

- [x] Build the responsive product website with original anime artwork, real app captures and 16 detailed guides.
- [x] Verify all 21 routes, search, image previews, keyboard focus and mobile navigation (49 browser checks).
- [x] Add GitHub Pages build, validation and deployment workflow.
- [ ] Enable GitHub Actions as the repository Pages source and verify the public deployment.
- [ ] Connect verified Windows/Mac public candidate downloads when available.

## Distribution candidates — 0.75

Current distribution scope: **Windows and macOS**. Linux release work is deferred by the artist; its earlier checks and build recipes are retained as history.

- [x] Add Release build configuration and capability metadata for macOS arm64/x86_64, Windows x86_64 and Ubuntu 24.04 x86_64.
- [x] Package Qt/FFmpeg dependencies, notices, SHA-256 manifests and isolated runtime verification; provide optional Mac DMG/signing/notarization controls.
- [x] Verify the macOS arm64 candidate's drawing/save/reopen, movie review, audio/video and 3D rendering from a relocated installation.
- [x] Collect and verify matching Mac and Linux SDK dependency sources plus the Linux software-codec sources and build recipe.
- [x] Pass all 247 Mac regression suites and 36 packaging/source-script checks after the save/export fixes.
- [x] Verify the Linux candidate with software H.264/AAC in relocated offscreen and native XCB checks.
- [ ] Complete Windows native CI after save-file and offscreen-font fixes; keep failed packages withheld.
- [ ] Resolve intermittent Intel Mac AAC export crashes and validate on actual Intel hardware.
- [ ] Complete the active Windows/macOS candidates and record final results in [release verification](release-0.75.0.md).
- [ ] Test physical Huion input and real production projects on each supported operating system.
- [ ] Validate clean-machine installation on supported OS baselines, including an actual Intel Mac and Windows compiler-runtime installation.
- [ ] Complete dependency-source coverage/replacement review and choose application distribution terms.
- [ ] Sign/notarize public production packages when developer credentials are available; current artist choice is unsigned test candidates.

## Scene and Movie review — 0.75

- [x] Add Shot/Scene/Movie review modes, lazy shot thumbnails, global frame/time navigation and direct return to editing.
- [x] Keep review navigation independent of drawing selection, current frame, artwork and Undo history.
- [x] Mix existing shot audio continuously through hard cuts, preserving source envelopes and fractional-FPS timing.
- [x] Render bounded background previews with reduced playback resolution, exact pause requests and stale-cut/seek protection.
- [x] Build 0.75.0; pass 130 regression suites plus nine native Mac/nine Retina suites (140 checks each); review compact thumbnail and workspace captures.
- [ ] Validate sustained playback and listening on the artist’s production movie and physical Huion.
- [ ] Consider global audio tracks, transitions and a pre-rendered playback-range cache as separate extensions.

## Project starters, workspaces and Timeline polish — 0.74

- [x] Implement Blank, Illustration, Anime, Storyboard and Animatic starters with real layer/track structures, editable setup and independent workspace choice.
- [x] Preserve legacy sheets; add explicit header/frame-guide visibility and conditional format 37.
- [x] Implement Illustration, Anime, Storyboarding, Animatics and Compact drawing arrangements, plus named custom Save/Update/Reload/Previous/Manage.
- [x] Guard active gestures and unfinished drafts; restore inspector category without changing selection, and roll back invalid saved layouts.
- [x] Compact Timeline controls, add Center playhead/context and correct selection-aware Trash with drawing/position-lock checks.
- [x] Build 0.74.0 and pass 126 affected regression suites plus ten offscreen Retina suites (145 checks); review compact/larger-text captures.
- [x] Complete unlocked native modal-focus verification: Create/Cancel, Settings Apply/Cancel, Save/Manage, return-to-canvas and same-application other-window focus preservation pass. All ten native suites pass 145 checks. Fix Return/keypad Enter activation in the workspace manager; retain inline rename behavior and pass focused normal/native/Retina rechecks.

See [workspaces](workspaces.md) for behavior, exact defaults and research sources.

## Animation review reopened — 2 November 2026

The artist returned to timeline/animation review. [Review findings and proposed order](animation-review-2026-11-02.md) distinguish existing features, reproduced defects and unverified performance risks. The first two stages were implemented in 0.66.0; 0.67.0 completed playback-preview performance. Version 0.68.0 adds multiple-cel timing and drawing reuse; 0.69.0 implements Symbol clips/frame picking. Version 0.70.0 adds marker/dialogue cues; 0.71.0 adds timed Symbol poses. Version 0.72 implements track organization. Version 0.73 implements and verifies the scoped effect/mask channels below. Version 0.75 adds continuous Scene/Movie review. Graph editing and anime timing-sheet views remain later planning choices.

- [x] Fix Symbol default/reset pivots so scale/rotation use the symbol's intended registration point; preserve user pivots and cover nested/group cases.
- [x] Integrate Symbol playback into whole-shot Insert/Delete/Move ripple, with explicit crossing-range semantics or safe rejection.
- [x] Cover effects-playback starvation with deterministic slow-worker tests and allow running frames to complete while coalescing waiting frames; retain exact paused results.
- [x] Add Full/Half/Quarter/Eighth playback previews with a bounded prepared-source cache, shared Canvas/Navigator quality and exact paused/output rendering. The native Retina 4K fixture improves from about 1 displayed FPS at Full to 18 at Quarter and 24 at Eighth. Unsupported 3D compositions retain an explained full-quality fallback.
- [x] Complete the unlocked native Timeline/workspace focus recheck on 3 November: both suites pass (36 Qt results), including unfinished numeric drafts and returned focus. Together with the renderer/Canvas checks, all four native suites pass (91 results); all 56 regressions and four offscreen 2× suites also pass.
- [ ] Validate sustained playback on the artist's production shots and physical Huion; automated and generated-scene results do not establish hardware feel.
- [x] Add timeline zoom and Fit Shot/Selection; compact the drawing/animation/camera/audio property rows.
- [x] Add cross-track cel selection, batch move/copy/paste/delete, common holds, repeat/reverse and a searchable drawing-reuse picker with labels/thumbnails. Retain unused artwork across exposure removal, ripple Delete time and Save/reopen; explicit unused deletion supports Undo. Verified in 63 regression suites and four native/four Retina UI suites (55 Qt results per mode).
- [x] Represent Symbols as timed clips with move/trim/slip, finite/through-end Hold, clear/restore exposure and compact properties. Add a visual First-frame/Still picker preserving clip placement and Speed. Format 32 stores finite/empty timing or nonzero Slip; legacy defaults retain format 31. Verified with the build, regression and focused native/Retina checks below.
- [x] Complete the 0.69 build and 82 regressions, four native Symbol/Timeline suites (60 checks) and six Retina suites (84 checks), including draft preservation, Undo, inactive drawing instances and picker focus.
- [x] Recheck native drawing-workspace focus in 0.70: all 17 checks pass, as do its offscreen Retina checks.
- [x] Resolve the native Symbol-editor focus issue in 0.71: return focus after the modal window is destroyed on Save or Cancel, and preserve another window's focus. The focused native checks pass; the 0.70 failures are historical.
- [x] Add named/color Marker, Key pose, Breakdown, In-between and Dialogue cues, overlapping range rows, direct move/trim, search, navigation, Start/Hold and speaker/text in the Cues inspector. Edits support Undo, format-33 storage and whole-shot ripple; cues stay outside artwork/exports and Symbol definitions.
- [x] Verify 0.70 with the full build and 85 regression suites; four native Cues/Timeline/drawing-workspace suites pass 62 checks and five Retina suites pass 69. Review normal/240px larger-text captures.
- [x] Add Poses playback, visual pose choice at the parent playhead, held pose lanes, previous/next navigation, precise pose/time controls and batch key move/copy/paste/delete. Preserve independent instance keys, nested rendering, existing clip clocks and source artwork; each committed operation is atomic and undoable.
- [x] Retain hidden pose keys on trim, keep Slip active and Speed disabled in Poses, preserve fallback First frame, and save mode/keys only when needed in format 34. Whole-shot ripple retains surviving held poses with boundary keys and rejects unrepresentable visibility or limits without partial edits.
- [x] Pass the five new pose suites and 33 focused native Mac checks. Manual pose choices do not imply automatic lip-sync or phoneme analysis.
- [x] Verify 0.71 with the app/test build and 90 regression suites; eight native and eight Retina UI suites each pass 106 checks. Review loaded thumbnails, held-pose labels and 240px larger-text properties. Native Save/Cancel and other-window focus checks pass.
- [x] Implement Timeline artwork visibility/lock/Solo controls, expandable folders, named folder creation and header drag reordering with clear destinations. Context and keyboard commands provide alternative reorder access; Layers shares the actual visibility, lock and hierarchy state.
- [x] Preserve exposures, property keys and Symbol pose timing across saved hierarchy edits with Undo. Permit animation tracks inside ordinary folders with format 35 only when needed, including Symbol source definitions; reject invalid ancestry, clipping changes, protected drawing-container moves and locked destinations.
- [x] Keep Solo as a Canvas/Navigator preview filter that respects saved visibility and required ancestors/clipping bases; leave exports, audio, project data and history unchanged. Keep Timeline collapse state per shot in the current workspace, resetting on project load.
- [x] Complete the 0.72 app/test build and all 103 affected regression suites. Eight native Mac suites and eight Retina suites each pass 129 checks; compact/deep folders, nested animation tracks, drop destinations and larger-text captures were reviewed. Native window-focus and simulated tablet checks pass; physical Huion and production-shot acceptance remain separate.
- [x] Verify the implemented 0.73 channels: 11 numeric effects plus 5 held master/effect switches; 7 numeric mask properties plus held enable/inversion; retain the original 6 transform/opacity channels.
- [x] Verify direct Effects diamonds and Transform / Effects / Mask inspector tabs, evaluated values, stable drafts, one-gesture Undo, disabled reasons and category selection from Timeline.
- [x] Verify independent mask placement, source-preserving inversion/feather/opacity, shared-image painting, nested Symbols, reduced/full previews and export parity; mask pivot and shadow/glow colors stay static.
- [x] Verify conditional format-36 save/reopen/recovery, older-format compatibility, source preservation and existing key move/copy/paste/ripple behavior.
- [x] Complete the 0.73 app/test build, focused regressions, native focus and simulated tablet checks, and Retina/narrow/larger-font capture review. Physical Huion and production-shot acceptance remain separate.
- [ ] Choose the next animation extension: full value/speed graphs, global audio/preview caching, or an optional X-sheet view/export. Automatic speech analysis and 3D object/rig keys remain later work.

## Latest completed work

- [x] Add a compact Symbols library with create/convert/insert, search, rename, duplicate, unused deletion, make unique and current-frame break-apart.
- [x] Edit shared raster/vector/text artwork and animation in an isolated modal workspace; Save updates linked instances across shots with one Undo, while Cancel retains the previous definition.
- [x] Reuse existing transform/effects controls for independent instances; support Loop, Once with final hold, Still and Ping-pong with start/first-frame/speed controls.
- [x] Persist definitions once in format 31 with nested references, sampled-brush assets, masks, validation and shared project resource limits.
- [x] Verify conversion, editing, nested timing, PNG export, save/reopen, light-table snapshots, effects previews, and existing drawing workflows in 50 affected suites.
- [x] Complete the 0.65 native Symbol Editor window-focus verification: all three Mac UI suites pass (20 Qt results), including the actual Save changes button and Cancel returning focus to the main canvas. The strengthened workspace test also passes at offscreen 1×/2×.
- [ ] Consider global/cross-project symbol libraries and symbol-file exchange separately; this pass is a movie-local library.

- [x] Add Color Models → Recolor drawings with character, source/target palettes and a checked drawing list covering the current shot or all shots (0.64.0).
- [x] Prepare cancellable background previews with per-drawing Before/After comparison, pan/zoom and change counts. Leave source artwork unchanged until Apply.
- [x] Map matching character-role colors while preserving alpha and overscan pixels; reject ambiguous role mappings and stale sources. Apply the selected batch as one Undo, then use ordinary project Save/reopen.
- [x] Complete 0.64.0 verification: all 11 affected regression suites pass; three native Mac suites and the same three at offscreen 2× each pass 24 Qt results. Native captures at scale factors 1 and 2 cover Before/After, narrow/larger text and no-change states. The three-drawing 4K preview benchmark passes with cancellation and a responsive main-thread heartbeat.

- [x] Implement Day plus independent Night / Sunset / Interior color palettes for new characters, with shared swatch roles, per-palette RGBA values, cross-shot reuse and project storage (0.63.0).
- [x] Add compact scrollable lighting tabs and duplicate/rename/remove actions to Color Models; allow older characters to add missing standard palettes without changing existing Day colors. Keep selection per character during the open session; reopen on Day.
- [x] Connect explicit Color Models palette navigation to Cel Paint Check's approved palette, retaining its independent palette chooser. Selecting a swatch sets the foreground; palette edits do not recolor existing pixels.
- [x] Add format 30, bounded role-to-color maps and artwork-history accounting, while preserving older project formats when no variants exist.
- [x] Complete the 0.63.0 variant, Paint Check and workspace checks: all 17 affected regression suites and six final UI follow-up suites pass. Four native Mac suites and four offscreen 2× suites each pass 33 Qt results with no failures/skips. Narrow/custom tabs, larger text, approved-palette selection and reopened colors were visually reviewed. Physical Huion acceptance remains separate.

- [x] Save Light Table snapshots, placement, selected pin, opacity, visibility and solo state inside the project (0.62.0). Embedded copies remain usable when source files or drawings disappear.
- [x] Save unfinished Cel Shading guides, fill-operation order, colors and controls per shot/outline/frame; retain drafts across Save and drawing changes without Apply, and restore the saved shot/frame on reopen (0.62.0).
- [x] Track drawing-aid edits as unsaved project changes while keeping them separate from artwork Undo and exports. Add bounded format-29 metadata and checksummed images; preserve compatible older formats for projects without saved aids.
- [x] Complete the 0.62.0 persistence, transition, dirty-state and reopen checks: 12 affected suites pass (130 Qt results, zero failures, one optional reference-capture skip). Four native Mac suites and four offscreen 2× suites each pass 45 results with no failures/skips. Restored Light Table/shading and narrow larger-text Light Table captures were reviewed; physical Huion acceptance remains separate.

- [x] Add **Cel Paint Check**: background inspection of selected raw raster drawings, character-palette checks, suspect navigation/highlights, original/repair comparison, checked batch repairs and single-step Undo (0.61.0). Edge-connected fills are suspects, not inferred silhouette violations; large holes remain inspection-only.
- [x] Add **Cel Shading**: colored shadow/highlight guides, separately filled regions, Hide/Color finish, full-stack preview, unchanged source outline and separate saved raster output with Undo (0.61.0).
- [x] Add **References → Light Table**: cross-shot drawing/folder and image snapshots, independent canvas move/rotate/scale, opacity, visibility, solo flipping, reset and refresh (0.61.0). Pins are excluded from artwork/history/exports; 0.62 adds project persistence.
- [x] Verify the original 0.61.0 workflows and pending-preview transitions: ten affected regression suites, four offscreen 2× suites and four native Mac suites pass. Native/2× captures reviewed. Physical Huion feel remains an artist acceptance check; automated pen-contact/lost-release tests pass.

- [x] Add Character Color Models in References: shared character identity, named Skin / Hair / Costume / Shadow swatches, foreground selection, RGBA editor, category moves, Undo/Redo and project storage (0.60.0). Save/Close resolve unfinished character names. All 15 affected regression suites and five offscreen 2× suites pass.
- [x] Complete the final Color Models native keyboard-focus verification on the unlocked Mac. All five native suites pass (48 Qt results, zero failures and one optional capture skip), including name drafts, exact swatch selection, Save/Close, Undo and cross-shot reuse. The focus check is closed.

- [x] Add Character Model Sheets as a References tab: project-wide characters with Turnarounds / Expressions / Costumes, atomic batch imports, image naming/category changes, embedded portable images and Undo/Redo (0.59.0).
- [x] Complete the final Model Sheets native keyboard-focus check on the unlocked Mac. All three final native suites pass (28 Qt results; one optional capture skip), alongside the 11 regression suites, three offscreen 2× suites and native/2× captures. The former focus recheck is closed.

- [x] Add Dynamic stamp creation from multiple images, continuous per-tip variation and opt-in source-color HSV dynamics (0.48.0). The specialized recipes are included in 0.49.0.

- [x] Add immediate stamp placement with move/scale/rotate handles, fixed random variation, per-tool mode, raster/vector support and one-step Undo (0.47.0).

- [x] Add Hold to shape with candidate thumbnails, Original/cancel, current-brush rendering and editable vector capture; add a three-point Arc with pen-hover preview (0.46.0). Detailed post-fit handles remain outside this gesture.

- [x] Add Command/Ctrl axis-locked brush strokes and Shift adjustable straight strokes, with mouse/tablet input, pressure, raster erasing, editable vector strokes, cancel and single-step Undo (0.45.1).

- [x] Add direct image import to the Layers footer, with transparency, proportional fitting, immediate positioning and Undo (0.44.2). File-menu import uses the same action; playback pauses before choosing the image.

Work through the remaining features one phase at a time. **The artist moved effects/compositing and advanced motion ahead of multiple-shot management. Video export, camera animation, effects/compositing and advanced motion are complete on macOS.** Complete and verify a usable workflow before moving to the next phase. This checklist tracks upcoming work; the [development log](DEVELOPMENT.md) records shipped behavior and verification.

The earlier compact drawing and 3D reference passes remain implemented: shared rows and five everyday panels (0.36), drawing inspectors and dock navigation (0.37), advanced brush settings and brush/tool management (0.38), References/Effects (0.39), 3D Library and Object/Camera (0.40), and Rig/Weights/Pose (0.41). Animation navigation and compact inspectors followed in 0.66–0.71; track organization follows in 0.72, and effects/mask channels in 0.73, with current verification tracked above.

The current tablet pass (0.41.1) repairs dock-to-canvas pen cursor handoff and interrupted gestures, and removes repeated display work from pen delivery. Automated/native Mac checks pass; physical Huion latency and feel remain an artist acceptance check. New brush collections and pressure-curve changes were not part of this pass.

| UX pass | Scope | Status |
| --- | --- | --- |
| 1 | Shared property rows; Toolbox, Tools, brush quick properties, Layers, Colors | Implemented in 0.36 |
| 2 | Selection/transform, drawing tools, vector, guides, Navigator and dock labels/tabs | Implemented in 0.37 |
| 3 | Advanced brush editor and brush/tool management layouts | Implemented in 0.38 |
| 4 | References and Effects layouts | Implemented in 0.39 |
| 5 | 3D Library and Object/Camera properties | Implemented in 0.40 |
| 6 | Rig, Weights and Pose layouts | Implemented in 0.41 |
| 7 | Customizable Quick access toolbar and Tab drawing mode | Implemented in 0.43; focused native Mac checks pass |
| 8 | Brush Preset size grid with custom sizes | Implemented in 0.44; targeted and native Mac checks pass |
| Animation | Compact Frame Properties, drawing reuse, Symbol clips/poses, cues, track organization and animated effects/masks | Implemented in 0.66–0.73; current effects/mask verification tracked above |

## Current foundation

Drawing tools, editable vector strokes, frame-by-frame raster/vector animation, layer/property keys, compatible vector path morphs, timeline editing, motion paths/pivots, 3D references with native rigging/posing and animated cameras, 2D shot-camera framing, audio preview, persistent masks, nondestructive effects with numeric/switch channels, independent mask-property animation, color adjustment layers, PNG sequences and MP4 export with mixed audio are implemented. Drawing edits work on the selected shot; Scene/Movie review and movie/queued exports span shots. See the [animation workflow](animation.md) and [drawing tools](drawing-tools.md) for current limits.

## Current drawing workspace controls

- [x] Add a compact top Quick access toolbar with **+** pinning for tools, foreground colors and navigation; remember up to 48 pins and their order across sessions (0.43.0).
- [x] Support right-click Move left, Move right and Remove without deleting the underlying tool. Navigation includes Hand, Zoom in/out, Fit sheet and Reset view rotation.
- [x] Toggle fullscreen drawing mode with **Tab** or the toolbar icon, keeping the canvas and Quick access visible; restore the previous docks, floating panels and window state on return.
- [x] Preserve text/dialog/menu Tab navigation, ignore mode changes during pen strokes or unfinished canvas gestures, and retain ordinary canvas shortcuts.
- [x] Pass native Mac toolbar (25 cases) and drawing-mode (9 cases) checks, including pin persistence, drawing/Undo and layout restoration.
- [x] Pass all 159 regression suites in the full 0.43.0 run. Physical Huion and Windows acceptance remain part of phase 10.
- [x] Add **Panels → Brush Preset** with 19 initial sizes, visible circle/number tiles and one-click size application for Brush/Eraser (0.44.0).
- [x] Add custom sizes with the shared slider/exact field; support right-click Edit/Remove and an overflow Reset sizes to defaults action. Remember up to 64 sorted sizes, from 1–256 px at one decimal precision.
- [x] Keep size presets independent of brush definitions and artwork history; preserve all other active brush settings and avoid interrupting active gestures.
- [x] Place the panel below Tools in fresh/reset workspaces while retaining existing saved layouts and Tab drawing-mode behavior.
- [x] Verify Brush Preset controls, persistence, gesture safety, native layout and existing workspace regressions: 12 targeted suites plus 24 native panel cases pass for 0.44.0.
- [x] Profile large-brush strokes at 1× and 2×; reduce detailed cursor redraw and redundant texture/compositing work without changing painted pixels, brush dynamics or delivered samples (0.44.1). All 32 baseline/output hashes match; see [performance results](performance.md#large-brush-stroke-work--0441-29-october-2026) for remaining extreme-texture display costs.
- [x] Pass all 160 regression suites for 0.44.1, plus final cursor checks at 1×/2× and native Mac cursor/tablet/workspace checks. Actual Huion feel remains an artist acceptance check.

## Current brush library expansion

The [catalog](brush-catalog-plan.md) defines 200 designs across 15 families. Versions 0.42–0.49 install all 200 catalog recipes in 15 subject groups, alongside the existing 19 starters (219 total). The catalog files document status; actual recipes and generated artwork are compiled into the app.

- [x] Research official Photoshop, Krita, Clip Studio Paint and Procreate behavior; map existing engine support and dependencies for all proposed entries.
- [x] Add the 112 current-engine defaults, original tip/texture/color artwork, categories/tags and subject groups while preserving edited/deleted tools.
- [x] Defer Tools/Add Tools previews and automatic catalog saving during drawing; cache visible previews and preserve the active workspace.
- [x] Correct filled line patterns, circular joins in continuous brushes and per-stroke-only foliage color from the artist's feedback; upgrade untouched defaults without resetting artist edits (0.42.1).
- [x] Check all 61 authored tip sets for distinct variants, review family sheets and a synthetic background, and verify transparent cloud borders plus raster/vector replay (0.45.0).
- [ ] Review the shipped brushes on real artwork with the artist's Huion, including the 32 pilot recipes; tune from that feedback.
- [ ] Add optional installation/uninstallation of packs and lazy decoded resources if future large assets justify it. Current small compiled resources are shared in memory.
- [x] Add deterministic multi-tip sets, per-tip anchors and primary sizes up to 1,024 px; author the 54 Wave 2 brushes and refine seven grass/foliage defaults (0.45.0). Real-artwork/Huion acceptance remains open.
- [x] Add fitted repeats, connected ribbons, dot/line/diamond/noise/fade tones, explicit perspective surfaces, source-color tint, clustered spray and timestamped speed dynamics (0.49.0).
- [x] Add the final 34 catalog recipes and optimize large colored thumbnails/cursor outlines (0.49.0).
- [x] Repair slow connected-strip joins (0.49.1) and reduce large connected-brush rendering work with pixel-identical bounded bands; profile at 1×/2× (0.49.2).
- [x] Reduce Retina connected-brush cursor rebuilds and redundant background painting; verify fractional movement, repeat-based cursor tips, pressure changes and pixel-identical artwork (0.49.3). Rare full-window capture outliers and physical Huion acceptance remain documented in the performance report.
- [x] Review 21 connected/repeating presets on slow curves and sharp turns; fix double-painted segment edges, open outer joins, mirrored-edge sampling and authored fence/railing/balcony/stone seams (0.49.4). Tight inner-turn compression remains documented.
- [ ] Review the full collection on the artist’s real production artwork and physical Huion tablet; refine recipes from that feedback.

The remaining work extends this collection; the 0.41.1 tablet fixes remain in place.

## Development order

| Order | Phase | Status |
| --- | --- | --- |
| 1 | Video export with audio | Complete on macOS |
| 2 | Camera animation | Complete on macOS |
| 3 | Multiple shots and movie management | Complete on macOS |
| 4 | Advanced motion controls | Complete on macOS |
| 5 | Timeline improvements | Complete on macOS |
| 6 | Audio finishing | Complete on macOS |
| 7 | Effects and compositing | Complete on macOS |
| 8 | 3D animation and reusable rigs | Queued |
| 9 | Drawing workflow refinements | Complete on macOS |
| 10 | Production performance and platform verification | In progress — platform/hardware acceptance remains |

The artist’s feature execution order was **3 → 6 → 9 → 10**. The six drawing/3D reference UX passes above are implemented. Performance and drawing regressions are checked throughout; phase 10 adds broader production and hardware verification. Brush Management, colored stamps, the native starter pack and input-response dynamics are also implemented through 0.31.0.

## 1 Video export with audio

- [x] Add an MP4 export path and verify H.264/AAC encoding with the current macOS Qt runtime. Windows and packaged runtime acceptance remain in phase 10.
- [x] Export evaluated artwork and mixed audio together, honoring clip placement, trims, volume and mute.
- [x] Offer whole-shot or explicit frame-range export, output dimensions and suitable quality settings.
- [x] Preserve project FPS and align sound to the first exported frame, including fractional FPS and nonzero range starts.
- [x] Provide progress, cancellation and useful errors; preserve existing output files if export fails.

**Done when:** a 5–10 second shot combining drawings, property animation and overlapping audio plays correctly in an external video player, with matching duration, frame order and sound timing. Existing PNG exports continue to work.

**Verified in 0.25.0:** all 103 CTest suites pass. A five-second animated shot passes external muted playback, independent frame decoding and decoded-audio timing/level checks. A 300-frame fractional-rate export and eight fractional single-frame/audio combinations pass timing checks. Export UI tests pass on native Cocoa and at Retina scale. AAC may add a short codec-padding tail to the reported container duration; the source sample interval and video frame count remain exact. See the [workflow](animation.md#video-export) for controls and limits.

## 2 Camera animation

- [x] Add a saved 2D shot camera with position, zoom and rotation keys.
- [x] Provide direct camera framing controls and a clear output-frame preview, separate from canvas navigation.
- [x] Animate the existing 3D reference camera on the same shot clock.
- [x] Keep drawing, playback and exports consistent with the evaluated camera while respecting reference-only content.

**Done when:** a saved shot can pan/zoom over 2D artwork, and a moving 3D city reference can guide hand-drawn character frames. Camera edits support Undo and survive reopening.

**Verified in 0.26.0:** nine camera suites cover interpolation, validation/storage, history, camera framing, drawing input, rendering, panels and workspace transitions. Native/Retina controls pass, and a real MP4 preserves independently calculated camera movement and zoom through encoding/decoding. Existing 3D reference cameras follow shot time; reference-only content stays out of exports. All 112 suites pass across the broad run and final recheck. Production-size city scenes and physical pen performance remain in phase 10. See [Camera animation](animation.md#camera-animation) for workflow and limits.

## 3 Multiple shots and movie management

- [x] Add a shot browser with create, duplicate, rename, reorder and delete operations.
- [x] Switch between shots while resolving pending edits and preserving each shot's layers, timing, audio and camera state.
- [x] Assemble ordered shots into scenes and a movie sequence.
- [x] Add queued shot exports and whole-sequence video export using the video export engine.

**Done when:** three distinct shots can be saved, reopened, reordered and exported as one movie with the correct cuts and sound. Existing first-shot projects migrate without losing content.

**Verified in 0.32.0:** all 143 suites pass across the broad run and focused reruns. Three-shot drawing/history/storage tests, decoded movie cuts/audio, queued export cancellation and destination protection pass. Native Cocoa shot-panel interactions pass; normal/narrow panels and export dialogs were visually checked. A retry with the Mac kept awake completed the movie export suite in 4.2 seconds after an interrupted run. Movie preview remains shot-based in the editor.

## 4 Advanced motion controls

- [x] Add a graph editor with editable easing curves and tangent handles.
- [x] Add curved motion-path handles beyond the existing draggable position-key markers.
- [x] Add optional orientation along a motion path.
- [x] Keep curve, canvas and numeric edits synchronized, with Undo and persistence.

**Done when:** an artist can shape acceleration and a curved trajectory, compare the result during playback, and retain it after reopening. Existing Hold/Linear/Ease presets remain usable.

**Verified in 0.28.0:** all 124 CTest suites pass across the full run and focused rerun. Six new suites cover normalized easing, spatial curves, stable orientation, paired timing/clipboard edits, morph timing, legacy formats, history and interactions. Native macOS panel/canvas suites pass at normal and 2× scale; 240-pixel controls were visually checked. An encoded/decoded MP4 verifies custom timing, curved travel and orientation. Custom camera curves, overshoot and multi-channel value/speed graphs remain later work. See [Advanced motion controls](animation.md#advanced-motion-controls-in-0280).

## 5 Timeline improvements

- [x] Add explicit ripple move/insert/delete behavior, defining how drawing exposures, absolute-time property keys and audio shift together.
- [x] Save and reuse custom drawing-layer templates.
- [x] Make onion-skin frame counts, colors and opacity configurable.
- [x] Support vector drawings inside frame-by-frame tracks, preserving editable stroke sources.

**Done when:** a sequence can be retimed, drawn with custom templates and reviewed with configurable onion skin without accidentally overwriting drawings, keys or audio. Vector drawings remain editable after save/reopen.

**Verified in 0.29.0:** all 131 CTest suites pass across the broad run and focused fixture rerun. Seven new suites cover shot-wide ripple timing/locks/history, templates and safe global-library persistence, vector cels/brush-source storage/rendering, onion samples/settings, and workspace interactions. Native macOS and high-DPI checks cover the new dialogs and saved preferences; narrow layouts were visually reviewed. Ripple is an explicit all-track action and retimes existing interpolation without adding cut-boundary keys. See [Timeline improvements](animation.md#timeline-improvements-in-0290).

## 6 Audio finishing

- [x] Add clip fade-in/out and crossfades.
- [x] Add volume keyframes/automation with preview and export parity.
- [x] Add optional audible scrubbing with immediate stop behavior.
- [x] Export a standalone mixed-audio file for the shot or selected range.

**Done when:** dialogue, music and effects can be balanced and faded, with the same result in preview, video export and audio-only export. Source audio remains unchanged.

**Verified in 0.33.0:** audio model/storage, sample-exact live/offline envelope parity, trimming/ripple, crossfade history, volume-key interactions, audible-scrub lifecycle, WAV range/movie export and destination protection pass. Native Retina Mix controls were checked at 240 and 320 logical pixels. Hardware listening and device-switch acceptance remain part of phase 10.

## 7 Effects and compositing

- [x] Add persistent editable layer masks, separate from transient selections and existing clipping controls.
- [x] Implement nondestructive blur, shadow and glow.
- [x] Add color-correction adjustments and adjustment layers.
- [x] Replace the Effects placeholder with working controls that preserve source artwork.
- [x] Integrate effects and masks with animated rendering, Undo, save/reopen and output.

**Done when:** a short shot can combine masks, lighting effects and color correction while source artwork stays editable and exported frames match the preview.

**Verified in 0.27.0:** all 118 CTest suites pass. Six new effects suites cover source-preserving rendering, filter halos, masks, adjustments, storage validation, history, transformed/pressure-aware mask painting, panel drafts and workspace Save/Undo/navigation. An actual encoded/decoded MP4 combines masks, lighting, correction and animated camera framing. Panel, canvas and workspace checks also pass on native macOS and at Retina scale. Effects were static layer settings in that release. Version 0.73 adds effect/mask channels, with its verification tracked above; production-size performance acceptance remains separate. See [Effects and masks](layers.md#effects-and-masks-in-0270).

## 8 3D animation and reusable rigs

- [ ] Keyframe 3D model position, rotation and scale alongside the camera from phase 2.
- [ ] Keyframe rig poses and supported FK/IK controls on the shot timeline.
- [ ] Save and reuse named rig/pose assets in the library.
- [ ] Import supported already-rigged/skinned characters with clear compatibility feedback.

**Done when:** a character can move through several saved poses against an animated background camera, remain synchronized with 2D drawings, and retain its rig/pose data after reopening. Reference-only visibility remains explicit.

## 9 Drawing workflow refinements

- [x] Add saved color palettes with reusable named swatches.
- [x] Expose the existing text engine through a dedicated Text tool and direct artwork editing flow.
- [x] Extend Photoshop brush import beyond sampled tips, starting with verified mappings for supported dynamics and settings.
- [x] Maintain an import compatibility report for unsupported brush settings and rendering differences.

**Done when:** palettes and text are easy to access, and representative brush packs preserve the newly supported settings without changing existing native brushes or overstating Photoshop compatibility.

**Implemented in 0.34.0:** named global palettes, editable named swatches, a standalone Text tool with inline multiline/Unicode editing, and bounded ABR preset mapping with per-brush compatibility notes. Tests cover palette persistence and failure protection, Text history/shortcuts/property handoffs, computed and sampled brush settings, and raster/vector persistence for 30 brushes from the artist's two local packs. Photoshop texture resources, proprietary behavior and complete rendering parity are not claimed. See [brush compatibility](brush-compatibility.md) and [drawing tools](drawing-tools.md).

## 10 Production performance and platform verification

- [x] Profile large layered shots, dense sampled brushes, vector edits, audio and animated 3D camera references under documented workloads.
- [x] Measure memory/cache usage and improve saving/recovery responsiveness; retain existing validated resource limits.
- [x] Repair tablet dock-cursor handoff and missing-release recovery; batch tile discovery and defer precise cursor contours until display (0.41.1).
- [ ] Exercise physical Huion input: pressure, tilt where supported, eraser, shortcuts and stroke latency.
- [ ] Verify Windows builds, rendering, audio, codecs, persistence and high-DPI layouts.
- [x] Prepare a macOS development bundle with runtime dependencies and verify relocated startup, persistence, codecs and 3D rendering.
- [ ] Verify clean-machine installation and the Windows distributable; complete public-release signing/notarization if distributing publicly.

**Measured in 0.35.0:** a four-shot, 96-layer 4K fixture saves asynchronously in about 2.1 seconds, with 0.059 ms maximum GUI submission and 1.034 ms p95 GUI-heartbeat lateness. Dense brush, earlier-vector-edit, selection, 64-clip audio and animated city-camera workloads pass their correctness checks. Full 4K composition reaches 23.87 ms and noisy color-selection construction remains a profiling follow-up; neither is hidden by raising limits. Recovery and explicit saves use immutable snapshots, cancellation and atomic publication. See [measurements and limits](performance.md).

**Mac package verified:** the 0.35.0 arm64 bundle passed all nine smoke checks both offscreen and on native Cocoa, each at actual 2× device scale, after relocation with SDK paths removed. Checks include project reopening, embedded icons, WAV, encoded/decoded H.264/AAC and real 3D rendering. The ZIP has a local ad-hoc signature and recorded SHA-256. Packaging scripts and macOS/Windows CI are implemented; Windows execution, clean-machine installation, physical Huion input and device-audio acceptance remain open. A configured CI job is not evidence of a successful Windows run. Phase 8 remains separately queued.

**Done when:** representative drawing and multi-shot projects meet documented performance targets on the tested hardware, and installation, recovery, playback and export pass the supported-platform checks.

## Completion rules

- Mark a task complete only after its behavior works and has appropriate verification; keep partial work unchecked and note its remaining scope.
- Keep one phase active at a time. Split large phases into small runnable changes and update this checklist as they ship.
- Follow the [UI guidelines](ui-guidelines.md): direct actions, recognizable icons, shared 600 ms tooltips, compact numeric controls and visual editors for complex settings.
- Preserve drawing responsiveness, pending-edit handling, Undo, existing project content and save/reopen compatibility as each feature is added.
- Update README and the development log with actual controls, test evidence and remaining limits. Keep manual hardware/platform checks distinct from automated tests.


## History and reusable workflows — 0.50.1

- [x] Add artwork History with current/saved/redo states and guarded state navigation.
- [x] Add Auto Actions recording/playback for supported layer setup, properties and effects; persist workflows independently of projects.
- [x] Support optional/reordered steps, action management, preflight failure protection and one Undo for successful playback.
- [x] Record/replay selection fills, paint buckets and applied raster move/resize/rotate; preserve target selections and one-step Undo (0.50.1).
- [ ] Extend recording to more drawing commands, parameter editing, action sets and quick-access assignment as requested. Freehand replay and third-party action import are not implemented.

See [History and Auto Actions](history-and-actions.md) for current controls and limitations.


## UX revamp — shared drawing foundation (0.51.0)

- [x] Centralize drawing sizes/spacing and apply them to Toolbox, Tools, Properties, Layers and Colors.
- [x] Add distinct semantic drawing icons and preserve custom catalog identities during default-icon upgrades.
- [x] Compact drawing tabs, property rows and dock headers while retaining precise input and pen targets.
- [x] Verify 23 targeted suites, focused native Mac interactions, narrow/larger-font layouts and explicit 2× captures (0.51.0); evidence is in the development log.
- [x] Add Drawing / Compact drawing workspace presets, adjacent drawing controls and Colors / Refs / Navigator above Layers; preserve existing saved layouts (0.52.0).
- [x] Compact size presets and Quick access, retaining exact size labels, custom pins and pen guards (0.52.0).
- [x] Verify the composition pass with 12 targeted suites, native Mac interactions, and 1×/2× workspace captures (0.52.0).
- [x] Replace the Compact drawing open dock stack with an explicit overlay drawer using the same Tools / Sizes / Properties widgets; preserve canvas viewport and view while opening/closing (0.57.0).
- [x] Route Panels/context-menu entries directly to drawer pages; retain normal docks, saved-layout migration, page/open state, drawing-mode restoration and numeric/gesture guards (0.57.0).
- [x] Verify drawer regressions: canvas invariance, exact-size control, same-widget restoration, numeric drafts, inline rename, advanced brush dialog, narrow/larger text, popup guards and saved state. All 24 affected suites pass across final integration/recheck; 26 offscreen 2× views and representative visual review are recorded in the development log (0.57.0).
- [x] Finish the unlocked native drawer/focus check: all 11 Mac suites pass with 256 Qt results; fix native enlarged-text tab clipping and recheck all four affected workspace suites offscreen (0.57.0).
- [ ] Review the explicit compact drawer on the physical Huion.
- [x] Retain all six retouch modes through pen release, hover-before-release and missed mouse release; preserve Escape cancellation, selection, save/reopen and one-step Undo/Redo (0.57.0).
- [x] Reduce Effects blur/light/color/mask work with exact output preservation; all 39 paired image checks match and 81 renderer results pass (0.57.0).
- [x] Move canvas and Navigator Effects previews to cancellable, coalesced background rendering; show adaptive raster drafts while editing and full-quality results after a pause or release, preserving source pixels and one-step Undo (0.58.0).
- [x] Verify background Effects with 26 affected suites (499 latest Qt results), seven native Mac suites (161 results), fractional/Retina pixel parity and paired 4K responsiveness measurements (0.58.0).
- [ ] Review heavy Effects preview cadence with the artist's physical Huion and production artwork; mixed vector/animation/3D stacks still use full-quality background previews.
- [ ] Consider optional Comfortable density and automatic compact adaptation only after the artist evaluates the explicit workflow; neither is implemented.
- [x] Compact Advanced Brush settings with direct response tabs, active-content sizing, preserved visual controls and collapsed inactive details (0.53.0).
- [x] Revise Brush/Tool Management with separate browsing and detail areas, pinned actions, responsive arrangement and preserved input drafts (0.53.0).
- [x] Compact References with a header caption, Fit beside Zoom and a thumbnail strip shown only for multiple images; preserve manual navigation and zoom drafts (0.54.0).
- [x] Compact Effects with five direct tabs, pinned enable switches, contextual effect/mask details and stable font-aware context/reasons (0.54.0).
- [x] Compact Guides with pinned creation/list/navigation, independently scrolling Selected / All settings and contextual custom-color details; retain all six types and canvas editing (0.55.0).
- [x] Separate Selection / Display settings, pin selection actions outside scrolling content and hide them during transforms; keep labeled Apply / Cancel below perspective/warp settings (0.55.0).
- [x] Compact Vector Properties with pinned modes, correction direction and simplify/reset actions, independent settings scrolling and stable contextual status (0.55.0).
- [x] Complete revamp Pass 3 implementation across advanced brush settings, management, References, Effects, Guides, Selection/Transform and Vector Properties.
- [x] Verify the 0.55.0 panel layouts and editing behavior: 16 targeted suites pass; generated 49 native and 49 explicit 2× captures and reviewed representative layouts.
- [x] Close the native Guide foreground-focus recheck: the unchanged suite passes all 20 results in the 0.56.0 baseline; no production activation workaround is required.
- [x] Implement final workspace polish: safe wheel scrolling over value controls, panel Tab navigation, canvas drawing-mode shortcut, keyboard-reachable Quick access without click-focus theft, and font-aware tool chooser rows (0.56.0).
- [x] Guard layout changes during numeric drafts, layer previews and tool gestures; preserve drafts when an activation is cancelled (0.56.0).
- [x] Implement canvas-to-panel pen cursor handoff and interrupted-contact recovery, including a new tip press over a panel; retain mask strokes across barrel-button release (0.56.0).
- [x] Verify 0.56.0 with 20 unique targeted regression suites, including source-draft commit on accepted tool changes; evidence is in `build/ux-pass6-workspace-tests.log` and `build/ux-pass6-final-tests.log`.
- [x] Compare five paired 2× drawing profiles: all artwork hashes match and input/redraw timings remain comparable (`build/ux-pass6-performance-comparison.json`).
- [x] Capture 16 native and 16 offscreen 2× workspace views and review representative narrow/larger-text layouts. Compact drawing fits 900 × 768; full Drawing retains a minimum width of 1067 native / 1087 offscreen, so the requested 1024 trial expands. Comparable canvas area is unchanged from 0.55.0.
- [x] Complete the unlocked native Mac recheck: all nine suites pass, totaling 181 Qt results including setup/cleanup (`build/ux-pass6-native-verified-results.json`).
- [x] Make Tab navigation include all application controls without changing macOS preferences; verify Tab / Shift+Tab / Space under a restrictive initial system focus policy, preserving rejected pen-activation drafts (0.56.0).
- [x] Recheck all 12 affected offscreen suites after the final native keyboard-policy fix; all pass (`build/ux-pass6-unlocked-focus-regressions.log`).
- [ ] Complete physical Huion workflow acceptance separately from the implemented Pass 4 polish; synthetic tablet tests cannot establish hardware comfort or latency.
- [x] Verify the 0.54.0 References and Effects pass with 12 targeted suites, native Mac panel/workspace checks and narrow/larger-font/2× captures.
- [x] Verify the 0.53.0 advanced brush and management pass with 18 targeted suites, all three panel suites on native macOS, and narrow/larger-font/2× visual checks.
- [ ] Verify physical Huion comfort in the denser controls; automated checks do not replace artist acceptance.
