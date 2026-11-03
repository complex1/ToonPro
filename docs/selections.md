# Selections and transforms — 0.14.6

Choose **Rectangle (M)**, **Lasso (L)** or **Ellipse selection** in Tools, then drag on the canvas. **Polygonal lasso** accepts successive clicks and finishes with Enter. **Magic wand** selects a connected color region, while **Color range** selects matching colors throughout the sampling area. Their Tool Properties provide tolerance and active/all/reference-layer sampling. Connected regions also support gap closing. See [drawing tools](drawing-tools.md) for the limits and algorithms. The selection is defined in artwork coordinates, so working-view zoom and rotation do not change what is selected. Rectangle edges align with the artwork. Tiny clicks leave the selection unchanged. Lasso paths close on release.

The thin black-and-white dashed border moves around the selection. A subtle diagonal hatch shades the unselected drawable area, including holes inside a selection, while the selected artwork stays clear. The production header stays unshaded. Turn the shading off with **View → Shade outside selection** or the floating bar's More menu. Tool Properties also offers border animation and a shade-strength slider with adjacent percentage entry. These effects are view-only and stay out of exports.

## Floating selection actions

The compact bar appears below a selection and follows it when the view changes. It moves above or beside the selection when space allows, and stays within the canvas. It hides while drawing or dragging so it does not intercept the gesture. Drag its grip to pin it; **More → Reset position** returns it to following the selection. After hiding it, restore it with **View → Selection task bar**.

- Four direct icon buttons offer **New**, **Add**, **Subtract** and **Intersect**. New replaces the current area. Hold Shift when starting a gesture to add, Alt/Option to subtract, or Shift+Alt/Option to intersect. The mode is captured at the start of the gesture. With no existing selection, each mode starts a new selection.
- **Transform**, **Invert** and **Deselect** are directly on the bar. During a transform, the bar shows **Apply** and **Cancel**.
- **More → Fill with foreground** fills the selection on the active raster layer. It uses the foreground color and its opacity, independently of brush settings. Alpha lock preserves existing transparency. **Clear selected pixels** removes selected coverage; turn off alpha lock to use it. Both are undoable, respect inherited visibility/locks, and keep the selection.
- **More → Expand selection / Contract selection** uses the shared slider and numeric field, from 1–100 pixels, then Apply. These native hard-edge operations round expanded corners and stay inside the drawable area. Contracting can produce an empty selection.

The bar takes its contextual placement and pin/reset/hide behavior from Adobe's [Contextual Task Bar](https://helpx.adobe.com/photoshop/desktop/get-started/learn-the-basics/boost-workflows-with-the-contextual-task-bar.html). The diagonal outside shading is Toon Pro's requested focus treatment. This is a working subset of manual selection actions; it does not provide Photoshop AI tools or Select and Mask.

Tool Properties switches to selection controls for these tools. **Select all** selects the active layer's drawable extent, including overscan and Notes margins where applicable. **Invert** selects its complement within that extent. **Deselect** restores unrestricted painting. An empty active selection permits no painting; it is different from having no selection. The Select menu provides these actions, with Cmd/Ctrl+A, Cmd/Ctrl+Shift+I and Cmd/Ctrl+D while the canvas has focus. Fill and Clear are also available there; Backspace clears selected pixels while the canvas is focused.

Brush and eraser respect the same selection coverage as pixel transforms, including symmetry copies, sampled tips and stationary airbrush. **Feather** in Tool Properties pairs a slider and exact pixel entry for graded coverage from 0–100 artwork pixels; a radius of zero keeps hard edges. Fills, gradients, retouching and clipboard extraction also respect feathering.

The current selection is workspace state: creating or clearing it does not dirty the project, appear in PNG exports, or persist when reopening a file. **Named selections** save geometry and feather radius in the project. The save, load and delete controls are beside the selection-name field; up to 64 named selections are supported per shot. Saving, replacing and deleting named entries participates in undo. Applied transforms also record the before/after selection boundary.

## Transform pixels

1. Select the relevant pixels on a visible, unlocked drawing layer. With no selection, Transform uses all painted pixels on that layer.
2. Choose **Transform (T)**, **Select → Transform pixels** (Cmd/Ctrl+T), or **Transform pixels** in Tool Properties.
3. Drag inside the box to move. Drag a corner to resize about the box center while retaining proportions. Hold Shift at a corner to scale width and height independently. Drag the circular handle above the box to rotate.
4. Tool Properties offers responsive rows with a label, slider and compact exact-entry field for X/Y displacement, width/height percentages and rotation. Long rows wrap when needed. The link icon beside Width preserves the current aspect ratio for numeric scaling. Canvas handles provide the direct visual interaction.
5. Choose **Apply**, or press Enter with the canvas focused. Choose **Cancel** or press Escape to discard the preview. Switching away from a changed transform asks **Apply / Discard / Cancel**: Cancel keeps the current tool and preview, while Apply or Discard resolves it before switching. An unchanged transform exits without a prompt.

