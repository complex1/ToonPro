# Convert to line art, shadows and tones

Implemented in the local 0.76 development build · 7 November 2026

Convert a selected image or drawing into a new group of editable raster lines and optional shading. The original layer and its native source are retained. This is an offline algorithmic feature; no model download, AI service or cloud upload is involved. Published 0.75 packages are unchanged.

## Start a conversion

1. Import an image using **File → Import image as artwork layer…**, or select an existing drawing layer.
2. Select its visible layer in **Layers**. Choose **Edit → Convert to line art…**, or right-click the layer and choose **Convert to line art…**.
3. Choose **Lines only**, **Cel shading**, **Manga tones** or **Grayscale**. Use **Source / Result** to compare the reference and conversion.
4. Adjust **Lines / Shading / Output**. Changes prepare a background preview; they do not edit the drawing.
5. Click **Create layers**. Full-resolution generation runs before the new group is committed. **Cancel** leaves artwork unchanged. One Undo restores the previous layers and original visibility.

The feature operates inside the focused drawing/current-shot editor. It adds no Movie, Scene or clip collection. For an animation drawing, output remains inside that drawing's folder; holds and reuse share the result just as they share other edits to that drawing. It does not generate an animated sequence or batch-convert cels.

## Lines

- **Detail** controls image-edge sensitivity.
- **Width px** controls line thickness in canvas pixels, including fractional coverage below one pixel.
- **Denoise %** suppresses grain and small texture variations before extraction. Higher values also remove more subtle image detail; lower it for fine drawing marks or delicate texture.
- **Smooth** follows nearby edge directions and smooths traced contours. It reduces staircase edges while retaining sharp turns and junctions.
- **Dust px²** removes small connected line fragments.
- **Line color** sets the generated ink, including alpha.

**Background cleanup** applies Detail 60, Width 1.5 px, Denoise 70%, Smooth 2.5 and Dust 12 px² in one click. Current ink/shade colors, shading mode and output choices are preserved. Start here for architectural or photographic backgrounds, then raise Detail if a faint edge is missing. Lower Dust for small disconnected marks. Use Source / Result to judge the tradeoff between cleanup and detail. Previews are reduced; inspect the created layers at 100% for final line quality.

Image extraction uses luminance, separable edge-preserving bilateral smoothing, a structure-tensor direction field, flow-guided difference-of-Gaussian responses and connected strong/weak edge thresholds. Component cleanup and thinning feed subpixel contour tracing, simplification and antialiased round strokes. Short weak islands are removed according to Denoise; strong small marks and long faint contours are retained when they pass the extraction thresholds. This is a raster algorithm, not semantic scene understanding or a vector-output mode. Textured backgrounds and low-contrast details still need artist judgment.

Source color, transparency and the selected layer's own rendered effects are considered; other layers are not merged into the input. Clipped source layers and clipping bases with dependent layers need an unclipped/merged copy first. Image-file support follows the existing artwork import codecs.

## Shading from an image

Image shading follows the reference's existing light and dark regions:

- **Cel shading** separates deep and middle shade regions into independently colored layers.
- **Grayscale** produces a grayscale shade layer.
- **Manga tones** produces raster dot patterns from the selected tone bands. **Dots px** is spacing in canvas pixels and **Angle °** rotates the pattern. These are saved pixels, not a live DPI-dependent print-tone layer. Very small patterns use average coverage in the reduced preview; inspect final output at its intended scale.
- Drag the two handles on the tone ramp, or enter exact **Deep shade / Shade limit** thresholds. The thresholds cannot cross. The ramp also supports Left/Right adjustment and Up/Down handle selection.

A dark material in a photograph may be included in the dark tone band. The algorithm does not infer an object's original material color separately from its lighting. It also does not reconstruct geometry or move a new light over a flat photograph. Existing [Cel Shading](cel-coloring.md) remains available for drawing deliberate shadow/highlight boundaries afterward.

## 3D scope — explicitly deferred

