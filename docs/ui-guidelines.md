# UI and interaction rules

Established with the artist on 14 October 2026 and updated through 0.76.0. These rules govern UI work and revisions to existing panels. Later user requests supersede earlier control choices, including the numeric-field rule below.

The artist returned to timeline and animation work on 2 November 2026. Apply the same compact drawing-control rules to animation navigation and inspectors; keep drawing workflows intact. The existing animation foundation remains available: version 0.20 introduced frame-by-frame drawing; 0.21 added layer transforms, opacity and compatible vector path morphs; 0.22 added timeline selection, bulk key editing, exposure dragging/trimming and playback ranges. Version 0.23 added canvas motion paths and shared pivot controls; 0.24 added audio waveform rows and synchronized playback; 0.25 added video export, and 0.26 added camera framing and pose keys.

## 1. Keep frequent actions close

- Make common actions available directly, with one click or a direct drag wherever practical.
- Keep frequently used controls visible near the tool, object or selection they affect. Reserve overflow menus for occasional actions.
- Switch tools and setting categories directly; avoid requiring a menu-opening click before every switch.
- Update previews while interacting. Avoid an extra Apply step for routine tool settings; keep Apply/Cancel where an operation needs a deliberate commit, such as the existing pixel transform.
- Remember the active tab and useful panel state. Preserve keyboard shortcuts and canvas focus while using tool controls.

## 2. Use icons first, with text where it earns its space

- Use compact icon buttons for recognizable tools and actions. Continue Lucide alongside matching original drawing-specific SVGs. Shared logical-pixel dimensions live in `src/ui_metrics.h`: ordinary actions use 16 px icons in 28 px targets, tool identities use 18 px symbols, and detailed symbols can retain 20 px where needed. Tools and Add Tools retain a proportional 46 × 24 preview column; named tool rows use a font-aware 30 px baseline. Toolbox is 40 px wide with 32 px rows and a 24 px preview slot that accommodates its scrollbar and group badge. Layers uses a font-aware 32 px baseline and 24 px thumbnails. Compact drawing and dock tabs use 16 px icons with a 28 px baseline that can grow for larger fonts. Preserve SVG proportions and high-DPI rendering. Detailed editors outside this pass may retain their existing 18 px actions.
- Use the shared semantic drawing-icon registry for Arc/Bézier, pressure/tilt, texture/tones, guides, warp, gradients, blur and transparency/clipping state. Reuse a feature’s identity across Tools and Properties. Preserve user-selected symbols, uploaded group images and saved tool IDs when upgrading built-in defaults.
- Every icon-only control needs a meaningful tooltip, an accessible name and a clear active/disabled state. Include an available shortcut in its tooltip.
- Use a shared **600 ms hover delay** for icon tooltips, including tabs and dynamically created controls. Moving to another icon starts the same delay. Hide tooltips during mouse/pen interaction so they do not cover the work. This is the application standard, not a claim of a universal platform delay.
- Use short visible text when an icon alone would be ambiguous, and for names, units, exact values, important decisions and error messages.
- Remove instructional paragraphs, repeated explanations and redundant labels from everyday working panels. Put help in tooltips or focused help surfaces.
- Keep controls comfortable to target with a pen; compact appearance must not mean tiny hit areas.

## 3. Choose the control that explains the setting

Every exposed scalar numeric field must offer a visible slider beside a compact manual-entry box. The artist can drag for quick adjustments or type an exact value without switching modes. This 0.12.1 request supersedes the previous allowance for numeric-only or scrub-only fields. Keep the slider and field synchronized with matching precision; exact entry can retain wider valid bounds where a smaller slider range is more useful. Model refreshes must not create edits or interrupt an in-progress entry.

Numeric boxes use a compact 48-logical-pixel width, sized around three ordinary digits. Do not display unit suffixes inside them; retain units in existing labels, tooltips and accessible descriptions. Omit unnecessary decimal zeros while preserving fractional precision. This is a width rule, not a three-digit value limit: larger values remain editable through the field's normal horizontal scrolling, with the full value and unit available in its tooltip.

Preserve direct visual manipulation for spatial or coupled settings. “Playful” means responsive controls that make their effect understandable through interaction and preview. Canvas handles, brush previews and color widgets remain available; their exposed numeric controls also use the slider-and-entry pair. Keep the label, slider and number on one row when space permits. The shared `PropertyRow` aligns labels and reserves at least 128 pixels for its editor (a 76-pixel slider, 4-pixel gap and 48-pixel number). The default label band is 64 pixels, growing to fit its actual text or accessory. For long labels or narrow widths, place the label above the pair rather than squeezing the track. Resizing must preserve focus and unfinished numeric text. Use a practical slider range for very large coordinates while retaining the documented exact-entry limits.

Interaction patterns:

| Setting | Target interaction |
| --- | --- |
| Brush angle and roundness | A brush-tip preview with rotation and squash handles; compact exact entry alongside it when needed. |
| Brush spacing | A live stamp-strip preview with an adjustable gap. |
| Brush input response | A five-point graph with draggable handles and Linear, Soft, Firm and Invert presets; keyboard selection and vertical adjustment remain available. |
| Scatter and count | A stamp-distribution preview that shows spread, axes and density as they change. |
| Pen pose | An interactive pen/tilt diagram for direction and inclination, with explicit override states. |
| Symmetry | Recognizable preset tiles and draggable guide centers/axes on the canvas. |
| Texture | Pattern thumbnails and a live texture preview with direct scale/position controls where supported. |
| Small sets of modes | Icon segments, visual preset tiles or toggle buttons. |
| Transform | Canvas handles first; paired sliders and compact exact entry for precision. |
| Color | Visible color chips and an interactive picker/palette instead of repeated dialog-opening actions. |

These examples establish an interaction direction, not additional implemented engine features. A widget must map to real behavior. For example, an editable pressure-response curve also requires pressure-curve engine support; it cannot be added as a decorative UI replacement.

## 4. Switch features with tabs

- Use persistent tabs for feature/category switching, including the twelve brush configuration categories.
- Prefer icon tabs when the category has a recognizable, distinct symbol. Use a short label where meaning would otherwise be unclear.
- Provide tooltips and keyboard navigation for every icon tab. Keep the selected tab unmistakable using shape/background/indicator as well as color.
- Choose horizontal or vertical tabs based on the panel's proportions. Longer sets can scroll while retaining the same tab navigation model.
- Use dropdowns selectively for genuinely long value lists, such as blend modes; avoid using them as the main navigation between features.

## 5. Use an Adobe-inspired color system

The artist requested an Adobe color design theme. The working interpretation is **Adobe Spectrum's dark UI color system**, with neutral charcoal/gray surfaces and readable neutral text/icons. The artist’s 0.36 refinement replaces the saturated blue chrome with silver accents and gray selection fills. Shared tokens are defined in `src/ui_theme.h`; these customized values are inspired by Spectrum, not claimed to be its exact token set or Photoshop’s application colors.

- Define shared semantic theme tokens for canvas surround, panels, raised surfaces, borders, primary/secondary text, icons, hover, selection, focus, disabled and feedback states.
- Keep the neutral surface hierarchy and semantic roles consistent. Reuse the tokens across the main window, floating toolbar, tabs, controls and previews.
- Use accent color purposefully; let the artwork provide most of the color on screen.
- Communicate state with an icon, indicator or necessary text as well as color. Keep keyboard focus visible and text/icons readable against their actual surfaces.
- Theme changes affect editor chrome. They must not tint artwork, production paper, imported references or exported images.

## 6. Keep interaction consistent and responsive

- Reuse visual widgets and behavior across panels. A control should respond to drag, click, precise entry and reset consistently wherever it appears.
- Keep previews responsive on large canvases. UI animation and hover must not unnecessarily re-render document pixels or modify history.
- Group a document-editing drag into a meaningful undo operation; transient tool settings and canvas navigation remain separate from artwork history.
- Use two panel layout types: normal resizable docks, and fixed-width icon columns spanning the workspace height on either side. Toolbox uses an icon column; Tools retains a normal dock with names and previews. Keep columns independent of dock splits and tabs. Future brush-size, opacity and preset columns can reuse this container. Make both types work at high DPI.
- Distinguish implemented UI from planned designs in documentation. Apply these rules as panels are revised; do not silently remove working capabilities during a visual redesign.

## References

