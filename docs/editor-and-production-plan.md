# Focused editor and optional production workflow

Planning direction · 5 November 2026

**Implementation update:** a first managed-project flow, separate focused editors, multiple document tabs, folders/tags and project Assets are now implemented locally. See [current controls and limitations](managed-workspace.md). The artist excluded legacy migration. Later production stages in this document remain proposals; the earlier UI-first sequencing has been superseded.

**Later artist direction:** project-dashboard screen design refined the flow before implementation. [Project flow and content model](../design/ux-screens/project-flow.md) now specifies Projects → New project → Drawing / Animation / Movie → type-specific project dashboard → content editor. An Animation project can collect multiple single-shot documents. A Movie project supports nested folders containing drawings/images and single-shot animations with predefined/custom tags. This supersedes the direct-to-document Home entry and Film/Series-first recommendations below; the single-shot editor boundary remains.

**Technical development update:** the [storage and application architecture proposal](storage-and-application-architecture.md) now recommends storage/session work before the broader UI rollout. Follow its A–E increments for the current screen/Assets scope; the original phases below remain the longer-term capability roadmap.

This document records the requested direction and recommends an implementation sequence. It does not describe new working controls. Current behavior remains documented in [workspaces](workspaces.md), [production projects](production-projects.md) and the README. This direction takes priority over earlier proposals to expand the combined editor with movie/clip sections. Implementation starts in a subsequent pass.

## 1. Product decision

Make Toon Pro approachable as a drawing application and a **single-shot animation application**. Build optional production tools around that editor.

The requested boundaries are:

- An artist can draw or animate without creating a film, series, scene or production project.
- Every animation document represents one shot. Its timeline contains that shot's drawing exposures and supporting animation, never a sequence of shots.
- The creative editor has no Movies, Scenes, Shots collection, or editorial Clips section. Adding another shot means creating another document outside this editor.
- Production management has a dedicated interface. It can open a shot in the creative editor without bringing its management panels along.
- Beginners see the essentials first. Experienced artists can expose deeper tools without converting their artwork or moving to a different drawing engine.

**Recommended packaging:** one installed application with two separate areas, **Editor** and **Studio**. Editor has Draw and Animate workspaces sharing familiar drawing controls. Studio has its own navigation, layouts and commands. These are working names, not final branding. Separate executables are unnecessary for the initial split; the architectural boundary should permit them later.

“Separate” here means drawing and animation authoring are separated from production management. Draw and Animate remain closely related tasks within the creative editor. There is no requirement to learn Studio before using either.

## 2. What the repository already provides

This inventory is based on source/document inspection, not a fresh runtime verification. Existing verification evidence remains in [DEVELOPMENT](DEVELOPMENT.md).

| Area | Current foundation | Gap for this direction |
| --- | --- | --- |
| Document model | `Project` owns `shots`, Symbols, characters, shared dimensions and FPS; `Document` owns project-wide artwork history | Standalone illustration/single-shot semantics are not separate from a production container |
| Creation | Film / Series / Drawing only; Drawing only starts one frame without animation setup | No first-class standalone animated-shot choice; production type still drives creation |
| Drawing-only projects | Can contain multiple artworks and share the production container | “Drawing only” is not a one-document boundary |
| Editor layouts | Illustration, Anime, Storyboarding, Animatics and Compact drawing | Layout changes arrange panels; they do not restrict commands or change ownership |
| Animation timeline | Cels, holds, property keys, camera, audio, cues, Symbols and Shot / Scene / Movie review | Single-shot authoring and sequence review share one surface |
| Production browsing | Home, Shots/Artworks planning, stages, notes and Assets | Browsing and drawing share the active multi-shot document and shell wiring |
| Files | Standalone `.toonproj`; `.toonstudio` contains one artwork container plus catalog/media | Folder packaging does not yet give each shot an independently loaded document |
| Production helpers | Character references/palettes, Light Table, cel coloring/checking, guides, Motion Study and shot exports | Useful foundations exist, but they do not constitute a full reviewed/versioned production pipeline |

Concrete coupling points include `src/project.h`, `src/document_shots.cpp`, `src/project_dialog.cpp`, `src/main_window.cpp`, `src/main_window_studio.cpp`, `src/main_window_workspaces.cpp` and `src/timeline_panel.cpp`. For example, Timeline directly constructs Shot/Scene/Movie tabs, and MainWindow constructs the Shots dock. A complete split must address command ownership and data access as well as visibility.