Apply and Cancel remain pinned below the scrolling pixel-transform properties, so they stay visible in a short dock.

You can move, scale or rotate the preview beyond the drawing extent and bring it back without losing pixels. Pending artwork remains visible beyond the paper within the workspace view. Apply crops moved raster pixels to the drawable area, including configured overscan and Notes margins; Undo restores them. Applied selection boundaries are also clipped, and a wholly outside selection clears so drawing can resume.

Space-drag or middle-drag pans during a transform; view zoom/rotation and Navigator navigation remain separate from the pixel edit. Focus can move to numeric fields without applying or cancelling. The canvas and Navigator show the preview, while saves and exports use committed document pixels. Save, export and project settings stay disabled until Apply/Cancel. Opening another project or closing the window offers a choice for the pending transform. Undo during a preview cancels that preview; an applied transform is one undo step, restoring both its pixels and selection boundary.

Dragging repositions a cached copy of the selected pixels at the visible canvas resolution. The preview uses nearest-neighbor sampling for responsiveness; Apply produces the final full-resolution result. An integer translation copies pixel bytes exactly. Other translations, scaling and rotation use premultiplied bilinear sampling from the original source once on Apply; repeated preview adjustments do not resample an earlier preview. Selected pixels are cut out and composited over the unselected remainder. Scaling down or applying repeated separate transforms can lose image detail, as with other raster edits. Transparent edges may extend slightly with filtering.

## Advanced layers, perspective and warp

**Select → Transform selected layers / perspective / warp** uses the current multi-selection in Layers, including a selected folder's descendants. It preserves each layer, hierarchy, blend mode, opacity and clipping relationship. Choose the individual **Perspective Transform** or **Warp** tool for four perspective/distort corner handles or a nine-handle warp mesh. Tool Properties provides horizontal/vertical flips and skew controls for the active transform tool. Drag away from a handle to move the whole transform. Enter applies; Escape cancels. Dragging changes only preview geometry, and Apply commits the result once.

**Select → Transform selection boundary** changes the outline without modifying any pixels. This has the same canvas handles, Apply/Cancel and undo behavior. Its bounds include blank selected areas. Matrix transforms preserve vector/text object data; mesh warp requires explicit rasterization first. Whole editable objects cannot be partially transformed through a pixel selection.

The mesh consists of eight affine triangles and must remain unfolded. Crossed quadrilaterals or mesh edges, locked targets and coordinates outside the supported ±32768-pixel range are rejected. Preview artwork may extend beyond the drawable area until Apply. Matrix transforms retain editable vector/text sources while cropping their rendered pixels; rasterize first if permanent pixel cropping is needed. See [advanced drawing tools](drawing-tools.md#advanced-transforms) for exact behavior and limits.

The Edit menu provides **Cut**, **Copy**, **Paste into new layer**, **Paste into active layer** and **Duplicate selected pixels**. Clipboard extraction preserves feathered transparency and original placement, including negative overscan. Newly pasted/duplicated layers enter positioning; creating the layer and applying its subsequent transform are separate undo steps.

## Current boundaries

- The basic **T** transform operates on one raster layer, including a clipped layer's own stored pixels. Use the advanced transform for groups/multiple layers, perspective, flips or warp. Neither tool moves a 3D camera. Turn off alpha lock before transforming.
- The basic transform box encloses selected **painted** pixels. Advanced transforms also include the active selection boundary in their editable extent, so empty selected margins survive mesh transformation.
- Basic numeric scales range from 1% to 1600%. Advanced flips, perspective and mesh warp are separate controls. Invalid values and coordinates outside the supported range are rejected during dragging. Apply crops the final filtered pixels to the drawing extent and checks the exact tile budget; if it cannot finish, committed artwork stays unchanged and the preview remains available to adjust or cancel.
- Feathering uses a native box-filter mask. Magic wand/color range use color tolerance and bounded raster sampling; they do not provide semantic subject selection, hair refinement or AI segmentation.
- Previews run on the CPU using viewport-sized buffers, capped at about four million pixels per buffer. Selection masks and unchanged layer composites are cached. Full-resolution Apply can still take time on large rotated/scaled artwork; very large views may show a lower-resolution preview. Physical tablet latency and production-scale multilayer workloads still need artist testing.

Applied raster results save and recover as ordinary tiles in the current version 8 project container. Editable vector/text data and named selections are also stored. Recovery snapshots include committed artwork and named entries; unapplied transform previews and the current transient selection are not recovered.
