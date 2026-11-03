# Native brush settings — 0.38.0

Tool Properties keeps **Size, Opacity, Flow, Hardness and Smoothing** visible as sliders with compact exact-entry fields. The pen icon toggles pressure-controlled size. Click the **wrench** to open **Advanced brush settings**, a modal dialog containing all eleven existing categories on a vertical icon tab rail. Hover an icon for 600 ms to see its category; arrow keys switch tabs. A short heading names the active category, which is remembered with the workspace. Categories scroll within the dialog, independently of dock width.

Edits update the selected tool immediately and affect subsequent strokes. **Close** or Escape outside a numeric edit dismisses the dialog and keeps committed changes; there is no extra Apply step. Enter commits a typed number without closing the dialog, and Escape during numeric entry cancels that entry. Both views stay synchronized; hidden advanced editors catch up when reopened. Each tool remembers its configuration; use **Tools → right-click → Duplicate** for an independent named copy.

Direct widgets let you rotate/squash the tip, adjust a stamp-spacing strip, change scatter, pose the pen, position the symmetry center and scale a texture preview. Texture thumbnails and symmetry tiles make those choices directly accessible. Numeric and choice rows place labels beside their editors where space permits and wrap above them in narrower layouts. Numeric fields retain a useful slider track and a 48-pixel exact-entry box. Resizing preserves unfinished input; Close remains outside the scrolling categories. Widgets map to existing native settings; they do not add new brush-engine effects. See the [UI rules](ui-guidelines.md).

These are native Toon Pro effects. They cover the requested setting groups but do not promise identical Photoshop brush rendering. Supported modern Photoshop ABR descriptors now map names, sampled/computed round tips, geometry and selected shape/opacity/flow/scatter dynamics to these native controls. Texture resources, dual brushes, color dynamics, blending and several other source features remain unsupported. Each imported preset's compatibility details identify mapped settings and limits; see [the import compatibility guide](brush-compatibility.md).

The guide properties’ **All guides → Snap to guides** toggle can constrain brush/eraser stroke paths to the shot's enabled [perspective drawing guides](guides.md). Guide geometry and shared snapping settings belong to the project, not the brush preset. Native brush dynamics, scatter, texture and symmetry still operate on the resulting stroke path.

## Editable stamp placement

