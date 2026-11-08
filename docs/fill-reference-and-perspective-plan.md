# Reference-layer filling and perspective detection

Initial design reviewed 7 November 2026 against the local 0.76 source. **The requested implementation has since been added locally.** Read [Fill using reference layers](fill-reference.md) and [Detect perspective from an image](perspective-detection.md) for current controls and limits, and [the development log](DEVELOPMENT.md) for validation. The sections below retain the original proposal and pre-implementation audit; they are design history, not a claim that every later extension has shipped.

## What the application already has

| Area | Existing behavior | Requested addition |
| --- | --- | --- |
| Fill boundaries | Current layer, visible drawing, or one explicitly chosen reference layer; color is written only to the active paint layer. | Make these modes clearer and connect Reference to persistent marks in Layers. |
| Reference selection | A layer picker in Fill Tool Properties; references are resolved for the current exposed drawing. | Mark/unmark any layer, retain the mark in the document, and support multiple marked layers or folders. |
| Fill quality | Tolerance, close gaps, selection coverage and antialiased-edge handling. | Preserve these behaviors for the marked-reference source. |
| Perspective guides | Editable one-, two- and three-point guides, four-corner perspective planes, parallel rulers and fisheye guides; snapping, Undo and saved guide sets. | Estimate guide geometry from an image or selected layer. |
| Image analysis | A background line-art conversion workflow with immutable project snapshots and stale-result checks. | Reuse that job pattern for geometric line analysis, without converting or modifying artwork. |

The existing Fill controls are three icon tabs in Tool Properties: **Sample visible layers**, **Sample active layer**, and **Sample reference layer**. The last mode exposes a layer picker. There is currently no persistent reference flag in `RasterLayer` or mark action in Layers. Automatic perspective detection is also absent; existing guides are placed manually.

## Fill workflow

Use one compact **Boundary source** control with three clearly named modes:

| Mode | Read boundaries from | Write color to |
| --- | --- | --- |
| **Current layer** | The active layer's artwork. | The active editable raster layer. |
| **Whole drawing** | Visible composited content of the current drawing. | The active editable raster layer. |
| **Reference layers** | Marked reference content, or a retained explicit-layer choice. | The active editable raster layer. |

“Whole drawing” refers to the current document and exposed drawing, never other editor tabs, inactive cels, project assets or every document in a Movie project. It does not mean painting onto every layer.

Add a direct **Mark as fill reference** icon beside the existing layer flags, a matching layer-row badge and a context-menu action. Multi-selection marks/unmarks the selected rows in one Undo command. Several layers can be marked; a marked folder refers to its content. Changing the active color layer must not change the reference. The label distinguishes this setting from the References image library and 3D reference scenes.

Example: mark **Ink** as a reference, select **Colors** below it, choose **Reference layers**, then click inside a closed ink shape. Ink supplies the boundary and remains unchanged; Colors receives the fill. Raster, vector and text sources use their rendered boundaries while remaining editable.

Reference mode should show a short source summary, such as **2 reference layers**. With no marks, show **Mark a layer as a reference** and leave the artwork unchanged; never silently fall back to the whole drawing. Preserve the existing explicitly chosen layer workflow for saved tools and actions, with its source name visible and a direct **Use marked layers** action. New Reference-mode selections default to the marked set.

### Reference sampling rules

- Any layer row may carry the reference mark. Marking does not change its content type, visibility, paintability or export behavior. A locked ink layer can be sampled while an unlocked color layer receives paint.
- Renderable artwork and folders provide boundaries. Other renderable layer types use their current image representation; this does not require new 3D authoring or scene controls. A lone adjustment has no independent image. An empty or nonrenderable reference set reports that no reference content is available instead of filling the canvas unexpectedly.
- Keep the existing explicit-reference convention: a chosen reference can supply boundaries while hidden or faded. For marked roots, normalize eye/opacity for sampling and show hidden sources in the source summary. Preserve their masks, local effects and world placement. Within a marked folder, retain child visibility and compositing. Explain this behavior in the tooltip/guide; Whole drawing continues to sample visible compositing normally.
- Combine marked reference roots in their original stack order. If a marked folder contains another marked item, sample that subtree once. Unmarked ancestors still supply coordinate transforms; they must not accidentally bring unrelated artwork into the reference image.
- Preserve the existing isolated-reference treatment of root blend/clipping, while keeping compositing within a referenced folder. Test clipped and masked sources explicitly. Do not flatten the document to obtain a reference.
- Tolerance, close gaps, antialias handling, alpha locks, selections, pixel budgets and one-command fill Undo remain shared with the existing fill engine. If sampling fails, do not write any pixels.
- For animated content, resolve only the current exposure. Marking belongs to the actual layer, so held exposures retain it and unrelated cels do not become references. Animation expansion is not part of this drawing-focused work.

