# Product requirements

**Superseding 3D direction, 7 November 2026:** the artist rejected the legacy skeleton-building/weight-painting module; it remains retired while saved scenes retain compatibility display/storage. The approved [3D reference workspace](3d-reference-workspace.md) is enabled by default with an explicit OFF option. Its cube / **View → 3D reference…** entry opens docked controls for editable reference layers directly on the drawing canvas, including posing and **Draw above**. Full CSP parity and full VRoid authoring remain incomplete; the [parity requirements](3d-reference-replacement-plan.md) govern remaining work. Earlier 3D milestones below are historical records.

Source: [Anime Production App Requirements Draft](https://chatgpt.com/space/page_7d21eaa04628819188fbcd80d1953fbe)

Snapshot taken on 13 October 2026. This records the agreed product scope; implementation status lives in DEVELOPMENT.md. The Page remains the discussion reference.

Draft 0.1 · 13 October 2026

A Windows and macOS application for a solo artist producing a hand-drawn anime movie and, later, a series. The immediate experience is to create a movie project and its first shot, open a blank production sheet, and arrange the panels needed for the work.

This draft defines project setup, the production sheet, the workspace, and canvas navigation. Detailed tools and the internal design of most panels will be specified one by one. Suggested defaults are identified separately from confirmed requirements.

# Product direction

The artist is currently developing characters and works by hand with a pen display, understood to be Huion; the exact model remains to be confirmed. Your Name and Demon Slayer are visual references for the intended artwork.

The application must accommodate raster and vector drawing, frame-by-frame animation, animated properties and camera movement, imported audio, and imported 3D environments and poseable characters. Simplicity, desktop availability, and affordability guide the product. Pricing and licensing have not been decided.

A movie project contains shots. Each shot has its own artwork and timing, while movie assembly connects shots. Series and episode organization can be added without changing the individual shot workflow.

# Create and open a project

1. Choose New Project.

2. Enter a movie/project name and choose a save location.

3. Choose the output frame dimensions and frame rate, with a preset or custom values.

4. Configure the first shot, its initial duration, surrounding drawing space, and production sheet margins.

5. Enter available production information; additional metadata can be completed later.

6. Review a preview showing the output frame, extended drawing area, margins, and header.

7. Choose Create. The application creates the movie project and its first shot, then opens that shot.

8. See blank artwork within the selected frame, surrounded by white paper space and a production header.

9. Open and arrange panels as needed.

The first workspace should be uncluttered, with optional panels initially closed. Essential application controls and an accessible Panels menu remain available. An existing saved workspace can be restored on subsequent sessions. No sample artwork, 3D city, character, or audio is inserted automatically.

## Project setup fields

| Group | Fields and behavior |
| --- | --- |
| Project | Movie/project name and save location. |
| Output frame | Width and height in pixels, aspect-ratio lock, and named presets. These describe the finished picture area. |
| Timing | Project FPS and initial shot duration, entered in frames or seconds with an explicit conversion. |
| First shot | Shot/cut number and shot/cut name; sequence and episode fields can remain empty. |
| Drawing extent | Extra space beyond the output frame for off-frame artwork and camera movement. |
| Production sheet | Left, right, top, and bottom white margins, plus a separate header height. |
| Appearance | Paper/view color and artwork background or transparency as separate settings. |
| Advanced configuration | Working color profile, bit depth, optional safe-area guides, and print resolution when physical sheet sizing is used. |
| Reuse | Save and recall project and sheet presets. |

Frame rate is a timing setting; it is not a number of keyframes. Hand-drawn poses and property keyframes are created during animation. Initial shot duration is extendable and does not require choosing the length of the entire movie.

Presets, custom pixel dimensions, and FPS are established setup concepts in [ToonSquid](https://toonsquid.com/handbook/projects/create_project/). [Krita](https://docs.krita.org/en/reference_manual/create_new_document.html) also exposes color model, bit depth, background, and print resolution. The exact defaults for this application remain proposed below.

## Configuration rules

Dimensions must be positive whole pixels; duration and frame rate must be valid positive values; margins must not be negative. Invalid fields show a specific explanation before creation.

Changing paper margins or header height must not resize or resample the artwork. Changing output dimensions later must distinguish reframing from scaling artwork. Changing FPS after animation or audio exists must explain its timing consequences before applying the change.

Pixel dimensions control digital output size. PPI controls the relationship to physical sheet size when printing; changing print PPI alone must not silently change movie resolution.

# Production sheet

The sheet has three related areas:

- **Output frame:** the picture area seen in the finished movie.

- **Extended drawing area:** artwork outside that frame, available for drawing and camera movement.

- **Production paper:** surrounding white space, a header, and annotations that make an exported sheet understandable by itself.

The artist can draw beyond the output frame. Production notes and off-frame artwork must be distinguishable: notes remain excluded from movie output even if a camera move reveals the same location. The header belongs to the sheet, not the animated scene.

The output boundary must remain legible against the white paper. Optional center and safe-area guides can be shown without becoming artwork. The reference model is Japanese animation layout paper; exposure/timing sheets are a separate future feature.

[Clip Studio Paint’s animation setup](https://help.clip-studio.com/en-us/manual_en/210_file/Creating_a_New_Canvas.htm) similarly distinguishes output dimensions, overflow, and blank margins. The [AJA layout-paper reference](https://aja.gr.jp/assets/files/aja_layout/aja_layout_doc.pdf) allows supplementary production fields to be customized. Our default sheet will follow that general layout logic; an exact AJA print preset would require its own dimensional specification.

## Header and metadata

The top of the sheet must include editable **movie title, shot/cut number, and shot/cut name**. “Shot/cut” is the working terminology for the shot information described in the discussion.

Additional proposed fields are episode, sequence/scene, current frame, drawing identifier when applicable, shot duration, artist, production stage, revision, date, and notes. Their visibility and ordering should be configurable so the sheet remains useful for solo production.

Store these values as editable project/shot metadata, rather than baking them into painted pixels. Movie information is shared across shots; shot information belongs to the current shot. Automatically displayed values, such as the current frame, follow the active shot and playhead. A drawing identifier must not be confused with a timeline frame number.

Header text stays outside the output frame. Long titles must wrap or expand the header without overlapping artwork. Empty optional fields must not prevent project creation. The header should be editable without adding a painted text layer.

## Sheet layout example

The diagram uses a sample 1920 × 1080 output frame. Its proportions and metadata arrangement illustrate the proposed structure; final margin measurements, guide styles, and header typography remain open. The drawing area starts blank.

[Production-sheet layout illustration is available in the source Page.](https://chatgpt.com/space/page_7d21eaa04628819188fbcd80d1953fbe)

# Sheet and movie exports

Two separate export purposes are confirmed:

| Export | Included content |
| --- | --- |
| Production sheet image | Current shot image, surrounding white paper, production header, and chosen notes or guides. One image carries the relevant production details. |
| Movie frame or video | Content inside the evaluated shot camera/output frame. Header, paper margins, production notes, editing handles, and UI guides are excluded. |

For example, a project with a 1920 × 1080 output frame keeps that movie frame size regardless of the margins around its sheet. A full-sheet image has its own larger dimensions.

Canvas zoom, pan, and working-view rotation do not change either export. A sheet export returns the sheet in its defined orientation. 3D layers marked Reference are excluded from movie exports; including them in a production sheet should be an explicit export option.

# Flexible panel workspace

Panels are independent components that the artist can open when needed. There are two layout types:

- **Normal panel:** resizable width, with docking, tabbing and floating. Tools, Tool Management, Tool Properties, Layers, Colors, Navigator, References, Effects, Timeline and Frame Properties use this type.
- **Icon column:** fixed width, one vertical stack of icons, spanning the full workspace height on the left or right edge. Toolbox uses this type for groups and standalone tools. It stays separate from normal dock stacks so splitting or resizing those panels cannot shorten it. Names remain available through tooltips and accessibility.

Both types can be shown or hidden and their placement is saved. Their arrangement must not change the artwork. Future preset, brush-size or opacity columns can reuse the icon-column container; those additional columns are not implemented.

| Panel | Scope established now |
| --- | --- |
| Toolbox | Fixed-width icon column containing user-arranged tool groups and standalone tools. See the raster tool navigation flow below. |
| Tools | Show the selected group's individual tools, or exactly one selected standalone tool. |
| Tool Management | Organize groups, tool membership, tags, label colors and group icons independently of painting. |
| Tool Properties | Show settings for the selected tool. Individual properties remain open. |
| Layers | Manage drawing/Notes layers and nested groups, visibility, painting lock, opacity, order, clipping and alpha lock. Current behavior is defined in [Layer controls](layers.md). |
| Colors | Select and organize colors. Detailed controls remain open. |
| Control or Navigator | Show a miniature of the current canvas and control its working view. |
| References | Keep visual references available while working. Import and arrangement details remain open. |
| Effects | Provide access to effects such as shadow and blur. Effect behavior and the full list remain open. |
| Timeline | Manage static and animated tracks, drawings, exposures, scrubbing, onion skin and playback. |
| Frame Properties | Edit the current drawing name and exposure start/hold independently of tool settings. |

New panel types must fit the same workspace system without forcing a redesign of existing panels.

The artist's [UI and interaction rules](ui-guidelines.md), established on 14 October 2026, now govern panel and control design: direct access with minimal clicks, icons with necessary text, visual widgets for complex settings, tabs for feature switching, and an Adobe-inspired color theme. These refine the interaction requirements without claiming the current interface already implements every rule. The current priority is the combined drawing-exposure and property-keyframe workflow, preserving the drawing workspace and UX. Frame animation arrived in 0.20; 0.21 adds layer properties and compatible vector path morphs.

Workspace behavior includes tabbed panel groups, named saved layouts and a reset-to-default command. Version 0.74 adds Illustration, Anime, Storyboarding and Animatics layouts and named custom save/update/reload/manage; see [workspaces](workspaces.md). Closing a panel hides its interface; it must not delete its layers, references, animation, or effects. Floating panels should work on additional monitors and return to a visible position if a monitor is disconnected.

## Raster tool navigation

Requested on 14 October 2026 and implemented in 0.13.0. **Toolbox** is the first panel and **Tools** is the second panel. The existing Tool Properties panel continues to show the active tool's settings.

Implemented structure:

- Hide the existing drawing-tool buttons in the top bar and old side palettes. Toolbox and Tools become the tool-selection interface. The existing Brushes list is incorporated into this flow rather than remaining a third tool selector.
- Toolbox entries may be groups or standalone tools, with a small group marker so their behavior is recognizable. A group can contain different kinds of raster tools.
- Selecting a group shows its individual tools in Tools. Selecting a standalone Rectangle shows **only Rectangle** in Tools, even if that same tool also belongs to a group elsewhere.
- Each brush preset is an individually selectable tool. Line, Rectangle, Ellipse, Linear Gradient, Radial Gradient and the supported retouch operations are also individual entries. This iteration exposes raster workflows; vector tools and their behavior remain deferred.
- Both panels have direct Add and Remove icons. Tools support right-click actions including Duplicate and Remove. Toolbox supports creating and removing groups and standalone placements.
- One shared modal supports group creation and adding tools. It offers searchable raster-tool choices, brush previews, and necessary naming/icon controls. It does not reproduce the full advanced properties editor.

Interaction defaults:

| Action | Result |
| --- | --- |
| Select a nonempty group | Show its members and activate its last-used tool; select the first member on first use. |
| Select a standalone tool | Show its single entry and activate it immediately. |
| Toolbox Add | Open the modal with Group selected; a Standalone Tool tab is also available. |
| Create Group | Enter a name, choose an icon, select zero or more tools, then create and select the group. |
| Add Standalone Tool | Pick one existing tool or create a named variant from a supported base tool; add and select it in Toolbox. |
| Tools Add while a group is selected | Open the same modal with that group as the destination; choose one or more tools. |
| Tools Add while a standalone tool is selected | Add a new standalone Toolbox entry and select it. Do not silently convert the original into a group or show multiple tools in a standalone view. |
| Duplicate inside a group | Make an independent copy of the tool's settings, insert it next to the original in this group, and select it. |
| Duplicate a standalone tool | Make an independent copy as a new standalone Toolbox entry, then show only that copy in Tools. |
| Remove from a group | Remove the membership, preserving the underlying tool and other placements. |
| Remove a standalone entry | Remove that Toolbox placement and its single-item view, preserving the underlying tool in the library. |
| Remove a group | Remove the group and its memberships, preserving its tools in the library and other placements. |

Modal details and edge cases:

- Group and Standalone Tool use tabs. Group name/icon and the searchable tool picker are the main content. When adding to an existing group, preselect the destination and omit irrelevant group-creation fields.
- Adding an existing tool links to the same tool definition. The same tool may appear in several groups or directly in Toolbox; editing its settings is reflected in each placement. Duplicate explicitly creates independent settings.
- Do not add the same tool twice to one group; a deliberate duplicate has its own identity. Tool names must be nonempty; canceling the modal changes nothing.
- Empty groups remain visible with an Add Tools action. Selecting an empty group does not invent or activate a hidden default tool; the previous active tool remains identified in Tool Properties until another is chosen.
- After removing the active placement, prefer the next remaining tool, then the previous one. If its group becomes empty, select another available Toolbox entry. If no tools are placed, show the empty state and suspend drawing-tool gestures until a tool is added.
- Use Remove for placement actions. Global deletion of a tool definition, if added later, must be a separately labeled library action. Routine organization should be undoable without confirmation popups, and must never modify artwork.
- Each individual tool remembers its settings; foreground/background colors remain shared by default. Groups, order and placements persist with the user's workspace/library, independently of movie files.
- Keep document commands such as New/Open/Save and Undo/Redo available in the top bar, and preserve drawing shortcuts. Shortcuts should activate the corresponding individual tool through the same path and synchronize both panels. Move tool-specific controls such as guide snapping to the relevant properties panel.
- Follow the shared icon tooltips, compact numeric fields, semantic colors and keyboard-access rules. Use short names or brush previews where an icon cannot identify an individual tool clearly.

The implementation uses stable tool identities, per-tool settings, a connected icon column and normal Tools panel, a shared modal and reversible organization in a machine-local `tools.json` catalog. Existing brush templates and ABR imports remain in `brushes.json`. Tool organization uses its own undo history: Cmd/Ctrl+Z follows focus in the navigation panels, and their menus always provide organization Undo/Redo. Brush imports begin a fresh organization history so older snapshots cannot discard imported definitions. Version 0.14 adds dedicated vector-editing navigation. Dedicated text navigation and cross-panel drag-to-group remain deferred; current dragging reorders rows within a panel.

### Tags and tool management

Requested on 16 October 2026:

- Every tool can have multiple tags and an optional organizational color. Linked placements share these labels; independent duplicates can be customized separately. Label colors do not change the paint color.
- Tools provides a tag filter with All and Untagged choices, alongside name search. Filtering narrows the current group's list without activating another tool. A visible empty-result state and clear-filter action prevent hidden tools from appearing deleted.
- Groups can have their own tags and color. A group tag makes all its tools match that tag while viewing the group; it does not overwrite their individual tags.
- Tools offers an Icons only option. Names remain available through the shared delayed tooltips and accessibility; the normal named list remains available. Remember the view choice and tag filter with workspace preferences.
- Tool Management is a separate dockable panel, opened from Panels or the tool-navigation options. It supports group creation, assigning multiple tools, editing tags and colors for groups/tools, and choosing a built-in or imported image for a group icon. Batch tagging adds or removes a tag while preserving unrelated tags.
- Browsing or labeling in the manager does not switch the painting tool. Organization changes are undoable independently of artwork. Changes that replace an active transform tool retain the Apply / Discard / Cancel protection.
- Imported group images are embedded in the local catalog, preserve aspect ratio/transparency, and use bounded dimensions. Existing catalogs migrate without resetting organization or brush settings.

Reference: [Krita tag management](https://docs.krita.org/en/user_manual/tag_management.html) and [Brush Presets docker](https://docs.krita.org/en/reference_manual/dockers/brush_preset_docker.html), checked 16 October 2026. This follows tag filtering and contextual assignment, without claiming Krita resource-file compatibility.

# Photoshop brush support

Confirmed requirement: import Photoshop brush packs in `.abr` format for drawing and painting on raster layers, including frame-by-frame animation drawings, on Windows and macOS. Adobe documents ABR as a format that can contain multiple brushes or brush packs. [Adobe brush import documentation](https://helpx.adobe.com/photoshop/desktop/apply-painting-techniques/brushes-presets/import-brushes-brush-packs.html).

Proposed workflow: import a pack, preview its brushes, select a brush, adjust its settings, and save a customized native preset for reuse across projects and app sessions. Brush library placement and detailed controls will be discussed with the drawing tools.

Compatibility target: preserve brush tips and map supported settings into the app’s brush engine. Supported ABR versions and the mapping of spacing, opacity, flow, texture, scatter, and pressure or tilt dynamics must be established during a brush-import prototype. Exact Photoshop rendering is not guaranteed. Clearly identify unsupported settings and any brushes imported as tips only, allowing the artist to adjust the result.

Acceptance: representative brush packs can be imported, previewed, used for tablet drawing, saved, and reopened on both supported desktop platforms. The importer reports unsupported brushes or settings without silently claiming full compatibility.

# Advanced brush engine and stamp pointer

Confirmed on 14 October 2026: implement the full set of brush-setting categories supplied by the artist: General (including distinct opacity/flow and airbrush build-up), Tip Shape, Shape Dynamics, Scattering, Texture, Dual Brush, Color Dynamics, Transfer, Brush Pose and global Behavior. Symmetry includes vertical/horizontal/dual/diagonal, wave, circle, spiral, parallel lines and radial modes. Choose controls according to the [UI rules](ui-guidelines.md): complex configuration should use meaningful visual interaction, and every exposed scalar numeric field has a slider beside compact manual entry. Switch categories with tabs, using icons where clear. The later 0.12.1 numeric-control requirement supersedes the earlier allowance for numeric-only fields. Controls must affect native rendering and persist in reusable brush presets; avoid decorative controls without engine behavior.

Updated on 16 October 2026: brush Tool Properties must show only frequently needed drawing controls. Keep Size, Opacity, Flow, Hardness, Smoothing and a pressure-size toggle directly available; a wrench opens advanced properties in a modal dialog. Preserve all existing categories and live tool-setting behavior. Closing the dialog keeps committed settings, and exact numeric entry must not accidentally close it.

The pointer must follow the selected stamp image or pattern instead of always being circular. It should reflect tip transforms, pressure and view scale/rotation; a round brush naturally retains a round pointer. Cursor and symmetry guides remain editor overlays excluded from artwork/export/history. This supersedes the earlier request for an always-circular brush pointer.

The [native settings matrix](brush-settings.md) records implemented ranges and the behavior of each control. Native effect support and importing Photoshop's saved values are separate concerns; maintain explicit ABR compatibility reports.

# Perspective drawing guides

Confirmed on 14 October 2026: add a perspective/guide tool, with Tool Properties taking its reference from Krita's drawing assistants, and allow multiple perspective setups in the same drawing. Maintain the agreed icon, tooltip, tab and direct-manipulation UI rules.

Version 0.9.0 implements an initial four-type set: perspective plane, vanishing point, two-point perspective and parallel ruler. Guides support canvas handles, a selectable list, per-guide/global display and snapping, locks, density, optional rectangular limits, shared/custom colors and native reusable guide sets. They belong to the shot, support undo and persistence, and stay outside image exports. See [the guide workflow](guides.md) for exact behavior and current limits. This is not a commitment that every Krita assistant or its file format is already supported.

References: [Krita Assistant Tool](https://docs.krita.org/en/reference_manual/tools/assistant.html) and [Painting with Assistants](https://docs.krita.org/en/user_manual/painting_with_assistants.html).

# Canvas display smoothing

Confirmed on 15 October 2026: add one top-header anti-aliasing/smoothing switch before starting the editable-vector-brush workflow. The artist explicitly chose **canvas display only**; artwork pixels, drawing operations and exports must remain unchanged.

- One shared view setting applies to the composited main canvas across every artwork layer and active drawing tool. Switching tools or layers does not reset it.
- Enabled: smooth image sampling when displaying the canvas at different zoom levels or rotation angles. Disabled: nearest-pixel sampling for a crisp pixel view. Disabling display smoothing does not remove anti-aliasing or softness already stored in artwork.
- Brush hardness, opacity, textures, selection feathering and the artwork renderers retain their existing behavior. Project exports are identical in either view mode, and toggling creates no artwork undo entry or project-dirty state.
- A compact checkable **AA** header button has the accessible name “Canvas smoothing”, a visible selected state and the shared 600 ms tooltip. The same action appears in View. The initial default is enabled, and local workspace settings remember the choice independently of projects. Navigator uses the same display setting.
- Implementation must avoid seams along sparse tile boundaries. Smooth the displayed composite with adequate sampling borders rather than independently antialiasing each tile quad.

The switch is implemented as a view preference. It invalidates the displayed artwork cache without changing document data or history. Validation covers exports/project state, tool switches, persistence and tile sampling; current build results are recorded in [the development log](DEVELOPMENT.md).

# Control panel navigation

The Control panel, also called Navigator, is specified more fully because its behavior is already defined.

It displays a miniature of the active canvas at the current animation frame, with an outline showing the area visible in the main canvas. It updates when the artwork, frame, or active shot changes.

The artist must be able to:

- Zoom in or out from the panel.

- Rotate the working canvas view.

- Fit the canvas to the available viewing area.

- Pan by dragging the miniature’s viewport, equivalent to using the hand tool on the main canvas.

Proposed additional controls are a numerical zoom value, a numerical rotation value, Reset Rotation, Fit Output Frame, and Fit Entire Sheet.

These operations affect only the working view. They must not resample pixels, transform layers, change the shot camera, create animation keyframes, or alter exported framing. The miniature remains usable when the main view is rotated, and its viewport indicator stays synchronized.

# Animation and 3D integration

The combined target is a synchronized shot timeline with drawing exposures and separate property tracks, including audio and later 3D/camera animation. The artist chose frame-by-frame raster drawing as the first implementation (0.20), followed by layer/folder transform, opacity and compatible vector path morph animation (0.21). Version 0.24 adds imported audio clips and synchronized preview playback. See [animation controls and limits](animation.md). Animated 3D and cameras remain future requirements.

A 3D Scene Layer contains imported models, a camera, and lights. Models can be positioned, rotated, and scaled. Supported rigged characters can be posed; static models are not automatically poseable.

The main use is reference: stage a city or room, set a camera movement, and draw characters against the evaluated 3D view at each frame. A 3D layer can also be included in final output when explicitly set to Rendered.

Hand-drawn 2D artwork remains under the artist’s control as camera perspective changes. The app must distinguish canvas navigation, temporary 3D inspection, and editing the actual shot camera. Production-sheet headers and notes remain outside camera animation.

Import formats, pose controls, lighting, renderer quality, and effects are later specifications. They must not crowd the initial blank workspace.

## 3D reference milestone — 0.16.0

Approved on 16 October 2026: provide starter models and a global library where artists can add their own models. The complete plan also includes creating and editing rigs inside Toon Pro, binding models, correcting weights and posing; the first implementation delivers the static reference foundation below.

The table records the 0.16 milestone; the rest-skeleton and binding milestones below extend its rigging capabilities.

| Area | Implemented in the first milestone | Still planned |
| --- | --- | --- |
| Library | Starter / My Assets, local GLB/glTF import, names, search, tags, favorites, add/remove custom assets | Rig-template and reusable-pose collections; more formats |
| Starter assets | Cube, sphere, ground plane, static mannequin, simple building | Poseable mannequin and a broader licensed content library |
| Scene | One shared reference scene per shot; multiple instances, move/rotate/scale, duplicate/delete, rename, hide/lock | Nested background/character/prop groups and animation tracks |
| Camera | Orbit, pan, dolly, fit selected/all, perspective/orthographic, lock | Animated camera tracks and multiple cameras |
| Reference display | Shared depth buffer, base-color textures, opaque/cutout materials, fixed simple lighting, display opacity | Editable lighting, transparent/PBR material fidelity, shadows/effects and explicit rendered export |
| Rigging | No rig or skin editor in this milestone | Bone creation/hierarchy/rest editing, mirrored skeletons, humanoid templates, automatic binding, weight painting |
| Posing | Static geometry placement only | FK joint rotation, IK hand/foot controls, pole controls, foot pinning, joint limits, mirrored poses and pose saving |

The intended complete workflow remains **choose/import model → create/edit rig → bind and correct weights → pose → set the shot camera → draw above the reference**. Rig editing changes structure and the resting skeleton; posing changes a character instance. Those modes must stay distinct. Library definitions and project instances must remain independent, with explicit asset updates rather than silently replacing existing project content.

For this milestone, open 3D Library through Panels, place/import a model, then use 3D Object or 3D Camera with Model/Camera tabs in Tool Properties. Models share a camera and depth buffer, so one can obscure another correctly. Object transforms and camera changes are project edits with Undo; ordinary canvas view navigation stays independent. Brush painting on artwork layers must reuse the last completed 3D snapshot. No 3D frame timer runs while the scene is stationary. Both sheet and movie-frame PNG exports omit reference geometry; an inclusion option is deferred.

Imports accept a bounded static subset of GLB/glTF 2.0: triangle meshes, transformed nodes, normals/UVs, base colors and PNG/JPEG textures with opaque or masked alpha. Skinning, animations, morph targets, compressed/required extensions and alpha-blended materials are rejected explicitly when unsupported. Blender, FBX, VRM and OBJ import are not implemented. External resources are local files or embedded data; network downloads and escaping the model directory are rejected. The parser validates resource counts, dimensions, accessor ranges, indices and finite geometry before insertion. Library removal never removes a model already embedded in a project.

The current renderer is native Filament 1.78.0 on macOS arm64 using Metal, rendering transparent images offscreen on a dedicated thread. Requests are coalesced and stale images are ignored; immutable geometry and textures are reused across camera and object edits. Unsupported builds show a renderer-unavailable message. Windows GPU integration is not verified. Starter models are generated within Toon Pro; the mannequin is expressly static, not a rigged character.

Limits: 128 scene objects, 64 scene/global project model resources, 256 meshes and one million vertices/triangles per model, 128 MiB per model source/serialized resource, 4096 pixels per texture side and 16 million texture pixels per model. The global library allows 128 imported models with separate 256 MiB stored and decoded budgets. Project model geometry/textures use a 256 MiB decoded budget; the complete project container retains its 300 MiB limit. Imported geometry is immutable and shared between instances, then embedded as checksummed model resources. Version 0.19 saves project format 12 with instance rest skeletons, skin bindings and current poses in scene schema 4; older project formats 1–11 remain readable.

References: [Filament 1.78.0](https://github.com/google/filament/tree/v1.78.0), [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html). These define the technical foundation, not a claim of supporting every glTF feature.

## Rest-skeleton milestone — 0.17.0

Step 2 implements skeleton creation and rest editing inside Toon Pro. Select a model and choose **3D Rig**, or the Rig tab in Tool Properties. Start with one root joint or a fitted 22-joint humanoid template. Add roots/children, rename inline, choose a valid parent, select and drag joints on the canvas, or edit precise XYZ values. Delete removes a whole branch with Undo. The hierarchy disallows cycles. Joint positions are absolute within the model: moving or reparenting a joint leaves its children in place.

Mirroring copies an unpaired branch across the model's local center plane and pairs the corresponding joints. Mirrored editing updates paired positions together; disabling it permits asymmetric adjustments. Front/Side/Top changes the saved drawing camera to an orthographic model view and respects camera locking. Each completed drag makes one Undo action, Escape cancels it, and Save commits a pending edit. Hidden/locked model or scene targets show unavailable-action feedback. The skeleton can be hidden independently of the model.

Skeletons belong to project instances, are included in save/reopen and recovery, and remain independent when a model is duplicated. No skeleton changes the original global library asset. Limit each rig to 256 joints with finite coordinates and valid parent/mirror references. Rest edits reuse the completed model render, and editor joints/bones never appear in artwork exports.

The humanoid template is a starting arrangement, not automatic joint detection. Imported models still use the supported static GLB/glTF subset. Moving a rest joint changes the skeleton only. Steps 3 and 4 below add binding, weight correction and posing; joint rest orientation controls and reusable rig/pose library assets remain later steps.

## Binding and weight-correction milestone — 0.18.0

Step 3 adds **Rest / Weights** icon tabs inside the Rig properties, sharing the joint hierarchy and Front/Side/Top controls. Choose a model with rest joints, open Weights and use Bind. Automatic binding runs off the UI thread, checks that the target is still current before applying its result, and creates up to four normalized influences per vertex using distances to rest joints and bone segments. Each instance owns its binding and weights; duplicating or editing an instance does not change another instance or the global model library.

Once bound, rest creation, deletion, naming, parenting and coordinate editing are disabled, with an instruction to unbind first. Joint selection remains available. Unbind removes the weights and enables rest editing again; Undo restores the binding. Binding and weight changes belong to project history, save/reopen and recovery. Project format 11 and scene schema 3 retain them; formats 1–10 load without inventing bindings. Binding budgets are checked before insertion and on load.

Weights provides Add, Subtract and Smooth brushes, a 1–250 logical-screen-pixel radius and 0–100 percent strength. Pen pressure scales strength. The selected joint's influence appears with a blue-to-cyan-to-yellow-to-red legend from zero to full weight. Painting affects front-visible vertices; hidden and rear surfaces are protected. Smooth approaches the falloff-weighted mean of selected-joint weights beneath the brush. This is vertex-based painting with brush-local smoothing; mesh density limits detail. Weights stay normalized with at most four influences. A vertex's sole influence cannot be reduced until another influence is added. Each completed stroke is one Undo action and Escape cancels a pending stroke.

The **Bend check** offers temporary selected-joint X/Y/Z rotations of −90 to 90 degrees, inherited by descendants, to inspect the binding. Reset returns to the rest shape and re-enables painting; painting is disabled while the check is nonzero. Joint/model changes, leaving Weights and switching away from 3D Rig clear the check. Preview angles, brush mode, radius and strength are transient and do not create saved poses or project-history entries. Numeric controls retain pending text during preview updates; Save resolves entered values while keeping transient settings outside the project file.

This milestone did not include FK/IK posing or pose saving; step 4 below adds them. Imported glTF skin/animation data, mirrored weight painting, topology-aware smoothing, weight transfer and reusable rig/pose library assets remain deferred. Automatic weights need artist review and are not anatomical joint detection. The starter mannequin is still supplied as an unbound static model. Bound models, temporary deformation, heat colors and rig handles remain drawing references excluded from both PNG exports. Final validation evidence is recorded with the release's build results rather than inferred from these requirements.

## Current-pose milestone — 0.19.0

Step 4 adds **Pose** beside Rest and Weights, with the same joint hierarchy. A visible, unlocked, bound model can be posed through FK local joint rotation and two-bone IK. FK uses an on-canvas rotation ring with selectable X/Y/Z axis and exact slider-entry controls; selecting a root also exposes whole-skeleton body translation and direct canvas dragging. Descendants inherit joint rotations. IK requires a selected endpoint with two nonzero parent bones, and provides endpoint/pole canvas handles, precise coordinates and a model-local pin toggle. A pinned target stays fixed when the body or parent joints move; transforming the whole model moves the target with it. Direct FK edits of joints controlled by a pinned chain require unpinning first.

Per-axis joint limits are optional, constrained to ±180 degrees and must include zero. The solver respects limits and retains an unreachable requested target with visible status. Mirror operates on the whole current pose using the rest skeleton's paired joints. Reset selected clears its rotation and any IK controls that touch it, and clears body translation for a root; Reset all clears rotations, body translation and IK controls while retaining limits. Canvas/numeric drags make one Undo step; Escape or Undo cancels the active edit. Save commits pending exact entry before serializing.

The current pose is instance-owned project content, including rotations, root translation, joint limits and IK targets/poles/pins. Version 12 / scene schema 4 preserves it in save/reopen and recovery; formats 1–11 open with identity poses when no pose exists. Duplicated instances remain independent and the immutable global library model is unchanged. Rest and Weights show the selected instance at rest without deleting its pose; the temporary weight Bend check remains separate. Leaving those edit modes restores the posed reference for drawing. Unbind clears both binding and pose, with Undo restoring them.

This milestone does not create a reusable named pose library, imported rigs, animation, rest-joint orientations or general multi-bone/full-body IK. Current poses remain reference-only and are excluded from both PNG exports. Build/test evidence is recorded separately after validation.

# Saving and workspace restoration

Project files must preserve movie and shot metadata, output settings, drawing extent, sheet configuration, and artwork. Later panel specifications will define how their content is stored within the same project.

Version 0.74 saves named user workspaces and restores the last active arrangement when reopening the application. A Reset Workspace command restores accessible panel positions without modifying project content.

Autosave and recovery must protect artwork and metadata. Save failures must be visible and must not be reported as successful saves. Changing a view’s zoom, pan, or rotation must not be treated as an artwork edit.

# Acceptance criteria

The initial project and workspace experience is complete when:

1. New Project creates one movie project containing its first shot.

2. The configured output width and height match the blank shot’s frame.

3. White margins and a top production header appear without changing output dimensions.

4. Movie title, shot/cut number, and shot/cut name can be edited and survive reopening.

5. Artwork can exist beyond the output frame and survives save/reopen.

6. Production annotations can remain excluded when off-frame artwork is revealed by a camera move.

7. A production sheet image contains the selected picture, header, and margins; a movie frame excludes them.

8. A fresh workspace starts with blank artwork and provides a discoverable way to add panels.

9. Normal panels can be opened, moved, resized, floated, tabbed, docked and closed without losing document content. Icon columns retain their fixed width and full workspace height on either side; they can be moved, hidden and restored independently.

10. Navigator zoom, rotation, fit, and drag-to-pan update the main canvas while leaving artwork and shot camera data unchanged.

11. Saving and reopening restores the document accurately; restoring or resetting a workspace does not alter it.

12. Adding a future panel type does not invalidate existing workspace layouts.

These criteria describe planned behavior, not implemented or tested features.

# Proposed defaults and later decisions

Suggested starting defaults are 1920 × 1080 pixels, 24 FPS, an extendable 5-second first shot, shot number C001, RGB color with an sRGB profile, and 8-bit channels. These are editable product defaults, not restrictions or confirmed production settings. Bit-depth options and higher-precision effects require separate engine decisions.

Still to define:

- Exact margin sizes, header height, typography, and metadata field order.

- The relationship between drawable paper margins, larger background artwork, and annotation editing.

- Which panels open in optional saved starter workspaces.

- Specific tools, layer controls, color controls, references, effects, and animation controls.

- Export file formats, print-sheet sizing, and any exact AJA-compatible preset.

- Supported canvas sizes, memory limits, performance targets, pen hardware, and OS versions.

C++20 with Qt 6 remains the proposed technical foundation. The UI shell must support the docking behavior described here; the choice of Qt Quick, Qt Widgets, or a combination must be validated against that requirement. The drawing engine, 3D integration, and multimedia components remain subject to technical prototypes.

# Additional layout reference

[Krita’s Japanese Animation Template](https://docs.krita.org/en/user_manual/japanese_animation_template.html) separates layout paper from drawing stages and timing sheets. It provides a useful visual reference for a framed drawing surrounded by production paper; its full layer structure is not required for the initial blank workspace.

[Studio Ghibli’s explanation of animation layouts](https://www.ghibli.jp/event/layout/) describes a layout as the shot’s visual plan, including relationships between characters and backgrounds, movement, and camera instructions. That purpose matches the requirement that a single exported sheet carries the context needed to understand its shot.


# Editable vector brush workflow

Confirmed on 15 October 2026: ordinary drawing brushes also work on a vector layer and retain their rendered brush character. Store editable path geometry and brush source data. Add Path Edit with a selection bounding box, move/resize/rotate, node editing, stroke width and color. Provide path/layer simplification and local Simplify Brush. Dynamic width includes both direct width controls along the path and a brush that thickens or thins selected regions.

Version 0.14 implements this through shared brush rendering and a Vector Editing group with Path Edit, Simplify Brush, Width Brush and Vector Eraser. Path Edit includes opacity and three icon tabs. Strokes retain sampled resources in project format 8; editable source and raster display tiles remain separate. [Drawing tools](drawing-tools.md#editable-vector-brush-strokes) describes the supported interactions and bounds. This is the agreed drawing workflow; animation and property keyframes remain deferred.


## Property animation milestone — 0.21.0

The artist requested drawing on a layer, moving through time and editing layer transforms or vector paths to animate them. The implemented scope uses the existing Timeline and Frame Properties docks, with Drawing/Animation tabs, enabled property channels, automatic current-frame keys, draggable key diamonds, direct transform handles and compatible path pose editing. Layer and folder X/Y position, X/Y scale, rotation and opacity support Hold, Linear, Ease in/out interpolation.

Vector path morphs retain the same source and topology, including captured sampled-brush resources. Node/control-point, width, style and affine-pose keys interpolate without altering canonical source artwork. Source mode exposes the original drawing for painting; existing animated paths require leaving Source or disabling their animation before editing. Keyframes and exposure timing remain independent and use the same shot clock. Audio, animated cameras/3D rigs, graph editors and automatic matching of unrelated paths are not part of this implementation. Detailed persistence and bounds are recorded in [architecture](architecture.md).