## 3. The experience an artist should get

### Start by making something

Home gives primary actions **New drawing**, **New animation** and **Open**, plus Recent. **Studio** is a separate, clearly named destination. Opening Editor does not automatically restore production panels from a previous Studio session.

| Task | Required setup | First working surface |
| --- | --- | --- |
| New drawing | Canvas preset or dimensions; background if needed | Blank canvas, drawing tools, color and Layers; no timing or production fields |
| New animation | Canvas preset or dimensions, FPS and an extendable duration | One shot, one editable animation layer, one assigned drawing, simple Timeline |
| New production in Studio | Production name/location; Film or Series when relevant | Production overview and shot planning, with explicit Create/Open shot actions |

Permit unsaved drawing/animation documents and ask for a location on first Save. More canvas, sheet and timing options remain available without making their completion a prerequisite. Existing precise-entry, validation and accessibility rules still apply.

A beginner animation loop should be: create animation → draw → New drawing → draw → adjust Hold → Play → Save or Export shot. Creating a new drawing assigns it to the chosen frame and selects a paintable layer. Unassigned/locked targets retain clear feedback. A timing change must never silently change another shot.

### One creative editor, two task layouts

| Surface | Draw | Animate |
| --- | --- | --- |
| Shared | Canvas, Toolbox/Tools, concise Tool Properties, color, Layers, undo/redo, save | Same controls and drawing behavior |
| Timing | Hidden for a static document | Single-shot Timeline, play/stop, frame stepping, New drawing, Hold, onion skin |
| Inspectors | Selection/tool/layer context | The same context, with cel/timing properties when relevant |
| Optional depth | Brushes, vectors, guides, references, advanced layers | Drawing tools plus shot-local camera, sound, Symbols, effects and timing tools |
| Output | Image and optional production sheet | Current frame, image sequence and single-shot video; explicit range/FPS |

Switching to Draw layout while editing an animation only changes the view; it does not remove timing, freeze a frame into a static document or change export scope. A static document's **Enable animation** command is an explicit, undoable content operation: preserve its layers, add timing and create an initial exposure when appropriate. Hiding Timeline is never a destructive conversion. Flattening an animation to an illustration, if later needed, is an explicit exported copy.

“Clip” is not a top-level authoring concept. Sound items and reusable animated artwork can retain their necessary timing, trimming and placement behavior within this shot, using contextual names such as Audio and Symbol timing. The requirement removes editorial sections, not working drawing reuse or sound synchronization.

### Reveal depth by task

Use optional tool groups and contextual tabs instead of a mandatory Beginner/Expert switch. The following levels describe learning and delivery order, not pricing tiers or separate file formats.

| Level | Artist's goal | Tools made easy to find |
| --- | --- | --- |
| 1 · Draw | Sketch, ink, paint and save | Brush/eraser, size/opacity, color, Layers, undo, selection, canvas navigation |
| 2 · Animate | Complete a short single-shot animation | Assigned cels, holds, onion skin, frame navigation, playback and shot export |
| 3 · Finish a shot | Clean up, color and present a demanding shot | Templates, Light Table, palettes, paint checks, camera, shot audio, effects, masks and reusable artwork |
| 4 · Produce | Coordinate multiple shots and their deliveries | Studio planning, assets, stages, review, revisions and reliable handoffs |

Simple defaults must preserve advanced content on open/save. If an existing shot contains camera/audio/effects, show concise contextual indications and access to those controls. Optional UI must not hide content that changes the result without any indication. Do not silently auto-switch tools or repeatedly expand panels while an artist draws.

The simple Timeline initially emphasizes drawing rows. Reveal camera, audio and other lanes when used or deliberately requested. Advanced brush settings remain one direct action away. Frequent commands stay direct; discovery uses short focused help and delayed tooltips rather than permanent explanatory paragraphs.

## 4. A separate Studio interface

Studio coordinates documents. It does not turn the drawing canvas into a film editor.

Proposed navigation grows only as each area becomes useful:

| Area | Small first useful version | Later capability |
| --- | --- | --- |
| Overview / Shots | Existing names, order, stages and notes; open one shot | True episode/sequence hierarchy, task dependencies and progress |
| Assets | Existing catalog, search, attachments and copied placement | Versions, relinking, usage tracking and deliberate reference updates |
| Story / Boards | Later milestone: shot briefs and board pages | Board timing and reviewed animatic handoff |
| Review / Delivery | Relocated sequence review and existing export coordination | Revision comparisons, annotations and validated delivery packages |