### Code and storage changes

1. Add a default-false `fillReference` flag to `RasterLayer`, document-backed batch mark commands, row state/badge and direct Layers action. Preserve marks through save/reopen, duplication and history; test merge/flatten handling deliberately.
2. Keep the current `RasterSampleSource` numeric values: ActiveLayer=0, AllLayers=1, ReferenceLayer=2. Append a separate marked-reference mode rather than reinterpreting 2 and changing saved tools or actions silently.
3. Add one shared source resolver to `raster_tools.cpp`; use it for Bucket and the existing Magic Wand/Color Range sampling path. Keep explicit-layer sampling working. The three user-facing groups need not expose every internal compatibility mode as another tab.
4. Update tool-state validation/restoration in `tool_session.cpp` and its catalog schema checks. Keep document-specific marks in project data, not global workspace preferences. Avoid auto-selecting the first unrelated layer if a saved reference disappears.
5. Extend `canvas_availability.cpp` with missing/empty-reference feedback. Extend Auto Actions validation/recording/replay deliberately: marked-reference fills resolve the replay document's current marks, while old explicit-reference actions retain their relative-layer behavior. A recording must not stop after an unrecognized fill has already modified artwork.
6. Write optional mark metadata only when needed and choose the next free project-format version at implementation time. Format 41 already serves native reference-layer assets and 42 serves Outline; do not reuse either number. Old documents default to unmarked layers. Validate unknown/malformed metadata atomically.

Primary implementation locations: `raster.h`, `document.h`/layer commands, `layer_panel.cpp`, `raster_tools.h/.cpp`, `drawing_tool_panel.cpp`, `canvas_availability.cpp`, `tool_session.cpp`, `tool_catalog.cpp`, `auto_actions.cpp`, `project.cpp` and `storage.cpp`.

## Perspective detection workflow

Add **Detect perspective…** to the existing Guides actions and the layer context menu. It opens a focused analysis dialog, rather than a new permanent panel.

1. Choose **Selected layer** or **Image file**. A layer invocation starts with the selected layer; a file is read locally without uploading it to a service. Allow a region selection to focus on a building, room or other part of the image.
2. Detect likely straight-edge families and show the proposed guides over the source image. Start with **Auto / One-point / Two-point** choices; show only supported candidates. Density uses the existing guide controls.
3. Show an evidence-based status such as **Strong match**, **Review suggested** or **Not enough straight edges**. Do not present an uncalibrated percentage as accuracy. Let the artist choose among distinct candidate families, exclude misleading detected lines and drag vanishing-point/anchor handles.
4. Offer a manual fallback: draw two lines that should converge to the same vanishing point. Their intersection seeds a guide; the rest of the geometry remains editable. This is useful for stylized or inconsistent perspective.
5. **Add guides** appends normal guides in one Undo operation. Existing guides and source pixels remain unchanged. Cancel creates no guide/history entry. After adding, the ordinary Guides panel provides snapping, density, color, locking and direct canvas handles.

Guides are independent shot data after acceptance. Moving or deleting the source layer later does not regenerate them automatically. This first version does not recover real-world measurements, reconstruct a 3D scene, change the photo's perspective or bake grid lines into artwork.

For a file source, fit the image into the drawing using an explicit image-to-artwork mapping shown in preview. Accepting stores guides, not an external path dependency. If the artist wants the photo in the drawing, use the existing image import; do not insert a new artwork layer silently.

### Recommended algorithm

Begin with a local CPU algorithm. No account, model download or GPU should be required for the baseline. This is an engineering recommendation, not a measured quality claim.

1. Capture an immutable source image plus its mapping into artwork coordinates. Analyze a bounded reduced copy, using light denoising, luminance and edge gradients. Composite transparency over an analysis-only background. Preserve the original image.
2. Detect straight segments with a bounded Hough-based detector. Filter short/noisy segments, merge collinear duplicates, and reduce the influence of text, image borders and texture. OpenCV's documented Hough/LSD methods are useful reference and benchmark options; adding OpenCV as an app dependency is a separate packaging decision. The first implementation can use original native C++ with the existing Qt image stack.
3. Fit candidate vanishing points with weighted robust consensus, such as RANSAC, using segment length, angular agreement and support across several image regions. Merely clustering similar line angles is insufficient: converging lines have different image angles. Keep alternative line families for artist review instead of assuming every scene is an orthogonal room.
4. Refine the candidates against their supporting segments and score both fit and spatial coverage. Repeated fragments of one edge must not count as many independent votes. For two-point output, establish which pair describes the intended horizontal plane; if ambiguous, ask the artist to choose the families in the preview. A single vanishing point does not establish a unique horizon.
5. Map accepted candidates into existing `DrawingGuide` objects. One supported finite direction yields a VanishingPoint guide; two supported finite directions can yield TwoPoint with a valid anchor. Parallel families use ParallelRuler. Do not invent a missing vanishing point or force an unsupported vertical ruler. Add three-point inference only after its candidate assignment and quality gates pass.
6. Use homogeneous geometry internally for distant/parallel vanishing directions. Existing guide points are finite and bounded to ±1,000,000 artwork pixels: choose parallel rulers or ask for correction when a result cannot be represented faithfully, instead of clamping a far-away point and changing its meaning.

