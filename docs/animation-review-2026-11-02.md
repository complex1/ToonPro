# Timeline and animation review — 2 November 2026

Reviewed the 0.65.0 implementation, current controls, existing tests and workflow documentation. Two Symbol integration issues were reproduced against the compiled core. The findings below record the original review; implementation status is updated separately here.

## Follow-up in 0.75.0 — Scene and Movie review

Continuous review is implemented through Timeline’s Shot/Scene/Movie modes, bounded shot thumbnails, a global playhead, and one synchronized audio stream across cuts. Review leaves the editing document/history unchanged; Edit shot returns at the reviewed local frame. Background preview rendering supports the existing camera/effects/masks/Symbol compositor and reduced rehearsal resolution. Global audio tracks, transitions and pre-rendered range caching remain separate follow-ups. Verified with 130 passing regression suites and nine native/nine Retina suites (140 checks each), including both window teardown orders and return-to-drawing focus. See [the workflow](animation.md#scene-and-movie-review-in-0750).

## Follow-up in 0.74.0 — workspace and Timeline UI review

Implemented task workspaces for Illustration, Anime, Storyboarding and Animatics, plus named custom layouts and independent New Project starter choices. Timeline now has responsive transport/edit/footer rows, an accessible overflow menu, Center playhead and selected-target/frame context. Review reproduced and corrected toolbar Trash choosing the wrong target for property/camera keys or audio, missing drawing/position-lock state, and unfinished frame-entry text being reset during responsive layout changes. Saved layouts preserve timeline selection when restoring the Camera inspector. Verification status is recorded in [the checklist](TODO.md).

The scope is workflow/layout polish. At that release, value/speed graphs, continuous scene/movie preview and timing-sheet views remained separate later features; the existing animation/compositing capabilities below remain available.

## Follow-up in 0.73.0 — verified

The scoped animated effects/mask implementation adds 11 numeric effect channels and five held master/effect switches, plus seven numeric mask channels and held enable/inversion. Together with the original six transform/opacity channels, 31 scalar/switch channels are available. Effects exposes direct animation diamonds; Frame Properties separates Transform, Effects and Mask categories and follows channel selection from Timeline. Numeric channels reuse the existing key/easing workflow; switches hold until their next key.

Mask placement, opacity and feather are independent of the artwork transform and use one shared painted source image. Inversion is nondestructive. Painting maps through the evaluated mask placement and edits that same source across time. Shadow/glow colors and the independent mask pivot remain static; per-frame mask drawings, vector-mask path animation and canvas mask handles are outside this pass. Conditional format 36 stores the added channels/properties, including within Symbols, while other projects keep their applicable older format.

Implementation and verification are complete for the stated scope: app/test build, 117 affected regression suites, and nine native/nine Retina UI suites with 110 checks per mode all pass. Compact/larger-font controls were reviewed; see the [current checklist](TODO.md). Existing background preview scheduling remains in use. Physical Huion feel and production-shot performance require separate acceptance. Full value/speed graphs, continuous scene/movie preview and optional X-sheet views remain the next planning choices.

## Follow-up in 0.72.0

Timeline track organization adds shared Visibility/Lock controls, temporary Solo, expandable hierarchy rows, named folders and header reordering with context/keyboard alternatives. Ordinary folders can contain animation tracks while drawing containers retain their ownership protections. Hierarchy edits preserve exposure/key/pose timing, support Undo and save with the project; nested animation tracks require format 35, including those inside Symbol definitions.

Solo filters Canvas and Navigator only, respects saved visibility, and retains required ancestors and clipping bases. It does not alter audio, exports, project data or history. Timeline collapse state is remembered per shot within the current workspace and resets on project load. Audio, camera and cue lanes retain specialized controls.

See the [current checklist](TODO.md) for build, regression and display-scale verification. Animated effects and masks are implemented and verified in 0.73 above; Scene/Movie review is now implemented in 0.75; graph editing remains a later choice.

## Follow-up in 0.71.0

The native Symbol editor now restores drawing-canvas focus after its modal window closes on Save or Cancel, while respecting another window selected by the artist. Focus verification was completed before implementing timed poses.

Manual lip-sync is implemented through a fifth **Poses** playback mode, the visual pose picker at the parent playhead, a held-pose timeline lane and compact Pose/Key frame controls. Independent instance keys support selection, move/nudge, copy/paste, deletion and previous/next navigation with atomic Undo. They use unscaled clip time: moves carry the schedule, trims retain hidden keys and preserve surviving poses, Slip remains active, Speed is disabled, and First frame remains the fallback. Nested rendering, reduced previews, exact pause, exports and Break apart use the evaluated held pose. Format 34 is selected only for Poses mode or retained keys; earlier projects keep their applicable save format.

The new pose suites and focused native checks pass; see [current verification](symbols.md#071-verification-status) for the final regression and display-scale status. Automatic speech analysis is not included. Track organization follows in 0.72 above.

## Follow-up in 0.70.0

Named/color markers, key/breakdown/in-between labels and dialogue ranges are implemented as shot cues. Timeline lanes support overlapping ranges, move/trim previews, cancellation, isolated selection, one-frame nudging and Fit Selection. A compact Cues inspector adds searchable notes, speaker/text, timing and navigation. Cues persist in format 33, follow whole-shot ripple edits and remain outside exported artwork and Symbol definitions. The 0.70 full build and 85 regressions passed, with four successful native suites (62 checks) and five Retina suites (69 checks). Native Symbol-editor focus was unresolved in that release and is repaired in 0.71 above.

Manual timed Symbol mouth/pose selection follows in 0.71 above. Dialogue cues themselves do not generate mouth poses or automatic lip-sync.

## Follow-up in 0.69.0

Symbol clip rows, move/trim/slip, finite or through-end Hold, clear-and-restore exposure and a shared Symbols inspector are implemented. Clip selection stays independent of cel/key/audio/camera selection, including instances in unused drawings. The 0.69 paged visual picker chooses the whole instance's First frame or Still pose; it resets Slip and preserves placement, Hold and Speed. Format 32 stores nondefault clip timing, while legacy Symbols retain format 31. Ripple rejects unrepresentable splits and preserves source definitions. That release's full build, 82 regressions, four native Symbol/Timeline suites (60 checks) and six Retina suites (84 checks) passed. Its broader native workspace/editor focus recheck was inconclusive; the 0.71 status is above.

Marker/dialogue cues follow in 0.70 and manual timed mouth/pose selection in 0.71 above. The original starting-frame picker remains separate from the new timed-pose picker. Automatic lip-sync and batch clip edits remain future work.

## Follow-up in 0.68.0

Multiple-cel selection, batch move/copy/paste/delete, common holds, repeat and reverse are implemented across drawing tracks. The Drawing inspector exposes selection-wide Start/Hold with mixed-state feedback. A compact reuse picker provides search, thumbnails, drawing labels, usage counts and linked exposures in the original track. Unused drawings survive ordinary exposure removal, whole-shot Delete time and Save/reopen; their deletion is explicit and undoable. Property keys remain separate from ordinary cel timing. Timed Symbol clips and visual frame picking follow in 0.69 above.

## Follow-up in 0.67.0

Reduced-resolution playback is implemented with Full/Half/Quarter/Eighth choices in the Timeline footer. Canvas and Navigator restore exact paused results; source artwork and exports remain full quality. The native Retina stress fixture now completes about 1 / 4.7 / 17.7 / 24 displayed FPS respectively, with exact paused reference matches and unchanged source/history. A bounded prepared-source cache supports repeated playback frames; there is no pre-rendered playback-range cache. Full-quality fallback covers unsupported 3D compositions. Production-shot and physical Huion acceptance remain open.

The subsequent multiple-cel selection/timing and drawing-reuse milestone is implemented in 0.68 above; Symbol clips/frame picking follow in 0.69.

## Follow-up in 0.66.0

Symbol pivots and whole-shot ripple are fixed, with explicit rejection of timing changes that require splitting a Symbol clock. Effects playback now keeps a running render and only the newest waiting frame, avoiding cancellation starvation; paused results remain exact. Timeline zoom, Fit Shot/Selection and compact Frame Properties are implemented. Verification results are recorded in the development log.

Native Retina profiling showed why playback quality controls should come next: a generated full-canvas 4K blur/glow scene with an animated Symbol and visible Navigator completed four previews per view during four seconds at a requested 24 FPS. The main-thread heartbeat maximum was 18.8 ms; pausing reached an exact selected frame in about 1.16 s. Source artwork and history were unchanged. Version 0.66 had no playback-range cache or reduced-resolution rehearsal mode; 0.67 addresses that performance priority above.

## Already implemented

| Area | Current capabilities |
| --- | --- |
| Drawings | Raster/vector frame animation, layered drawing templates, blank/duplicate drawings, editable holds, exposure move/trim, drawing/frame navigation, playback ranges and configurable onion skin. |
| Motion | Layer/folder position, scale, rotation and opacity; compatible vector path morphs; multiple-key selection/copy/paste/retiming; motion paths, pivots, preset and custom segment easing, path orientation. |
| Cameras | 2D shot framing and animated 3D reference cameras, canvas controls, pose keys and export evaluation. |
| Audio | Waveforms, import, move/trim, overlapping clips, mute/gain, fades, crossfades, volume keys, audible scrubbing and mixed WAV export. |
| Shots/output | Scene/shot organization and reordering, Shot/Scene/Movie playback, PNG sequences, movie MP4 export with audio and a queued export workflow. |
| Reuse/compositing | Project Symbols with internal animation, independent instance playback and timed held poses for manual lip-sync; numeric/switch effect and independent mask-property channels evaluated with animated artwork (0.73 verified). |

These are existing features, not proposals to rebuild.

## Original confirmed findings (addressed in 0.66.0)

### 1. Symbol rotation and scale use the wrong default pivot

`defaultAnimationPivot()` in `src/property_animation.cpp:327` measures owned pixels or editable objects. Canonical Symbol instances own neither, so the function falls back to the whole canvas centre. New property animations and Reset Pivot use this path, while the canvas handles now show tight symbol artwork bounds.

Reproduction: a 40 × 60 prop centred at (65,65) in a 320 × 180 canvas gets pivot (160,90). Applying 90° rotation moves its centre to (185,-5), instead of rotating about the prop's centre. A user can move the pivot manually, but the default is misleading.

Required: a defined symbol registration/default-pivot policy using the shared artwork bounds, consistent for new motion, Reset Pivot, groups and nested instances. Preserve deliberate user pivots. Add rotate/scale tests, not only translation tests.

### 2. Whole-shot ripple leaves Symbol playback behind

`applyTimelineRipple()` in `src/timeline_ripple.cpp:121` retimes drawing exposures, layer/path keys, cameras and audio. It does not retime `SymbolInstance::startFrame`, which controls the internal animation clock in `src/symbols.cpp`.

Reproduction (zero-based frames): symbol and motion start at frame 20. Inserting 12 frames at frame 10 moves the first motion key to 32 but leaves the symbol start at 20. At the intended new beginning, the symbol is already on internal frame 12. The operation returns success and the project still validates.

Required: define and test symbol timing under Insert/Delete/Move, including cuts crossing an active symbol, nested playback and instances exposed inside cels. Retiming only the start is insufficient for every crossing-range case; unsupported cases need an explicit safe rejection until their behavior is implemented.

Evidence: `build/animation-review/findings.json`, generated by the isolated review probe in `build/animation-review/`. No app source changes were made to reproduce these findings.

## Original playback reliability risk (scheduler updated in 0.66.0)

There is a conditional risk in effects-heavy playback: every frame change invalidates the effects preview context (`src/canvas.cpp:439`); invalidation cancels running work (`src/effects_preview_renderer.cpp:204`), and results from old contexts cannot publish. If rendering consistently takes longer than a playback interval, the audio/playhead may advance while the canvas retains an older image.

This is a code-supported risk, **not a reproduced playback failure**. Measure representative 4K shots with blur/glow and animated Symbols. Check completed displayed frames, sound synchronisation and cancellation behavior. Depending on results, add a playback-aware scheduler, bounded cached preview range and/or an explicit reduced-resolution rehearsal mode while preserving full-quality output.

## UX backlog (navigation and compact rows completed in 0.66.0)

| Priority | Improvement | Why it is needed |
| --- | --- | --- |
| Done in 0.66 | Timeline zoom, Fit Shot and Fit Selection | Frames are fixed at 20 px (`src/timeline_panel.cpp:474`). Long cuts require excessive horizontal scrolling. Preserve the playhead/selection while zooming. |
| Done in 0.66 | Compact Frame Properties | Transform channels currently use a heading row plus a separate value row (`src/keyframe_panel.cpp:120`). Apply the established label/control layout to animation, camera and audio controls. |
| Done in 0.68 | Multiple-drawing exposure editing | Cross-track selection, batch move/copy/paste/delete, common holds, repeat/reverse and selection-wide Start/Hold; independent of property keys and pixel clipboard. |
| Done in 0.68 | Reuse existing drawings | Searchable original-track picker with thumbnails, labels and usage counts; expose shared artwork at the playhead and preserve unused drawings. |
| Done in 0.69 | Symbol clip representation | Actual Start/Hold bars, exposure intersections, move/trim/slip, clear/restore timing, compact inspector and visual First-frame/Still picker. Cycle-boundary ticks appear when zoom permits; batch clip editing remains later work. |
| Done in 0.71 | Timed Symbol poses | Visual source-frame choices at parent times, held-pose lanes, precise pose/time edits, batch key operations and independent instance schedules; manual lip-sync without speech analysis. |
| Implemented in 0.72 | Track organization | Shared Visibility/Lock, temporary Solo, expandable folder hierarchy and direct reordering with named folder creation and keyboard/context alternatives. |
| Done in 0.70 | Markers and dialogue notes | Named/color cues, pose labels and overlapping dialogue ranges, with direct timeline timing edits and a Cues tab in Frame Properties. |

## Later animation extensions

- **Further effect/mask animation:** 0.73 implements numeric effect values, held switches and mask placement/opacity/feather. Animated effect colors, mask-pivot editing, per-frame mask drawings and vector-mask path animation remain outside that scope.
- **Full graph editor:** custom outgoing-segment easing already exists. The missing capability is multi-channel value/speed graphs, overshoot and custom camera curves.
- **Automatic lip-sync:** manual timed Symbol poses and dialogue cues are implemented in 0.71 and 0.70. Optional speech/phoneme analysis remains later work.
- **Movie review extensions:** Scene/Movie playback and shot thumbnails are implemented in 0.75. Global audio tracks across cuts, transitions and a pre-rendered playback-range cache remain optional follow-ups.
- **Optional X-sheet view/export:** expose the existing timing data vertically for anime production, without creating a second timing model.
- **3D object/rig animation:** current 3D animation is camera-only. Model transforms, skeleton pose keys and shape keys are later work; lower priority for the artist's hand-drawn workflow.

## Suggested implementation order

1. Fix Symbol pivots and ripple timing; run the focused effects-playback check.
2. Improve timeline zoom/navigation and compact Frame Properties.
3. Add multiple-cel timing edits, drawing reuse and proper Symbol clips/frame picking.
4. Markers/dialogue cues are complete in 0.70 and manual timed mouth/pose selection in 0.71; track organization is implemented in 0.72 with verification tracked above.
5. With the scoped animated effects/masks verified in 0.73, Scene/Movie review is implemented in 0.75. Choose graph editing, global audio/preview caching or an optional X-sheet workflow next according to production needs.

## References and review limits

[ToonSquid's Timeline](https://toonsquid.com/handbook/interface/timeline/) documents zoom and direct drawing/timeline manipulation. [OpenToonz's Xsheet/Timeline workflow](https://opentoonz.readthedocs.io/en/latest/working_in_xsheet.html) is a useful reference for repeated exposures, timing edits and later lip-sync/timing-sheet work. These references guide the proposed flow; they are not claims of application compatibility.

The review includes source inspection and the two runtime reproductions above. It does not establish physical Huion performance, Windows behavior or real-time playback performance on a production movie. Existing passing tests remain valid evidence of their covered behavior; they did not cover the two Symbol cases found here.