The artist deferred 3D conversion on 7 November while the reference-3D system is being revamped. There is no model import, orbit/light widget, 3D extraction engine or 3D conversion panel in this change. Existing native 3D layers are refused as direct input. The separate 3D-authoring gate is preserved.

A PNG/JPEG snapshot from a 3D view can be imported and processed as an ordinary 2D image. It has the same tone-extraction limitations as a photograph. A future connection to the revamped 3D system should pass a rendered image snapshot into this tool; a geometry-aware conversion pipeline or new 3D panel is not implied.

## Editable output and source preservation

The new group contains only nonempty layers requested by the settings, stored bottom to top:

1. **Base**, when **White base layer** is enabled.
2. **Shadows**, for middle cel shade, grayscale or screentone pixels.
3. **Deep shadows**, for the deep cel band.
4. **Lines**.

The two cel bands are disjoint; their colors do not accidentally accumulate over each other. All are ordinary raster artwork that can be painted, erased, masked, transformed, saved and reopened. No new project storage format is needed.

**Hide original after conversion** changes visibility reversibly. A locked source can be read, but remains visible and locked; its hide option is disabled. Locked/hidden destination folders are refused. Output preserves the source's folder and Notes ancestry rather than silently changing export membership.

Output is an independent snapshot. Changing the original image, model or camera later does not regenerate it. This version does not save a conversion recipe, editable vector strokes, per-object line styles or linked conversion instances. The source itself remains editable.

## Limits and job behavior

- Canvas limit: **9,437,184 pixels**, with at most **16,384 pixels per side**. Final layers use actual canvas resolution; reduced previews are not enlarged into final artwork.
- Project layer, tile, storage and history limits are checked before committing. A refused conversion does not hide the source or leave partial layers.
- Preview requests are coalesced on a dedicated single-worker pool. Cancellation prevents publication of partial/outdated output. Edits, Undo/Redo, source selection, frame or document changes invalidate the open conversion.
- Rendering is limited to the artwork frame. Sheet margins and overscan are not included in the generated layers.
- There is no 3D conversion, image relighting AI, temporal propagation, vector output, editable screentone layer, saved preset catalog or automatic regeneration in this version.

## Relationship to Clip Studio Paint

Clip Studio Paint EX's **Convert to lines and tones**, also called LT conversion, is the workflow reference: image/3D sources become separate line/tone layers with preview. Its 3D controls distinguish geometry lines, texture lines and tones, and its documented scope includes vector line output. Toon Pro uses its own algorithms and currently produces raster output; equivalent results or full feature parity are not claimed.

Its line-density, fragment-removal and thickness controls inform the quality direction. Toon Pro's Denoise and Background cleanup controls are its own implementation; they do not reproduce Clip Studio Paint's proprietary extractor.

Official sources checked 7 November 2026:

- [Convert to lines and tones — EX manual](https://help.clip-studio.com/en-us/manual_en/390_filters/Convert_to_lines_and_tones_%28EX_only%29.htm)
- [Official explanation of geometry, texture and tone categories](https://support.clip-studio.com/en-us/faq/articles/20240039)

## Implementation and remaining acceptance

Image conversion and tone composition live in `src/line_art.cpp`; the edge-processing pipeline lives in `src/line_art_edges.cpp`. Source preparation and guarded document publication live in `src/document_line_art.cpp`. `src/line_art_dialog.cpp` supplies the modal preview, visual controls and background jobs. The existing 3D modules are not modified by this feature.

Automated coverage is in `tests/test_line_art.cpp` and `tests/test_line_art_quality.cpp`; final verification and capture locations are recorded in [the development log](DEVELOPMENT.md). Quality checks include a deterministic architectural drawing with grain, faint structural lines, sharp window corners, close parallel lines and a photographic building sample. Broader production-reference tuning, physical tablet feel on generated layers and additional platform packages remain artist/platform acceptance work.

Possible later extensions: editable vector lines, retained recipes and preset catalogs, per-object/exterior line styling, more advanced line and tone quality, and optional local AI image simplification or estimated relighting. These remain separate from the implemented raster conversion.