In **0.47.1**, the quick properties show an **Adjust stamp** row with an explicit **On/Off** toggle for sampled grayscale/colored Brush tools. It defaults on for the Stamp and Colored Stamp types and remembers an explicit override independently for each tool. Enable it for other sampled brushes when placing individual elements; disable it to place the current pending stamp and return to continuous stamp trails. The mode is a tool interaction preference, not a brush rendering parameter or a change to saved vector strokes. [Placement controls and limits](drawing-tools.md#editable-stamp-placement).

## Brush Management

Open **Panels → Brush Management** or **Tools → ⋯ → Manage brushes…**. The panel shows every brush tool, including brushes outside the current tool group. Tag and search share one row; category and type filters narrow the same list. Browsing and organizing do not change the drawing tool until you choose **Use** or double-click a brush.

Brush rows use compact proportional previews and names, with type, category and tags in their tooltips. The selected-brush heading shows its type and category. Search, filters and Use/settings stay outside the scrolling metadata editor. The panel starts at 360 × 600 pixels in a new workspace and can fit a 240-pixel dock; saved workspace geometry is retained. Status messages keep a stable row, with complete text available through help and accessibility.

- **New brush** creates a Pen, Ink, Soft brush, Spray, Stamp, Colored stamp, Dynamic stamp or Custom starting configuration. The **Current brush** tab copies the current settings. Supply a name, optional category/tags and an optional destination group.
- **Import** accepts Photoshop ABR packs or stamp images. **Duplicate** creates independent settings; the wrench opens the selected brush's advanced settings.
- Select several brushes to assign a category, add/remove tags or add them to a tool group. A category is a single label; tags support multiple labels. Names can contain up to 120 characters, categories 80, and each brush can have up to 32 tags of 40 characters.
- **Delete** removes selected brush definitions from every tool group and standalone placement. The panel's **Undo/Redo** restores organization, separately from artwork Undo. Imported source templates remain cached locally; a saved deletion record prevents them from reappearing automatically after restart. Existing drawings remain intact.

Brush types provide useful starting settings for the same native brush engine. Pen and Ink use round tips, Soft lowers hardness and opacity, and Spray uses scattered repeated stamps. Stamp and Colored stamp require an image. These settings remain editable in Tool Properties.

## Dynamic stamp brushes in 0.48.0

Open **Brush Management → New brush → Dynamic stamp**. Add **2–16 images**, choose **Foreground** or **Image colors**, then enter a name, optional category/tags and destination group. Foreground turns transparent images into alpha masks and opaque images into inverted-luminance masks; Image colors keeps source colors and transparency. Each image fits within 512 × 512 pixels while retaining its proportions. Failed batches leave the chosen images and library unchanged. Creating the brush is one organization Undo step. Choose **Use** to draw with it.

Dynamic stamp starts with **Adjust stamp Off** and paints a continuous series while you move. Defaults are Shuffle order, 25% size variation, 12% rotation variation and 15% saturation variation. Pressure size starts off so random size differences remain visible at ordinary pen pressure. Size and spacing remain adjustable. The separate Adjust stamp toggle still allows positioning a single impression when enabled.

Open the **wrench → Tip shape** to manage the stamp thumbnails and order. Add retains existing stamps; Remove and ordering arrows edit the selected stamp. The count shows the 16-image limit. **Ordered** repeats the listed sequence; **Random** uses each stamp's weight; **Shuffle** cycles through every stamp without adjacent repeats, including between cycles. Existing Pressure and Direction selection remain available. Root anchors control where each image sits against the stroke.

For sets containing multiple stamps, **Size variation**, **Rotation variation** and **Saturation variation** appear together below the stamp editor. These edit the existing dynamics and explicitly choose variation between impressions within one stroke. Setting one to zero removes that per-tip jitter. Shape dynamics and Color dynamics retain their detailed controls; changing a channel to once-per-stroke or disabling it shows zero for that channel's per-tip shortcut. Saturation changes are only visible when the source color has room to become more or less saturated; neutral foreground ink is not a multicolor source.

Image-color variation is an explicit opt-in for existing colored stamps. The new Dynamic stamp type enables it when Image colors is chosen. **Color dynamics → Vary image colors** allows hue, saturation, brightness and purity changes while preserving source transparency and relative image detail. Foreground/background mixing controls hide for colored tips. With Apply per tip off, one palette variation is resolved for the entire stroke, including Count/scatter copies. Original tip images remain unchanged. Per-stroke color variation captures a recolored atlas for each vector stroke, so large tip sets can consume the existing resource budget sooner; default per-tip variation shares the original images.

Random choices remain deterministic for the captured seed and stamp index. Raster strokes and editable vector replay use the same renderer, and saved vector artwork owns its image resources. Existing brushes and old captured strokes retain their earlier appearance. Image-color variation uses brush-library format 6, vector capture 5 and project format 25 when needed; the new Dynamic stamp type or image-color setting requires tool-catalog format 11. Older files remain readable, while unsupported downgrade attempts are rejected. No new default catalog recipes were added in 0.48.0; version 0.49.0 expands the installed count to 219.

Large, closely spaced colored stamps still cost more to render because each changed palette requires pixel processing. Cursor outlines do not perform color variation, and the recolor cache is bounded. Physical tablet feel remains a separate acceptance check.

## Standard starter brushes

The original starter collection includes **19 built-in brushes**: the original three and 16 additional presets. Starter presets appear in Drawing when that group exists and remain available through Brush Management. Version 0.42 adds a separate 112-brush catalog, bringing the built-in total then to **131**. Version 0.45 adds 54 more, for **185 built-ins**. Existing names, settings, organization and deleted-brush records are preserved; new metadata is assigned when a preset is first added.

These are original Toon Pro configurations for familiar brush families. Their small procedural masks and grain patterns are generated locally. They do not contain Photoshop, Krita, Clip Studio Paint or Procreate brush assets and do not claim to match those applications' rendering.

If the existing tool catalog has no remaining capacity, additions are deferred with a warning while existing tools stay usable and savable. Freeing capacity and restarting allows missing presets to be added.

The 112 catalog brushes use 13 subject groups: Line art and ink, Sketch and dry media, Soft paint and spray, Grass and ground cover, Trees and foliage, Clouds and atmosphere, Ground rock and terrain, Walls and construction surfaces, Water and reflections, Manga tones and hatching, Manga effects and decoration, Weather and particles, and Painted color stamps. Category filters use those exact names. **Default** finds the new collection; **Dynamic** finds recipes with deliberate jitter/scatter/color variation. These tags supplement existing type and search filters. Group icons retain the shared delayed help.

Catalog groups are added on first introduction without selecting them or moving existing tools. Removing a group, moving its brushes elsewhere, editing a recipe or deleting a brush remains respected after restart. If Toolbox is full, brushes remain available in Brush Management and Add Tools without creating more groups. Built-ins do not consume the 128-custom-preset allowance; tools still share the existing 512-tool and resource limits. Tools and Add Tools generate only visible matching previews in small timed batches and pause during pen contact; the automatic catalog save timer also defers while a drawing/editing gesture is active.

The new recipes use original bounded procedural tips and textures, pressure/tilt/direction controls and seeded variation supported by the current engine. Seven colored stamps retain their source colors. They are layered procedural illustrations, not imported third-party brush art. Primary nominal sizes now reach 1,024 pixels; secondary masks remain limited to 256. Hatching and stipple are drawing/texture marks, not resolution-aware screentones. These earlier hatching presets remain unchanged; the 34 specialized designs added in 0.49.0 include proper surface tones.

In 0.42.1, brick, stone-block, roof, wood, fabric, tatami, cracks, strata and manga line-pattern brushes paint lines on transparency, including after repeated passes. Concrete/plaster/grain brushes still paint texture, with softer edges and closer spacing to reduce circular stamp boundaries. Foliage color, size and angle vary between dabs within one held stroke; pressure controls paint flow without clamping away the size variation. Colored stamps vary size/angle while retaining their source colors. Precision ink and regular patterns remain consistent deliberately.

The catalog applies recipe revision 2 once to canonical built-ins from 0.42.0. Each unchanged setting or resource adopts its corrected default; edited fields, pressure curves, custom images, copies, imports, names, tags, categories, placements and deletions remain intact. New Line-only tags are assigned to fresh defaults; existing tags remain untouched. Existing raster pixels and captured vector strokes do not change. Later deliberate edits are not repeatedly reset. If the complete upgrade exceeds the existing image budget, it is deferred with a warning until space is available.

| Brush | Category | Starting behavior |
| --- | --- | --- |
| Round Ink — retained | Drawing | Original pressure-sized round ink, unchanged. |
| Round Sketch — retained | Drawing | Original low-flow round sketch brush, unchanged. |
| Round Blocking — retained | Drawing | Original broad, constant-width round brush, unchanged. |
| Hard Round | Painting | Solid round brush with constant width. |
| Pressure Round | Painting | Pressure varies width and paint flow. |
| Soft Airbrush | Airbrush | Soft, constant-width tip; pressure controls flow, with stationary build-up. |
| G Pen | Inking | Pressure varies a hard round tip from a fine end to a broader line. |
| Fine Liner | Inking | Fine, uniform-width opaque line. |
| Graphite Pencil | Sketching | Paper grain with pressure-controlled width and flow. |
| Mechanical Pencil | Sketching | Fine, constant-width line with pressure-controlled flow and paper grain. |
| Charcoal | Sketching | Rough native tip and paper grain; pressure varies width and flow. |
| Dry Ink | Inking | Broken, textured coverage with a tip that follows the drawing direction. |
| Flat Paint | Painting | Angled flat tip with pressure-controlled width and flow. |
| Soft Paint | Painting | Soft-edged paint with pressure-controlled width and flow. |
| Pigment Wash | Painting | Light textured coverage with a low stroke-opacity cap; no fluid simulation. |
| Particle Spray | Texture | Small particle cluster with randomized size, angle and placement; pressure controls width and flow. |
| Stipple | Texture | Spaced scattered dots with randomized size and flow. |
| Chisel Marker | Markers | Fixed-width angled flat tip with translucent, pressure-controlled opacity. |
| Calligraphy | Inking | Angled flat nib with pressure-controlled width. |

The original three retain their Drawing category and do not receive the Standard tag automatically. Every preset remains editable, and duplicates have independent settings. These additions provide drawing, inking and painting starting points; they do not add bristle physics, wet-paint mixing or pigment diffusion.

## Image stamps

Stamp import accepts PNG, JPEG, WebP and BMP files up to **32 MiB**, **8192 pixels per side** and **33,554,432 source pixels**. Stored copies fit within **2048 × 2048**, retaining aspect ratio and at least one pixel per side. Empty stamps are rejected, and the source file is unchanged.

A normal **Stamp** paints with the foreground color. Images containing transparency use their alpha as coverage; opaque images use dark-on-white coverage. A **Colored stamp** retains the image's original colors and alpha, including an opaque background if present. Foreground changes do not tint it by default. Opt-in image HSV variation is available in 0.48.0, and Foreground tint is available in 0.49.0. Size, pressure, rotation, roundness, softness, flow, opacity, scatter, texture and selection still affect the stamp. Both raster and editable vector layers use the same rendering; the pointer and path picking follow the stamp's alpha, including holes.

## Start with a brush

1. Choose a brush in **Toolbox → Drawing → Tools**, or create/import one in **Brush Management**.
2. Set size, opacity, flow, hardness and smoothing directly in **Tool Properties**. Use the pen icon for pressure-controlled size. Open the **wrench → General** for spacing: a low value makes a continuous stroke; a high value separates the stamps.
3. In the advanced dialog, use **Tip shape** to rotate, flatten or flip the tip. The primary tip picker offers Round and sampled/color tips from the local brush library; the secondary picker uses grayscale masks. Picking a tip keeps the other settings. Selecting Round clears an existing colored stamp.
4. Enable the desired texture, dual brush or color effects in their categories. Use **Shape dynamics** for size/angle/roundness and **Transfer** for opacity/flow. Choose an input, adjust its response graph and optionally add jitter. **Behavior → Randomness** controls the seed.
5. Right-click the tool and choose **Duplicate**, then **Rename**, to keep a separate configuration. The Add dialog can also create a named independent copy.

Foreground and background colors can be set in the Colors panel or Color Dynamics category. Switching tools preserves both current colors; colored stamps use their own image colors. Each tool restores its own texture settings. Native configurations retain settings and embedded primary, colored, texture and secondary images. Raster artwork stores painted pixels; editable vector strokes embed the resources needed to replay them. Neither needs the original image file.

## Setting coverage

| Category | Controls and ranges | Native behavior |
| --- | --- | --- |
| General | Size **1–1,024 px**; hardness **0–100%**; stroke blend mode; opacity and flow **0–100%**; spacing **1–400%**; smoothing **0–100**; Pressure size; airbrush build-up and **1–60 build-up steps/s** rate. | Size is the round-tip diameter or sampled-tip longest side. Opacity caps the accumulated result of one stroke. Flow controls each stamp's contribution within that cap. Build-up deposits paint while the pen or mouse remains pressed and still. Smoothing trades additional pen lag for a steadier path. |
| Tip shape | Tip picker; angle **−360° to +360°**; roundness **1–100%**; spacing **1–400%**; Flip X; Flip Y. | Roundness compresses the tip along its local Y axis; angle and flips apply to the actual mask. Spacing is a fraction of nominal size, with a minimum sampling distance of 0.25 artwork pixels. The spacing controls in General and Tip Shape edit the same value. |
| Shape dynamics | Size, angle and roundness jitter **0–100%**; once-per-stroke jitter toggles; five-point input-response graphs; minimum size **0–100%**, minimum roundness **1–100%**; separate Control selectors and **1–10,000** Fade steps; tilt scale **0–400%**; brush projection. | Size and roundness vary between their configured floor and full value. Angle jitter spans up to ±180° around the base angle. Angle supports Rotation, Initial Direction and Direction. Response graphs remap the chosen input; jitter can vary each stamp or remain constant for a stroke. Projection is geometric flattening and rotation, rather than a bristle simulation. |
| Scattering | Scatter **0–1000%**; Both Axes; scatter Control and Fade; Count **1–16**; count jitter **0–100%**; count Control and Fade. | Scatter offsets stamps across the stroke; Both Axes also offsets them along its direction. Count creates multiple stamps at each spacing step. Count dynamics reduce the number down to a minimum of one. |
| Texture | Pattern picker/import; Invert; scale **10–1000%**; brightness and contrast **−100% to +100%**; Texture Each Tip; mask mode; depth, depth jitter and minimum depth **0–100%**; depth Control, Fade, input-response graph and once-per-stroke jitter. | Pattern luminance modulates tip coverage. Continuous texture stays anchored in artwork space; Each Tip gives each stamp a new pattern offset. Depth blends the untextured and textured coverage, with dynamics scaling the configured depth. |
| Dual brush | Secondary tip picker; mask mode; size **1–256 px**; spacing **1–400%**; scatter **0–1000%**; count **1–16**. | The secondary tip is a mask, not a second complete preset. Its size, phased spacing, scatter and repeated stamps modify the primary tip within its existing boundary. The selected secondary tip's own saved dynamics are not recursively applied. |
| Color dynamics | Enable; foreground/background colors; Apply Per Tip; FG/BG jitter, hue, saturation and brightness jitter **0–100%**; FG/BG Control and Fade; purity **−100% to +100%**. | Mixes the chosen foreground/background colors and varies HSV values. With Per Tip off, the stroke keeps a color sampled from its initial input. Purity changes saturation independently of the random saturation amount. For colored stamps, enable **Vary image colors** to adjust their source HSV values without foreground/background mixing or alpha changes; existing brushes default to preserving their original colors. |
| Transfer | Opacity and flow jitter, minimum opacity and minimum flow **0–100%**; separate Control, Fade, input-response graphs and once-per-stroke jitter. | Dynamics multiply the general opacity cap or flow. An opacity reduction does not remove paint already deposited during the stroke. |
| Brush pose | Independent override switches for Tilt X, Tilt Y, Rotation and Pressure; tilt **−90° to +90°**; rotation **−360° to +360°**; pressure **0–100%**. | Enabled values replace hardware data. Disabled values use the incoming pen data. This also permits controlled effects with a mouse. |
| Symmetry | Off, Vertical, Horizontal, Dual Axis, Diagonal, Wave, Circle, Spiral, Parallel Lines, Radial; center X/Y **0–100%** of the artwork frame; angle **−360° to +360°**; count **2–12**; radius/distance/wavelength **1–8192 px**; amplitude **0–8192 px**. | Shows the selected guide and creates transformed copies within the same stroke and undo step. Relevant parameters depend on the mode; see the table below. |
| Behavior | Noise; Wet Edges; Airbrush Build-up; Smoothing; New each stroke / Fixed seed; seed slider/exact entry and reseed action. | Noise introduces seeded coverage grain. Wet Edges reduces the stamp's interior coverage relative to its local edge; it is an edge effect, not fluid or pigment simulation. Build-up is the same setting as General's airbrush switch. Smoothing enables/disables the General smoothing amount. Protect Texture remains stored for legacy preset compatibility but is hidden in catalog-managed properties, which restore each tool's own texture configuration. |

Stroke blend modes are **Normal, Multiply, Screen, Overlay, Darken, Lighten, Color Dodge, Color Burn, Linear Dodge, Linear Burn, Difference, Exclusion, Subtract, Divide, Hue, Saturation, Color, Luminosity and Dissolve**. They blend against the existing pixels of the active raster layer. Dissolve uses seeded coverage noise. Erasing uses brush coverage to remove alpha.

Texture and dual-brush mask modes are **Multiply, Darken, Lighten, Subtract, Color Burn, Linear Height, Add and Difference**. These operate on coverage values; Linear Height uses a clamped additive mask operation. They are not Photoshop's proprietary texture or dual-brush implementation.

## Dynamics and pen input

Open the **wrench → Shape dynamics** to vary size, angle or roundness, or **wrench → Transfer** to vary opacity and flow. The same response and jitter controls are available for **Texture → Depth dynamics**. Each channel has its own configuration; for example, pressure can change size while tilt changes opacity and angle uses random variation.

Control selectors offer **Off, Pen Pressure, Pen Tilt, Stylus Wheel, Fade and Pen Rotation**. Angle additionally offers **Initial Direction** and **Direction**. Off disables the selected input control while its separate jitter still applies. Fade decreases the input over the chosen number of spacing/build-up steps. Minimum values bound the resulting dynamic factor. Pressure Size in General and the Size Dynamics Control selector stay synchronized, including presets that use the earlier pressure-size flag.

When a control is active, its response graph maps input on the horizontal axis to output on the vertical axis. Drag any of the five points vertically, or use **Linear, Soft, Firm and Invert** presets. Linear preserves the incoming value; Soft reaches stronger output with lighter input; Firm requires stronger input; Invert reverses the response. Values between points interpolate linearly. Left/Right selects a point, Up/Down adjusts it by 1%, and Shift adjusts by 10%. Escape during a graph drag restores the curve from before that drag. The graph changes the selected channel, not the global tablet calibration.

Jitter adds deterministic random variation. The icon beside Jitter switches between varying each stamp and choosing one random amount for the whole stroke. **Once per stroke** holds only the random part: pressure, tilt or other live input can still change the channel while drawing. This option covers size, angle, roundness, texture depth, opacity and flow. Scatter/count and color dynamics retain their existing separate controls; Color Dynamics already has its own Per Tip setting.

In **wrench → Behavior → Randomness**, **New each stroke** gives each new stroke a different seed. **Fixed seed** reuses the configured seed: the same path, input samples and settings reproduce the same variation. The wand button chooses another fixed seed. Exact entry accepts the full unsigned 32-bit seed range, while the slider covers a smaller practical range. A fixed seed does not force differently drawn paths to look identical. Raster artwork and captured vector strokes retain the outcome independently of later preset changes; vector replay retains the stroke's seed.

Hardware tilt, barrel rotation and wheel data require a pen and driver that provide those values through Qt. Pen Rotation controls barrel rotation, not cursor travel or canvas rotation; choosing it for angle applies the hardware rotation once. Wheel uses tangential-pressure capability and is unrelated to the mouse scroll wheel. Without the relevant capability, rotation is zero and wheel is full scale. A mouse supplies full pressure and upright tilt. Brush Pose overrides remain available without special hardware. Speed-controlled dynamics are not implemented. Physical Huion input and Windows behavior still need device acceptance testing.

The familiar brush families and input/jitter concepts draw on [Krita's preset families](https://docs.krita.org/en/reference_manual/krita_4_preset_bundle.html), [Clip Studio Paint brush customization](https://help.clip-studio.com/en-us/manual_en/240_brushes/Customizing_brush_tools.htm), [Photoshop brush dynamics](https://helpx.adobe.com/photoshop/desktop/apply-painting-techniques/fill-objects-selections-layers/brush-dynamics-settings.html) and [Procreate Brush Studio](https://help.procreate.com/procreate/handbook/5.4/brushes/brush-studio-settings). Toon Pro implements its own settings and response behavior rather than importing those applications' engines or preset assets.

## Texture images

The pattern picker provides **Paper, Canvas, Checker and Noise**. Enabling Texture with no pattern selects Paper. The folder button imports PNG, JPEG, BMP or another supported image format from a file no larger than **16 MiB** and **2048 × 2048 pixels**. Images are converted to grayscale coverage: white is full coverage, black is zero coverage, and transparency reduces coverage. Invert reverses that mask. A damaged or oversized image leaves the current texture unchanged.

The imported pattern is embedded when the tool configuration is saved automatically to the local catalog. Its original image file is not needed afterward. Selecting a primary or secondary sampled mask also copies the pixels into the brush configuration, so subsequently deleting the source preset does not invalidate the current brush.

## Symmetry geometry

Center and angle position the guide in artwork coordinates. These are native geometric constructions, with the following definitions:

| Mode | Transformation and relevant parameters |
| --- | --- |
| Vertical / Horizontal | Reflection across one axis through Center, rotated by Angle. |
| Dual Axis | Reflections across both perpendicular axes, producing up to four copies. |
| Diagonal | Reflection across a 45° line through Center, with the additional Angle rotation. |
| Wave | Reflection at the nearest tangent of a sinusoidal guide; Amplitude and Wavelength set its shape. This is a local curve approximation. |
| Circle | Reflection at the nearest circle tangent; Radius sets the guide. |
| Spiral | Reflection at the nearest tangent of a finite four-turn Archimedean spiral; Radius sets its starting radius and Distance its pitch per turn. This is a local curve approximation. |
| Parallel Lines | Translated copies on alternating sides; Count and Distance set the repetition. |
| Radial | Rotated copies around Center; Count sets the number of sectors. |

## Stamp-shaped pointer

The pointer follows the brush mask instead of always enclosing it in a circle. Asymmetric sampled tips show their shape, including separated islands and holes. Angle, flips, roundness, current input/pose and canvas zoom/rotation affect the outline. A plain round brush retains a round outline.

For soft masks, the contour uses **20% of the mask's peak coverage** so a nearly transparent feather does not make the pointer unreadable. The outline represents tip coverage, not a promise that every enclosed pixel will be painted fully opaque: flow, stroke opacity, blending, scattering and dynamic effects still affect the resulting stroke. Randomized masks are evaluated using the engine's stroke state while drawing; hovering shows a representative nominal tip.

## Current limits

Nominal primary sizes reach **1,024 px**; secondary masks retain their **256 px** limit. More stamps, large tips, fine spacing, textures and multiple symmetry copies increase CPU work. Prepared-tip caching and partial canvas redraws reduce repeated work without changing spacing, particle counts or texture settings. The document keeps its existing tile budget and cancels a stroke that exceeds it while preserving earlier artwork. Native brush settings apply to both raster drawing and captured vector brush strokes; they do not provide a Photoshop-compatible brush file format.

Automated checks cover native settings persistence, control binding and rendering cases. Constructed descriptor fixtures and a library integration test cover imported name/UUID association, supported settings and computed round tips. The artist's two real ABR packs pass import, persistence and raster/vector painting checks, as recorded in [brush compatibility](brush-compatibility.md); that local-pack test does not assert every source descriptor setting. Exact Photoshop output comparisons and physical pen acceptance remain separate validation work.


## Compact quick properties in 0.36.0

Size, Opacity, Flow, Hardness and Smoothing use aligned label/slider/value rows. Pressure size sits beside Size; the wrench remains accessible above the scrolling controls. At 220 logical pixels wide, all five settings fit in a 204-pixel-tall panel without scrolling. Narrower editor rows and long labels wrap above their controls without resetting a typed draft or focus. Exact fields stay 48 pixels wide, omit units inside the box, retain valid precision/ranges and show units in help. Advanced categories and brush behavior are unchanged.


## Advanced tips and quality in 0.45.0

The default catalog now contains 166 brushes, plus the 19 starters: 185 total. New sets cover rooted grass and reeds, varied foliage and whole trees, clouds and smoke, rocks and ridges, ripple/rain/splash stages, birds, butterflies, fish, feathers, snowflakes and petals. Dynamic subject systems has its own group. Seven existing grass/foliage catalog defaults gain multiple original silhouettes. The update changes future marks from untouched defaults; custom tips, other artist-edited fields and captured vector/raster artwork are preserved. The original recipe revisions remain available internally for migration comparisons.

Open the brush wrench and choose **Tip shape**. The tip grid shows the current images. **+** adds images; Trash removes the selected tip while retaining at least one; the arrows change its order. Imported set images fit proportionally inside 512 × 512 pixels. Mask brushes use transparent-image alpha, or inverted luminance for opaque images; colored-stamp brushes preserve RGBA colors. An existing single tip is retained when adding another. Sets hold up to 16 images in a shared atlas, within the existing 2,048-pixel resource side and library/project budgets.

The selection tabs provide **Random**, **Ordered**, **Shuffle**, **Pressure** and **Direction**. Random uses each tip's relative Weight; Shuffle uses every image once per cycle and prevents immediate repeats across the boundary. Its choices follow the saved stroke seed and dab index, independently of input batching. Pressure selects from low to high tip indices; Direction divides the artwork-direction circle into ordered tip sectors. Existing size, angle, color, flow and opacity dynamics remain independent.

The preview below the grid has a draggable root cross. Place it at a grass/tree base to keep that point on the drawn route despite changing silhouettes and sizes. Arrow keys adjust the anchor, Shift makes larger steps, Home centers it and End places it at the bottom. Roots rotate and mirror with their tips. The canvas cursor follows the chosen silhouette and its anchor.

Primary Brush/Eraser sizes support **1–1,024 px**, including the size grid's custom entries; initial size tiles remain unchanged. Large sizes increase rendering cost, especially with tight spacing, texture, high particle counts or symmetry. Source atlases, frames and transformed tips use bounded caches. Vector captures include the atlas and frame metadata and replay the same selected images after reopening. Tip sets/larger sizes use brush library format 5, tool catalog format 9, vector capture format 4 and project format 24 when captured in editable artwork. Older formats remain readable; older app versions cannot read these new formats.

The later 0.49.0 wave adds connected ribbons, architectural repeats, full-color surface patterns, independent manga tones, explicit perspective mapping and speed dynamics, together with those 34 catalog designs. This update does not add Photoshop import mappings or claim physical bristle/fluid simulation. Real-artwork review with the artist's Huion remains separate from synthetic rendering and pen-event checks.


## Connected brushes, patterns and specialized dynamics in 0.49.0

The default library now contains **219 brushes**: the 200 catalog designs plus 19 starters. The new Architecture and repeating details group holds windows, balconies, rooflines, fences, shopfronts, railings, insulators, cables and perspective surfaces. The other additions appear in their existing subject groups. Imported images and third-party brush packs are not required. Older recipe revisions remain available for migration; new brushes do not replace artist edits or restore previously deleted tools.

**Connected marks.** In the wrench dialog, **Tip shape → Ribbon / Repeat** maps the primary image along the drawn curve with joined segments. Width uses Size and supported size/speed/pressure dynamics. Repeat length controls distance along the curve relative to width; Ribbon additionally respects the source image proportions. Complete repeats can select among tip frames using the existing ordering modes, with optional alternate flipping. Color variation is evaluated per complete repeat. Transparent source pixels stay transparent. Pen-down alone does not place a whole motif; drawing movement builds the strip. Adjust stamp is unavailable in connected mode.

Connected modes use flow, opacity, blending, selection, alpha lock, symmetry and continuous canvas texture. Ordinary stamp spacing, count/scatter, tip angle/roundness/projection, root anchors, hardness, stationary airbrush buildup, dual-tip masks, each-tip texture, wet edges and noise are not connected-strip parameters. Their saved values are retained for Stamps mode. Tight reversals are bounded to avoid folded projective patches; a very sharp corner can still compress the motif. Raster and editable vector marks use the same rendering. Vector cuts preserve the remaining repeat phase, including short fragments between recorded input samples.

**Surface patterns and tones.** The **Patterns and tones** category offers Dots, Lines, Crosshatch, Diamond, Noise, Fade dots and Image. Frequency means **cycles per 100 artwork pixels**, not physical print LPI. Density controls ink coverage independently of frequency; angle and offsets position the grid. Fade dots adds end density and fade length along the pattern Y axis. Noise uses a fixed seed and cell grid. Tones keep the same phase across dabs and strokes; within one stroke, antialiased pattern edges cap paint coverage rather than becoming solid through repeated impressions. Ordinary alpha compositing still applies between separate strokes and layers. Minified tones average their coverage; export uses the artwork's pixel resolution without automatic print-DPI conversion.

Image imports preserve source colors and alpha in an independent repeating tile, without replacing the brush's tip. Tile width/height define repeat size in artwork pixels. **Perspective plane** maps the source plane onto a convex four-corner artwork-space quad. Drag its corners in the preview or use the precise X/Y controls. **Fit to artwork** places the plane over the current artwork frame, respecting its camera mapping and excluding production-sheet margins/header. This is an explicit action; selecting a preset does not relocate its saved plane. Source width/height control how much pattern fits inside the plane. Paint outside the quad stays transparent. This is an explicit 2D mapping; it is not automatically linked to drawing guides, 3D cameras or vanishing-point assistants. Image tiles use wrapped sampling and bounded mip filtering to reduce seams and aliasing.

**Specialized dynamics.** **Scattering → Clustered spray** supplies cluster count, radius, spread, radial bias, angular span and direction, with a direct distribution preview. The existing Count controls particles per dab. The cluster distribution replaces ordinary scatter offsets. All random placement uses the captured stroke seed. **Color dynamics → Foreground tint** moves a colored stamp toward the foreground hue/saturation while retaining source brightness shading and transparency; zero preserves the original palette. HSV variation remains separately opt-in. **Transfer → Speed response** changes size and opacity according to timestamped artwork-space pen/mouse speed. Reference speed is in artwork pixels per second; signed amounts choose thinner/lighter or larger/stronger fast marks. Queued event delivery and canvas zoom do not define velocity. Captured speed values replay with vector strokes.

Connected strips filter fine source linework before minification so small cables and fences remain continuous. Perspective thumbnails show a fitted plane swatch; flat surface thumbnails scale their repeats to the preview. Large brush thumbnails reduce source images before optional color processing and keep bounded caches; the original artwork resource is unchanged. Large cursor outlines trace a bounded preview mask while keeping the full-size outline coordinates. These optimize thumbnails and pointer feedback, not the full-resolution painting cost. Dense 1,024-pixel surfaces and connected strips still require substantial pixel work. Physical Huion feel and final production-artwork quality remain manual acceptance checks.

New data uses brush-library format **7**, tool-catalog format **12**, vector capture **6** and project/container format **26** when needed. Surface images share the established memory/resource limits. Older formats remain readable; unsupported downgrade attempts fail without replacing existing data. Source images, random seeds, speed samples and repeat phase belong to captured vector artwork, independently of future library edits.


## Connected join and tile quality in 0.49.4

Adjacent strip segments own shared longitudinal edges once, preventing extra flow buildup along those edges. If a sharp direction change resets the strip cross-section, the renderer bridges the outer wedge at the current repeat phase using the source image's transparency. Alternate-flipped repeats clamp to their reflected edge instead of bleeding in pixels from the opposite side of the image.

Catalog recipe revision 5 corrects Fence Posts and Railings and Bars at tile boundaries, spaces Balcony Row evenly across the repeat, and includes off-tile neighboring stones in Stone Perspective Path. Only unchanged default fields/resources migrate. Artist names, sizes, colors, custom source images, copied brushes and deleted tools remain protected by the existing upgrade rules. No new controls or file-format version are required. Existing painted raster pixels do not change; connected vector strokes adopt the rendering corrections on replay, while their saved source artwork remains embedded.

The review covers all 21 connected/repeating recipes on dense slow curves and abrupt corners. Very tight inside turns can still overlap/compress motifs, and a pattern crossing an outer bevel stretches with that bevel. Architectural motifs do not automatically redesign themselves into corner-specific windows, posts or roofs. Smooth curves and wider turns preserve the motifs more naturally. Physical Huion feel and artist review on production artwork remain separate acceptance checks.