- [Clip Studio Paint: customizing Tool and Sub Tool palettes](https://help.clip-studio.com/en-us/manual_en/150_tools/Customizing_the_Tool_and_Sub_Tool_palettes.htm) — visual brush previews, icon-only group tabs and flexible property presentation.
- [Clip Studio Paint: brush tool customization](https://help.clip-studio.com/en-us/manual_en/240_brushes/Customizing_brush_tools.htm) — organization of detailed brush settings. The direct-manipulation widgets above are Toon Pro design proposals, not claims that Clip Studio uses those exact widgets.
- [Adobe Spectrum: colors](https://spectrum.adobe.com/page/color/) — shared color roles, surface hierarchy, contrast and restrained emphasis.

## Implemented in 0.8.0

The drawing workspace uses icon actions, delayed help, brush category tabs, direct tip/spacing/scatter/pen-pose/symmetry-center/texture-scale widgets, exact numeric fields, an inline HSV color picker, layer-row visibility/lock buttons, direct selection-mode segments, and a shared neutral gray/blue theme. Canvas handles remain the primary transform interaction. Existing engine behavior and storage formats are unchanged. Some advanced controls still use compact fields or dropdowns where the choice is a scalar or long list.

Palette source: [Adobe Spectrum Design Data](https://opensource.adobe.com/spectrum-design-data/tokens/color-palette/), verified 14 October 2026.

## Applied to guides in 0.9.0

The [perspective guide tool](guides.md) uses four icon tabs for type selection and two property tabs for selected/shared settings. Canvas handles are the primary spatial controls; rectangular limits retain compact exact fields. The assistant list exposes visibility, lock and snapping directly. Shared/custom opacity remains a simple slider, while naming, density and Hex colors use short explicit fields. New icons follow the same delayed-help and accessible-name rules.

## Applied to extended guides in 0.10.0

Three-point and fisheye perspective extend the same type-tab navigation, list actions and shared/selected properties. Canvas handles continue to define geometry directly: three vanishing points plus a grid anchor, or two ellipse-axis endpoints plus a perpendicular-radius handle. These types reuse density, color, opacity and area controls rather than introducing a numeric geometry form.

## Applied to Layers in 0.11.0

The Layers panel keeps blend mode, scrubbable opacity and lock/clipping actions above its independently scrolling list, with creation/duplicate/delete icons below. Rows are 44 logical pixels tall, with an aligned eye column, a 32-pixel content thumbnail, optional Notes/clipping marks, inline name and lock status. Names edit in place. Folders indent and collapse; selected rows move through drag-and-drop with an accent insertion line. Row colors use a narrow mark and restrained tint. Visibility sweeps and opacity scrubs update live and commit once per gesture. Context menus hold less frequent actions. Vector/text controls were unavailable in 0.11; version 0.12 enables their document engines and type badges.

Reference: [Clip Studio Paint: Layer palette](https://help.clip-studio.com/en-us/manual_en/180_layers/Using_layers.htm), checked 14 October 2026, plus the artist's supplied layer-panel illustration.


## Applied to drawing tools in 0.12.0

Drawing categories use icon tabs and direct tool actions. Gradient stops are draggable color handles, with keyboard position edits and foreground/color actions. Shapes use stroke/fill toggles and exact scrubbable width. Bézier handles, perspective corners and a warp mesh edit geometry on the canvas. New scalar settings use compact scrubbable values; text content, font family and paragraph layout retain necessary labels and controls. Selection properties include feather, named selection save/load/delete and view-only shading/animation settings. Reference colors can be sampled directly without changing reference navigation.


## Numeric controls in 0.12.1

The shared `ValueSlider` keeps a visible track beside a compact numeric field. It replaces standalone scalar fields in drawing properties, brush settings, layer opacity, selection and transform properties, guide settings and project configuration. Existing visual editors remain available. Layer opacity occupies its own row beneath blend mode, keeping the list and footer compact. Document-backed gestures retain their existing Apply/Cancel or single-undo behavior; tool settings and view changes remain outside artwork history.

Enter commits the exact value without activating a parent dialog's Create/Apply button; Escape cancels the current numeric edit. No-change drags close the gesture without creating undo history. This change adds no drawing-engine features and does not change the project-file format. The macOS build, all 40 CTest suites and 11 UI/interaction suites at Retina scale pass. Offscreen visual review checked narrow panels and project setup; native pen interaction remains a separate acceptance check.


## Raster navigation in 0.13.0

Toolbox and Tools are separate navigation panels with compact Add/Remove icons and a small folder marker on groups. The column follow-up makes Toolbox a fixed-width icon-only column; Tools keeps named rows and brush previews in a normal resizable dock. A standalone Toolbox entry yields one Tools row. The shared Add dialog uses Group / Standalone Tool tabs, search, necessary naming fields and icon choices; detailed settings remain in Tool Properties. Context menus provide Duplicate/Rename/Remove and organization Undo/Redo. Dragging reorders rows. The main toolbar contains project/view commands, and guide snapping lives in guide properties. Existing scalar controls and shared 600 ms delayed help remain in use.

## Canvas display preference

The header uses the short **AA** label for canvas smoothing because a generic blending icon is ambiguous here. Its selected state means smooth display sampling is enabled. A 600 ms tooltip and the accessible name “Canvas smoothing” explain the control; View exposes the same action. The choice is remembered with workspace preferences and never changes artwork history, saved pixels or exports.


## Editable vector tools in 0.14.0

Vector Editing uses the existing Toolbox/Tools navigation. Path Edit has three icon tabs for transform, nodes and local width. Canvas bounds, Bézier controls and width handles provide direct manipulation; selected width/opacity use the shared slider and compact entry, and color uses a swatch plus foreground action. Simplify Brush and Width Brush are independent tools. Their properties show radius, tolerance or strength as appropriate, with direct thicken/thin tabs. Each document-backed scalar drag previews live and commits once; cancelling restores the selected object's values. A short selection/status label replaces instructional paragraphs. New icons inherit the shared 600 ms help delay.

## Unavailable canvas actions in 0.14.2

Use a forbidden cursor and a concise bottom-bar reason for known tool/layer/selection restrictions. A blocked click or pen press briefly highlights the reason with the shared warning icon. Keep this feedback nonmodal; runtime errors stay readable for eight seconds instead of being replaced by pointer movement. Update hints when the active tool, layer, locks or selection changes, and clear them when the pointer leaves. Preserve legal alternatives such as panning, selection, sampling and raster-style brushes on vector layers. Do not alert for harmless no-op edits or repeatedly announce the same gesture failure on every move. Hover feedback must not composite artwork or enter undo history. Full text must remain accessible when narrow status areas elide the visible message.


## Pending transforms in 0.14.6

Changed transforms require Apply / Discard / Cancel before switching tools. Cancel keeps the old tool, panel selection, settings and preview; failed Apply also stays in the transform. No-change transforms leave without a dialog. Explicit Escape discards, and temporary Space panning preserves the preview. Keep pending artwork visible beyond the sheet while the pointer moves it; crop raster overflow only when applied.

## Tool organization in 0.15.0

Tools keeps a compact tag selector and search together on one row above the list, with an optional icon grid in its options menu. Filter changes must preserve the current drawing tool; show a clearable no-match state and disable actions for hidden selections. Icon tiles keep proportional brush previews, accessible names and the shared delayed tooltips. Tag/group labels belong to the local tool catalog, independently of artwork and brush paint color. Use narrow color markers so organizational color does not replace the selection/focus state.

Tool Management uses Groups/Tools tabs, direct group creation, a membership chooser and batch tags/colors. Browsing metadata must not activate painting tools. Group tags apply to filtering within that group without changing individual tags. Custom group images remain proportional and embedded. Keep existing rows stable during metadata refreshes so typing and selection do not interfere. Organization Undo/Redo follows focus in docked or floating tool panels; it must not undo artwork. The manager starts hidden and floating, leaving the drawing layout compact until the artist opens it.

Reference: [Krita tag management](https://docs.krita.org/en/user_manual/tag_management.html), checked 16 October 2026.


## Brush quick properties in 0.15.1

Keep Size, Opacity, Flow, Hardness and Smoothing in the drawing dock, with pressure-size and wrench icon actions. The wrench opens one modal advanced editor containing all eleven categories and existing visual controls. Use live settings and a single Close action: committed changes remain, Enter commits numeric entry, and Escape first cancels an unfinished numeric edit. Preserve category navigation and tool-specific settings. Hidden advanced editors do not rebuild during quick adjustments. The normal dock must stay narrow regardless of dialog width; short docks scroll their quick controls while keeping the wrench accessible.


## Static 3D references in 0.16.0

3D Library uses Starter/My Assets tabs, search, tags and favorites, with import/add/remove icon actions. It starts as a hidden floating dock. 3D Object and 3D Camera are ordinary catalog tools; their property panel uses Model/Camera icon tabs, direct manipulation modes and precise slider-entry pairs. Keep the object list compact for small scenes and scroll it for larger ones. Show object visibility/lock controls inline, with names edited in place. Camera and model drags commit one Undo action; Undo cancels an unfinished gesture first, while Save commits it. The ordinary canvas Hand/zoom/rotation remain separate from the saved 3D camera. Missing projects, locked scenes and unsupported imports receive readable feedback. Rendering feedback occupies a permanent single line so it cannot move controls or change the scroll position.

## Rest skeletons in 0.17.0

3D Rig adds a third icon tab with a compact joint hierarchy, inline names, direct add/delete/mirror actions and XYZ slider-entry pairs. Use the short label “Rest skeleton” and concise help describing the current rest-edit mode. Keep tree rows and partially typed values stable during model refreshes. Front/Side/Top retain text because those views are clearer by name. Joints and bones use consistent screen-size handles on the canvas; selected joints have a concise name label. Locked or hidden targets use the shared unavailable-action feedback. Numeric and canvas drags preview live, commit once, and cancel with Escape. Mirroring and display toggles are transient editor choices; joint structure and positions are saved project content.


## Skin weights in 0.18.0

Keep the joint hierarchy shared between Rest and Weights icon tabs. Weights exposes bind/unbind actions, the selected joint, a blue-to-red influence legend, Add/Subtract/Smooth tabs, radius/strength pairs and a temporary XYZ bend check with Reset. Bound models retain joint selection while rest editing is disabled with an explanation. Binding and surface-preparation status occupy a permanent single line; changing values or completing renders must not move controls. Preserve unfinished scalar input during unrelated refreshes. Weight painting uses a screen-size circular cursor and the shared unavailable-action feedback. Temporary brush and bend settings stay outside artwork history; each painted stroke is one undo action.


## Posing in 0.19.0

Keep the same joint hierarchy across Rest, Weights and Pose. Pose uses FK/IK icon tabs, a canvas rotation ring with an explicit X/Y/Z axis, and separate target/pole handles for limb positioning. Root translation remains direct on the canvas, with paired sliders and exact entry for precision. Expose pins, mirror and reset as compact icons with the shared delayed help; retain short labels for Body position, Limb control and Limits. Pins explicitly describe their model-local behavior in help. Limits use axis tabs and paired minimum/maximum values. Keep status on a permanent single line and preserve unfinished text during render/model refreshes. Save commits pending values; Undo/Escape cancels them before restoring the document. Rest/Weights previews must preserve the saved pose, and each drag is one history action.


## Frame animation in 0.20.0

Timeline and Frame Properties are separate normal docks. Track labels stay pinned beside the scrolling frame grid; explicit drawing blocks and their holds share a frame ruler and playhead. Transport, blank/duplicate drawing, exposure removal, looping and onion skin use consistently sized icons with accessible names and the shared 600 ms tooltip delay. Current-frame entry changes only workspace time. Track creation uses Static/Frame by frame tabs and Simple/Rough/Anime templates, with necessary names and a compact layer preview.

Frame Properties shows drawing name, Start and Hold using the shared slider/48-pixel unitless entry. Pending typed text survives unrelated refreshes; one timing adjustment commits once, and rejected collisions restore the saved timing. Scrubbing resolves pending transforms with Apply/Discard/Cancel and does not create document history. The Layers panel exposes only the current drawing's sublayers plus the static stack. Playback suspends drawing and onion overlays; current frame and onion preferences do not enter project files. Existing workspace layouts remain intact; open Timeline from Panels to begin.


## Property animation in 0.21.0

Keep Timeline and Frame Properties as the two animation docks. Drawing/Animation tabs separate exposure timing from property editing. Show only the selected layer's enabled keyframe channels in the timeline, with compact draggable diamonds. Each scalar channel has a direct enable diamond and a shared slider/48-pixel exact field; labels remain visible to distinguish X/Y, scale, rotation and opacity. Preserve pending numeric input during refreshes and group each gesture into one Undo action.

Animated canvas bounds provide move, scale and rotation handles, independent of destructive pixel transforms. A brush-icon Source toggle shows original artwork for painting; a move-icon toggle returns to animated handles. Use the shared bottom-bar feedback for blocked source edits, locks and incompatible path topology. Path morph uses the existing Path Edit controls and requires a selected vector path. Key navigation/add/delete remain direct icon actions; interpolation uses compact Hold/Line/In/Out/In-Out tabs with full tooltips. All icons retain accessible names and the shared 600 ms hover delay.


## Timeline editing in 0.22.0

Keep the existing pinned labels, frame grid and two animation docks. Selected diamonds have a distinct fill/outline. Shift-click extends a range, Ctrl/Cmd-click toggles, and dragging empty key-lane space creates a selection box. Drag selected keys together; expose Copy/Paste/Delete through shortcuts and the context menu. Each batch is atomic and one Undo step. Preserve source values and outgoing interpolation when copying; never silently overwrite keys.

Expose drawing movement through block bodies and hold resizing through both edges, with a horizontal resize cursor. Preview timing until release and mark exposure collisions. Escape/Undo cancels unfinished drags; Save and navigation resolve them. Use stable identities through refreshes, and cancel a gesture if its source changes externally.

Shift-drag the ruler to define an inclusive playback range; show its endpoints and make them draggable. A compact icon toggle, range label and settings button provide the alternate exact-entry route using shared ValueSliders. Range controls must fit a 240-pixel dock. Playback range is temporary workspace state and does not crop exports. All new icons retain accessible names and the shared 600 ms delayed tooltip.


## Motion controls in 0.23.0

Keep Transform, Motion Path and Pivot as compact, mutually exclusive icon modes in Frame Properties → Animation, beside the existing Source workflow. Use the available move/spline/focus icons, recognizable active states, accessible names and 600 ms help. Show the pivot's X/Y slider-entry controls and center/reset only in Pivot mode. Preserve pending typed values across refreshes and keep feedback height fixed.

Motion Path displays evaluated pivot travel, exact position-key markers and a distinct current-frame marker. Dragging changes position at that key time without moving the playhead. Keep labels sparse and readable; cache path geometry across view-only changes. Bound curve sampling on long shots, and never connect Hold jumps as if they were continuous movement.

The pivot is shared across time. Explain through its tooltip that moving it preserves the current pose by offsetting position keys and may change other rotated/scaled poses. Use a constant screen-size crosshair, exact source coordinates and one-action reset. Canvas overlays remain outside artwork and exports. Preserve one Undo per gesture, Escape cancellation and Save resolution.

## Audio in 0.24.0

Audio appears as waveform rows in the existing Timeline, with direct body movement and source-preserving edge trims. Keep audio selection independent of artwork selection. Selecting a clip opens Frame Properties → Audio, with a name field, mute icon and the shared slider/compact-entry controls for timing and volume. Keep partially entered values stable during refresh and preserve exact source samples on unrelated edits.

Import/cancel actions use music and close icons with accessible names and the shared 600 ms tooltip delay. Progress occupies a fixed-height single line. Generate waveform peaks once per immutable source and draw only visible portions. Clip drag previews commit once; Escape/Undo cancels them and Save/navigation resolves them. Errors use the shared nonmodal feedback. Audio shares the existing play/pause/loop/range transport.


## Video export in 0.25.0

Keep export in one compact modal reached from File. Use Full shot/Custom range tabs, an explicit icon action to copy the playback range, and a visible project-FPS/duration summary. Use the shared slider/48-pixel unitless fields for range and dimensions, with an aspect-lock icon and Draft/Standard/High tabs. Retain clear labels for frame numbers, dimensions, audio inclusion, destination, Export and Cancel. Icons keep accessible names and the shared 600 ms help delay.

Commit pending editor/numeric drafts before taking the export snapshot. Default to the full shot, independent of rehearsal range; never move the playhead to export. Keep status/progress height stable, freeze settings during encoding and wait for cancellation acknowledgement before closing. Preserve failed-export settings for retry. Normalize the MP4 extension before checking overwrite, and ask only once at Export. Use a nonnative file chooser on macOS so the native chooser cannot add an earlier replacement prompt. Explain the opaque white video background in the compact summary; PNG controls remain unchanged.


## Camera animation in 0.26.0

Camera lives in the existing Frame Properties dock with 2D/3D tabs, a clear target label, one animation diamond for the complete pose, shared scalar controls, direct framing/view icons and the existing key/easing actions. Keep fields stable while editing and status height fixed. Camera rows follow the existing timeline rows and preserve the artwork selection; only explicit 3D canvas editing selects the scene layer. New icons retain accessible names and shared 600 ms tooltips.

Separate layout framing, camera output view and workspace navigation. In layout framing, drag inside the camera rectangle to move, corners to zoom uniformly and the outside round handle to rotate. Brush/selection/guide coordinates and cursor size/orientation must follow the same evaluated camera as the displayed artwork. Camera controls never write workspace pan/zoom/rotation into the project. Draft gestures retain one-action Undo, Escape cancellation and Save/navigation resolution. Do not display stale 3D camera imagery under a new timeline frame while an updated reference is rendering.


## Effects in 0.27.0

Use five compact icon tabs for Mask, Blur, Shadow, Glow and Color. Keep the active-layer name and a fixed-height status line above them. Each effect has a direct enable switch; scalar controls retain the shared slider and 48-pixel unitless entry. Shadow placement also uses a draggable, keyboard-accessible offset pad. Color choices use swatches with live preview and cancellation. Mask actions stay close to the thumbnail; Hide/Reveal uses clear text tabs because those opposite actions must be unambiguous.

Adjustment layers expose only Mask and Color. Disabled actions follow actual layer/folder locks, Notes/3D restrictions and playback. Preserve drafts, tab choice, scrolling and panel width during refresh. Each document gesture creates one Undo; Save/navigation resolves drafts and Undo/Escape cancels unfinished edits. Icons retain accessible names and the 600 ms tooltip delay.


## Advanced motion in 0.28.0

Keep custom easing in Frame Properties → Animation beside the existing outgoing-key presets. A compact graph uses draggable circular handles, keyboard adjustment and an accessible segment description; avoid four redundant numeric controls. Preserve its drag state through live refreshes and keep the graph/status height fixed. Custom curves stay inside the normalized graph with ordered time handles.

Motion Path mode exposes Curve and Follow path directly. Show incoming/outgoing circles for the selected key, constant screen-size hit targets and sparse stems. Keep ordinary key movement direct; Alt-drag recovers a collapsed outgoing handle and Alt+Shift a collapsed incoming handle. Rotation becomes Rotation offset while following the path. Curved X/Y keys share timing and selection, and a newly inserted key subdivides existing motion. All graph/canvas gestures retain one Undo, Escape cancellation and Save/navigation resolution. New icons use shared colors, accessible names and 600 ms tooltips.


## Applied to timeline improvements in 0.29.0

Timeline retains its compact transport and frame controls. The lower action row adds named, delayed-help icons for onion settings and explicit Ripple time; ordinary timeline dragging keeps its existing scope. Ripple uses Insert/Delete/Move tabs, slider-and-entry frame controls and a stable result/error area. Apply is required because it changes timing across all tracks; Cancel leaves the shot unchanged.

Add Track embeds a top-to-bottom editable layer recipe with Raster/Vector choices and icon actions for add/remove/reorder/save. The preset picker combines built-ins and the potentially long saved list. Saving a named recipe is an explicit global-library action, independent of creating a track. Onion settings preview live with two color/count sections and shared opacity; Reset is reversible with Cancel, and OK remembers the workspace preference. Numeric controls use the same 48-pixel unitless fields and remain outside artwork Undo for view-only settings.


## Brush Management in 0.30.0

The manager starts hidden and floating, with direct icon actions, tag/search on one row, category/type filters, cached brush previews and a compact selection inspector. Multi-selection applies tags/categories or group membership in one organization edit. Browsing does not switch drawing tools; Use, double-click or the advanced-settings wrench activates intentionally. Delete has no confirmation and uses organization Undo. Creation uses New brush / Current brush tabs and recognizable type tiles; stamp images retain a visual preview. Keep import reports behind a details action, retain pending text during refreshes, and defer brush previews while a stroke is active. All icons use the shared delayed tooltips and accessible names.


## Shots in 0.32.0

Shots starts hidden and floating. Keep create, duplicate, delete, reorder and export as compact named icons. Use a stable row list with editable cut number/name and a short duration column. Scene filtering and assignment use necessary text. Selection opens a shot through the shared pending-edit resolver; canceled navigation restores the list selection and artwork preview. Multi-selection supports batch scene assignment, deletion and queued exports without changing movie order implicitly. Keep inline drafts stable on unrelated document updates and retain the shared 600 ms tooltip delay.

The video export dialog uses Current shot / Movie / Shot queue tabs. Queue entries preserve their individual range and output settings. Keep progress, cancellation, skipped existing files and failure outcomes visible; do not overwrite an existing file without explicit approval.

## Audio finishing and drawing refinements in 0.33–0.35

Audio retains quick Volume above Clip/Mix tabs. Use the waveform itself to shape fades and place volume keys; dragging and keyboard edits form one Undo action. Keep crossfade partner names visible and preserve source-position timing through trims. Audible scrub is an explicit remembered Timeline toggle. WAV export uses the same shot/movie/range vocabulary as video export, with progress and cancellation.

Colors exposes a compact named-palette chooser and direct swatches. Creation, rename and deletion use compact actions; swatch names appear in help without filling the panel with labels. Preserve the selected palette across sessions. Inline Text editing starts at the canvas position, supports multiline input and resolves on Save/navigation. Escape cancels the draft, and text-editor Undo stays inside that draft. Font/layout controls remain in Tool Properties, with the shared numeric controls and clear labels. Moving between the properties editor and inline editor must retain the latest value and a coherent Undo history.

Automatic recovery compresses and writes in the background without refreshing drawing controls during pen contact. Explicit Save offers cancellable progress. Cancellation or failure preserves the destination, and edits made after a snapshot remain unsaved until a later save.


## Compact drawing panels in 0.36.0

The first density pass covers Toolbox, Tools, brush quick properties, Layers and Colors. Toolbox is 44 logical pixels wide with 34-pixel icon rows; named Tools rows are 32 pixels tall. Add/Remove remain direct actions, and tag filtering and search share one line. Brush quick properties use responsive inline rows with Size pressure beside its label and the advanced wrench kept above the scrolling controls. Layers uses 36-pixel rows, 28-pixel thumbnails and compact pinned actions. Colors keeps its picker, Hex field, palette chooser, Add Swatch and six-column swatches; occasional palette/swatch management is available from the overflow and swatch context menus.

The theme uses charcoal panels, gray selections and silver focus/slider accents. Compact styling retains 28-pixel control targets and 48-pixel exact fields. The 600 ms help delay, pen-priority behavior and existing edit/Undo boundaries remain in force. This pass does not reorganize advanced brush categories, animation, 3D or the other inspectors.


## Compact drawing inspectors in 0.37.0

Use the shared responsive property row in selection/transform, fill, gradient, shape, text, vector, guide and Navigator controls. Keep visual gradient stops, path and guide canvas handles, text content and direct tool-mode tabs. Properties should reflect the current tool; hidden Text pages must not force simple Fill controls to scroll. Allow long labels to wrap above useful tracks and retain exact-entry focus and drafts when docks resize.

Selection/transform actions remain directly accessible; Apply/Cancel is pinned below the scrolling properties so a short dock never hides the completion step. Guides keeps all six type icons visible at 220 logical pixels and combines selected-guide naming with its flags. Ordinary icon actions use 28-pixel targets and 18-pixel icons.

Dock labels stay short and stable: Properties, Frame and Navigator. Panels-menu entries and accessibility keep the full panel name; Properties help retains the active tool’s name. Native dock tabs use 16-pixel icons, compact padding and normal scrolling when needed. Floating, re-docking and saved layouts retain Qt’s existing behavior. This does not reorganize advanced brush categories, management libraries, Effects, References, 3D or timeline inspectors.

## Advanced brush and management panels in 0.38.0

The advanced brush dialog uses responsive label/editor rows across all eleven categories, 28-pixel category/action targets and smaller group spacing. Keep the tip, spacing, scatter, pose, symmetry, texture and input-response widgets at useful sizes. Conditional settings hide their complete row; jitter cadence sits beside its label. Close stays outside the scrolling pages. The modal opens at 480 × 600 pixels and supports 360 × 400, preserving drafts, selected category and scroll positions. Custom symmetry symbols provide high-DPI pixmaps with consistent logical proportions.

Brush Management uses 32-pixel rows, proportional 46 × 24 previews and a selected type/category heading. Keep Add, Import, Duplicate, Delete, Undo/Redo, search and filters directly accessible. Use and advanced settings remain above an independently scrolling metadata editor. Tool Management keeps Groups/Tools tabs, pinned search/create/delete and history controls, compact Name/Tags/Color rows, direct icon choices and membership. Lists use additional available height instead of leaving unused space below the editor. Both managers support 240-pixel widths and default to 360 × 600 in a new workspace; saved geometry stays intact.

Status feedback reserves a stable row with full text in help/accessibility. Browsing does not activate tools; organization edits retain their separate Undo, batch semantics and pending text. Brush previews remain deferred during pen contact. New-brush type tiles retain labels where needed, with compact metadata rows and a visible image preview for stamps. This pass changes layout and icon rendering, not brush behavior or saved formats. References and Effects are the next drawing UX pass.

## Compact References and Effects in 0.39.0

References uses 28-pixel Import/Remove/Fit actions with 18-pixel icons, a horizontal image-only thumbnail strip and an expanding image viewer. Retain full reference names for keyboard navigation and accessibility, with name/dimensions in delayed help and a fixed-height elided caption. Proportional thumbnails provide high-DPI images. The shared Zoom row keeps its slider and exact entry visible, and a fit-on-resize update must not overwrite an unfinished numeric draft. Reference navigation and sampling remain view-only; imports/removals retain document Undo.

Effects keeps the target, stable status and five compact category tabs above the scrolling body. Scalar settings use responsive property rows; color swatches stay directly accessible. Measure only the active category so a simple Blur page does not inherit Shadow's height. Remember each category's scroll position during the panel session, preserve exact-entry drafts through resize/refresh, and keep the Shadow offset pad and mask thumbnail usable. Actions use 28-pixel targets and 18-pixel icons with the shared delayed help. Document edit, cancellation and Undo behavior remain unchanged. Both panels retain their existing dock identities and saved workspace placement.

## Compact 3D Library and Object/Camera in 0.40.0

The 3D Library keeps Starter/My Assets tabs, tags/search/favorites on one row, and direct placement/import actions. Asset rows use a compact 32-pixel baseline with proportional 24-pixel icons, increasing for larger fonts when needed. Long names and tags retain full help/accessibility text. Reserve one status line so feedback does not move the list. Browsing and filtering must preserve matching selections without activating tools or changing artwork. New workspaces use a 320 × 480 floating library; retain existing saved geometry.

Object/Camera keeps the object list, stable feedback, category tabs and manipulation modes above the scrolling settings. Reset/framing and camera-lock actions stay at the bottom. Use shared responsive property rows for XYZ and camera values, and measure only the active page. Short panels reduce the visible object rows and let the list scroll independently. Remember each category's scroll position while the panel is open; preserve unfinished numeric entries and unchanged control geometry during render updates. Keep 28-pixel action targets, 18-pixel icons and the shared 600 ms help. The existing Rig/Weights/Pose controls remain accessible; their internal density pass is separate.

## Compact Rig, Weights and Pose in 0.41.0

Keep Rest/Weights/Pose navigation and skeleton flags pinned with the 3D category strip, and Front/Side/Top views in the footer. The joint hierarchy stays in the scrolling body, with one to four font-aware rows according to available height and joint count. Preserve inline joint naming, selection and expansion through unrelated updates. Measure only the active editing page and remember each mode's scrolling without resetting it on render updates.

Rest coordinates, weight-brush radius/strength/bend angle and pose values use shared responsive rows and 48-pixel exact fields. Ordinary actions use 28-pixel targets with 18-pixel icons and the shared 600 ms help. Weights keeps its selected-joint heat legend and front-surface context; its bend axis and reset sit together. Pose retains visible FK/IK and Target/Pole labels, rotation-axis buttons and direct joint-limit controls. Hide the complete body-position section for non-root joints; hidden FK/IK pages must not reserve extra space. Stable status rows retain full help/accessibility text. Binding, transient weight previews, saved poses, cancellation and Undo keep their existing meaning.

## Quick access and drawing mode in 0.43.0

Keep Quick access fixed across the top with 18-pixel icons in 28-pixel targets. The drawing-mode toggle and **+** customization control precede pins so they remain reachable when later items overflow. Use the shared 600 ms tooltip delay and accessible names; tools and foreground swatches show their active state. The **+** menu offers current-tool/current-color pinning, a searchable tool chooser, a color picker, and Hand, Zoom in/out, Fit sheet and Reset view rotation. Right-click a pin for Move left, Move right or Remove. Remember up to 48 pins and their order in workspace preferences, using tool identities rather than copies of brush settings. Removing a pin leaves the tool library unchanged.

**Tab** enters fullscreen drawing mode with only the canvas and Quick access toolbar as workspace chrome. Hide docks, side columns, project controls, menu and status bar, including floating panels; tool changes must not reopen them. Tab or the persistent return icon restores the prior dock arrangement, visibility and window state. Preserve ordinary canvas shortcuts and return focus to the canvas after toolbar actions. Closing in drawing mode saves the previous normal layout.

Do not intercept Tab while entering text or numeric values, using dropdowns, dialogs or open menus. Ignore mode changes during pen contact or unfinished canvas gestures so resizing cannot interrupt artwork. Playback stops on entry. Pin refreshes compare only bounded tool/color state, without generating brush previews or sampling artwork, and the workspace event filter leaves pen and paint events alone.

## Brush Preset in 0.44.0

Brush Preset is a normal resizable dock with a compact grid of size tiles. Keep a circle cue and the exact numeric size visible together; the circle communicates relative size rather than a rendered brush-tip preview. Clicking a tile changes size directly and returns focus to the canvas. Highlight the matching current size without altering other brush settings. Use neutral shared selection/focus colors, accessible names and 600 ms delayed help.

Keep **+** directly available. Add and Edit use the shared slider and compact exact field, with 1–256 px bounds and one decimal place; Add starts at the current brush size. Right-click holds Edit and Remove; the overflow menu holds Reset sizes to defaults. Keep the list sorted, prevent duplicate sizes and remember up to 64 entries independently of brush definitions and artwork history. Managing the list must not change the current brush size. Applying is limited to the active Brush/Eraser workflow; other tools and mask/camera/transform overlays retain their own controls. Guard active gestures at activation time without adding work to pen movement or interrupting a stroke.

Place the dock below Tools in a fresh or reset drawing workspace so sizes remain directly available. Preserve existing saved layouts; expose the panel through **Panels → Brush Preset**. Floating, docking, hiding and Tab drawing-mode restoration follow the existing workspace lifecycle. This addition does not change brush rendering or project formats.


## Advanced tip sets in 0.45.0

Keep tip-set editing in the wrench dialog's existing Tip shape category. Use a compact image grid with Add/Remove/order icons, selection-mode tabs, a draggable root cross and a shared Weight slider/entry for weighted Random mode. Preserve the selected tip while its anchor or weight changes. Full tip artwork and atlas assembly happen only during explicit tip edits, not pen delivery. The ordinary drawing dock keeps its existing quick controls. Primary size bounds may exceed three digits; preserve the fixed-width precise entry and existing size-preset tiles.

## Hold to shape and Arc in 0.46.0

Hold to shape uses one checkable wand action beside the brush wrench, with an accessible name and the shared delayed help. After a 700 ms pen pause and release, a compact thumbnail strip offers Original, plausible fitted shapes and Cancel. Keep it centered near the bottom of the canvas, wrap on narrow views, and contain mouse/tablet events so choosing a thumbnail cannot paint underneath it. The strip inherits neutral theme tokens; it adds no permanent property form. A chosen shape commits immediately as one Undo step. Moving again before release restores freehand, and the toggle persists independently of each brush preset.

Arc lives in the existing Shapes group and Add Tools, with existing shape stroke/fill controls. Show a live curve after the endpoints are released and short informational guidance in the status bar. Hover updates only the canvas preview; do not rebuild settings on each packet. Unavailable layers retain the existing forbidden cursor and reason.

## Editable stamp placement in 0.47.0

As refined in 0.47.1, use an explicit **Adjust stamp** row in quick Brush Properties with a compact move-icon button and visible **On/Off** text. The label and state clarify a mode that was too easy to miss as an icon alone. Keep the configured preference per tool; show the complete row only when sampled Brush input is available, including after mask/camera/keyframe editing returns control. Stamp/Colored Stamp metadata supplies the default until the artist explicitly changes it. Ordinary property edits must not convert that default into an override. Turning Off through the property button places any pending stamp before restoring continuous drawing; keep On if placement fails. Restoring saved tool preferences must never commit artwork.

After contact, show the existing move box, resize corners and rotation handle around only the new impression. Reuse the transform property rows with a short “Stamp placement” status. Hide unrelated selection commands. Keep Place/Discard icons available even when the artist normally hides the selection toolbar, and contain pen events inside the bar. Hide the bar during contact or a handle drag and restore it on release, including interrupted pen contact. Enter places, Escape discards, and a new contact outside the box places the previous stamp and begins another. Switching tools uses the existing Apply/Discard/Cancel decision, including an unmoved stamp. The selection mask remains fixed; it is not the placement box.


## Dynamic stamp workflow in 0.48.0

Use a Dynamic stamp tile in the existing New brush dialog. Keep multi-image management in a bounded thumbnail strip with Add/Remove and a count, and distinguish Foreground/Image colors through two labeled tabs. Preserve names, categories, tags and group assignment from the shared creation flow. Add complete batches atomically and retain the selected sources if a batch fails.

Keep everyday brush properties compact. Manage images/order/anchors in the existing Tip shape page, with Size/Rotation/Saturation variation rows nearby for multi-tip sets. These shortcuts explicitly mean variation between impressions; their values must reflect per-tip effects actually enabled. Image-color variation is opt-in for existing brushes and never changes alpha. Hide foreground/background mixing for colored tips. Dynamic stamp defaults to continuous drawing with Adjust stamp Off; ordinary Stamp/Colored stamp defaults remain unchanged.


## Connected and surface brushes in 0.49.0

Keep Stamps/Ribbon/Repeat as three tabs in Tip shape, and disable irrelevant stamp controls while connected drawing is active. Keep Adjust stamp unavailable for connected paths. Repeat length uses the shared compact slider and precise entry; alternate flipping is a small toggle. Preserve normal quick drawing properties.

Patterns and tones has its own advanced category with pattern tabs and a live preview. Perspective uses draggable quad corners and exact coordinates for the selected corner; avoid a permanent wall of corner fields. Show frequency/density for tones, tile dimensions/import for image patterns, and plane controls only when mapping is enabled. State frequency units in the tooltip. Clustered spray uses a distribution preview plus compact scalar rows; disable the ordinary scatter fields it replaces. Speed response and foreground tint remain in Transfer and Color dynamics respectively. New advanced widgets stay top-aligned in tall dialogs. All icons retain accessible names and the shared delayed help.


## History and Auto Actions in 0.50.0

Both panels are compact resizable docks available from Panels, initially hidden in the Layers tab group. History uses 28-pixel rows, current-state emphasis, subdued redo states and guarded navigation. Auto Actions uses an action selector, compact Record/Stop/Play controls, checkable step rows, plain-language parameter summaries and context management. All icon controls retain shared delayed tooltips and accessible names. Live pen samples do not rebuild either panel's list. Recording is explicitly scoped to supported operations; unsupported artwork edits stop with a concise reason. Replay validates the entire enabled sequence before committing one artwork Undo step.


## Shared drawing density in 0.51.0

Toolbox, Tools, Properties, Layers and Colors use the shared sizing roles described above. Quick brush properties have 4 px margins/gaps and 16 px action icons, preserving 28 px pen targets, the wrench, Hold to shape, pressure and explicit Adjust stamp On/Off. Drawing property pages use consistent tabs and semantic icons; advanced brush categories retain their arrangement. Scalar fields keep exact entry, active drafts and their existing edit/Undo boundaries.

Layer rows retain names, thumbnails, visibility sweeps, multi-selection, inline rename and pinned actions; their selected state includes a neutral edge marker. Colors uses 28 px foreground/background controls with a full-size interactive picker and 28 px palette swatches. Tools retains search and tags on one row, names or icon-only browsing, custom groups and deferred brush previews. Named rows and status text adapt to enlarged fonts.

Compact styling uses explicit component properties rather than ancestor-wide selectors, so a drawing panel cannot unintentionally restyle a modal editor. Dock tabs use shared spacing and still retain native keyboard navigation, accessible panel names and 600 ms help. No new per-pen-sample refresh work is added. Saved dock geometry and grouping are retained. The following 0.52 pass adds explicit workspace presets; Comfortable density remains separate.

## Drawing composition in 0.52.0

Use the Drawing arrangement for a fresh workspace: Tools, Sizes and Properties stacked on the left; Colors / Refs / Navigator tabs above Layers on the right. When these daily upper panels form their own group, share the available width across all three tabs, retaining icons, elided labels and full tooltip/accessibility names. Avoid scroll-arrow clicks for this small group; mixed custom groups retain normal tab scrolling. Preserve dock IDs and independent floating/resizing. Short headers retain full accessible and Panels-menu names. Existing saved layouts restore unchanged; apply the new arrangement only through the explicit View → Workspace presets or Reset workspace.

The original 0.52 behavior (superseded by the 0.57 drawer below) deliberately collapses the three drawing docks. A persistent current-tool summary with the exact brush size opens/closes them, and a foreground swatch reveals Colors. This is the existing dock stack, not a second set of controls or a floating overlay. Keep tool names elided before the exact size and retain full delayed help. Do not open the stack automatically on tool changes or window resizing. Preserve zoom when toggling it, ignore layout requests during pen contact, and reject pending edits without applying/discarding artwork. Tab drawing mode hides this context control and restores its prior workspace state when leaving.

Brush size tiles use the shared 40 px minimum-width, 38 px baseline-height and 16 px circle roles. Let the longest exact label and application font increase their size; keep keyboard focus and a selected edge visible. Recompute columns without retaining an unnecessary scrollbar after resize. Preserve preset values, precision, bounds, sorted order and Add/Edit/Remove/Reset behavior.

Quick access uses 16 px ordinary actions and 18 px tool/color identities in 28 px targets, with a 32 px strip minimum that can grow for text. Separate adjacent pin kinds without regrouping user order. Keep return/customize controls before overflow, preserve external workspace controls when pins rebuild, and retain the existing action and settings guards. Physical Huion validation precedes any automatic compact-mode behavior.

## Advanced brush and management composition in 0.53.0

The advanced brush dialog retains all 12 top-level categories and the quick drawing panel's existing controls. Use the shared 16 px action icons, 28 px targets, compact tabs, 4 px gaps and responsive property rows. Keep interactive graphs, stamp previews and pattern widgets at useful drawing sizes; reducing chrome must not shrink their interaction area unnecessarily. Keep Close outside the scrolling category content and retain the 480 × 600 default and 360 × 400 minimum dialog sizes.

Use direct Size / Angle / Roundness tabs within Shape dynamics and Opacity / Flow / Speed tabs within Transfer. Measure only the active response page and retain each response's scroll position when switching. Avoid duplicate section headings when the tab already names the response. Texture, Dual brush, Clustered spray and Speed response keep their enable toggle visible while hiding the complete inactive detail body. Toggling a feature off must preserve its configuration for re-enabling. Bounds, exact entry, live preview, edit/Undo semantics and control identities remain unchanged.

Brush Management keeps the selected-brush Use and wrench actions outside its scrolling details and separates Brush from Organize with named tabs. The New Brush dialog pins source tabs above its independently scrolling form and Create/Cancel below it. Source and metadata changes retain existing creation, import, duplicate and library behavior. Preserve input drafts while the surrounding layout adapts.

Tool Management separates list browsing from detail editing. At a panel width of at least 560 px, place the two areas side by side; below that width, stack them. Reuse the same widgets when changing orientation so resizing preserves selection, input drafts and keyboard focus. Keep group membership actions outside the scrolling metadata. Compact icons and targets retain accessible names and the shared delayed tooltips.

This is a partial implementation of revamp Pass 3. References and Effects follow in 0.54.0; Guides, Selection/Transform and Vector Properties await the same layout review. Do not change saved dock IDs/placement, library identities, artwork Undo or file formats as part of this UI work. Native verification and physical Huion acceptance are tracked separately from automated regression results.


## References and Effects composition in 0.54.0

References keeps import/removal direct and moves the selected image's caption into the header. Elide it visually while retaining the complete name and dimensions in delayed help and accessibility. The caption height grows with the application font. Keep Fit beside the Zoom label, with the existing slider and 48 px precise field; the shared property row wraps at narrow widths or larger fonts. Preserve unfinished zoom text across layout changes and artwork refreshes.

Show the thumbnail strip only when more than one reference needs choosing. Keep its existing model, selected identity, proportional 40 px thumbnails, keyboard names and horizontal browsing. Zero/one-image states give the space back to the viewer; adding/removing/undoing another reference must preserve a manual zoom and image-center position. Keep import bounds, embedded-image persistence, pan and color sampling unchanged. Wrap the empty-view prompt for larger fonts without adding an instructional paragraph.

Effects keeps Mask, Blur, Shadow, Glow and Color as five directly available icon tabs, including a 180 px dock. Keep their full tooltip/accessibility names and use a visible category name beside the selected effect's pinned enable switch. Only its details scroll. Disabling an individual effect hides its body without resetting configured values; bypassing the master effects switch leaves configured bodies visible but disabled so their state can still be inspected. Preserve the shadow offset pad and the exact scalar controls at useful interaction sizes.

Show mask creation actions when no mask exists, then the existing enable/remove/paint controls when it does. Do not reset mask paint settings when their controls are hidden. Keep the selected layer context and concise unavailable-action reason in stable, font-aware rows so editing does not move the surrounding controls. Use the shared 16 px action icons, 28 px targets, 4 px gaps and responsive property rows throughout both panels. Preserve the 600 ms help delay, focus, pending-edit resolution and existing Undo boundaries.

This completes the References and Effects portion of revamp Pass 3; Guides, Selection/Transform and Vector Properties follow in 0.55.0. Saved dock identities/placement and file formats remain unchanged. Automated/native validation and physical Huion acceptance are tracked separately in the development log.

## Guides, Selection/Transform and Vector Properties composition in 0.55.0

Guides keeps creation, the assistant list, list actions and Selected / All tabs outside the scrolling settings. Give the selected-guide and shared-settings pages their own scroll positions and measure only the active page. The guide list can shrink in a short dock and grows its rows with the application font. Keep placement feedback on the creation row, eliding its caption while retaining the complete accessible name and tooltip. Custom color and opacity controls appear only when enabled, retaining their values when hidden. Canvas placement and geometry handles remain the primary spatial interaction.

Selection keeps Select All, Invert, Deselect and Transform in a compact pinned action row. Hide that row during a pending transform; keep the status and Apply / Cancel visible outside scrolling content. Reuse Selection / Display tabs for feathering, saved selections and appearance in every selection tool that already exposes those settings. Display preferences remain available during ordinary transforms; stamp placement retains its focused Place / Cancel flow. Preserve saved-selection name drafts and exact transform values across resizing and font changes. Perspective and warp also keep labeled Apply / Cancel actions pinned below their scrollable settings.

Vector Properties pins path-edit modes and correction direction above its independently scrolling property rows, with simplify and width-reset actions below. Preserve the existing enabled states for locked, animated and unsupported targets when moving actions out of their former parent. Keep selection feedback in a stable font-aware row with full delayed help, and use a compact cached checker-backed stroke-color swatch. Canvas selection, handles, brush correction behavior and edit/Undo boundaries remain unchanged.

These panels retain shared 16 px action icons, 28 px targets, 4 px gaps, responsive property rows and 600 ms help. This completes the implemented panel scope of revamp Pass 3. Physical Huion acceptance and rollout polish remain separate follow-ups; saved workspace identities and project formats are unchanged.

## Workspace interaction polish in 0.56.0

Tab on the canvas toggles drawing mode. Inside panels, tool lists, sliders and Quick access, preserve ordinary Tab / Shift+Tab focus navigation. The application opts into Tab traversal of all eligible controls so macOS's text/list-only default does not skip buttons; individual widget focus policies still apply and system keyboard preferences are unchanged. Keep the drawing-mode toolbar action available and explain its canvas shortcut in delayed help. Quick access buttons accept keyboard focus without taking focus on pen clicks; cancelled tool activation must preserve the current editor and unfinished text. Before an accepted tool switch captures source-tool settings, commit a focused brush/tool-property numeric draft to that source, not to the newly selected tool. Do not commit drafts from unrelated windows.

Scrolling over an unfocused shared slider or number must scroll its inspector without editing its value or creating an Undo entry. A deliberately focused control retains wheel adjustment, except while precise text is unfinished: preserve that draft and let the inspector scroll. Existing slider dragging, exact bounds, Return and Escape behavior remain unchanged. Explicit workspace or drawing-mode changes must not hide and implicitly commit an active numeric entry, layer preview, pen stroke or tool gesture.

Pen-only movement across dock separators must recover the canvas cursor, and returning to a panel must restore that panel's cursor without waiting for mouse motion. Keep application cursor overrides, popups, modal windows and active grabs intact. A missing tip release must end at the last contact when hover, proximity exit, an outside release or a fresh tip press confirms the old contact ended. Recovery on a fresh panel press is scoped to the same workspace, including its floating panels, and must not end another canvas's independent stroke. Distinguish the tip from barrel buttons for ordinary ink and mask painting; a side-button release must not split a held mask stroke. No recovery may connect separate strokes with a line or commit a stroke twice.

Use shared font-aware row sizing in the Quick access chooser and preserve its search/selection when fonts change. Keep saved dock placement, tool identities, artwork and file formats intact. Synthetic tablet events, native focus checks and Retina captures are implementation evidence; physical Huion comfort requires the artist's hardware check recorded separately in the development log.


## Compact drawing drawer in 0.57.0

In the explicit Compact drawing workspace, the persistent current-tool/size button opens Tools / Sizes / Properties as three named tabs in an overlay over the left side of the canvas. Reuse the existing panel widgets; do not create a second brush state or new tool identities. The overlay sits outside the canvas layout, so opening, closing and changing its page must preserve the canvas viewport, zoom, rotation and pan, including a fitted view. Fit the overlay inside the available canvas area as the window resizes; scroll settings independently and keep its tab row and Close available. Retain the shared 16 px action icons, 28 px targets and 600 ms accessible help.

Keep opening and closing deliberate. Neither tool changes, window resizing nor pen hover opens the drawer. The summary button and Close are keyboard reachable without taking focus on a pen click. Escape in a drawer page closes it when no editor owns the key; inline rename and exact numeric editors retain their own cancel behavior. Reject closing or switching layout while numeric text, artwork previews or gestures are unfinished, or while a popup/modal is active. A rejected request must preserve focus, typed text and artwork. Use the existing advanced-settings wrench and dialogs from the same Properties content.

Panels-menu and dock/toolbar-context entries for Tools, Sizes and Properties open the corresponding drawer page directly in Compact drawing; do not briefly show an empty dock, which can resize the canvas. The regular Drawing preset restores those same widgets to their original docks. Preserve normal saved dock identities/placements, and migrate a previously open compact dock stack to an open drawer. Remember the drawer page and open state across sessions; Tab drawing mode hides it and restores that state on return.

Keep all three tabs fully reachable at larger fonts. The drawer-specific tab bar permits text to elide down to a 28 px minimum tab target while retaining the native font-aware height, full accessible name and tooltip; native style minimum text widths must not push a tab outside the header.

This replaces only the compact workspace's former open dock stack. Comfortable density, automatic breakpoint switching and hover activation remain unimplemented. Drawing-workspace regressions pass for canvas invariance, same-widget restoration, drafts, inline rename, advanced brush dialogs, popup guards, larger text and legacy saved state. All 11 native Mac suites pass after the unlocked-desktop follow-up. Physical Huion workflow acceptance remains separate; automated checks do not establish hardware feel.


## Background Effects previews in 0.58.0

Keep Effects controls responsive while the canvas and Navigator render. Preserve the last completed image and use the existing fixed status area for “Updating preview…”. Preview completion must not rebuild controls, disable editing, commit a numeric draft, move focus or add history. Actual editing restrictions and errors take precedence over the pending message.

Use lower-resolution temporary previews for eligible static raster stacks during a continuous edit; refine after 150 ms without a new value or on release. Both views coalesce intermediate settings and reject obsolete document/view results. A gesture remains one Undo entry. Cancellation restores the authoritative state when its current preview finishes; an older worker result may never reappear afterwards. Source pixels, masks and full-quality exports are unchanged. Keep sheet labels and paper in a GUI-prepared cache so background text rendering cannot change the production header.

Complex compositions retain full-quality background rendering rather than applying an unsupported approximation. This includes editable vector/text objects, animation, onion skins, 3D and active transforms. Their completion rate can be lower. Physical Huion interaction and large production stacks require separate acceptance; synthetic GUI responsiveness does not establish pen-to-screen latency.


## Character Model Sheets in 0.59.0

References uses **Images / Model Sheets** collection tabs. Images remains shot-specific; Model Sheets is shared by the project's shots. Keep character selection and add/rename/remove actions compact above the Turnarounds / Expressions / Costumes category tabs. Categories use their named symbols with shared delayed help, showing full labels where width permits and icons when labels would truncate. A single sheet hides the redundant thumbnail strip. The inline sheet name, import/remove actions, viewer and shared Zoom/Fit row remain directly available.

Library changes use document Undo/Redo; a batch import commits once. Category moves are available from either the thumbnail or viewer context menu. Preserve reference selection, zoom/pan and unfinished numeric or name entries across unrelated artwork refreshes and shot changes. Switching characters or sheets resolves a name edit on its original sheet before selecting the destination. Browsing never modifies artwork or exported frames. Imported copies belong to the project, independently of their source file paths.


## Character Color Models in 0.60.0

References adds a Color Models tab beside Images and Model Sheets. Keep one shared character identity and selection across the two character collections. At narrow widths, collection/category tabs use their recognizable icons with named tooltips and accessible labels; full labels return when they fit. Color rows retain visible names because color alone does not identify a production role.

A swatch click sets the drawing foreground without changing project history. Add/replace use the current foreground, with direct edit/remove actions and category moves in the context menu. The deliberate color editor uses the shared picker, exact hex entry and opacity slider/number pair, committing once on OK. Keep name drafts stable through foreground changes, artwork refreshes and shot switches. Save/Close resolve pending names before taking snapshots or checking for unsaved work. Project palette edits use document Undo/Redo and do not attach work to live pen samples.


## Cel coloring and Light Table in 0.61.0

Cel Paint Check and Cel Shading use compact standard docks with distinct original SVG identities. References gains a fourth, adaptive Light Table tab when attached to the drawing canvas. Scalar settings reuse PropertyRow and ValueSlider; actions retain meaningful text where inspection/commit choices need clarity, alongside the existing icon targets and delayed help.

Paint suspects start unchecked. Previous/Next highlights the selected issue and its drawing; a separate viewer compares original and proposed repairs before a single batch Apply. Cel Shading separates colored working guides from finished paint, and its Fill mode previews the final boundary treatment and layer order. Paint-check repair previews use Apply, Discard or Cancel before Save/Close and timeline transitions. In 0.62, unfinished shading is preserved without requiring Apply; sources and lock state are revalidated before committing finished paint.

Light-table pins expose direct move/rotate/scale handles, plus precise controls and solo flipping. Hidden panels cannot keep intercepting pen input. Boundary editing and light-table editing are mutually exclusive; leaving either restores the ordinary canvas cursor. Background workers handle expensive preparation and light-table viewport composites are cached across pen redraws. These are drawing aids; no new animation editor is introduced.

## Saved drawing aids in 0.62.0

Light-table placement, visibility, opacity, selected pin and solo state belong to the project. Unfinished Cel Shading work belongs to its shot, outline layer and frame, including guide/fill order and current controls. Save keeps drafts without converting them to artwork. Switching drawings restores each target's own draft; reopening restores the saved shot/frame. Keep Apply deliberate and preserve the panel's own step-back and Cancel actions.

Saved aid edits mark the project unsaved, but must not create artwork Undo entries or disappear during artwork Undo/Redo. The normal unsaved-project decision protects them on Close. Finish active canvas and exact-entry gestures before taking a save snapshot. Rebuild restored shading previews in the background; unavailable sources retain their saved drafts and report a concise reason instead of silently discarding work. Source drawings, exported artwork, dock placement and the established pen interaction rules remain independent.

The 0.62 workflow passes four native Mac suites and the same four suites at offscreen 2× scale. Restored Light Table and Cel Shading were visually checked at both scales; the narrow Light Table with larger text was also inspected. These checks validate the documented persistence and interaction behavior; physical Huion feel remains a separate artist check.

## Character lighting palettes in 0.63.0

Color Models uses a compact scrollable lighting tab row above its existing category tabs. Keep Day/Night/Sunset/Interior names visible where space permits; retain tooltips, accessible labels and the shared icon sizing/hover behavior. Add is direct, while duplicate/rename/remove and missing-preset creation share the options menu. Do not add a large preset configuration form.

Colors remain recognizable named production roles. A role's name/category is shared across lighting palettes, while its RGBA value is independent in each palette. Preserve pending name edits through valid palette/character changes; rejected edits stay editable instead of silently switching context. Palette browsing leaves the drawing foreground alone until a swatch is used. Explicit navigation can choose the approved Paint Check palette, while pending repairs retain their existing resolution guard.

Preserve each character's palette selection across shot changes within the open session. Reopening starts on Day and exposes all saved variants. Do not imply that choosing Night changes scene lighting or recolors painted pixels: standard palettes are editable copies and existing artwork is unaffected. The final tabs retain natural label widths and scroll instead of squeezing names. Four native Mac suites and the same four at offscreen 2× each pass 33 Qt results with no failures or skips. Narrow/custom names, larger text, palette selection and restored values were visually reviewed. Physical Huion feel remains a separate acceptance check.

## Previewed recoloring in 0.64.0

Keep **Recolor drawings…** directly available as a compact, named-tooltip icon in Color Models. Use a focused modal for this deliberate batch pixel edit: character, From/To palettes, shot scope and checked drawings alongside a large preview. Retain visible labels for these choices and **Preview / Apply / Cancel**. **Before / After** tabs preserve pan and zoom. A narrow dialog stacks the controls and preview instead of shrinking the preview to an unusable column.

Only the active drawing starts checked. Unavailable targets retain a concise reason, and a status line reports conflicts, no matches and changed-pixel counts. Prepare previews in the background, discard stale results, and keep Apply unavailable until a valid changed result exists. Cancel and Escape never commit. The comparison displays source paint, not the full composited shot; communicate that scope in tooltips and workflow documentation. Opening resolves existing paint repairs and pauses playback; closing returns keyboard focus to the canvas. Shared semantic colors, icon metrics and the 600 ms tooltip delay apply.


## Symbols workspace (0.65.0)

- Keep the Symbols library compact: icon actions with accessible names and shared delayed tooltips, a search field, and instance controls only when an instance is selected.
- Use named playback tabs when space allows; narrow panels switch to icons with accessible names and tooltips so every mode stays directly reachable. Explain that Once holds the final frame and that Break apart captures the current internal frame.
- Keep source editing in a separate draft with visible Save changes and Cancel. Restore focus to the main canvas after the modal window has closed and been destroyed; source changes must be one parent Undo operation. Respect another window the artist activated while closing rather than taking focus back. The native Save/Cancel focus repair is implemented in 0.71.
- Use the shared property-row and value-entry widgets for start/first frame, speed and duration. Symbol edits must not change the main workspace's brush organization or shot selection.
- Hide unsupported camera/audio timeline controls in the symbol editor. Show a clear blocked-tool message when painting directly on a linked instance.


## Timeline and Frame Properties in 0.66.0

Timeline zoom and Fit Shot/Selection are view controls: they must preserve timing, selection and document history. Ctrl/Cmd + wheel anchors zoom under the pointer. Use adaptive ruler intervals at low zoom, a readable playhead badge, and painted key hit targets. Very short cels/audio clips reserve their body for moving; precise timing remains available in properties or after zooming in. Ignore zoom changes while a timeline drag/scrub is unfinished.

Drawing, animation, camera and audio properties reuse responsive label/control rows. Keep channel diamonds and channel selection direct; short visible labels retain full accessible names. Actions use 16 px icons in 28 px targets. The four Frame Properties tabs retain names where they fit and become named icon tabs at narrow widths. Resizing or changing fonts must preserve focus and unfinished exact entry.

Playback scheduling may display the latest completed effects frame while a newer one is rendering. Stop, source/view changes and edits invalidate that playback context and restore the exact requested state. Do not claim the playhead or hardware refresh rate as the achieved rendered-frame rate.


## Playback quality in 0.67.0

Keep the current playback ratio visible in one compact Timeline-footer control: 1/1, 1/2, 1/4 or 1/8. The short text communicates a value better than an ambiguous icon; retain the shared target, accessible name and delayed tooltip. Use one menu for the four quality choices and the existing fixed status row for reduced-preview or full-quality-fallback feedback. Do not add another panel or move controls when render results arrive.

Quality is a persistent view preference. Changing it must not stop playback, modify the project, add Undo entries or accept an unfinished property value. The popup returns focus to its originating editor without stealing focus from another window. Canvas and Navigator use the same requested ratio. Pause requests exact artwork, and exports stay exact. Unsupported content uses an explained full-quality fallback instead of disappearing. Report completed displayed frames when measuring performance, independently of playhead events.


## Multiple-cel editing in 0.68.0

Keep cel selection visually distinct from the active track and playhead. Reuse familiar Shift-range, Ctrl/Cmd-toggle and blank-area marquee gestures across drawing tracks. Bulk drags preview timing only, commit once on release and cancel on Escape; show conflicts without modifying artwork. Single-cel edge trimming remains available, while multiple selections move together. Keyframe/audio selection and their clipboard commands remain separate.

Drawing properties use the existing Start/Hold rows for selection-wide edits, with an explicit selected count and mixed-hold label. Disable the shared name field for multiple drawings. On 1/2/3 are short numeric buttons; Repeat/Reverse use recognizable icons and delayed help. Selecting a cel returns to Drawing properties. Preserve unfinished exact entry during refresh and cancel it without changing timing.

Use one direct Timeline-footer action and context entry for drawing reuse. The focused picker combines searchable names/labels, usage counts, asynchronous thumbnails, Hold and Reuse/Cancel; explain that artwork is shared. Its delete action applies only to unused drawings and uses Undo. Browsing and thumbnail completion must not alter source artwork, history or pending numeric values. Do not add a large new permanent dock for this workflow.


### Symbol clips and frame picking (0.69)

Keep each Symbol instance on one compact clip row, including drawing-owned instances. Solid portions show the owning drawing's exposure; dashed portions remain editable outside it. Use direct body/edge/Alt-slip gestures, preserve playback phase on trims, and keep clip, cel and property-key commands separate. Frame Properties uses a Symbol tab with shared property rows and a direct frame-picker icon. The modal picker shows bounded pages of asynchronously rendered frames, with exact entry and a visible Hold selected frame choice. Apply changes only the original instance; stale source edits require reopening the picker. Clearing a clip retains its source and instance, with a short empty-state instruction for restoring its Hold.

## Applied to timeline cues in 0.70.0

Frame Properties gains a Cues tab instead of another dock. Cue kinds use five compact icon tabs, 28-pixel actions and shared 600 ms help. Name/color, Start/Hold and speaker controls follow the responsive property-row layout; dialogue/notes use a multiline editor. Searchable rows retain full text in tooltips, and overlapping timeline ranges receive separate lanes. Text and timing drafts survive ordinary refreshes, commit once, and discard safely on stale source changes. Native and Retina captures cover the normal workspace and 240-pixel properties with larger fonts. Notes remain editor metadata and do not alter artwork or exports.

## Timed Symbol poses in 0.71.0

Use the existing Symbol inspector and a fifth **Poses** playback tab, with one distinct timed-pose icon reused in the lane, picker and properties. Keep all five tabs reachable at 240-pixel widths and larger text; show names where they fit and accessible icon tabs otherwise. Pose and Key frame reuse shared property rows, while Previous/Next/Delete remain compact direct actions with 600 ms help. Disable Speed in Poses, keep Slip available, and explain First frame as the pre-key fallback through help rather than a permanent instruction block.

The timeline adds one compact Poses lane per relevant instance, with held intervals, source-frame labels and key diamonds. Solid and dashed/dimmed states distinguish exposed and unexposed drawing timing. Pose selection and commands remain separate from clip, cel, property/path/camera, audio and cue selection. Reuse existing selection, marquee, drag, nudge, copy/paste/delete, Fit Selection and Escape conventions; timing previews commit once on release. Do not create cel exposures merely to select an inactive pose lane.

Use the bounded asynchronous thumbnail picker for deliberate pose choices at the captured parent frame. Apply creates one held pose key; Cancel changes nothing. Recheck the instance, source definition and playhead after pending drafts resolve, so a stale choice cannot overwrite newer timing. Preserve clip placement, Slip, saved Speed and the fallback. Existing hidden keys within shot bounds remain editable through Properties; new pose choices require a frame inside the clip. Retained keys must not silently disappear on trim or playback-mode changes.

Manual lip-sync means the artist chooses the drawing and its timing. Dialogue notes and audio scrubbing assist that choice; they do not automatically generate poses. The current native and Retina verification status is recorded in [Symbols](symbols.md#071-verification-status).


## Timeline track organization in 0.72.0

Keep Visibility, Lock and Solo directly accessible in artwork headers using shared 16 px icons, 28 px targets, accessible names and 600 ms help. Visibility/Lock reflect the actual saved layer state, including inherited folder restrictions. Solo has a distinct active state and a visible Clear action whenever it is in use; it isolates only the workspace preview and respects saved visibility. Do not add artwork hierarchy controls to audio, camera, cue or property-only lanes.

Use indentation and direct carets for folders. Keep names readable and elide with full-name tooltips when necessary. Collapse is a workspace display choice, remembered per shot during the open project and reset on load; it must not modify artwork or Undo. Folder creation retains visible Name, Create and Cancel controls in a focused modal. New folders use the existing Layers hierarchy, so the two panels agree on parentage, order, visibility and locks.

Distinguish header dragging for organization from time-grid dragging for timing. Show an insertion line or folder destination before release, cancel on Escape/lost focus, and explain rejected destinations. Retain context Move up/down/Move to top level and Ctrl/Cmd+Shift+Up/Down alternatives. Reorder whole subtrees atomically, preserve exposures and all animation clocks, honor inherited locks and clipping dependencies, and reject moving registered drawing containers. Do not silently flatten folders or detach shared drawings to make a drop succeed.

Pending edits and stale project/shot contexts must resolve or reject safely before document changes. Changing Solo or collapsing folders must preserve a rejected edit or cancelled decision. Preserve the user's current editing domain; inspecting or organizing an unexposed Symbol must not expose it merely to activate controls. Current build, focus and Retina verification belongs in the checklist and development log, separate from physical tablet acceptance.


## Animated effects and masks in 0.73.0

- Reuse direct diamonds beside effect settings. Frame Properties groups channels into Transform, Effects and Mask tabs; choosing a Timeline channel opens its matching category.
- Treat an enabled channel as an evaluated value at the playhead. Editing it writes that frame's key without baking other animated settings into the source. Unkeyed controls remain static; a mixed shadow-offset gesture is one Undo operation.
- Use held On/Off keys for enable states and mask inversion. Do not offer interpolation or easing curves for those switches.
- Separate mask Paint and Placement tabs. Placement provides paired sliders and exact entry for position, scale, rotation, strength and feather. The thumbnail is labeled as the mask source; moving the mask does not rewrite its pixels.
- Keep unfinished numeric drafts stable during refresh/resize. Resolve drafts before changing the frame or target. Disable unavailable channels with a reason, including a missing mask or locked position.
- Render animated effect and mask previews on the existing background workers, including effects enabled only by keys and nested Symbols. Use bounded reduced previews while adjusting, then exact idle/committed rendering. Exports always use exact rendering.

## Project starters and task layouts (0.74)

Keep document templates separate from workspace arrangements. Provide blank/plain-canvas options without altering legacy production sheets. Built-in drawing, anime, storyboard and animatic layouts must preserve artwork, tool settings and timeline selection. Named saved layouts store panel/toolbar arrangements and selected inspector category; they do not replace brushes, Quick Access contents or window geometry. Guard unfinished edits and pen contact before switching; restore compact tab categories by stable identity, without triggering editing actions. Bulk layout changes should complete without dock animation overlays.


## Scene/Movie review in 0.75.0

Keep Shot editing and Scene/Movie review as compact, visibly named Timeline tabs. Review owns a separate playhead and snapshot; crossing cuts must not select drawing-document shots or refresh all editing panels. Use lazy, bounded thumbnail cards and a global slider/exact-entry pair. Preserve unfinished frame text when resizing or changing preview quality. Thumbnail click seeks; double-click/Edit shot deliberately enters the reviewed drawing. Space/arrows/Enter/Escape and pen focus follow the same actions.

Review pixels are presentation-only. Source artwork, Undo, camera/effect evaluation and exports must remain unchanged. Render frames and thumbnails off the UI thread, reject stale cuts/seeks, and show a correct-shot thumbnail while a heavy frame prepares. Workspace changes and Drawing Mode exit review before placing keyboard focus back on Canvas.

## Production-project foundation in 0.76.0

Keep Film, Series and Drawing only as visibly named project-type tabs, separate from canvas starters and editor layouts. Drawing only offers Blank/Illustration, starts with one frame and no animation setup, hides timing fields, and defaults to the illustration workflow. Preserve title/location and entered dimensions when the production type changes. Existing project settings show the saved type without destructive conversion controls. New project folders and legacy standalone files must remain explicit, distinguishable choices.

Project Home uses compact Recent/Pinned/Archived tabs and readable project/type/location rows. New, Open file, Open folder and Continue remain direct actions. Pinning, archiving and removing a Home entry manage the local list only; their help must not imply that project files are deleted or moved. Missing locations remain readable and recoverable through Open at the new location.

Keep Project Home, Project workspace and Drawing workspace within the existing window. Browsing production content temporarily hides drawing docks and restores their arrangement on return. Guard active strokes, pending transforms, numeric drafts, notes and asset imports before transitions; rejected navigation retains the current context. Use Artworks/Open drawing wording for Drawing-only projects and Shots/Open shot for productions. Episode labels apply to Series; stage and notes remain explicit planning metadata, not automatic production execution or revision approval.

Assets uses compact icon actions with accessible names and the shared 600 ms tooltip delay, visible type/folder/search controls, and selected-item metadata. Keep image previews asynchronous, media playback deliberate, and status feedback stable. Hiding the panel stops media playback; preview work must not attach expensive processing to pen samples. Bulk file copies run outside the GUI thread, with Cancel retained until completion and stale results rejected.

Separate **Attach** from **Place a copy**. Attached to lists record deliberate planning links; they must not claim automatic dependency detection. Image/audio placement produces independent existing-editor content. File-type classification is not proof that a rendering engine supports that media. **Removed / Restore** retains asset bytes and already placed artwork; do not imply permanent deletion or artwork Undo for independently saved catalog changes.

Keep package creation and Save As explicit, cancellable and source-preserving. Do not overwrite an existing package or silently flatten it to a standalone file. The first pass retains existing artwork-container limits; storyboard grids, global asset libraries, revision pinning, a movie editor and lazy shot loading must remain labeled as future work until implemented and verified. Current verification belongs in the development log, separately from physical tablet acceptance and published 0.75 downloads.


## Motion Study windows

Keep studies independent of the drawing document until explicit timeline insertion. Show source FPS separately from retained image rate; sampling changes holds rather than silently speeding up the clip. Keep skipped frames restorable. Source/Study tabs and compact frame controls explain which image is being previewed. Distinguish editable-cel insertion from the locked, movie-export-excluded reference track. Preserve the first pose during FPS conversion. Decode/copy off the GUI thread, keep cancellation available and report missing external decoders or resource limits without replacing the current study. Current controls and limitations are documented in [Motion Study](motion-study.md).

## Managed project dashboards and document tabs

The local managed-project entry separates Projects/typed dashboards/Assets from focused authoring. Project type uses three large selectable Drawing/Animation/Movie cards. Use compact gallery cells capped at 220 logical pixels wide; wider windows add columns instead of enlarging existing project, drawing or folder cards. Keep labels and action targets readable. Drawing content is a simple gallery and Animation content defaults to thumbnail rows; folders, media filters and the asset inspector belong to Movie/Assets. Keep Projects/Project dashboard navigation and document tabs together in a single horizontal header row to preserve vertical canvas space; overflow tabs scroll horizontally. Each native drawing or single-shot animation opens in its own retained editor tab; navigation resolves pending gestures without losing another tab's undo/view state. Editor layouts and command routing must not reintroduce Shots/Scene/Movie sections. Current thumbnail galleries, canvas-preview setup, background recovery, import/preview behavior and remaining asset refinements are documented in [managed workspace](managed-workspace.md).