Do not expose empty future tabs in the first implementation. Studios and solo artists can use external editing/sound tools. A full movie editor, transitions, cross-shot clip editing and global sound mixing are outside the initial scope and are not required to validate the split. Any later sequence assembly belongs exclusively in its own Studio/editor surface.

Opening a Studio shot passes a stable document identity and a short shot name to Editor. A single **Back to Studio** action can return to its origin after resolving pending edits. The canvas does not gain a shot browser, episode fields or production dashboard. A standalone shot works identically without that link.

Scene/Movie review already exists and should remain available through Studio/legacy navigation during the transition. It must be removed from the focused Timeline rather than relabeled as single-shot work. Draw/Animate layouts cannot reintroduce it.

## 5. Production workflow to build toward

Use the following as a configurable workflow reference, not a universal studio standard or a mandatory wizard. Toei describes storyboards, key drawings, in-between drawings, color, background, compositing and recording/editing; Bandai Namco Pictures also distinguishes layout. Our stage grouping and product boundaries below are design recommendations derived from those descriptions. [Toei production process](https://corp.toei-anim.co.jp/ja/company/animation_production.html), [Bandai Namco Pictures: making animation](https://www.bn-pictures.co.jp/business/making.html).

| Stage | Artist's work and expected handoff | Product responsibility |
| --- | --- | --- |
| Story and design | Brief/script, character and environment references, color models | Studio organizes intent and references; Editor creates artwork |
| Storyboard and animatic | Boards, action/dialogue/camera notes, provisional timing | Dedicated Studio/board surface later; no shot assembly in Editor |
| Layout | Composition, perspective, background layout, camera/output bounds | One shot in Editor; optional layout guides, sheets and 3D references |
| Key animation | Acting poses, breakdowns, timing and corrections | One-shot animation tools; later timing-sheet and correction overlays |
| In-between and cleanup | Resolved drawings, exposure timing, line consistency | Drawing/cel tools; later explicit drawing labels and checking workflow |
| Color and finish | Approved palette, flat color, shadows/highlights and checks | Shot-local palettes, cel coloring and paint checking; reusable models |
| Background / CG | Background paintings and other shot elements | Parallel asset work; explicit imports and future versioned dependencies |
| Compositing | Camera, layer integration, masks, effects and final shot render | Shot-local finishing; deeper tools remain optional |
| Review and retakes | Compare a specific revision, record corrections, deliver another revision | Studio owns review identity/status; Editor performs the correction |
| Editorial, sound and delivery | Sequence assembly, dialogue/music/effects and final outputs | External tools initially; optional separate Studio capabilities later |

Background, color design and CG can proceed in parallel; retakes can return to earlier tasks. Changing a stage label does not perform the work, approve pixels or lock a document. Planned review statuses should distinguish work stage from Draft / Ready for review / Changes requested / Approved.

For a Clip Studio Paint user, the familiar starting point is layers/folders, drawing cels and exposure timing. Its official manual describes animation folders whose layers/folders are assigned to timeline cels. Toon Pro should preserve this understandable relationship while presenting its own supported controls; this does not promise `.clip` compatibility or identical shortcuts. [Clip Studio Paint: animation folders and cels](https://help.clip-studio.com/en-us/manual_en/600_animation/Animation_folders_and_cels.htm).

“Production ready” must ultimately mean dependable pen input, correct timing, consistent renders, recoverable files, explicit dependencies, revision identity and verified deliveries. A large panel count or a production-stage dropdown is insufficient. Do not advertise a studio-quality guarantee before representative artist production work passes acceptance.

## 6. Technical boundaries

### Separate three independent concepts

1. **Document kind:** Illustration or Animated shot. Determines content constraints and valid operations.
2. **Editor workspace:** Draw, Animate, and optional task arrangements. Determines presentation only.
3. **Production membership:** An optional Studio relationship. Determines organization, shared resources and review context.

Film/Series belongs to Studio, not the standalone New animation dialog. Enabling camera tools or changing panel arrangements does not change document kind or create production membership. A single-shot document may have many drawing layers and animation tracks; it still represents one continuous shot.

### Reuse the engines; narrow their authority

Keep raster/vector rendering, brush engines, cel editing, property animation, camera, effects, audio and exports shared. Initially a compatibility adapter can present a legacy `Project` containing exactly one `Shot` to these engines. This is an implementation bridge, not the final production data model.

Define the focused editor's scope centrally and enforce it at document/controller command entry points, menus, shortcuts, panel discovery, saved-layout restore and export dispatch. Unsupported production commands cannot merely be hidden while remaining callable. A restored custom layout must not reopen Shots or Movie review in Editor. Build only the panels/controllers required for the active area where practical; avoid leaving hidden production work subscribed to every pen/frame update.

Studio owns collections/order, production metadata, assets and sequence review. Editor owns the open document's canvas, local clock, undo/redo and dirty state. Reuse transition guards for strokes, transforms, unfinished numeric/notes drafts, paint operations, imports and background saves. Successful navigation restores expected canvas focus; Cancel retains the original content and context.

### Move toward independent shot storage

Longer term, Studio should own a manifest referencing independently saved shot documents, assets and review/delivery records. Each shot carries its own canvas/output settings and FPS; production defaults and delivery rules must explicitly validate mismatches. Studio's sequence timing maps to shot-local timing without silently changing source FPS or duration. Opening one shot should not load every production shot.

Do not choose a new file extension or rewrite every format in the first UI milestone. Reserve a standalone-document discriminator only when its round-trip and older-reader behavior are specified. Unknown/new required semantics must be rejected by older readers rather than silently dropped. Existing `.toonproj` and `.toonstudio` files remain supported as legacy inputs.

The current 300 MiB artwork-container bound in `src/storage.cpp` still applies during the adapter stage. Splitting UI does not raise this limit or prove production-scale performance. Independent shot storage, bounded caches and lazy loading are a separate milestone with measured budgets.

### Specify resource ownership before extraction

Legacy shots can use project-level Symbols, character models/palettes and embedded media; drawing aids can refer across shots. Independent-shot extraction must include or deliberately snapshot every required dependency, including nested Symbols, brushes, masks, audio and models. Preserve timing, saved guides and recoverable drafts. Validate IDs/references and resource limits before publishing output.

Use explicit copied resources for the first independent documents. Later live references need missing/stale-media handling, revision identity, relinking and a deliberate update action. Copy placement must not be presented as a live dependency. Global libraries and collaboration are separate extensions.

Saving a shot and updating a Studio manifest/catalog are different persistence operations. Use atomic file publication and detectable revision identities; if either operation fails, retain dirty/retry state and report which completed. Never claim a distributed save/approval succeeded merely because one file was written. Production metadata retains its own appropriate history; a brush stroke must not undo unrelated catalog edits.

## 7. Compatibility and migration

| Existing content | Proposed safe route |
| --- | --- |
| One static artwork | Open in focused Editor after validation; retain source file semantics until explicit conversion |
| One animated shot | Open in Animate with required resources and contextual advanced controls |
| Multiple-shot Film/Series | Open through Studio/legacy project browsing; enter one scoped shot at a time |
| Drawing-only project with multiple artworks | Open its collection through Studio/legacy browsing; do not silently select and discard all but one artwork |
| Legacy custom workspaces | Preserve original settings; restore an area-compatible layout or explain a fallback |
| Existing sequence exports/review | Keep reachable outside focused Editor while they are relocated |

An explicit **Save independent copy** should be the first conversion path. It leaves the source untouched, bundles the required resource closure and compares rendered/timing results before declaring success. Permanent splitting of an entire legacy project is later work with backup, validation and failure recovery.

Do not implement one-shot isolation by deleting other shots from a live project. The temporary bridge must distinguish a scoped editor over an intact legacy project from a true independent document. Save of the former preserves the full project; Save of the latter writes one document. Make this difference explicit in save routing and migration tests.

## 8. Delivery sequence and acceptance gates

### Phase 1 — Focused entry and editor boundary

Deliver New drawing / New animation, simple setup, independent Editor/Studio navigation, one-shot authoring scope, and area-specific panel/menu/layout behavior using existing storage where safe. Provide a basic Animate arrangement instead of defaulting to every timeline channel. New standalone documents must not require Film/Series setup. Keep legacy productions accessible through Studio.

Accept when a first-time artist can create, draw, save and reopen an illustration, and create, time, play and export a two-drawing animation without encountering production fields or sequence sections. Check fresh startup, old settings, keyboard commands, Panels menu, saved layouts and single-shot export scope. No other shot may change through focused authoring commands. Saved copies must preserve all required content.

### Phase 2 — Independent document ownership and storage

Formalize document kinds, per-document history/recovery and format compatibility. Implement resource-complete single-shot copies and a Studio manifest referencing independent shot files. Preserve legacy round-trips and offer explicit conversion. Introduce lazy shot loading before claiming large-production support.

Accept when a shot can open, edit, save, recover and export without its parent production; a multi-shot production opens one shot without decoding all others; copied/moved projects retain required resources; missing references and interrupted saves recover predictably. Compare rendered frames and audio timing with legacy sources.

### Phase 3 — Discoverable shot finishing

Organize existing references, templates, palettes, Light Table, paint checks, camera, audio, Symbols, masks and effects around the active task. Improve discoverability and context before adding more engines. Evaluate an optional X-sheet/timing-sheet view, drawing-number labels and correction overlays as separate additions; they must share authoritative timing/drawings with Timeline.

Accept when an artist can complete a representative rough → cleanup → color → composited-shot task, reopen it in a simple layout without data loss, and match paused preview to export. Verify physical pen behavior and sustained playback on the artist's actual target machines and files.

### Phase 4 — Story and production handoffs

Build the smallest useful board/animatic workflow, stage/task tracking and deliberate asset use. Give boards and shots distinct identities; promotion to a shot must preserve reviewed timing, references and notes through an explicit handoff. Keep existing stage labels honest until these behaviors exist.

Accept when a small multi-shot exercise moves from boards to independently editable shots without renumbering drawings, losing notes or forcing storyboard controls into Editor. Returning to a board for a retake must not silently overwrite an animated shot.

### Phase 5 — Reviewed production delivery

Add immutable shot revision snapshots or equivalent content-addressed records, review notes, retakes, approved-revision selection, validated export presets and dependency/relink diagnostics. Add external editorial handoffs before considering a full sequence editor. Team roles, simultaneous editing and server infrastructure require their own design.

Accept when a Studio delivery references exact reviewed shot revisions, later source edits cannot silently replace them, required dependencies are present, cancellation leaves no partial published output, and FPS/range/color/alpha conventions are validated against a chosen real delivery specification.

Phases are ordered dependencies and reviewable increments, not dates or release commitments. Each implementation pass updates the README, workflow docs and verification log with only the behavior actually delivered.

## 9. Verification plan for implementation

- Document/model checks: one-shot invariants, forbidden production mutations, resource closure, format round-trips and old-reader rejection.
- UI checks: no production navigation in Editor through menus, shortcuts or restored layouts; direct drawing/cel actions; contextual controls; accessible names and the shared 600 ms tooltip delay.
- State checks: switching areas/layouts preserves selection, current frame, unfinished drafts, undo and dirty state; active pen/transform cancellation keeps context.
- Rendering checks: representative frames, camera, effects, nested Symbols, masks and audio timing match before/after migration and between paused preview/export; notes/guides stay excluded as intended.
- Persistence checks: save/reopen/recovery, missing resources, cancelled/stale workers, interrupted package publication and per-shot/manifest partial failure.
- Artist acceptance: compact and large-font layouts, native/Retina displays, physical Huion, and a representative multi-layer production shot on macOS/Windows. Record actual latency, memory and completed-frame playback; set budgets from agreed fixtures rather than assuming universal realtime 4K playback.

Start from existing creation/workspace, drawing, animation, timeline, shots, Studio, save/storage, movie-review and export suites. Extend them for changed boundaries; documentation changes alone do not establish runtime verification.

## 10. Decisions to settle as implementation starts

The single-shot boundary and separate production interface are requested requirements. The following are recommendations to refine without expanding the first milestone:

| Topic | Recommended starting choice |
| --- | --- |
| Application packaging | One application; dedicated Editor and Studio areas |
| Creative workspace names | Draw and Animate, with short visible labels |
| Default new animation | One assigned drawing, one animation layer, simple timeline; editable FPS/duration |
| Beginner/advanced control | Optional task controls and contextual discovery, no mandatory skill-level selection |
| Standalone save | Unsaved start, existing extension where safe, explicit independent-copy conversion |
| Production assembly | Existing review outside Editor; external editing/sound handoff before a new movie editor |
| First implementation slice | Phase 1; scope, creation, navigation, layout and export guards together |

No application code or interface behavior changes are part of this planning pass.
