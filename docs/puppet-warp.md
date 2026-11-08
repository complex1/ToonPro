# Puppet Warp for raster artwork

Local 0.76 development build · 7 November 2026

Puppet Warp deforms a raster image with freely placed control pins. A triangular mesh follows the pins, while the other pins hold their positions. Apply retains the original raster source, mesh bounds, density and pins on the layer. Later edits render from that same source, avoiding repeated resampling of the deformed result.

## Use it

1. Import an image as an artwork layer, or select an existing raster layer in Layers.
2. Choose **Edit → Puppet Warp…**. You can also choose the **Warp** tool and its **Pins** tab in Tool Properties. **Grid** retains the earlier nine-handle warp.
3. Set **Density** with the slider or exact entry. It controls the number of mesh cells along the longer side, from **4 to 32**. You can change density after moving pins; current pins are retained.
4. Click inside the mesh to add pins. Put pins where the artwork should stay fixed as well as where you want to move it. Drag a pin to deform the image. One pin moves the whole image; add more pins to constrain it.
5. **Apply** or Enter saves the editable deformation in one Undo step. **Cancel** or Escape discards the current preview. Choosing another tool with a changed preview offers Apply / Discard / Cancel.

Apply can also save a mesh before adding pins; reopen it later to place them.

The mesh covers the selected artwork's rectangular pixel bounds, including transparent gaps inside those bounds. With a pixel selection, it covers the selected region and moves only the selected artwork. A selected folder or several raster layers share one mesh while retaining their layer structure. Drawing margins and overscan follow the existing transform rules; pixels outside the allowed layer extent are cropped on Apply.

## Pin controls

- **Shift-click** pins to add or remove them from the current selection. Drag a selected pin to move the selected pins together.
- **Alt-click** a pin to delete it, or use the trash icon / Delete / Backspace for selected pins.
- **Show mesh** hides the triangle overlay while keeping pins and the live deformation visible.
- **Reset deformation** restores the original shape and retains the pins.

## Reopen and adjust an applied mesh

Select the layer and choose **Edit → Puppet Warp…**, or right-click it in Layers and choose **Edit Puppet Warp…**. The Pins inspector also has a **Reopen saved mesh** icon after Apply. The layer's own pins and density are restored, including after saving and reopening the project. Other raster layers can retain independent meshes.

Apply updates that layer's saved deformation from its original pixels. Cancel leaves the previously applied appearance and settings intact. Opening and applying an unchanged saved mesh creates no Undo step. Reset restores the original shape while retaining the pins and editable source. A saved partial-selection warp continues to use its original selected source region; reopening it does not replace a different current pixel selection.

Saved meshes retain a full copy of the original layer's raster tiles, including pixels outside the drawing extent. The current appearance is cached as raster tiles for normal rendering, previews and export. Source pixels and settings are stored inside the project; no original image file is needed after import. Pins/Grid and Show mesh remain local tool preferences, while each retained layer owns its density and pins.

## Bake when ready to paint

An applied editable warp protects its source and generated pixels from direct painting, erasing and other pixel transforms. Continue adjusting the mesh, or choose **Edit → Bake Puppet Warp** / **Layers context menu → Bake Puppet Warp** to keep the current appearance and return to ordinary raster editing. Baking removes the stored mesh and source from that layer. One Undo restores them. Visibility, opacity, layer effects and organization remain available while the mesh is retained.

## Implementation and limits

The implementation uses rigid moving least squares to compute the deformation, with pins inserted as mesh vertices so raster interpolation honors their positions. The mesh is rebuilt from source geometry when density changes. Bilinear sampling on Apply preserves premultiplied transparency; shared triangle edges are sampled once. Identity previews avoid resampling. Applying a newly placed set of pins saves its editable state; applying an unchanged reopened mesh creates no history.

- Up to **64 pins**, density **4–32**. Higher density uses more triangles and takes more work to preview.
- Raster artwork only. Rasterize vector/text content first. Native 3D and linked Symbols use their existing editing workflows.
- Existing layer/folder visibility, position/alpha locks, animation-source restrictions and drawing coordinate limits apply. The **1,024-tile project limit** counts both the rendered artwork and retained source tiles; saving also retains the existing 300 MB container bound.
- A new warp refuses an initial selection above **512 path elements**; simplify it or deselect first. Reopening a saved warp uses its retained source region and preserves the current selection, including a more detailed one. The regular grid warp retains its 4,096-element limit.
- Moves that fold/collapse a triangle, cross the outer mesh boundary or leave valid drawing coordinates are refused; the last valid preview remains available.
- This version does not provide per-pin rotation, overlap order, silhouette-derived meshes, mesh expansion or animation of pins. Saved deformation settings are supported for raster layers; they are not an animation rig.

Projects containing a retained warp use **format 40**, with checked binary source-image resources and bounded metadata. Projects without one retain their existing format selection. Older files remain readable; earlier app builds cannot open format 40. Meshes from the earlier baked-only implementation cannot be recovered from their rendered pixels unless the original source and pins still exist elsewhere.

The workflow reference is [Clip Studio Paint's Mesh Transformation and Puppet Warp guide](https://help.clip-studio.com/en-us/manual_en/360_transform/Types_of_transformations.htm), with [mesh/pin settings](https://help.clip-studio.com/en-us/manual_en/810_subtools/M.htm), reviewed 7 November 2026. Clip Studio distinguishes grid handles from freely placed puppet pins. Toon Pro implements the pin workflow using its own code and limits; full feature parity is not claimed.

Algorithm reference: Schaefer, McPhail and Warren, [Image Deformation Using Moving Least Squares](https://people.engr.tamu.edu/schaefer/research/mls.pdf), SIGGRAPH 2006. No external deformation library or AI service is required.

Code: `src/puppet_warp.cpp` supplies geometry and deformation; `AdvancedTransform` supplies preview and atomic document publication; the drawing controller and properties panel supply canvas interaction. Verification and local capture paths are recorded in [DEVELOPMENT.md](DEVELOPMENT.md).
