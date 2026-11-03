# Toon Pro

A native desktop workspace for a solo artist making hand-drawn anime. Built with **C++20, Qt 6 Widgets, and CMake** for macOS, Windows and Linux.

This repository contains a native **drawing workspace with frame-by-frame animation, layer/property keyframes, vector path morphing and production-sheet exports**. 3D references, rest-skeleton editing, mesh binding, weight painting and FK/two-bone IK posing are available. Drawing exposures, property animation, 2D/3D camera keys and embedded audio clips share the shot timeline.

## Desktop release candidates — 0.75.0

Optimized desktop packages, dependency deployment, relocated runtime checks and SHA-256 manifests are now available through the [packaging workflow](docs/packaging.md). The source and Windows candidate CI are in [complex1/ToonPro](https://github.com/complex1/ToonPro/tree/codex/release-candidate-0.75.0).

These are **unsigned/ad-hoc-signed test candidates**, not a signed production release. macOS arm64 includes the Metal 3D renderer; the other platform builds currently provide the 2D drawing/animation workflow without 3D rendering. See [release verification and remaining acceptance work](docs/release-0.75.0.md) for the exact build/test status and artifacts. Older milestone results below describe their original development builds.

## Scene and Movie playback in 0.75.0

Timeline now has **Shot / Scene / Movie** tabs. Scene reviews shots with the current shot’s scene label; Movie follows the Shots panel order. A thumbnail strip, global frame slider and frame/time readout let you navigate the sequence. Click a thumbnail to review its beginning; double-click or use **Edit shot** to resume drawing at the reviewed local frame. Returning to Shot restores the original editing position.

Playback crosses hard cuts with one synchronized audio stream, preserving each shot’s clip trims, fades, volume keys and mute. Review is separate from the drawing document and Undo history. Previews and thumbnails render in the background, with bounded caches and Full/Half/Quarter/Eighth playback resolution; pausing requests the exact frame. Very heavy shots can drop display frames while audio keeps time. Movie-wide audio tracks, transitions and a pre-rendered playback-range cache remain future work.

All **130 regression suites pass**. Nine native Mac suites and nine Retina suites each pass **140 checks**, including window teardown, workspace return focus, numeric drafts and simulated tablet input. See [Scene and Movie review](docs/animation.md#scene-and-movie-review-in-0750). The development build is `build/ToonPro.app`; the standalone package is unchanged.

## Project starters, workspaces and Timeline polish in 0.74.0

**New movie project** offers Blank, Illustration, Anime, Storyboard and Animatic starters. Blank has one drawing layer and no margins, header or frame marks. The other starters provide editable canvas/timing settings and appropriate initial drawing layers. A live preview shows the sheet before Create; existing projects retain their original presentation.

Choose **Illustration, Anime, Storyboarding or Animatics** from the top **Workspace** button or **View → Workspace**. Illustration prioritizes drawing, Storyboarding brings Shots alongside the canvas, and Anime/Animatics show Timeline and Frame Properties. Compact drawing remains available. **Save workspace as**, Update, Reload, Previous layout and Manage provide named custom panel arrangements without changing artwork or tool settings.

Timeline now keeps playback, frame entry and core drawing actions reachable in narrow panels, moves secondary commands to More, and provides a **Center playhead** button and current-selection/frame context. Trash handles selected property/camera keys, audio clips, cels, cues and Symbol exposures correctly, including drawing/position locks. Workspace switching preserves unfinished edits and restores inspector tabs without selecting a camera.

The **0.74.0** app builds; all **126 affected regression suites pass**. Ten native Mac suites and ten offscreen Retina suites each pass **145 checks**, including modal focus and unfinished edits. Native verification caught and fixed Return/keypad Enter activation in the workspace manager; its two focused regression suites and native/Retina follow-ups pass. Compact/larger-text captures were reviewed. See [project starters and workspaces](docs/workspaces.md) for defaults, saved-layout scope and research. The development build is `build/ToonPro.app`; the standalone package is unchanged.

## Animated effects and masks in 0.73.0

**Effects** now has direct animation diamonds beside blur, shadow, glow and color-correction settings. Enable a channel, move the playhead and change its value to create or update a key. **Frame Properties → Animation** separates **Transform / Effects / Mask** channels into compact tabs; enabled channels use the existing Timeline key editing and easing controls. Master/effect enable switches hold their values until the next key. Shadow and glow colors remain static.

**Effects → Mask → Placement** adds independent mask position, scale, rotation, opacity and feather controls. Each can be keyed; mask enable and inversion use held keys. Mask opacity controls how strongly the mask hides artwork, with zero bypassing it. The mask's painted source remains shared across time, and its saved pivot stays independent of the artwork's animation pivot. Painting a mask edits that shared image rather than creating a new mask cel.

Editing an enabled channel preserves the source setting and changes its current-frame key. Unkeyed controls remain static. Each completed drag is one Undo step; pending numeric input survives refreshes, Escape cancels it and navigation resolves it. Background previews continue to coalesce edits; the same evaluated effects and masks feed playback and export. **Format 36** is selected only when effect/mask channels or extended mask properties are present, including inside Symbols; other projects retain their applicable earlier format.

The **0.73.0** app builds and all **117 affected regression suites pass**. Nine native Mac suites and nine Retina suites each pass **110 checks**, including focus, simulated tablet input, animated 4K Draft→Exact previews and compact/larger-text controls. The development build is `build/ToonPro.app`; the standalone ZIP is unchanged. Physical Huion feel and production-shot acceptance remain separate. See the [animated effects workflow](docs/animation.md#animated-effects-and-masks-in-0730) and [current checklist](docs/TODO.md).

## Timeline track organization in 0.72.0

Timeline artwork rows now provide direct **Visibility**, **Lock** and **Solo** controls. Visibility and locks use the same saved layer state as Layers, including inherited folder restrictions. Solo temporarily isolates visible artwork in Canvas and Navigator without changing the saved visibility, audio, Undo history or exports; **Clear timeline solo** restores the normal preview.

Use **New timeline folder** to create a named folder, then drag track headers to reorder or nest them using the drop indicator. Folder carets expand/collapse their contents. The context menu provides Move up/down, Move to top level and folder creation; **Ctrl/Cmd+Shift+Up/Down** reorders the selected artwork row. Folders and order save with the project and support Undo, while cel exposures, motion keys and timed Symbol poses keep their timing. Collapse state belongs to the current workspace and resets when a project is loaded.

Ordinary folders can contain animation tracks; drawing containers retain their ownership protections. Nested animation tracks use **format 35**, including those inside Symbol definitions. Projects without nested tracks retain their applicable earlier format. Audio, camera and cue lanes keep their existing specialized controls.

The app/test build succeeds and all **103 affected regression suites pass**. Eight native Mac suites and the same eight at Retina scale each pass **129 Qt checks**, including Timeline organization, Layers, Symbol-editor/workspace focus and simulated tablet input. Compact and larger-text captures were reviewed. That release updated the development app to **0.72.0** at `build/ToonPro.app`; the standalone ZIP was unchanged. Physical Huion and production-shot acceptance remain separate.

See the [organization workflow](docs/animation.md#timeline-track-organization-in-0720) and [remaining checklist](docs/TODO.md).

## Timed Symbol poses and editor focus in 0.71.0

Use **Frame Properties → Symbol → Poses** for manual lip-sync or other held pose changes. Move the playhead, click the timed-pose icon, and choose a source drawing from the visual picker. The pose holds until the next key; **First frame** supplies the fallback before the first key. Speed is disabled in Poses, while Slip shifts pose timing underneath the clip. Each instance keeps its own keys and continues to share its source artwork.

A **Poses** lane shows the keys and held intervals. Select, move, copy/paste or delete keys, or use the compact **Pose / Key frame** controls and previous/next buttons. Moves carry pose timing with the clip; trims retain hidden keys and preserve surviving poses. Each committed edit is one Undo operation. Dialogue cues and audio remain useful timing references; this is manual posing, without automatic speech analysis.

The native Symbol editor now returns focus to the drawing canvas after Save or Cancel, after its modal window closes. Closing it does not take focus from another window the artist selected. Projects use **format 34** only when Poses mode or retained pose keys require it; other projects retain their applicable earlier format.

At that release, all **90 affected regression suites passed**. Eight native Mac UI suites and the same eight offscreen at Retina scale each passed **106 Qt checks**, including Save/Cancel focus, pose timing, numeric drafts and compact layouts.

That release updated the development app to **0.71.0** at `build/ToonPro.app`; its standalone ZIP and the artist's running session were unchanged.

See the [timed-pose workflow](docs/symbols.md#timed-poses-and-manual-lip-sync-in-0710) and [remaining checklist](docs/TODO.md).

## Timeline markers and dialogue cues in 0.70.0

Add **Marker, Key pose, Breakdown, In-between and Dialogue** cues from the Timeline’s pin button; **M** adds a marker while the timeline has focus. Named, colored ranges can overlap, move and trim directly. Double-click opens **Frame Properties → Cues** for search, previous/next navigation, timing, speaker and notes/dialogue text. Each edit supports Undo and cues save with the project.

Whole-shot Insert/Delete/Move keeps cue timing aligned, including splitting ranges when necessary. Cues remain separate from drawings, Symbol definitions and rendered exports. Timed mouth/pose selection follows in 0.71 above.

At that release, all **85 affected regression suites passed**. Native Cues/Timeline and drawing-workspace checks passed **62 Qt checks** across four suites; five offscreen Retina suites passed **69 checks**. Two older native Symbol-editor focus assertions failed in 0.70; the 0.71 fix is described above.

That release updated the development app to **0.70.0** at `build/ToonPro.app`; its standalone ZIP was unchanged. Cue-bearing projects use format 33 unless a newer feature requires a later format. See the [cue workflow](docs/animation.md#markers-and-dialogue-cues-in-0700).

## Symbol clips and visual frame picking in 0.69.0

Symbol instances now appear as timed clips, including instances inside folders and reused drawings. Drag the body to move, either edge to trim, or Alt-drag to slip the internal animation. Trimming preserves playback phase, including fractional speeds. The **Symbol** tab in Frame Properties provides Start, Hold, First frame, Speed, Slip and playback modes.

Double-click a clip, or use its frame-picker button, to browse small background-rendered previews and choose a starting pose. **Hold selected frame** keeps that pose for the whole clip. Apply makes one Undo step; Cancel leaves the artwork unchanged. Clearing a clip keeps its instance and shared source available for reuse.

All **82 affected regression suites passed**. Four native Symbol/Timeline suites passed **60 Qt checks**, and all six offscreen Retina suites passed **84 checks**. At that release, broader native drawing-workspace and Symbol-editor focus checks were inconclusive after losing window activation while Chrome was in front; their Retina equivalents passed. The current focus status is in 0.71 above.

That release updated the development app to **0.69.0**; its standalone ZIP was unchanged. Projects with explicit clip timing use format 32; unchanged legacy Symbol settings retain format 31. See [Symbols](docs/symbols.md) for the workflow and limits.

## Multiple-cel editing and drawing reuse in 0.68.0

Select several cels with Shift/Ctrl/Cmd-click or a selection box, then move, copy/paste, repeat, reverse or apply common holds. Drawing properties show the selection's Start and Hold, including mixed durations, with direct On 1/2/3 controls. Collisions, shot boundaries and locked drawings reject a batch without partial edits; each edit supports one-step Undo.

The Timeline footer's **Reuse existing drawing** folder button opens a searchable thumbnail library for the current track. Reused cels share artwork; Duplicate drawing still creates an independent copy. Removing exposures keeps unused drawings available, including after whole-shot Delete time and Save/reopen. The picker can explicitly delete unused drawings with Undo.

All **63 affected regression suites pass**. Four native Mac UI suites and the same four at Retina scale pass **55 Qt checks each**, including numeric drafts, focus, shared-artwork reuse and compact layouts.

That release updated the development app to **0.68.0**; its standalone ZIP was unchanged. See the [cel workflow](docs/animation.md#multiple-cel-timing-and-drawing-reuse-in-0680) and [remaining checklist](docs/TODO.md).

## Playback preview quality in 0.67.0

The **1/1** button in the Timeline footer now offers **Full / Half / Quarter / Eighth** playback resolution. Choose Quarter or Eighth for heavy effects; Canvas and Navigator use that quality while playing, then restore the exact selected frame when paused. The choice is remembered across sessions. Source artwork and exports stay full quality.

The native Retina 4K stress scene improved from about **1 displayed FPS at Full** to **18 at Quarter and 24 at Eighth**, with responsive controls. Full-quality pause refinement took about 1.16 seconds on that scene. Raster/vector/text animation, masks, groups and nested Symbols are supported; compositions containing 3D references use full quality with an explanatory status. This is a measured preview improvement, not a guarantee for every production shot.

All **56 affected regression suites pass**, along with four offscreen high-DPI suites (91 Qt results). All four native Mac suites also pass (91 Qt results), including the final unlocked Timeline/workspace focus recheck. That release updated the development app to **0.67.0**; its standalone ZIP was unchanged.

See the [playback workflow](docs/animation.md#playback-preview-quality-in-0670) and [remaining animation checklist](docs/TODO.md).

## Timeline reliability and navigation in 0.66.0

Timeline now has a compact zoom slider/number, **Fit Shot**, **Fit Selection**, and **Ctrl/Cmd + wheel** zoom around the pointer. Fit Shot stays fitted while resizing; Fit Selection frames selected keys, audio, the enabled playback range or current drawing. Zoom changes only the view. Compact Drawing / Animation / Audio / Camera properties reuse the shared responsive rows, smaller icons and accessible tabs.

Symbol rotation/scale starts around its artwork instead of the canvas centre, while saved custom pivots remain unchanged. Whole-shot ripple edits now move Symbol playback with its keys and exposures. Cuts that would require splitting an instance's internal clock are rejected with an explanation, without changing the project.

Effects-heavy playback allows the current preview to finish and keeps only the newest waiting frame. Canvas and Navigator can keep progressing when rendering is slower than the playback rate; pausing requests the exact selected frame. This skips superseded preview frames, preserves full-quality output, and does not guarantee real-time playback for every 4K composition.

All **55 affected regression suites passed** for 0.66. Five UI suites also passed on native macOS and offscreen Retina scale (58 Qt results in each mode); compact layouts and captures were reviewed. Its full-quality heavy 4K Retina stress scene stayed responsive but displayed about **1 finished frame/s**, motivating the 0.67 playback quality controls above. See the [animation workflow](docs/animation.md) and [remaining animation checklist](docs/TODO.md).

## Reusable Symbols in 0.65.0

Open **Panels → Symbols** to turn selected artwork into a shared symbol, create an empty symbol, or place linked instances in other shots. Double-click a library entry to edit its raster/vector/text layers and internal animation in a separate workspace. **Save changes** updates every linked instance with one Undo; **Cancel** leaves the source untouched.

Each instance retains its own transform, opacity/effects and **Loop / Once / Still / Ping-pong** playback, start frame, first frame and speed. **Make unique** creates an independent definition; **Break apart at current frame** restores editable layers for the displayed internal frame. Symbols save once in the project library (format 31), including nested references, masks and sampled-brush artwork. The library is project-local; Animate/ToonSquid file import, interactive button scripts, audio and 3D symbol contents are outside this pass.

All **50 affected regression suites passed** for this release, along with its final UI checks at 1× and 2×. All three native Mac UI suites also passed, including canvas focus after Save and Cancel. See the [Symbols workflow](docs/symbols.md) for controls, conversion constraints and verification. The latest development app is listed above.

## Previewed character recoloring in 0.64.0

Open **References → Color Models → Recolor drawings…** (the paint-bucket icon). Choose the character's **From** and **To** palettes, then check the raster drawings to change from the current shot or all shots. **Preview** prepares the result in the background; select a drawing and compare **Before / After**, with pan, zoom and fit. These views show the drawing's raw paint, before layer effects or compositing.

**Apply** changes every checked drawing in one Undo step; Cancel or Escape leaves the artwork untouched. Recoloring matches shared character roles, preserves pixel transparency and includes painted areas outside the frame. Narrow antialiased boundaries between approved colors are recolored too; unrelated colors and broad gradients remain unchanged. Roles sharing a source color but requiring different replacements are flagged instead of guessed. Full-resolution pixels are saved normally; the preview is bounded and no new project format is required. See [the recoloring workflow and limits](docs/cel-coloring.md#previewed-character-recoloring-in-0640).

Palette edits remain independent from artwork: this is an explicit pixel edit, not automatic palette-linked recoloring.

All **11 affected regression suites** pass, along with three native Mac suites and the same three at offscreen 2×. The three-drawing 4K background-preview benchmark completes in **148 ms**, with an **11 ms** maximum main-thread heartbeat gap on the tested Mac. Native layout captures and recolored boundaries were visually reviewed. These measurements describe the 0.64.0 recoloring pass; the current development app is listed above.

## Character Color Variants in 0.63.0

**References → Color Models** adds lighting-palette tabs for **Day**, **Night**, **Sunset** and **Interior**. New characters receive the three lighting palettes alongside Day. They start as matching, independently editable copies; their names do not automatically tint colors or artwork. Existing characters keep their saved Day colors, with **Lighting palette options → Add missing lighting palettes** available when wanted.

Use **+** or **Duplicate palette…** to copy the selected palette under a new name. The options menu renames or removes a lighting palette; Day remains the base. Swatch names and Skin/Hair/Costume/Shadow categories are shared across palettes, while each palette has its own RGBA values. New swatches start with the same foreground color in every palette, and removing a swatch removes that shared role from all palettes. All library edits use Undo/Redo.

Palette selection stays with each character while switching shots in the open project. Reopening starts on Day; all palette definitions and colors are saved. Selecting a swatch sets the foreground. Explicit character/palette navigation also selects the matching approved palette in Cel Paint Check, whose palette list remains independently usable. Existing painted pixels retain their colors.

Projects containing variants use format 30 and retain embedded model sheets and drawing aids. Limits are 16 additional palettes per character, 256 shared swatches per character, 8,192 shared swatches per project and 65,536 total color entries across Day and all variants. All 17 affected regression suites pass, followed by six UI suites after the final tab-readability adjustment. Four native Mac suites and the same four at offscreen 2× each pass 33 Qt results with no failures or skips. Restored palettes, narrow/custom tabs, larger text and Paint Check selection were visually reviewed. The development app at `build/ToonPro.app` reports **0.63.0**; the standalone ZIP is unchanged.

## Saved drawing aids in 0.62.0

**References → Light Table** now saves embedded snapshots with their placement, scale, rotation, opacity, selected pin, visibility and solo state. Reopening restores the references even if an imported source image has moved or been deleted.

**Cel Shading** retains unfinished guides, fill operations and their controls for each shot, outline layer and frame. Save and drawing/frame changes preserve this work without applying it. Return to its drawing to continue, or reopen the project at the saved shot/frame. Apply creates ordinary shade layers and clears that draft; Cancel removes the current draft.

Drawing-aid changes mark the project unsaved without adding artwork Undo steps. Undoing artwork leaves the current pins and unfinished drafts intact. Pins and guides remain excluded from exported artwork. Projects with saved aids use format 29; projects without them retain their existing compatible format. Limits remain eight pins, 64 MiB of snapshot pixels and 2,048 pixels per pin side, with up to 64 unfinished shading drafts per project.

All 12 affected suites pass: 130 Qt results, zero failures and one optional reference-capture skip. Four native Mac suites and the same four offscreen 2× suites also pass, with 45 results in each mode. Restored Light Table and shading views were reviewed at both display scales. The development app at `build/ToonPro.app` reports **0.62.0**; the standalone ZIP is unchanged. See [cel coloring and tracing](docs/cel-coloring.md) and [the development log](docs/DEVELOPMENT.md) for workflow and verification details.

## Cel coloring and Light Table in 0.61.0

- **Panels → Cel Paint Check:** inspect checked raster drawings for unpainted holes, stray pixels, possible edge-reaching fill leaks and colors outside a character palette. Jump between suspects, compare proposed repairs with the originals and apply selected repairs as one Undo.
- **Panels → Cel Shading:** draw separate shadow/highlight boundary guides, fill the enclosed regions and preview hidden or recolored boundaries. Apply creates ordinary Shadows/Highlights layers below the unchanged outline.
- **References → Light Table:** pin a drawing, folder or imported image; move, scale, rotate and adjust its opacity; flip between pins while drawing. Pins never change their source or exports. Version 0.62 adds the project storage described above.

Background analysis, cancellable previews and cached light-table overlays keep this work out of live brush processing. Pending paint repairs are resolved before Save, Close or timeline transitions. Version 0.62 preserves unfinished shading drafts through those transitions without requiring Apply. Paint checks report suspects, not guaranteed errors; inspect each repair before applying it. See [cel coloring and tracing](docs/cel-coloring.md) for the workflow and current limits.

Ten affected regression suites and all four new workflow suites at 2× and on native macOS pass. Native and Retina captures cover compact panels, larger text and canvas previews.

## Character Color Models in 0.60.0

Open **Panels → References → Color Models** to organize named **Skin**, **Hair**, **Costume** and **Shadow** swatches for each character. Model Sheets and Color Models share the same character entries and selection; their colors travel with the project and stay available across shots.

Click a swatch to set the drawing foreground color. Add saves the current foreground; Replace updates a selected swatch from it. Edit opens the color picker with hex and opacity controls. Names edit inline, and the context menu moves colors between categories. Palette edits support Undo/Redo; Save and Close resolve unfinished names before saving or checking for unsaved changes. Colors are copied into the brush when selected; existing artwork retains its painted colors.

All **15 affected regression suites**, **five 2× display suites** and **five native Mac suites** pass. Native/2× layout captures were reviewed, and the final keyboard-focus, unfinished-name and Save/Close checks are complete.

## Character Model Sheets in 0.59.0

Open **Panels → References → Model Sheets**. Add a character and import images into **Turnarounds**, **Expressions** or **Costumes**. The character library belongs to the movie project and is available from every shot, with no duplicate imports. **Images** keeps the existing shot-specific references.

Import several PNG/JPEG/BMP files at once, edit a sheet's name inline, or right-click its preview/thumbnail to move it to another category. Character and sheet changes support Undo/Redo. Pan, zoom, fit and color sampling work in the model-sheet viewer; switching shots keeps the current character reference and view. Embedded copies survive moving or deleting the original files. Imports fit within 2048 pixels per side without changing the source files.

The final validation passes all **11 affected regression suites** and **three native Mac suites**, including keyboard focus and unfinished entries. Native and 2× captures cover narrow panels and larger text.

## Responsive Effects previews in 0.58.0

Effects previews now render in the background for both the canvas and Navigator. Dragging a setting shows a faster draft on static raster artwork; pausing or releasing refines it to full quality. The last completed image stays visible while the latest setting renders, and the Effects panel shows **Updating preview…** without disabling its controls or replacing unfinished numeric text.

Old work is cancelled or replaced when settings, views or documents change. A slider gesture remains one Undo step. Draft resolution affects only the temporary display: source pixels, saved settings and full-quality exports remain unchanged. Vector, text, animation, 3D and active-transform compositions use full-quality background rendering, so complex previews may update less frequently. Physical Huion timing remains a separate check. See the [performance measurements](docs/performance.md) and [validation log](docs/DEVELOPMENT.md).

In the paired 4K test at 2× display scale, the maximum GUI heartbeat interval falls from **1.80–1.85 seconds to 23.33–24.45 ms**. Drafts update about every 101–120 ms; full-quality refinement still takes roughly two seconds in the background. Both Canvas and artwork match the previous build exactly. All **26 affected regression suites** and **seven native Mac suites** pass.

## Drawing drawer, retouch and Effects in 0.57.0

Choose **View → Workspace → Compact drawing**, then click the current-tool/size button in Quick access. **Tools / Sizes / Properties** now open in an overlay drawer: the canvas keeps its dimensions, zoom and position. The drawer remembers its page and open state, fits short windows, and restores the same controls to their docks when you choose Drawing. Close it with its **×** button or Escape after finishing any text edit. Existing pen gestures, numeric drafts and modal editors stay protected.

Retouch strokes now retain their painted result when tablet hover arrives before the tip-release event, or a mouse release is missed. Native window event delivery no longer prematurely ends brush, retouch or mask contact. Explicit Escape still cancels an unfinished gesture, and retouch remains undoable.

Effects rendering now reuses neighboring filter regions, processes shadow/glow alpha directly, and avoids repeated mask and color calculations. Full-resolution output and edit/Undo behavior are preserved. Heavy combined effects on 4K artwork still have a measurable pause; this release does not introduce background previews. See the [performance measurements](docs/performance.md) and [validation log](docs/DEVELOPMENT.md).

All **24 affected regression suites pass**. After the desktop was unlocked, **all 11 native Mac suites passed: 256 Qt results**, including drawer focus, pen events and every retouch mode. Native verification also caught and fixed enlarged-text tab clipping. All 39 paired Effects image checks match the previous renderer. Physical Huion acceptance remains pending.

## Workspace and pen-flow polish in 0.56.0

The final UX implementation pass protects drawing from incidental panel input. Wheel scrolling over an unfocused slider or number scrolls its panel; a deliberately focused control can still use the wheel when no numeric draft is active. Tab navigation includes eligible panel buttons and switches drawing mode from the canvas. This application-level keyboard policy does not change macOS preferences. Quick access remains keyboard-reachable, while pen or mouse clicks preserve the current focus until an action is accepted. Accepted tool changes commit the source tool’s draft before restoring the chosen tool. Tool chooser rows grow with larger fonts, and layout changes are guarded while numeric drafts, layer previews or tool gestures are active.

Pen cursor handoff now works from the canvas back to panel controls as well as across dock separators into the canvas. Interrupted contact resolves at the last drawn position, including lost release followed by a panel tip press. Mask painting retains its contact when a barrel button is released and ends interrupted tip contact without adding a connecting stroke. Existing workspace placement, tool identities, project formats and Undo behavior remain part of the preserved workflow.

All **20 targeted regression suites pass** for 0.56.0. The final unlocked-desktop run also passes **all nine native Mac suites: 181 Qt results**, including focus navigation, Quick access, drawing mode, tablet events, masks, cursors and Guides. The earlier native focus checks are closed. Five paired 2× drawing profiles retain identical artwork hashes and comparable input/redraw timings. Physical Huion comfort remains a separate acceptance check. See the [development log](docs/DEVELOPMENT.md) for evidence, visual review and the remaining physical tablet check.

## Guides, Selection and Vector UI in 0.55.0

**Guides** keeps guide creation, the guide list and the Selected / All tabs available while each settings page scrolls independently. All six guide types retain their on-canvas editing and exact controls. Custom-color details appear only when enabled, preserving their values; placement help and larger text fit without adding shifting rows.

**Selection / Display** tabs separate selection settings from its appearance. Selection actions stay available outside the scrolling settings and hide during an active transform. Transform completion remains direct, with labeled Apply / Cancel below the scrolling perspective and warp controls. **Vector Properties** keeps mode, correction direction and simplify/reset actions visible while settings scroll independently; contextual status has stable space.

This completes the implementation of revamp Pass 3. Existing values, unfinished input, selection state, canvas editing, Undo and saved layouts remain part of the preserved workflow. All 16 targeted test suites pass, and narrow/larger-text Retina layouts were reviewed. The native Guide foreground-focus recheck is closed by the 20-result baseline for 0.56.0. Pass 4 implementation and its verification status are described above; physical Huion acceptance remains pending.

## References and Effects UI in 0.54.0

**References** gives more space to the image: its full name and dimensions remain available through the compact header's tooltip and accessible name, with Fit beside Zoom. The thumbnail strip appears when there are multiple references and hides for zero or one image. Existing import/removal, image selection, pan, sampling and exact zoom remain available; changing the strip does not reset a manually positioned view.

**Effects** keeps all five category tabs directly reachable in narrow panels. The selected effect's enable switch stays above the scrolling settings. Turning an individual effect off collapses its details without discarding values; the master bypass leaves configured details visible but disabled. Mask creation actions appear before a mask exists, and mask editing actions appear once it does. Layer context and unavailable-action reasons keep stable, font-aware space. Both panels use shared 16 px action icons, 28 px targets and responsive property rows.

This continues revamp Pass 3 after the advanced brush and management pass. Guides, Selection/Transform and Vector Properties follow in 0.55.0. Saved workspace placement, reference/project formats, drawing behavior and edit/Undo boundaries are preserved; physical Huion acceptance remains open.

## Advanced brush and management UI in 0.53.0

Advanced brush settings retain all 12 categories with shared compact actions, tabs and property rows. Shape dynamics now has direct **Size / Angle / Roundness** tabs; Transfer has **Opacity / Flow / Speed** tabs. Each response measures and scrolls its own content, keeping the active controls reachable without space reserved for hidden pages. Texture, Dual brush, Clustered spray and Speed response keep their enable switch visible and hide inactive details without resetting their values. Interactive previews and response graphs remain available.

**Brush Management** separates **Brush** and **Organize** details, keeping the selected brush's Use and advanced-settings actions visible. New Brush keeps its source tabs and Create/Cancel actions outside the scrolling form. **Tool Management** separates lists from details, placing them side by side when the panel is wide enough and stacking them when narrow. Resizing reuses the same editors to preserve selection and unfinished input; group membership actions stay visible. Ordinary actions use 16 px icons in 28 px targets, with shared spacing and responsive rows.

These changes preserve brush behavior, library identities, artwork Undo, saved workspace placement and project formats. This completes the advanced brush and management portion of revamp Pass 3. References and Effects follow in 0.54.0; Guides, Selection/Transform and Vector Properties follow in 0.55.0. Physical Huion acceptance remains open.

## Drawing workspace in 0.52.0

Choose **View → Workspace → Drawing** or **Compact drawing**, also available from the workspace button in the project toolbar. Drawing places Tools, Sizes and Properties together on the left, with Colors / Refs / Navigator tabs above Layers on the right. New workspaces use this arrangement; existing saved layouts keep their placement until a preset or Reset workspace is chosen. The short **Sizes** and **Refs** headers remain **Brush Preset** and **References** in the Panels menu.

Compact drawing keeps the current tool, brush size and foreground swatch in Quick access. In 0.52, the tool summary opened or closed the existing drawing docks; **0.57 replaces that behavior with the overlay drawer described above**. Click the swatch to reveal Colors. Switching tools or resizing the window does not automatically open drawing controls. Tab drawing mode retains its canvas-and-Quick-access workflow and restores the previous compact state on exit.

Size tiles now have a 38 px baseline with their exact number and circle cue, adapting to width and larger fonts. Quick access uses shared compact icons/targets, separates adjacent tool/color/navigation runs without changing pin order, and retains customization and overflow access. Tool libraries, presets, artwork and project formats are unchanged.

## Shared drawing UI in 0.51.0

Toolbox, Tools, Properties, Layers and Colors now share compact sizing and spacing: a 40 px tool column, 30 px named tool rows, 32 px layer rows, and 16 px action icons with 28 px targets. Property rows keep their slider and 48 px exact field, with responsive labels and preserved in-progress input. Nineteen original drawing icons distinguish curves, guides, pressure, surface settings, gradients, blur and layer states while retaining the existing Lucide family. Known built-in defaults upgrade without replacing custom group images, user-selected symbols or tool identities.

Drawing tabs and dock headers use shared rules. Layer names, brush previews, color picking, tooltips and existing actions remain available. Saved workspace placement and grouping are preserved. See the [UI rules](docs/ui-guidelines.md) and [revamp plan](design/ux-redesign-2026-10-31/REVAMP-PLAN.html).

## History and Auto Actions in 0.50.1

Open **Panels → History** to navigate artwork edits, see the current/saved state and return through available redo steps. **Panels → Auto Actions** records reusable layer setup, effects, fill, bucket and raster move/resize/rotate workflows with Record/Stop/Play, optional steps and a saved action library. Playback validates the sequence before changing artwork and commits it as one Undo entry. Unsupported artwork operations stop recording with an explanation; freehand drawing, selection creation and perspective/warp or multi-layer transforms remain outside the recording scope. Fill reuses the target selection (or the drawing area); transforms record on Apply/Done and use the new target’s bounds. See the [workflow and supported commands](docs/history-and-actions.md).

## Default brush library

Version **0.49.0** provides **200 catalog brushes in 15 subject groups**, alongside the unchanged 19 starters: **219 built-in brushes in total**. The latest 34 recipes cover connected ropes, chains, vines, cables and smoke; joined architectural repeats; editable manga tones; perspective surfaces; clustered spray, shaded color tint and speed-sensitive ink. Open a subject group in Toolbox, or use **Panels → Brush Management** and its category, tag and search filters. No import is required.

The [200-brush catalog](docs/brush-catalog-plan.md) records all installed recipes. Shipped artwork is generated from original Toon Pro shapes and textures; colored stamps are layered procedural illustrations. Artist review on real production artwork and a physical Huion session remain open. Tool settings, custom groups and deleted-tool records are preserved during upgrades. Tools and Add Tools build only visible previews between drawing gestures; automatic tool-library saving waits for the gesture to finish.

Version **0.42.1** corrects the catalog's brush marks: brick, roof, wood and related line patterns leave their interiors transparent; continuous grain and cloud strokes use softer, closer-spaced dabs; foliage varies color, size and rotation within a held stroke. Colored stamps vary geometry while keeping their original colors. Existing catalog tools adopt corrected settings only where those fields still match the original defaults. Custom settings, copied/imported tools, organization and existing artwork are preserved.

## Brush Preset

Version **0.44.0** adds **Panels → Brush Preset**, a grid of reusable brush sizes. Each tile shows a circle cue and its numeric size; click once to set the active Brush or Eraser size. The panel starts with 19 sizes from 1 to 256 px. Click **+** to add a size using the shared slider and exact number, initially set to the current brush size. Right-click a tile to edit or remove it; **⋯ → Reset sizes to defaults** restores the initial list. Sizes stay sorted, support one decimal place, and remember up to 64 entries across projects and sessions.

Size presets change only the active brush's size, preserving its tip, color, opacity and dynamics. Adding, editing or removing saved sizes leaves the current brush unchanged until a tile is selected; these preferences do not enter artwork Undo. Retouch, shape, mask and vector-correction sizes keep their separate controls. A new or reset workspace places Brush Preset below Tools; existing saved layouts are retained, with the panel available from the Panels menu. It can be resized, moved or floated and hides/restores with Tab drawing mode.

## Quick access and drawing mode

Version **0.43.0** adds a compact, customizable **Quick access** toolbar at the top of the workspace. Click **+** to pin the current tool or foreground color, choose another tool or color, or add **Hand, Zoom in, Zoom out, Fit sheet** and **Reset view rotation**. Clicking a pin activates that tool, color or navigation action. Right-click a pin to move it left/right or remove it. Up to 48 pins and their order are remembered across sessions; removing a pin does not delete its tool.

With a project open, press **Tab** or click the toolbar's drawing-mode icon to enter fullscreen with the canvas and Quick access toolbar. The ordinary panels, project controls, menu and status bar hide. Press **Tab** again, or click **Return to workspace**, to restore the previous dock arrangement, floating panels and window state. Tool changes keep panels hidden while drawing mode is active. Tab retains its normal behavior in text/numeric editors, dropdowns, dialogs and open menus; an active pen stroke or unfinished canvas gesture prevents a mode change. Save, Undo/Redo, canvas tool shortcuts and navigation remain available.

## Connected-brush and pattern quality in 0.49.4

Connected segments now paint shared boundaries once, avoiding dark seams in translucent strokes. Sharp direction changes bridge their outer corners with the motif's own transparency, and alternate repeats reflect edge pixels without sampling the opposite end. Fence/railing posts are complete at repeat seams, balcony spacing is even across tiles, and the stone pattern includes neighboring shapes at its tile edges.

All 21 connected/repeating presets were reviewed on slow curves and sharp corners. Tight inside turns can still overlap or compress motifs; this is not a full corner-redesign system. Corrected catalog defaults adopt recipe revision 5 while retaining customized fields and edited source images. Existing raster pixels remain unchanged; editable vector strokes use the corrected renderer when rebuilt. See [brush behavior and limits](docs/brush-settings.md#connected-join-and-tile-quality-in-0494).

## Retina connected-brush redraw in 0.49.3

Large detailed cursors now reuse their outline during fractional pen movement. Connected cursor tips advance with the painted repeat, and irrelevant pressure/tilt changes no longer rebuild a fixed-shape cursor. The opaque canvas also avoids redundant background painting. Brush detail, delivered pen samples and painted output are retained.

At 2× display scale, the 1,024 px chain's full-window redraw p95 improves from 18.51 to 12.88 ms (median of three final runs); all 30 before/after artwork hashes match. Nine targeted suites pass, with 22 cursor results also passing on native macOS. Rare full-window capture outliers remain; these synthetic measurements do not establish physical Huion latency. See [measurements and limits](docs/performance.md#retina-connected-brush-redraw--0493-31-october-2026).

## Large connected-brush performance in 0.49.2

Connected brushes now rasterize narrow horizontal bands instead of scanning the empty tile area around each strip segment. In an isolated 1× workspace benchmark, 1,024 px chain input p95 fell from 50.31 ms to 4.23 ms; all 20 model/workspace before/after artwork hashes match across 1× and 2× runs. Six targeted suites and 32 native connected results pass. Full-window Retina redraw still has costs at extreme sizes; physical Huion latency is not measured by this benchmark. See [performance evidence](docs/performance.md#large-connected-brushes--0492-31-october-2026).

## Connected-brush correction in 0.49.1

Slow connected strokes now use distance-based cross-section turns and bounded geometry steps. Affine triangle mapping replaces unstable projective mapping of tiny segments, removing the gaps and distortion reproduced with dense pen samples. The fix applies to connected and repeating brushes, including editable vector replay. Existing raster pixels stay unchanged; rebuilt connected vector strokes use the corrected renderer. Very tight corners can still compress a motif. Physical Huion acceptance remains a manual check.

## Available in 0.49.0

The brush wrench adds **Tip shape → Stamps / Ribbon / Repeat** and **Patterns and tones**. Connected strips bend along the drawn curve; repeats keep their distance-based phase. Tones expose frequency, density and angle, and image patterns can map onto a draggable four-corner plane. **Scattering** adds a clustered distribution editor; **Transfer** adds speed response; **Color dynamics** adds foreground tint for colored stamps. Existing brush defaults and captured artwork remain unchanged. See [connected brushes and patterns](docs/brush-settings.md#connected-brushes-patterns-and-specialized-dynamics-in-0490).

The 0.49.0 changes pass 43 targeted test suites across the regression run and focused recheck, plus native macOS checks for the new rendering and property controls. The current development app is `build/ToonPro.app`; physical Huion drawing and artistic acceptance remain manual checks.

Version **0.48.0** adds **Dynamic stamp** in Brush Management: create one brush from 2–16 images, use foreground or original image colors, and vary tip order, size, rotation and saturation throughout a continuous stroke. Tip shape groups the stamp images and per-tip variation controls; old brushes keep their previous rendering. See [Dynamic stamp brushes](docs/brush-settings.md#dynamic-stamp-brushes-in-0480).

Version **0.47.0** adds **editable stamp placement**, with a labeled On/Off toggle in **0.47.1**. With a sampled brush, turn on **Tool Properties → Adjust stamp** to place one impression per contact and adjust it immediately. Drag inside its box to move, a corner to resize, or the round handle to rotate. Enter / the checkmark places it; Escape discards it. Clicking outside the box places the current stamp and starts another. Stamp and Colored Stamp tools enable the mode by default; each tool remembers an explicit choice, and switching it off places the current pending stamp before restoring continuous brushing. Existing artwork stays fixed. The stamp keeps its sampled tip and random variation while you adjust it, and a completed placement is one Undo step. See [stamp placement](docs/drawing-tools.md#editable-stamp-placement).

Stamp placement passed 30 targeted regression suites in 0.47.0. The labeled toggle update in 0.47.1 passes four affected suites and nine native macOS settings results. Very large enlarged stamps still cost more to preview at full quality; physical tablet feel remains a manual acceptance check. The development app is `build/ToonPro.app` (0.56.0).

Version **0.46.0** adds **Hold to shape** and **Arc**. Draw with a brush, pause with the pen down for 0.7 seconds, then lift to choose a fitted line, arc, circle, ellipse, rectangle or triangle from the suggestions. **Original** keeps your freehand stroke; Escape cancels. The wand beside the brush wrench toggles the gesture. The fit uses the current brush, pressure, texture, selection and symmetry, including editable brush strokes on vector layers. With **Shapes → Arc**, drag between endpoints, lift, move/hover to bend the curve, then tap or press Enter to finish. Existing four-point Bézier curves remain available. See [drawing gestures and limits](docs/drawing-tools.md#hold-to-shape). All 26 targeted regression suites pass; Arc and Hold to shape also pass native macOS checks. Physical Huion feel remains a manual acceptance check.

Version **0.45.1** adds straight strokes to the current Brush and raster Eraser. Hold **Command or Ctrl before dragging** to lock to the first clear horizontal or vertical movement. Hold **Shift before dragging** for a straight-line preview from initial contact to the pen's current position, at any angle. Release the pen to commit; Escape cancels. Each gesture is one Undo step. Pressure remains active, and drawing on vector layers keeps an editable brush stroke. See [straight brush strokes](docs/drawing-tools.md#straight-brush-strokes). Nine targeted regression suites pass; all 28 modifier cases pass offscreen and on native macOS. Physical Huion feel remains a manual acceptance check.

Version **0.45.0** adds brush tip sets with up to 16 grayscale or colored images, weighted random/ordered/shuffled selection, pressure/direction selection and draggable per-tip anchors. Open the brush wrench → **Tip shape** to add, remove or reorder images and edit their roots. Shuffle avoids adjacent repeats across cycles. Primary Brush/Eraser sizes now reach 1,024 px, including custom Brush Preset sizes. Seven existing grass/foliage defaults gain varied original tips; edited tips, settings and saved artwork retain their own data. See [brush controls and limits](docs/brush-settings.md#advanced-tips-and-quality-in-0450).

Version **0.44.2** adds a direct **Import image** icon to the Layers footer. Choose an image to create its own artwork layer, then move or resize it on the canvas and press Enter to apply positioning. Transparency is preserved; oversized images fit proportionally inside the output frame. The same import remains in the File menu. Playback pauses before the file chooser opens.

Version **0.44.1** improves large-brush strokes by reducing detailed cursor redraw work, avoiding duplicate continuous-texture calculations and speeding up ordinary brush compositing. The paired 256 px texture workload's display p95 fell from 309.24 to 32.75 ms at 2× scale, with identical artwork before and after. The most complex texture cases still exceed a 60 Hz frame budget in this synthetic full-window workload; physical tablet feel remains a separate check. See the [measurements and limits](docs/performance.md#large-brush-stroke-work--0441-29-october-2026). Brush size, texture detail, dynamics and delivered pen samples are retained.

Version 0.41.1 repairs pen cursor handoff from dock dividers to the canvas, including mask painting. Interrupted pen strokes finish at the last contact point instead of drawing through hover. Drawing-time display work is batched: layer-tile discovery runs once per delivered batch/display pass, and exact textured cursor outlines are traced when painted. Pen positions, pressure, brush rendering and Undo are retained; pressure curves and the brush collection are unchanged.

The drawing workspace uses charcoal-and-silver chrome and compact Toolbox, Tools, brush quick properties, Layers and Colors. Quick properties align label, slider and exact value on one row, wrapping only when needed. The Toolbox is 44 pixels wide and layer rows are 36 pixels tall. Palette management sits in Colors’ overflow menu, while the picker and Add Swatch stay directly available. Version 0.37 extends responsive rows to selection/transform, fill, gradient, shape, text, vector and guide properties, plus Navigator. Pixel-transform Apply/Cancel stays pinned below the scrolling controls. Short dock labels keep the full panel names and active tool context in delayed help.

Version 0.38 compacts the advanced brush dialog and Brush/Tool Management. All eleven brush categories retain their visual editors, with responsive label/editor rows, smaller actions and a pinned Close button. Managers use compact lists and fields, pinned search/actions, stable feedback and scrolling details. Brush type/category remain visible for the selection; tags and complete names stay available in help. New workspaces open both managers at 360 × 600 pixels; saved layouts are retained. See [brush settings and management](docs/brush-settings.md).

Version 0.39 compacts References and Effects. References keeps Import, Remove and Fit above an image-only thumbnail strip, with a stable selected-image caption and inline Zoom controls. Effects uses responsive property rows, smaller category tabs and actions, and a scrolling body sized to the current category. Each effect category remembers its scroll position while the panel stays open. Shadow's offset pad and the mask preview remain available. Dock placement and saved layouts are retained.

Version 0.40 compacts the 3D Library and Object/Camera properties. The library combines tags, search and favorites on one line, with smaller asset rows and stable feedback. Object/Camera keeps the object list, category tabs, movement modes and framing/reset actions visible while the current settings page scrolls. Transform and camera values use responsive inline rows and retain unfinished entries through resize and render updates. New workspaces open the library at 320 × 480 pixels; saved layouts are retained.

Version 0.41 compacts Rig, Weights and Pose. Rest/Weights/Pose tabs and skeleton flags stay above the scrolling settings; Front/Side/Top views stay below. The joint hierarchy uses fewer visible rows in short docks, and each mode remembers its scroll position. Rest coordinates, weight-brush controls and pose values use responsive inline rows. Weights keeps its heat legend and temporary bend check; Pose keeps direct FK/IK, Target/Pole, limits, pinning and reset controls. Hidden mode pages no longer force extra scrolling. Saved layouts and existing edit/Undo behavior are retained.

To begin animating, open **Panels → Timeline**, click **+**, choose **Frame by frame**, and select a drawing template. **Frame Properties → Drawing** controls drawing name, starting frame and hold length. To animate existing artwork, select a layer and open **Frame Properties → Animation**, enable a property diamond, move the playhead and change the value.

- Create a movie project with a live production-sheet preview. Open **Panels → Shots** to create, duplicate, rename, reorder or delete shots, assign scene names, and filter the movie list. Each shot retains its own artwork, timing, audio and cameras.
- Animate multilayer raster and vector drawings with static backgrounds, explicit holds, blank/duplicate drawings, onion skin, frame/drawing stepping and FPS-based looping playback.
- Use separate Timeline and Frame Properties panels; start with Simple, Rough or Anime, customize the drawing-layer recipe, and save it to a global template library. Export an output-sized PNG sequence. See [drawing and property animation](docs/animation.md).
- Use **Ripple time** for explicit shot-wide Insert/Delete/Move operations across drawings, layer/path keys, cameras and audio. Ordinary exposure/key dragging keeps its local behavior.
- Configure onion skin with 0–8 distinct drawings before/after, separate colors and shared opacity; accepted settings persist in the workspace.
- Animate layer/folder position, scale, rotation and opacity with automatic keys on enabled channels, direct canvas handles, movable timeline diamonds and five interpolation modes.
- Select multiple timeline keys, copy/paste their timing and values, move them together, and drag or trim drawing exposure blocks. Use a selected playback range to rehearse part of a shot.
- Edit motion paths directly on the canvas: drag position-key markers or create a position key at the playhead. Place the shared rotation/scale pivot with a canvas handle or exact X/Y controls, keeping the current frame in place.
- Shape acceleration with an editable easing graph, bend motion paths using canvas tangent handles, and enable Follow path with a Rotation offset. Paired position keys stay together through timing and clipboard edits. See [advanced motion controls](docs/animation.md#advanced-motion-controls-in-0280).
- Import audio as waveform tracks; trim clips, edit fade handles, crossfade overlaps and animate volume in Frame Properties → Audio. Optional audible scrubbing stops on release. Playback, MP4 and standalone shot/movie WAV export share the audio mix. See [audio finishing](docs/animation.md#audio-finishing-in-0330).
- Animate a 2D shot camera with position, uniform zoom and rotation, plus the existing 3D reference camera, from **Frame Properties → Camera**. Edit complete-pose keys, easing and timeline timing; frame the shot directly on the canvas. Workspace navigation remains independent.
- Export the current shot, the whole movie, or a queue of separate shot files as MP4 with mixed audio from **File → Export video MP4**. Choose an inclusive frame range, output size, quality and audio inclusion; queue entries retain individual settings. MP4 uses an opaque white background; PNG retains its existing transparency options.
- Morph matching vector paths through node/handle, transform, width, color and opacity keys while retaining their sampled brush source. Toggle Source artwork to paint the original drawing.
- Configure output dimensions, aspect lock, FPS, duration in frames, title, cut number and name.
- Set extended drawing space, independent paper margins, header height, and a safe-area guide.
- Open/save a versioned `.toonproj` file with atomic replacement. Save compresses a snapshot off the drawing thread with progress/cancellation; automatic recovery runs in the background. Undo and project changes invalidate stale recovery, and edits made during saving remain unsaved.
- Preserve delivered pen/mouse movement samples with native macOS event coalescing disabled at startup; Qt input compression is disabled separately.
- Prioritize live brush strokes: update canvas artwork while keeping panel/button states stable; refresh layer thumbnails and other document panels after pen-up or cancellation.
- Draw with round or sampled brushes, including pressure and native dynamics, on raster layers or editable vector strokes.
- Use Path Edit to move/resize/rotate strokes, edit nodes, color, width and opacity; simplify paths globally or with a brush, and adjust local width with handles or a thicken/thin brush.
- Use Width and Simplify brushes on captured symmetry copies; pick sampled strokes through their painted coverage, including transparent stamp holes. Reuse cached paint from unchanged vector strokes when editing earlier strokes.
- Choose individual brush tools, duplicate them with independent settings, and organize tools in custom groups or standalone Toolbox entries.
- Tag tools and groups, filter Tools by tag or search, and switch its named list to an icon-only grid. Filtering leaves the active drawing tool unchanged.
- Open Tool Management to create groups, assign their tools, batch-add/remove tags, set organizational colors, and choose built-in or imported group icons. These edits have separate organization Undo/Redo.
- Open **Panels → Brush Management** (or **Tools ⋯ → Manage brushes**) to create, import, duplicate, rename and delete brushes, edit tags/categories in bulk, filter the library, and assign brushes to tool groups. Deletion removes all tool placements and supports organization Undo.
- Start with Pen, Ink, Soft, Spray, Stamp, Colored stamp or Custom settings, or copy the current brush. Image stamps can use the foreground color or retain their original colors and transparency on raster and vector layers.
- Choose from 219 built-in brushes: 19 familiar starters and 200 anime/environment recipes. Filter Brush Management by category, **Default**, **Dynamic**, or subject tags. Dynamic identifies deliberate randomness or scatter; ordinary pressure-only ink stays predictable. Existing brush edits and deletions are preserved.
- Import supported Photoshop ABR sampled and computed round brushes with real preset names, verified geometry/dynamics mappings and per-brush compatibility reports. Unsupported Photoshop engine features are reported; source files remain unchanged.
- Edit hardness, stroke opacity independently of flow, blend mode, tip angle/roundness/flips, shape dynamics, scattering, texture, dual tips, color dynamics, transfer and manual pen pose.
- Shape size, angle, roundness, opacity, flow and texture depth with editable response graphs. Use pressure, tilt, supported pen rotation, stylus-wheel input or fade; choose random variation per stamp or per stroke and new or fixed seeds. Paint with symmetry guides, noise, wet edges and stationary airbrush build-up.
- Reuse prepared brush tips and redraw affected canvas regions while painting with AA enabled; live strokes avoid refreshing unchanged tool properties. Brush detail, spacing and particle counts are preserved.
- Choose color, size and flow; save named color palettes and swatches globally. Manage layer names, visibility, painting locks, opacity, ordering and separate Notes layers.
- Organize layers in nested groups, apply clipping masks for shadows/highlights, and lock transparency while recoloring.
- Open **Panels → Effects** for persistent paintable masks, blur, shadow, glow and color correction. Add a color-adjustment layer to affect lower siblings while keeping the original artwork editable. See [the Effects workflow](docs/layers.md#effects-and-masks-in-0270).
- Use a compact layer panel with live thumbnails, inline names, visibility sweeps, multi-selection, drag-and-drop folders, slider-and-entry opacity, 12 blend modes, position locks and row colors.
- Duplicate, group or delete selected layers in one undo step; merge compatible layers or flatten compatible artwork/notes stacks while preserving their export behavior.
- Sample artwork or reference-image colors, bucket-fill contiguous colors with tolerance/gap closing, and use active, visible or reference layers to find boundaries.
- Recover adjacent antialiased fill edges while preserving solid ink boundaries; keep Deselect available for empty selections and use Escape to clear idle selections. Dense Color Range selections use bounded contour processing and cached border display.
- Draw linear/radial gradients with editable color stops and raster lines, rectangles, ellipses, polygons and cubic curves with fill/stroke controls.
- Select with rectangle, ellipse, freehand or polygonal lasso, magic wand and color range; soften edges and save named selections with the project.
- Smudge, mix color, blur, sharpen, dodge and burn with pressure-aware retouch strokes and one undo per gesture.
- Cut/copy/paste, paste into a new layer, duplicate selected pixels, or import an image into the artwork and position it.
- Use the standalone Text tool to place and edit multiline Unicode text directly on the canvas. Double-click to reopen text; font/layout changes remain editable with Undo. Preserve vector/text sources and rasterize explicitly when required.
- Move/resize/rotate selected pixels and use flips, skew, perspective/distort corners, a 3×3 warp mesh, grouped/multiple-layer transforms or selection-boundary-only transforms with Apply/Cancel and undo.
- Move transform previews beyond the paper and bring them back before Apply. Apply crops moved raster pixels to the drawable extent; Undo restores them. Switching away from a changed transform offers Apply / Discard / Cancel. Editable vector/text source geometry is retained.
- Focus on selections with animated dashed borders, diagonal shading outside, and a floating action bar. Add/subtract/intersect areas, fill/clear pixels, expand/contract borders, or pin/hide the bar.
- Add multiple perspective grids, vanishing points, two-point and three-point perspective guides, fisheye grids and parallel rulers; edit their handles, density, colors, limits, visibility, locks and snapping through Tool Properties.
- Snap brush/eraser stroke paths to enabled straight or curved guides; save reusable native `.toonguides` sets independently of artwork.
- Keep size, opacity, flow, hardness, smoothing and pressure-size control in the brush panel. Open its wrench button for eleven advanced categories, with tip, spacing, scatter, pen-pose, symmetry and texture widgets.
- Adjust exposed numeric settings with a visible slider and a compact 48-pixel manual-entry box. Units stay outside the box; larger values and decimal precision remain supported. Visual brush editors and canvas handles stay available.
- Pick foreground/background colors directly in an HSV field, swap colors, or enter a Hex value.
- Preview brush/eraser coverage with a tip-shaped outline that follows the stamp, angle, roundness, view and pressure; ordinary round brushes retain a circle.
- See a forbidden cursor and a bottom-bar reason when the current tool cannot act on the layer or selection. Blocked clicks and runtime failures receive a brief, nonmodal alert; panning and temporary color sampling remain available.
- Smooth mouse and tablet strokes with adjustable stabilization; use 0 for direct drawing.
- Import PNG, JPEG and BMP references into a separate panel with thumbnails, pan, zoom and fit controls; embed copies in project saves and recovery snapshots.
- Select a Toolbox group to see its tools, or a standalone entry to see exactly one tool. Add, duplicate, rename, remove and reorder placements without changing artwork.
- Use compact panels with proportional Lucide icons, consistently sized tool symbols and aligned labels, accessible names and 600 ms tooltips, with shortcuts where available.
- Work in a shared Adobe Spectrum-inspired dark gray theme with restrained blue selection and focus states.
- Undo/redo strokes and layer/property edits; save raster tiles together with the project.
- Recover unsaved work from a 30-second recovery snapshot.
- View the production sheet with metadata outside the movie frame.
- Pan, zoom, rotate the working view, and fit the sheet from the canvas or Navigator.
- Toggle **AA** in the header (or **View → Canvas smoothing**) for smooth or nearest-pixel canvas display. The setting starts enabled, follows every artwork layer and tool, also updates Navigator, and is remembered locally. Artwork, drawing behavior and exports remain unchanged; switching it off does not remove softness already painted into pixels.
- Use a fixed-width, full-height Toolbox icon column on the left or right. Tools and the other normal panels remain resizable, dockable, tabbed and detachable. Show/hide both types from Panels, restore the last layout or reset the workspace.
- Open the **3D Library**, place a starter cube, sphere, ground plane, static mannequin or simple building, and import supported GLB/glTF models into a persistent global library.
- Search, tag and favorite library models; place independent instances into one shared 3D reference scene per shot. Move/rotate/scale, duplicate, rename, hide and lock models; orbit/pan/dolly the camera, switch perspective/orthographic projection and lock the drawing camera.
- Adjust model and camera properties without the controls or scroll position shifting as the 3D preview updates; rendering feedback uses a reserved status line.
- Keep a visible move cursor for 3D Object and a hand cursor for 3D Camera over the canvas, with disabled-action feedback for locked or hidden scene layers.
- Use **3D Rig** to create a root skeleton or a 22-joint humanoid template on a model instance. Add, rename, reparent, move and delete joint branches; mirror branches and enable paired position edits.
- Arrange rest joints directly on the canvas or with precise XYZ controls, with Front/Side/Top views, one Undo per drag and Escape to cancel. Skeleton edits reuse the existing model preview and are saved with the project.
- Switch **3D Rig → Weights** to bind a model to its rest skeleton. Automatic binding runs in the background and assigns up to four normalized joint influences per vertex from distances to joints and bones.
- Select a joint in the shared hierarchy and paint its influence with Add, Subtract or Smooth, a screen-space radius, strength and pen pressure. The heat legend shows zero-to-full influence; painting affects front-visible vertices and supports Undo/Escape.
- Use the temporary **Bend check** with X/Y/Z angle controls to inspect deformation, then reset it to resume painting. Bend checks and brush settings stay outside project history and saved poses. Unbind to change rest joints; binding and weights belong to each model instance and persist with the project.
- Switch **3D Rig → Pose** to rotate bound joints with FK rings or XYZ controls, move the whole skeleton from a root, and use two-bone IK endpoint and pole handles. Pin an endpoint within the model, set per-axis joint limits, mirror the whole pose, or reset selected/all joints. Each instance's current pose saves with the project; a reusable pose library remains deferred.
- Draw on artwork layers above the cached GPU-rendered 3D reference. Static scenes do not render continuously while painting. Project saves embed models independently of the source files and global library.
- Export a production-sheet PNG or an output-sized movie-frame PNG, independently of the working view. 3D references are excluded from both exports.

Toolbox, Tools, Tool Management, Brush Management, Tool Properties, Layers, Colors, Navigator, image References, 3D Library, Shots, Timeline and Frame Properties now work. Effects provides masks, blur, shadow, glow and color correction. Shot management, whole-movie export and queued shot exports are available; interactive playback remains shot-based. Animated 3D models/rigs, multi-channel value/speed graphs and reusable rig/pose library assets are not implemented yet. Starter models arrive without a rig or binding; create a skeleton and bind an instance to pose it. The advanced brush controls are native Toon Pro behaviors. Modern ABR import maps supported names, sampled/computed round tips, geometry and selected dynamics; textures, dual brushes and full Photoshop rendering remain unsupported. See [drawing tools and limits](docs/drawing-tools.md), [layer controls](docs/layers.md), [brush settings](docs/brush-settings.md) and the [import compatibility guide](docs/brush-compatibility.md).

The [prioritized development checklist](docs/TODO.md) records completed work and remaining validation. Shot management, movie export, audio finishing, selected ABR descriptor mappings and background snapshot saves are implemented. Physical tablet and Windows acceptance remain separate from the automated and macOS benchmark results. See [the development plan](docs/DEVELOPMENT.md), [architecture](docs/architecture.md), and [agreed product requirements](docs/requirements.md).

Current focus is the combined drawing-exposure and property-keyframe workflow, including compatible vector path morphs, while preserving drawing responsiveness and source editability. Version 0.8.0 applies the artist's [UI rules](docs/ui-guidelines.md): direct icon actions, delayed help, tabs for feature switching, visual controls for complex settings and shared theme tokens. Necessary names, units and commit decisions retain text.

Version 0.11.0 compacts [Layers](docs/layers.md) into a pinned properties bar, 44-pixel thumbnail rows and a bottom action bar. Double-click a name to rename, sweep down the eye column, and drag selected rows between layers or into folders. Shift selects a range; Ctrl/Cmd selects separate rows. Right-click for merge, flatten and row colors. Version 0.12 adds working vector/text layer creation, type badges and explicit rasterization to that panel.

At the 0.12.0 milestone, the [drawing tools](docs/drawing-tools.md) were expanded while animation was still deferred. New operations respect the relevant selection and layer locks; destructive raster edits remain undoable. Retouch mixing uses a native color blend, and mesh warping requires rasterizing editable objects first. These tools do not imply complete Photoshop or Clip Studio feature parity.

Version 0.13.0 replaces the old tool selectors with **Toolbox** and **Tools**. Toolbox is a fixed-width icon column; Tools is a normal resizable panel with names and brush previews. A shared Add dialog, searchable choices, brush previews, context actions and reversible ordering implement the [approved navigation flow](docs/requirements.md#raster-tool-navigation). The top bar keeps project and view commands. Brush imports and canvas shortcuts remained available; animation and dedicated text navigation were deferred at that milestone and have since been implemented. Version 0.14 added the Vector Editing group and shared brush rendering on vector layers.

Version 0.15 adds tags, organizational colors and **Tool Management**, available from **Panels → Tool Management** or **Tools → ⋯ → Manage tools**. Right-click a tool for quick tag assignment. The Tools tag selector and search share one compact row. The tag selector filters the current group; group tags match all members without replacing their own tags. Name search works together with the tag filter, and **Clear tool filters** restores a list with no matches. **Tools → ⋯ → Icons only** switches to a compact grid with delayed name tooltips. The icon-view choice and tag filter are remembered. Reordering uses the unfiltered named list.

In Tool Management, **Groups** provides direct creation, naming, comma-separated tags, color swatches, built-in/image icons and a membership chooser. **Tools** supports multiple selection for additive/removal tags and shared colors. Browsing and metadata edits keep the drawing tool unchanged. Group membership changes that require another active tool retain pending-transform confirmation. The manager starts hidden and floating so it does not widen the drawing workspace; it can be docked and its placement is saved.

Version 0.12.1 simplifies numeric adjustments throughout the workspace and project settings: a visible slider sits beside a compact field for exact entry. The existing visual widgets and canvas handles stay available. Layer opacity has its own paired-control row beneath blend mode, and dragging it still commits one undo operation. Enter commits the typed number without activating a dialog's Create/Apply button; Escape restores the value from before the edit. All 40 CTest suites pass, including 11 UI/interaction suites repeated at Retina scale.

Version 0.10.0 extends [perspective guides](docs/guides.md) with three-point perspective and fisheye grids, bringing the total to six assistant types. Tool Properties follows Krita's drawing-assistant workflow. Choose a guide in **Toolbox → Guides → Tools**, click **Add** in Tool Properties, then place its handles on the canvas. Repeat to combine multiple perspectives. Enable **Snap to guides** in the guide properties’ All guides tab for straight-line or curved drawing assistance. These are native Toon Pro guides; they do not implement every Krita assistant or import Krita files.

For correcting drawings, see [selection and transform controls](docs/selections.md). Use **M** for Rectangle, **L** for Lasso and **T** for Transform while the canvas has focus. Tool Properties switches to selection actions and numeric transform controls. Enter applies and Escape cancels a transform on the canvas; the panel has Apply/Cancel buttons too.

Version 0.7.1 caches selection masks and layer composites. Transform dragging uses a viewport-sized preview; full-resolution resampling happens on Apply. The build commands below enable compiler optimization for interactive drawing.

Version 0.7.2 adds selection visuals and contextual actions. Animated borders reuse cached viewport artwork; shading and the action bar remain outside project pixels and exports.

Version 0.16 starts the approved 3D plan with **static drawing references**. Open **Panels → 3D Library**, choose **Starter** or import a GLB/glTF into **My Assets**, and double-click a model or use **+** to place it. Select **3D Object** or **3D Camera** in Tools to edit it through the canvas and Tool Properties. Use an artwork layer above the reference scene to draw. Camera changes are project edits; ordinary canvas pan/zoom/rotation still affect only the working view. The first model creates the scene; all later models share its camera and depth buffer. See [3D scope and limits](docs/requirements.md#3d-reference-milestone--0160).

Version 0.17 introduced **step 2: rest-skeleton editing**. Select a model, choose **3D Rig** in Tools or the Rig tab in Tool Properties, then use **Rest** to create a root skeleton or humanoid template. Select joints in the tree or on the canvas, drag them into place, and use the parent selector to arrange the hierarchy. Double-click a joint name to rename it. Mirror an unpaired limb branch, or enable mirrored editing for existing pairs. Front/Side/Top switches the saved drawing camera to an orthographic view of the model.

Version 0.18 adds **step 3: binding and weight correction**. Open **Weights**, click the link icon to bind, select a joint and paint its influence. Smooth moves the selected influence toward the weighted mean of front-visible vertices under the brush. The temporary Bend check rotates the selected joint and its descendants for inspection; reset it before painting again. The scissors icon unbinds the instance and makes Rest editable again, with Undo available. Mesh detail limits the precision of vertex-based weight painting. See [the weight-painting workflow](docs/drawing-tools.md#skin-binding-and-weight-painting).

Version 0.19 adds **step 4: current-pose editing**. **Pose** shares the joint tree with Rest and Weights. FK offers a canvas rotation ring with clickable X/Y/Z axis labels and precise local rotations; dragging a root moves the body. IK provides endpoint/pole handles for a two-bone chain and optional model-local pinning. Per-axis limits constrain rotations, and direct actions mirror the whole pose or reset selected/all joints while keeping limits. The current pose is saved independently for each instance. Rest and Weights temporarily show the selected model at rest without erasing its saved pose. Imported skins, animated 3D models/rigs and reusable rig/pose library assets remain deferred. See [the posing workflow](docs/drawing-tools.md#current-pose-editing).

## Build and run

Requires a C++20 compiler, CMake 3.24+, and Qt 6.10+ with Core, Gui, Widgets, Svg, Concurrent, Multimedia and Test. The development SDK is pinned to **Qt 6.10.3**. Qt 6.8.3 did not link against this machine's macOS 26 SDK because it still references AGL; use the pinned SDK instead.

### macOS

With the project-local Qt SDK already downloaded in this workspace, install the pinned renderer SDK once on macOS arm64:

```sh
./scripts/setup_filament.sh
cmake -S . -B build -DCMAKE_PREFIX_PATH="$PWD/.deps/Qt/6.10.3/macos" -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open build/ToonPro.app
```

To install the same SDK in a fresh checkout:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install aqtinstall==3.3.0
.venv/bin/python -m aqt install-qt mac desktop 6.10.3 clang_64 --outputdir .deps/Qt --modules qtmultimedia --archives qtbase qtsvg qtmultimedia
```

Python is only an SDK installation helper; the application itself is C++.

Existing local SDKs can add Multimedia with the same install command using `--modules qtmultimedia --archives qtmultimedia`. The module includes the FFmpeg backend used for decoding; retain its bundled runtime libraries.

### Windows

Install Visual Studio 2022 with Desktop development with C++, CMake, and Qt 6.10.3 for MSVC 2022 x64, including Qt SVG and Qt Multimedia. In PowerShell, replacing `C:/Qt` with your SDK location:

```powershell
$env:PATH = "C:\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
cmake -S . -B build -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64
cmake --build build --config RelWithDebInfo --parallel
ctest --test-dir build -C RelWithDebInfo --output-on-failure
.\build\RelWithDebInfo\ToonPro.exe
```

Windows support is a build target; it must be verified on Windows before being called supported in a release.

## First session

1. Choose **New movie project** and enter your title and first cut.
2. Configure the picture and paper while reviewing the preview. Duration is entered in frames with seconds displayed below it.
3. Choose the `.toonproj` save location and create the project.
4. The drawing panels open with the project. Choose a group in **Toolbox**, then a tool in **Tools**. A standalone Toolbox entry activates directly. Toolbox fills the workspace height on either side; drag its top grip to move it. Normal panels remain resizable, tabbed and detachable. **Drawing workspace** reopens the main panels.
5. Draw on Drawing 1. Use **B** for Brush, **E** for Eraser, or **H** for Hand while the canvas has focus. Choose color in Colors, a saved size in Brush Preset, or size/flow in Tool Properties. Use Layers to add, reorder or edit layers. **Notes** layers are included on sheets and excluded from movie frames.
6. Use **File → Project & shot settings** to adjust metadata and paper. Save with Cmd/Ctrl+S.
7. Use the separate PNG export actions to inspect the sheet versus the movie frame. These exports include your artwork; movie frames exclude Notes layers and production-sheet guides.

Brush and eraser use a left drag. Hold Space and drag, use the middle mouse button, or select Hand to pan. The wheel zooms around the pointer. Escape cancels the current stroke. Cmd/Ctrl+Z undoes, and the Edit menu provides redo. Brush flow accumulates across dabs; layer opacity controls the entire layer. Cmd/Ctrl+0 fits the sheet; Cmd/Ctrl+1 sets 100% view scale. On high-DPI displays this scale is in Qt logical pixels, not a guaranteed physical screen-pixel mapping.

Use **Quick access → +** to keep favorite tools, colors and navigation at the top. Press **Tab** from the canvas for drawing mode, and press it again to return to your panels. Pins are workspace preferences shared across projects; closing the app in drawing mode preserves the previous normal layout for the next session.

Use **Panels → Brush Management** to organize the installed brushes. Click **+** for a new type, a stamp image or a copy of your current brush; the import icon also accepts supported Photoshop ABR packs. Select one or several brushes to edit categories/tags, add them to a tool group or delete them. Browsing keeps the drawing tool unchanged; double-click or use the brush icon to activate a selection. The wrench opens its advanced settings. Deleting removes the brush from every tool group without changing existing artwork; organization Undo restores it. Imported source presets remain cached locally. Colored stamps use their image colors, so foreground color and Color dynamics do not recolor them.

Filter by **Standard** for the new starter brushes. These are original native configurations for familiar drawing and painting families, with small locally generated tips and textures. In the wrench dialog, **Shape dynamics**, **Transfer** and **Texture** offer five-point response graphs and per-stroke jitter. **Behavior → Randomness** provides fixed seeds and a reseed action. The same captured settings replay on editable vector strokes. Rotation and tilt depend on pen capabilities; speed dynamics and wet-pigment simulation remain future work. See the [preset table and dynamics guide](docs/brush-settings.md#standard-starter-brushes).

**Smoothing** in Tool Properties ranges from 0–100. Higher values reduce small wobbles but add pen lag and round tight corners. The brush outline follows the filtered tip while drawing. Releasing joins the short remaining gap immediately, and the whole stroke remains one undo step. The setting applies to brush and eraser and is snapshotted for each stroke. Each tool retains this setting between sessions.

Use **Toolbox → Drawing** for Round Ink, Round Sketch, Round Blocking, imported brushes and Eraser. Each tool remembers its adjustments. Right-click **Duplicate** to make an independent copy, then **Rename** to name it. The **+** button opens the shared Add dialog for groups or standalone tools; adding an existing tool shares its settings across placements. **Remove** removes only the placement, and organization undo is available from each panel’s overflow/context menu or Cmd/Ctrl+Z while that panel has focus. Drag rows to reorder them. Colors remain shared. Spacing is a percentage of nominal brush size; a sampled tip’s size is its longest side and the pointer follows its coverage outline.

Tool Properties keeps drawing-time controls visible: size, opacity, flow, hardness, smoothing and a pressure-size toggle. Click the **wrench** for a modal **Advanced brush settings** dialog with General, Tip shape, Shape dynamics, Scattering, Texture, Dual brush, Color dynamics, Transfer, Brush pose, Symmetry and Behavior icon tabs. Changes apply live to the selected tool; Close (or Escape outside an unfinished numeric edit) returns to drawing and keeps committed changes. The last category remains remembered. Hidden advanced controls refresh when reopened. Visual widgets edit the real brush fields directly; each exposed scalar field also pairs a slider with compact numeric entry. Opacity caps the contribution of one stroke while flow controls deposition per stamp. Airbrush build-up adds stationary stamps only while a painting gesture is held; release, focus loss or cancellation stops it. Noise, wet edges, texture blending and curved symmetry use the native algorithms described in [brush settings](docs/brush-settings.md); exact Photoshop output equivalence is not claimed.

Use **Toolbox or Tools → … → Import Photoshop brushes…** for an `.abr` file. The importer supports ABR 1, 2, 6.1, 6.2, 10.1 and 10.2 with raw or PackBits-compressed 8-bit sampled tips. Supported modern descriptors map preset names, sample UUID associations, computed round tips, geometry and selected shape/opacity/flow/scatter dynamics into native settings. Unsupported or missing descriptor layouts retain supported sampled tips with reported native defaults. Textures, dual brushes, color dynamics and several other Photoshop engine features remain unsupported; this is not full Photoshop rendering parity. Files are bounded to 256 MiB, and large sampled tips are smoothly resized to at most 1024 pixels on the longest edge. The source library has a 64 MiB decoded-image budget; grayscale pixels count as one byte and colored pixels as four. Import reports and stored notes disclose mapping, resizing, limits and recognized unsupported features. Imported tools appear in Drawing when that starter group exists and remain available in Add Tools. Original ABR files remain unchanged.

The artist's Free Brush Stroke and Free Watercolor Washes 7 packs have been verified together: **30 tips imported**, library/tool-catalog reload passed, and each tip passed raster/vector painting and vector-project persistence checks. See [compatibility and limits](docs/brush-compatibility.md).

Open **Panels → References**, choose **Import**, and select an image. Click thumbnails to switch references; drag the preview to pan, scroll or use the numeric slider to zoom, and use the fit icon to recenter. Alt/Option-click a reference to sample its color; the Eyedropper tool also enables direct reference sampling. Import and removal support undo/redo. References belong to the shot and are excluded from sheet/movie exports. Imported copies are embedded, so moving or deleting the original does not break a saved project. Originals are never overwritten. Imports accept files up to 32 MiB / 16 million source pixels, with at most 16,384 pixels on either side; copies are reduced to a maximum of 2048 pixels per side while preserving aspect ratio. A project can contain up to 16 references and 16 million reference pixels combined.

The compact References layout keeps Import, Remove and Fit together above the thumbnail strip. Full names and dimensions are available in hover help and accessibility; the selected caption stays one line so long filenames do not resize the preview. Zoom uses the shared slider and compact exact field, and unfinished input survives panel resizing and artwork updates.

## Storage and limits

`.toonproj` is a versioned binary container: a bounded UTF-8 JSON manifest followed by lossless PNG artwork tiles, reference images, deduplicated vector-brush resources, embedded 3D model resources, deduplicated audio PCM and grayscale layer-mask tiles with SHA-256 checksums. Atomic replacement saves drawing tracks/templates/exposures, layer property channels, vector path poses, audio clip timing/volume/fades and gain keys, scene names and movie shot order, 2D/3D camera poses/keys, layer effects and their channels, independent mask placement/opacity/feather/inversion and channels, adjustment settings, custom easing, motion tangents and path orientation alongside metadata, layer hierarchy, blend modes, position locks, row colors, drawing guides, editable vector/text objects, named selections, model-instance rest skeletons, skin weights, current poses and pixels. A held drawing stores its artwork once. Scene schema 4 stores each instance's binding and pose, including body translation, joint rotations, limits and IK controls; temporary bend checks, playback state and weight-brush settings are not saved. Supported formats 1–36 are readable. Saving selects the applicable format for the features actually present: animated effect/mask channels or extended mask properties require 36, animation tracks nested inside ordinary folders require 35, Poses mode or retained Symbol pose keys requires 34, timeline cues require 33, explicit Symbol clip timing requires 32, and original Symbol definitions require 31. Projects without these features retain the earlier format appropriate to their artwork and metadata. Symbol definitions remain shared; each instance saves its own playback and pose schedule. See [Symbols](docs/symbols.md#content-saving-and-limits) for timing and pose limits. Path pose keys reference the original brush source rather than copying its images or dabs. Playhead, playback range, key/audio selection, key clipboard, Source mode, camera/layout view, Timeline Solo/collapse state and canvas navigation transforms remain transient. Reusable guide sets keep their independent version 1 schema. Keep independent backups while using this development preview.

Drawing guides belong to each shot and persist in saves, undo history and recovery snapshots. Limits are 64 guides per shot, 256 per project, and finite coordinates within ±1,000,000 artwork pixels. Reusable `.toonguides` files are bounded native JSON sets, limited to 1 MiB; they are separate from Krita's `.paintingassistant` format. Import adds guides with fresh identifiers and applies the saved global guide settings. Drawing guides and their handles never appear in sheet or movie-frame exports.

When a saved project has unsaved changes, a 30-second timer starts a background sibling `<project>.toonproj.recovery` snapshot between artwork gestures. Opening the original project offers a newer valid snapshot for recovery. Saving or explicitly discarding removes the owned snapshot. Undo/Redo, project transitions and invalidated recovery cancel stale jobs before publication. The most recent interval and an active stroke may be absent after a crash. Explicit Save and recovery compress/write immutable snapshots on a worker; publication is atomic. Explicit Save retains a progress/cancel dialog and only marks the document clean if its generation and revision still match the completed snapshot.

Panel placement is local to the machine and stored in Qt's application configuration directory as `workspace.ini`. No login or network service is used at runtime.

Imported brush templates and older custom presets remain in `brushes.json` beside `workspace.ini`, independently of projects. Saves use version 5 for tip sets/larger sizes, version 4 for extended dynamics, version 3 for colored tips, or version 2 otherwise; versions 1–5 are readable. Tip, texture and dual-tip images are embedded and provenance is retained. Version 1 brush libraries migrate in memory on load and upgrade when edited; 0.4 cannot open the new library format. A failed library load disables writes to protect the existing file. Painted artwork remains portable in `.toonproj` files without the originating brush library or ABR file. Back up the brush library separately to move editable presets to another machine.

Tool definitions, per-tool settings and Toolbox placements are atomically saved in `tools.json` beside those files, using version 10, or 11 for dynamic stamps and 12 for connected/surface brushes and advanced dynamics. Versions 1–12 are readable without resetting organization. Earlier catalogs gain the standalone Text tool once when capacity permits; later removal is respected. Version 1 also gains Vector Editing once, while later removal of that group remains respected. Version 3 added tags, label colors and embedded group icons; version 4 added 3D Object and 3D Camera navigation, version 5 added Rig, and version 6 added brush types/categories, colored tips and deleted-preset records. Missing source presets are reconciled without replacing custom organization, subject to catalog limits. Duplicate tools retain their labels and brush resources independently. Organization history is separate from artwork and session-local, keeping up to 100 actions. Metadata/name Undo preserves the current drawing tool; importing new brush definitions starts fresh organization history. Per-tool setting changes do not enter that history. The catalog allows 512 tools, 128 Toolbox entries, 64 MiB of decoded images and a 128 MiB file. Grayscale pixels count as one byte and RGBA pixels as four. Tags allow up to 32 names per item, each up to 40 characters without commas/control characters; matching is case-insensitive. Group icon uploads accept PNG/JPEG/WebP up to 8 MiB and 4096 × 4096 pixels, fitted proportionally to at most 256 × 256 with transparency retained. Corrupt or newer unsupported catalogs are preserved and cannot be overwritten in that session. Organization edits are disabled; fallback tools remain usable with temporary settings. Back up `tools.json` and `brushes.json` together. The column workspace still uses `layoutV3`; existing `layoutV2` arrangements migrate while leaving older keys intact.

Version 0.42.1 adds validated built-in recipe revision metadata to canonical catalog tools. The one-time correction merges only untouched setting fields/resources and refreshes the active brush and preview caches after validating the full candidate. Copies/imports are excluded; newer recipe revisions are retained. A successful correction starts fresh organization history, while artwork history and saved vector brush captures remain unchanged.

Icons are embedded [Lucide SVG assets](https://lucide.dev/guide/static), rendered with Qt SVG. Their [license and provenance](resources/icons/lucide/NOTICE.md) are retained in the repository and application resources.

Prototype limits: 64–8192 px per output dimension, a 64-megapixel sheet budget, 1–120 FPS, 1–1,000,000 frames per shot, and sRGB 8-bit output. Header height is a minimum and expands for wrapped metadata. Sheet PNGs include production-sheet boundaries and enabled safe-area marks; a configurable production-mark export option is still pending. Perspective drawing guides and 3D reference scenes are excluded from both exports. Movie PNGs composite visible drawing layers over the configured white or transparent background, with no Notes, header or editor guides. Pixels are stored relative to the output frame, so changing margins/header does not move them; changing output dimensions reframes without resampling.

Raster storage uses 256 × 256 tiles allocated as touched, with at most 1024 tiles per project (256 MiB of decoded pixels), 64 layers and groups combined per shot, eight nested group levels and a 300 MB container limit. History keeps up to 50 edits; when its conservative 64 MiB estimate is exceeded, older history is cleared before retaining the newest edit. One large edit can exceed that history estimate. Fully erased tiles are retained in this prototype. These are development limits, not movie-scale performance targets.

Vector projects retain up to 65,536 captured dabs per stroke and 262,144 per project. Shared grayscale brush resources are bounded to 2,048 pixels per side and 16 million pixels total, with at most 1,024 distinct images. Version 8 allows a 32 MiB manifest for editable source records.

Bucket/wand/color-range operations use a bounded 32-million-pixel working region. Feathering is a cached box-softened edge from 0–100 px; gap closing uses a bounded 0–20 px morphological radius. Projects support 64 named selections per shot. Object and advanced-transform limits are documented in [drawing tools](docs/drawing-tools.md).

The global 3D library stores up to 128 imported model copies independently of project history, with separate 256 MiB limits for stored assets and decoded geometry/textures. Removing an entry does not remove placed instances or embedded project resources. Scene resources allow up to 128 instances and 64 model assets, at most one scene per shot; project-wide model geometry and textures have a 256 MiB decoded budget, within the existing 300 MiB file limit. Each imported model allows 256 meshes, one million vertices/triangles, 128 MiB of source/serialized data and 16 million texture pixels; textures are bounded to 4096 pixels per side. Rendering is supported by the pinned Filament 1.78.0 macOS arm64 Metal build; unsupported builds report that rendering is unavailable. The material source and compiled package live under `resources/scene3d/`, alongside bundled [Filament license](resources/scene3d/licenses/Filament-LICENSE.txt) and [third-party notices](resources/scene3d/licenses/Filament-THIRD-PARTY-NOTICES.txt).

## Verification

The 0.19 posing panel suite passes FK/IK controls, limits, mirroring/reset, locks, one-gesture history, pending entry and narrow layout checks. Five unfinished-field Save/reload cases and three Undo/Escape cancellation cases also pass in native Cocoa windows. FK and IK panels were visually checked at normal/Retina scale and widths of 240/280 logical pixels, with no overlapping controls or horizontal scrolling. Evidence: `build/pose-panel-{tests,native,preview-1x,preview-2x}.log` and `build/test-artifacts/pose-panel-{1x,2x}/`. The optimized macOS arm64 **0.19.0** build is warning-free; all **71 CTest entries pass**, including native startup and both local ABR packs. **Fifteen affected suites also pass at Retina scale**, and final native Cocoa unfinished-entry Save/reload and Undo/Escape checks pass. Coverage includes pose persistence/migration, instance independence, bone lengths and limits, nested IK controls, pin release/reset, synthetic tablet contact loss, posed-object picking, rest/heat-preview isolation, changed weights and renderer clipping. The actual starter mannequin was bound and posed with a bent elbow and knee, then visually checked in front and perspective renders. Evidence: `build/pose-{build,tests,retina}-final.log`, `build/pose-panel-native-final.log`, `build/pose-mannequin-preview.log` and `build/test-artifacts/pose-mannequin/`. Physical Huion interaction and non-macOS rendering remain unverified.

The previous **0.18.0 optimized macOS arm64 build was warning-free; all 68 CTest entries passed**, including native startup and both local ABR packs. **Twelve affected suites passed again at Retina scale.** Checks covered binding, normalized weights, visible-surface picking, painting/Undo/Escape, instance independence, cross-shot memory limits, format-11 storage and format-10 migration, transient deformation, preview isolation, and stable narrow panels. Three pending-entry Save cases also passed with native Cocoa windows. Normal/Retina panels and canvas previews were visually reviewed. A 495,616-vertex benchmark measured 66 ms surface preparation, 6 ms for 1,000 local brush queries and 304 µs for ten revisioned render-request submissions; these are component timings, not tablet latency or GPU frame-rate guarantees. Evidence: `build/skin-{build,tests,retina}-final.log`, `build/skin-panel-native.log`, `build/skin-view-benchmark.log` and `build/test-artifacts/skin-*`. Physical Huion interaction and non-macOS execution remain unverified.

The previous **0.17.0 optimized macOS build was warning-free; all 64 CTest entries passed**, including local ABR imports through temporary libraries and native workspace startup. **Ten affected suites also passed at Retina scale.** Rig coverage included hierarchy/mirror validation, transformed-model joint dragging, mouse/tablet event handling, locks, instance independence, cache reuse, export exclusion, project-v10 round trips and older-file migration. Save/reopen, Undo and Escape with unfinished numeric/name edits also passed in native Cocoa windows. Normal/Retina rig panels and front/perspective canvas previews were visually reviewed. Evidence: `build/rig-{build,tests,retina}-final.log`, `build/rig-editor-native-tests.log` and `build/test-artifacts/rig-final-{1x,2x}/`. Physical Huion interaction and non-macOS execution remain unverified.

The previous 0.16.0 checks added real Metal offscreen rendering (transparent background, depth ordering, model movement, UV orientation, masked textures, unchanged-scene reuse, coalesced edits, invalid-input recovery and safe shutdown), 3D model import/library tests, project v9 round trips/corruption checks, and drawing-workspace integration. All **61 CTest entries** and **nine affected suites at Retina scale** passed. Evidence and limits are recorded in [the development log](docs/DEVELOPMENT.md). The reference-renderer preview is `build/test-artifacts/scene3d/scene3d-starter-models.png`.

The previous **0.15.1 macOS build passed all 57 CTest entries**, including compact brush controls, live advanced settings, dialog close/focus behavior, hidden-library refresh, per-tool persistence, catalog organization, pending-transform protection, drawing engines and native Cocoa startup. Both local Photoshop packs pass import and raster/vector checks for all 30 tips through temporary libraries. Nine affected suites pass again at Retina scale: brush settings/widgets/presets, controls, references, UI theme, foundation, selection properties and tool session. Build logs contain no compiler warnings. Normal/Retina visual checks cover the narrow drawing panel and advanced categories. Evidence is in `build/brush-properties-build-final.log`, `build/brush-properties-tests-final.log`, `build/brush-properties-retina-final.log` and `build/test-artifacts/brush-properties-{1x,2x}/`. Physical Huion interaction and Windows execution remain unverified.

A synthetic 512 × 512 textured Color Range selection with 177,311 path elements decreased from **9.53 seconds to 44 ms** including its overlay at standard display scale. The updated build measures **272 ms** at Retina scale; Invert takes **352 ms** and Deselect followed by drawing takes **6.4 ms** there. These are isolated workspace measurements, not guarantees for every artwork. Detailed mesh selections above 4,096 elements are rejected safely for Warp; Perspective Transform and Deselect remain available. Evidence: `build/fill-selection-benchmark-before.log` and `build/fill-selection-benchmark-final.log`.

In the live brush benchmark at Retina scale with AA enabled, median redraw/capture time decreased from **16.55 to 10.92 ms** for the 6 px particle brush and **23.54 to 17.06 ms** for a 128 px sampled/texture brush. The four comparable model/workspace workloads produce identical artwork hashes before and after the change. An additional particle run with stabilization 10 measures **1.68 ms** median input processing and **11.23 ms** redraw/capture. These timings include a forced workspace capture and do not measure physical tablet latency. AA-off retains full artwork redraws to preserve nearest-pixel display. Details and evidence are in the [development log](docs/DEVELOPMENT.md#live-brush-responsiveness--0144-16-october-2026).

In the isolated 48-stroke, 1920 × 1080 benchmark, editing the first stroke now takes a median **8.06 ms** per subsequent preview with round brushes (previously **81.59 ms**) and **7.70 ms** with sampled/texture/dual brushes (previously **152.71 ms**). These are synthetic model-edit measurements, not complete pen-to-screen latency or guarantees for all projects. The bounded replay cache and remaining limits are described in [drawing tools](docs/drawing-tools.md). Logs: `build/vector-refinements-benchmark-{before,after}.log`.

`toon_tool_catalog_tests`, `toon_tool_session_tests` and `toon_tool_navigation_panel_tests` cover shared placements and independent duplicates, organization undo, empty groups and removal fallbacks, per-tool restoration, shortcuts, Add-dialog behavior, local catalog round trips and damaged-file protection. Current build results are recorded in the development log.

`toon_tests` exercises setup/persistence, geometry, PNG exports, navigation and workspace restoration. `toon_drawing_tests` covers real painted pixels, undo/redo, tile seams at fractional zoom, layer compositing/Notes, pressure events, transformed mouse input, binary round trips, corruption rejection, migration, memory limits and recovery. It also saves rendered UI images under `build/test-artifacts/` for visual review.

`toon_color_range_tests` covers exact contour topology, holes and diagonal contacts, fragmented textures, pixel-accurate boolean operations, rounded expand/contract and atomic complexity/working-area rejection. `toon_raster_tools_tests` covers antialiased boundary recovery on transparent/opaque/partially transparent backgrounds, reference-layer fills, bucket tolerance/gap closing, sample sources, contiguous/global selection, gradient stops, raster shapes, alpha locks, feathered brush/fill/clear/transform coverage, saved-selection round trips, all six retouch modes and cancellation. `toon_selection_extras_tests` covers compact controls, named selections and reference color sampling without navigation or history changes. Object, clipboard, advanced-transform and canvas interaction coverage is described in [drawing tools](docs/drawing-tools.md).

The `.app` produced by a regular build depends on the local Qt SDK. Packaging, signing and distribution are a later milestone.

`toon_ui_theme_tests` checks theme tokens, tooltip timing/help coverage, icon rendering and help suppression during interaction. `toon_brush_widgets_tests` checks direct spatial editing and exact-field synchronization; `toon_color_panel_tests` checks HSV/Hex edits, foreground/background selection, swaps and keyboard interaction.

`toon_controls_tests` checks slider/numeric synchronization, bounded values, keyboard input, one undo edit per opacity drag, and vector icons at high DPI. `toon_cursor_tests` checks coverage under zoom/rotation/pressure, hand-mode transitions, and exclusion of the cursor from exports.

`toon_reference_storage_tests` checks embedded-image persistence, legacy migration, corruption and resource limits. `toon_reference_tests` exercises import, undo/removal, portable saves, recovery and independent reference navigation, and renders `references.png`. `toon_stabilizer_tests` checks jitter reduction, event-density independence, pressure/cursor alignment, gesture endpoints, undo, cancellation and tool/focus transitions.

`toon_abr_tests` checks supported ABR records, raw/PackBits samples, coverage polarity, malformed inputs and limits. `toon_brush_library_tests` checks preset persistence and failed-write/load protection. `toon_brush_engine_tests` checks sampled-tip pixels, pressure, erasing, spacing, undo and tile behavior. `toon_brush_panel_tests` retains coverage of the legacy preset/import component, including import-to-paint, preset application/save/reload, provenance, cursor coverage and artwork independence from the library. Its workspace test exercises import and duplication through the current Toolbox/Tools panels and saves `build/test-artifacts/brush-library.png`.

`toon_brush_advanced_tests` covers native effects, dynamics, stroke blending/opacity and symmetry. `toon_brush_settings_panel_tests` covers the compact drawing panel, advanced modal/live synchronization, close and numeric-entry behavior, tool-specific persistence, image/tip selection and category layouts. Cursor tests also exercise asymmetric contours, tip transforms, texture protection and timer lifecycle. Synthetic pen input does not establish hardware support for tilt, rotation or wheel data on a particular tablet.

`toon_layer_document_tests`, `toon_layer_rendering_tests`, `toon_layer_storage_tests` and `toon_layer_panel_tests` cover batch editing and history, inherited locks, layer blending and transform-preview parity, safe merge/flatten, clipped partial transparency, sparse tile seams, persistence/migration, live thumbnails and compact panel interactions. `toon_scrub_value_tests` covers drag, exact entry, cancellation and interrupted gestures. Generated visual checks are `layers-panel.png` and `layer-character-study.png`; the latter is synthetic test artwork.

`toon_guide_storage_tests` covers all six guide types, project/guide-set round trips, versions 1–4 migration, malformed geometry and settings, resource limits, atomic-save protection, and exclusion from image exports. Geometry, canvas interaction and guide-panel checks have separate `toon_guide_geometry_tests`, `toon_guide_canvas_tests` and `toon_guide_panel_tests` targets; current acceptance evidence is recorded in the development plan.

`toon_selection_document_tests`, `toon_raster_edit_tests`, `toon_selection_canvas_tests` and `toon_selection_panel_tests` cover painting masks, exact moves, transformed pixel sampling across tile boundaries, atomic rejection, preview/apply/cancel/history, saved and recovered pixels, rotated-view mouse/tablet gestures, handle interaction and numeric bindings. Selections and transform handles are view-only and excluded from saved project data and exports.

`toon_transform_tool_switch_tests` covers Apply/Discard/Cancel through direct tool changes, Toolbox/Tools navigation, shortcuts, perspective/warp variants and organization history. Cancel and failed Apply retain the old preview, settings and selected row. Boundary-only transforms remain undoable; unchanged transforms, Escape and temporary Space navigation keep their direct behavior.

`toon_tool_catalog_tests`, `toon_tool_navigation_panel_tests` and `toon_tool_management_panel_tests` cover metadata validation/Undo, older catalog migration, embedded icon bounds, batch tag/color edits, group membership, filtering without activation, hidden-selection protection, stable focus-out edits, context tag menus, icon grids and accessible controls. Foundation tests verify manager visibility/layout, keyboard organization Undo from a floating dock, and restored tag/view preferences. Opt-in UI previews use `TOON_UI_PREVIEW` with the navigation `renderPreview` and management `managerPreviews` test functions.

`toon_selection_actions_tests` covers fill/clear masks, alpha lock, undo, sparse allocation and atomic budget rejection. `toon_selection_context_bar_tests` checks functional actions, slider entry, following/pinning, gesture hiding and narrow layouts. `toon_selection_visuals_tests` checks animated borders without artwork recomposition, shade holes/header exclusion, fixed screen-space hatching, cache invalidation and export isolation. Its generated illustration screenshots are `selection-focus.png` and `selection-focus-rotated.png`; these are synthetic QA artwork, not an artist project.

`toon_transform_preview_tests` checks viewport preview composition and rendering cache invalidation. For repeatable large-canvas timings, configure with `-DTOON_BUILD_BENCHMARKS=ON` and build `toon_selection_benchmark`. On macOS, run both workloads:

```sh
QT_QPA_PLATFORM=offscreen build/toon_selection_benchmark 3840 2160 model
QT_QPA_PLATFORM=offscreen build/toon_selection_benchmark 3840 2160 canvas
```

Use `1920 1080` for Full HD. The canvas workload includes selection visuals by default; append `no-focus`, `no-bar` or `no-overlays` to isolate their cost. It reports geometry-update time separately from update plus drawing. Timings are informational and are not pass/fail thresholds in CTest.

The same build option also provides `toon_brush_live_benchmark` for particle and textured brushes:

```sh
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 build/toon_brush_live_benchmark
```

It measures 240 input movements on a 1920 × 1080 drawing, separately timing brush input and event processing plus a forced full-workspace capture. The comparable model/workspace cases use stabilization 0; an additional workspace case checks stabilization 10. Results include final artwork hashes. An optional directory argument copies `brushes.json` and `tools.json` into temporary settings, using the saved Round Sketch configuration and the first sampled tip without writing to the originals. These local timings are not physical pen-to-screen latency measurements.

Current verification: the **0.26.0** macOS arm64 build is warning-free. All **112 CTest entries pass** across the broad run and final affected-suite recheck, including nine new camera suites and the existing drawing, animation, audio, 3D and local ABR checks. All five camera workspace/control suites pass on native Cocoa and at Retina scale. Narrow camera panels were visually reviewed; saved screenshot pixels also verify the key actions remain visible. Exact sheet/movie pixels, drawing coordinates, Undo, save/reopen, older formats and real MP4 camera motion were checked. Windows execution, physical Huion input and production-scale performance remain separate acceptance work. See [the development log](docs/DEVELOPMENT.md) for evidence and limits.

Previous milestone verification: the 0.20.0 macOS arm64 build is warning-free. All **77 CTest entries pass**, including the six animation suites; **11 affected suites pass at Retina scale**, and native Cocoa timing-entry Undo/Save/reopen checks pass. Timeline/Frame Properties were visually checked at 240/280-pixel widths and in the complete workspace. Evidence is in `build/animation-{build,tests,retina,native}-final.log` and `test-artifacts/animation-{normal,retina}/`.

Validation details are recorded in the development plan. Offscreen pressure tests use synthetic tablet events; actual Huion input, broader native UI interaction and Windows execution still require verification. A Windows/macOS CI workflow is provided for a future remote repository.


Effects/compositing verification in **0.27.0**: all **118 CTest suites** pass, including six effects suites for rendering, persistence, document history, mask input, panels and workspace transitions. Three interaction suites also pass on native macOS at normal and Retina scale. An encoded/decoded MP4 combines masks, lighting, color correction and camera animation. The [layer workflow](docs/layers.md#effects-and-masks-in-0270) describes controls and current limits; effect-parameter keyframes were deferred in that release and are implemented in 0.73 above.


Advanced motion verification in **0.28.0**: all **124 CTest suites** pass across the full run and focused fixture rerun. Native macOS/Retina panel and canvas checks cover curve editing, paired timing, Undo, Save/reopen and narrow controls. A decoded MP4 verifies curved motion, custom easing and orientation. See the [advanced motion workflow](docs/animation.md#advanced-motion-controls-in-0280) for controls and current limits.


Timeline verification in **0.29.0**: all **131 CTest suites** pass across the full run and focused fixture rerun. Seven new suites cover ripple edits across drawings/keys/cameras/audio, custom templates, editable vector cels, configurable onion skin, safe persistence and workspace interactions. Native macOS checks at normal and high-DPI scale cover the new controls. Projects save in format 19; formats 1–18 remain readable. See [the timeline workflow](docs/animation.md#timeline-improvements-in-0290).


Earlier tablet-input change in **0.29.1**: startup disables AppKit mouse/tablet coalescing and Qt high-frequency/tablet compression. Six relevant test suites passed; native Cocoa tests verified the actual AppKit flag and queued curved-stroke parity for raster/vector layers, pressure, stabilization, rotated views and Undo. The artist subsequently reported that the physical tablet issue remained, leading to the workspace change below. Project format remains 19.

Live drawing verification in **0.29.2**: all **133 CTest suites pass across the broad run and focused rechecks**, and the new full-workspace suite passes all ten cases on native macOS and at high DPI. With Layers, Timeline and Frame Properties visible, six raster/vector circle cases produce zero document-wide notifications, layer-row updates or button-disable transitions during contact. Thumbnails refresh after release, while Undo/Redo, cancellation and no-op completion retain their behavior. The build is warning-free. Synthetic tests do not establish physical tablet latency; save work and relaunch the updated app before retrying the Huion tablet. See [the development log](docs/DEVELOPMENT.md) for evidence and limits.


Brush management verification in **0.30.0**: all **136 CTest suites pass across the broad run and focused rechecks**. The manager also passes native Cocoa and Retina interaction tests, including closing a focused editor, preserving drafts, batch tags/categories, filtering, delete/Undo, Current brush copies and deferring previews during pen contact. Normal/Retina panel and creation-dialog previews were visually reviewed. Colored-stamp checks cover RGBA/alpha, pressure/transforms, raster/vector parity, hit testing, Undo, project/library/catalog round trips and large-resource rejection. The optimized macOS arm64 app reports 0.30.0 with no compiler warnings; physical tablet and Windows acceptance remain separate. See [the development log](docs/DEVELOPMENT.md).


## Production checks and development packages

The **0.38.0** macOS arm64 build passes all **154 CTest suites across the full run and final focused checks**. Advanced brush categories and both managers are checked for narrow/short layouts, exact-entry drafts, conditional controls, batch edits, organization Undo and drawing-time preview deferral. Native Mac suites and Retina previews cover the final layouts and icons. Drawing, animation, movie export, tablet-policy and real ABR-pack regressions remain covered. [The development log](docs/DEVELOPMENT.md) records the evidence and the remaining hardware/platform checks.

The **0.39.0** References/Effects pass builds without compiler warnings and passes all **12 targeted regression suites**, covering both panels, effects rendering/storage/history, reference persistence, workspace integration, shared controls/theme and live-stroke behavior. Native Mac checks pass for References (10 cases), Effects (13) and workspace integration (6). Retina previews verify narrow/short panels, centered thumbnails and reachable controls; the saved-layout check covers floating, re-docking and reopening. The development app is updated; the standalone ZIP remains the previously verified 0.35.0 package.

The **0.40.0** 3D Library/Object/Camera pass builds without compiler warnings and passes **16 targeted regression suites**. Native Mac checks pass for the library (7 cases), 3D workspace (25), Rig (18), Weights (10) and Pose (16). Retina previews verify compact rows, narrow/short properties and pinned actions clear of the scrollbar. Typed values and focus survive resizing and render updates; existing edits, Undo and rig workflows remain covered. The development app is updated; the standalone ZIP remains 0.35.0.

The **0.41.0** Rig/Weights/Pose pass builds without compiler warnings and passes **14 targeted regression suites**. Native Mac checks pass for Rig (20 cases), Weights (15), Pose (23) and the 3D workspace (25). Retina previews verify active-page sizing, compact hierarchy/controls and pinned navigation/views in narrow and short docks. Drafts, focus, mode-specific scrolling, binding, locks, Save, cancellation and Undo remain covered. The development app is updated; the standalone ZIP remains 0.35.0.

The **0.41.1** tablet-flow fix builds without compiler warnings and passes **all 155 regression suites**. Native Mac checks pass for tablet input (19 cases), brush cursors (17), the drawing workspace (12) and UI/help (27). Coverage includes actual Qt dock separators, native cursor state, mouse resizing, missing pen releases, untouched busy cursors, fast queued circles, deferred display work and unchanged drawing-time controls. The development app is `build/ToonPro.app`; the standalone ZIP remains 0.35.0. Physical Huion feel/driver latency still requires the artist's retry.

The **0.42.0** brush-catalog build passes **156 regression suites**, with a focused catalog/navigation/theme recheck after the final group-icon update. Native Mac navigation, Brush Management, ToolSession, live drawing and tablet tests also pass. All 112 new brushes have distinct deterministic previews, matching raster/vector strokes and verified Undo/save/reload behavior. Seven representative workspace profiles reproduce the model's artwork exactly; measured timings and limits are in [performance.md](docs/performance.md). Family swatches and native Mac navigation were visually reviewed. The development app is updated at `build/ToonPro.app`; the standalone ZIP remains 0.35.0. Physical Huion and real-artwork brush tuning remain open.

The **0.42.1** brush corrections pass **all 157 regression suites**. New checks cover full-size line-pattern transparency after repeated passes, smoother texture overlap at varied pressure, dynamics during a held stroke, and one-time upgrades that preserve customized fields, images and organization. An immutable pre-edit fixture checks all 112 original recipes used for migration. Before/after strokes at 256 px were visually reviewed, and seven representative drawing workloads retain exact model/workspace artwork parity. The development app is `build/ToonPro.app`; the standalone ZIP remains 0.35.0.

The **0.43.0** Quick access and drawing-mode build passes **all 159 regression suites** with no compiler warnings, plus native Mac checks for the toolbar (25 cases) and drawing mode (9 cases). These cover pin persistence and validation, activation, overflow controls, Tab exclusions, drawing/Undo, and workspace restoration. Normal/fullscreen/restored workspaces were visually checked at native Retina scale. The development app is `build/ToonPro.app`; the standalone ZIP remains 0.35.0. Physical Huion and Windows acceptance remain separate checks.

The **0.44.0** Brush Preset build passes all **12 targeted regression suites** with no compiler warnings. Its 24 panel checks also pass in native Mac windows, covering size-only changes, per-brush persistence, saved-size validation, Add/Cancel, gesture protection and floating/fullscreen layout restoration. Narrow, wide and docked layouts were visually reviewed. The updated development app is `build/ToonPro.app`; the standalone ZIP remains 0.35.0. Physical tablet and Windows acceptance remain separate checks.

The **0.44.1** large-brush performance build passes **all 160 regression suites**, with no compiler warnings. Final cursor checks pass at offscreen 1×/2× and in native Mac windows (21 cases each); native tablet input (19) and live workspace (12, on recheck) also pass. All 32 before/after benchmark artwork hashes match. Detailed cursor geometry, clipped edges, holes, cache invalidation and the bounded fallback are covered. Native cursor previews at 1× and 2× were inspected. The development app is `build/ToonPro.app`; the standalone ZIP remains 0.35.0. Extreme textured-cursor display costs and physical Huion acceptance remain documented in the performance report.

The **0.44.2** image-import update passes five targeted suites: Layers, clipboard images, animation tools, transform switching and the live drawing workspace. All 19 Layers cases also pass in native Mac windows, including actual file selection, transparency, fitting, cancellation, Undo, playback, pending opacity and compact footer geometry. The rebuilt app is `build/ToonPro.app`; the standalone ZIP remains 0.35.0.

The [performance report](docs/performance.md) records current macOS workloads for large layered movies, background saves, textured brushes, vector edits, selections, audio mixing and animated 3D reference cameras. Resource budgets remain unchanged. Physical Huion latency/driver behavior and Windows runtime acceptance remain separate checks.

The [packaging workflow](docs/packaging.md) stages Qt/codec dependencies, relocates the app away from its SDK, and checks startup, artwork persistence, WAV and encoded/decoded MP4 audio/video, plus the supported 3D renderer. Local Mac packages use ad-hoc signing for development; Developer ID/notarization and clean-machine Windows acceptance are not claimed.

The local **0.35.0 arm64 package passed both relocated smoke runs at 2× scale**, including actual 3D and codec output, and is available at `build/package-local/ToonPro-0.35.0-darwin-arm64.zip`. See the packaging document for the recorded checksum and acceptance limits. The current [checklist](docs/TODO.md) keeps physical tablet/audio, Windows and clean-machine checks open; 3D model/rig animation remains the separate queued phase 8.


The **0.45.0** advanced-tip/quality build passed all **161 CTest suites** in the full run, followed by focused checks after the final editor cleanup. Native Cocoa checks pass for the new tip editor, advanced settings, Brush Management, tool navigation, ToolCatalog, tablet events, live drawing and cursors. The previously blocked panel-focus cases pass in the unlocked-desktop rerun on 30 October. Six brush workloads have matching model/workspace artwork at both 1× and 2×; large colored stamps still have measurable peak costs. See [advanced-brush measurements](docs/performance.md#advanced-tip-sets-and-larger-stamps--0450-29-october-2026). The app is `build/ToonPro.app`; the standalone ZIP remains 0.35.0.