Start by proving one- and two-point detection on rooms, streets, architecture and straight-edged illustrations. Organic subjects, blank/soft images, fisheye distortion, multiple independent perspective systems and intentionally distorted anime backgrounds may provide insufficient or conflicting evidence. Return a useful partial proposal or the manual fallback rather than an invented grid. Fisheye inference and a learned detector are later extensions.

### Integration and responsiveness

- Separate `perspective_detection.h/.cpp` for image analysis, a dialog for review, and a document command for committing accepted guides. Keep widgets and mutable Document access off the worker thread.
- Reuse the immutable-snapshot, cancellation and stale-result pattern of `line_art_dialog.cpp`. Do not call `lineArtSource` unchanged: it excludes folders/clipped sources and imposes conversion-specific mutation restrictions. A new shared read-only capture helper must preserve actual layer/folder world transforms, masks, clipping and effects without requiring a locked source to be unlocked.
- Capture in artwork coordinates before camera/view transforms, or carry an explicit inverse mapping. Test translated/scaled/rotated parents, overscan and image orientation so grids align with the source rather than the current zoom or preview size.
- A source layer/folder can be rasterized from the current exposure for analysis while its native source stays intact. A saved-file image uses `QImageReader` with orientation handling and decode-size validation. Unsupported source types get a clear reason, not a blank success.
- Use one running job and one replaceable pending request, cancellation between stages, bounded line/iteration counts and allocation checks. Proposed starting budget: approximately 2 megapixels, no more than 2048 pixels on the longer side, and a conservative 256 MiB working cap; choose final limits from profiling rather than promising a completion time.
- Before accepting, revalidate the document/shot/source/frame identity, content revision and guide capacity. A completed job for a closed tab, edited source or different drawing cannot add guides. Resolve pending editing gestures through the existing workspace lifecycle.
- Existing guides already save and support Undo/snapping. Store accepted geometry through that path; analysis edges/candidates remain temporary. A new project format is unnecessary for plain generated guides. Persisting a regeneration recipe or linked source would be a later format decision.

## Development order and acceptance

1. **Fill reference marks:** finish source semantics, layer flag/history/storage, direct UI and sampling. Verify multi-layer/folder references, hidden/locked sources, missing marks, saved tools, Auto Actions, selections, antialiased edges and source preservation.
2. **Perspective proof:** implement the detector as a testable image-to-candidates function. Use synthetic one/two-point scenes with known vanishing points, rotated horizons, off-canvas points, parallel families and noise; include negative images where no guide should be proposed. Record angular error and false-positive behavior rather than relying on attractive screenshots.
3. **Review and correction UX:** add local-file/layer capture, editable candidate overlays, region/line selection, manual two-line fallback and one-command guide insertion. Confirm Cancel, Undo/Redo, save/reopen, snapping, non-exported guides, stale jobs and source preservation.
4. **Artist quality pass:** review real background references and stylized drawings at normal/Retina scale before calling automatic detection production-ready. Add a third finite family only when evidence supports three-point inference. Consider local AI later only if the measured failures justify it.

## Evidence and references

Existing behavior was traced through `raster_tools.cpp`, `drawing_tool_panel.cpp`, `canvas_availability.cpp`, `tool_session.cpp`, `auto_actions.cpp`, `drawing_guide.h`, `guide_tool.cpp`, `guide_panel.cpp` and the existing fill/guide tests. The review's build and baseline-test logs are under `build/fill-perspective-audit-*`; they verify existing features, not the proposed additions.

- [Clip Studio Paint: Reference layers](https://help.clip-studio.com/en-us/manual_en/180_layers/Reference_layers.htm) documents layer/folder marking, multiple reference layers and fill/selection usage. It informs the intended interaction without claiming full CSP parity.
- [OpenCV: Hough Line Transform](https://docs.opencv.org/4.x/d9/db0/tutorial_hough_lines.html) explains edge preprocessing and straight-line/segment detection.
- [OpenCV: LineSegmentDetector](https://docs.opencv.org/4.x/db/d73/classcv_1_1LineSegmentDetector.html) documents a grayscale-to-segments alternative for comparison.
- [Lin et al., Deep vanishing point detection: Geometric priors make dataset variations vanish, CVPR 2022](https://arxiv.org/abs/2203.08586) describes learned detection with line/geometric priors. It is a possible research direction, not an adopted dependency or a validated model for Toon Pro artwork.
