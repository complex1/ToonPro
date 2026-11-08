# Character, anatomy and pet library

The artist approved this model/posing workspace for the normal application on **7 November 2026**. Open **View → 3D reference…** or use the editor toolbar cube action to access the docked controls and place models on native drawing-canvas reference layers. The compatibility-named `TOON_ENABLE_EXPERIMENTAL_3D` option now defaults to ON, with an explicit OFF opt-out. Legacy saved scenes keep their interpretation and the old rig/weight authoring UI stays retired. This approval enables the reviewed controls; full Clip Studio Paint parity remains open.

## Current controls

- **Canvas layers:** **Place model** adds to the selected native 3D scene or creates a new reference layer. Move, rotate, scale and pose it on the drawing canvas, then choose **Draw above** to create a raster layer for sketching. Normal layer visibility, opacity, ordering and locks apply; the drawing’s history and project saving own the editable scene.
- **Models:** Body, Head and Hand tabs expose four body proportion presets, nine head presets and seven hand presets. Anime adds Seed-san and Alicia; Pets adds a dog, cat and eagle with their own animal rigs. Search and actual rendered thumbnails help selection. Place a new study or apply a compatible shape to the selected study while preserving its pose and placement.
- **Proportions:** regional tabs expose 63 figure, 27 head and 23 hand scalar controls, plus the standalone-hand handedness mode. These include stature/head count, build/definition, chest fullness, waist/hips/limbs, facial regions with supported side-specific edits, and palm/finger/wrist/nail detail. A two-axis Build/Definition pad edits both body properties directly. Scalars retain sliders and exact entry.
- **Mixers:** the nine head and seven hand recipes have independent weights and optional mixture limits. Pure-preset thumbnail actions select a single recipe. These are Toon Pro's recipes over the CC0 anatomical base, not copies of CELSYS's model families.
- **Poses:** 20 body and 11 hand presets have category tabs, search and real model thumbnails. Body poses cover standing, sitting, crouching/kneeling/lying, movement, balance, reach and action. Hand presets cover open/relaxed, fist, pointing, pinch, grip and gestures. Apply/mirror and Both/Left/Right hand choices use one Undo operation.
- **Hands:** All/per-finger Curl and Spread manipulate the actual rig. One-channel edits retain the other channel independently on each hand. Arbitrary FK may be shown as an approximate/mixed projection onto these coupled controls. Standalone handedness has explicit Left/Right tabs; switching mirrors the pose around the object's placement plane.
- **Materials:** save the existing typed scene, character, body/hand pose and compatible shape materials. Opening, saving, Undo/Redo and gesture cancellation retain the canonical source data.

These are the implemented controls. Complete CSP-equivalent part semantics, corrected anatomy at every extreme, persistent contact constraints, editable clothing/hair, capture/inference and broader rendering behavior remain acceptance work in the [parity plan](3d-reference-replacement-plan.md). The library contains 31 named static recipes, not an exhaustive pose catalog or animation library.

## Model sources

The adjustable Toon Pro model families use an anatomical base derived from MakeHuman/MPFB's CC0 graphical assets, with independent Toon Pro shape/mixer definitions and original pose recipes. Their implementation is `src/reference3d_builtin.cpp`; source provenance and license are retained in [third-party/makehuman](../third-party/makehuman/README.md). No CSP mesh, material, pose file or Tripo-generated output is included. The separate Anime and Pets catalogs use the downloaded source models described below. The intended quality is a lightweight drawing reference with legible form and useful articulation. Visual anatomy and pose acceptance remains an artist review, separately from numerical correctness.

### VRoid and VRM

The current importer accepts supported rigged GLB/glTF and VRM 0/1 data. It retains supplied humanoid mappings, skinning, morphs and morph expressions. It does not reconstruct the VRoid Studio editor's complete proportion/hair/clothing system. MToon, gaze, physics and some avatar extension behavior remain outside the current base-color preview.

The Anime catalog bundles prepared copies of two official VRM sample characters: [Seed-san](https://github.com/vrm-c/vrm-specification/tree/94e82dd346fa6cf0337c4421728640e5252dd38e/samples/Seed-san) and [Alicia Solid](https://github.com/vrm-c/UniVRM/tree/efd20f62f1bf0ac3fcc34de1e497324c5197d146/Tests/Models/Alicia_vrm-0.51). They contain actual hair, clothing, texture maps, mapped body/finger rigs and supplied facial morph expressions. Seed-san retains 45,058 source triangles, draws 42,042 after detaching its separate mechanical-arm accessory, and has 17 supported expressions; Alicia has 31,798 triangles and 12. They can use the existing humanoid pose library and joint controls. They do not acquire the anatomical starter's shape-regeneration sliders or the VRoid Studio authoring system.

The Pets catalog uses the dog, cat and eagle from [Quaternius's animated animal pack](https://opengameart.org/content/animated-animales-low-poly). Their small animal rigs are retained, with named joints and 13 static presets sampled from their supplied clips (four dog, four cat and five bird). The dog/cat/eagle have 602/807/1,532 triangles and 20/20/24 skin joints. Dog and cat idle heights are 0.65/0.38 m. The eagle is normalized to a 1.2 m glide wingspan and starts in a raised-wing flight pose. Its original Idle snapshots are named Wings lowered, since the source does not contain folded/perched wings. Static poses are grounded individually without changing instance placement. These are stylized low-poly references, not anatomical animal studies or timeline animation support. Pet presets apply only to the matching rig and preserve object placement.

Prepared assets are bundled, decoded off the GUI thread when browsed, and shared immutably. The application does not download models at runtime. Technical preparation manifests record pinned source URLs, hashes, counts and explicit offline conversion steps. The Seed-san preparation removes identity UV transforms, bounds texture resolution and detaches the separate mechanical-arm prop; it preserves its geometry, skin, morphs and hierarchy. Alicia receives a parent-root half turn so its front matches the review camera. Seed-san's optional mechanical-arm mesh is detached in the drawing-reference variant while its original source data is retained. Original sources remain available in the local preparation folder. Unsupported shader, spring, gaze and constraint behavior is recorded in import diagnostics. The preview decodes imported glTF sRGB textures, evaluates linear factors, vertex colors, lighting and alpha blending, then encodes screen colors; authored QColor studies retain their existing behavior. This corrects the dark animal appearance without changing source material values.

### Reproduce the anime assets

Build the opt-in native packaging tool, then run the pinned downloader/preparer. The script checks original hashes, performs only its recorded changes and calls the native importer/validator to create canonical resources. macOS `sips` is used for the explicit texture resize. This is a developer preparation step; the installed review app has no Python dependency.

```sh
cmake --build build-3d-review --target toon_reference3d_prepare_asset --parallel 4
python3 scripts/prepare_reference3d_anime.py \
  --canonical-tool ./build-3d-review/toon_reference3d_prepare_asset
```

The portable [anime source manifest](../resources/reference3d/anime-sources.json) records the final canonical checksums. Pet preparation uses Blender offline to export the original rigged animals and sample their authored clips; the runtime app uses the resulting bundled canonical assets and pose data.

### Reproduce the prepared catalog

Offline preparation uses Python 3, the built native packers, macOS `sips` for the pinned anime texture resampling, and Blender 5.2 for animal source conversion. It is separate from the application runtime. Write a reproduction into a build directory before replacing reviewed resources:

```sh
cmake --build build-3d-review --target \
  toon_reference3d_prepare_asset toon_reference3d_prepare_pets --parallel 4
python3 scripts/prepare_reference3d_anime.py \
  --output-dir build/reproduced-anime \
  --resource-dir build/reproduced-anime/canonical \
  --canonical-tool "$PWD/build-3d-review/toon_reference3d_prepare_asset"
python3 scripts/prepare_reference3d_pets.py \
  --output-dir build/reproduced-pets \
  --blender /Applications/Blender.app/Contents/MacOS/Blender \
  --canonical-tool "$PWD/build-3d-review/toon_reference3d_prepare_pets"
```

The scripts verify pinned downloads and retain source/preparation manifests. Pet export explicitly reduces source skin weights to four influences at four dog, one cat and fifteen eagle vertices, normalizing the remainder; the maximum discarded weights are 1.22%, 0.40% and 11.31%. The runtime importer retains its explicit rejection of unsupported influence counts. The final fresh pet pipeline reproduces all three canonical models and the pose bank byte-for-byte.

### Optional CC0 anatomy import fixture

[MakeHuman/MPFB's core graphical assets](https://www.makehumancommunity.org/about/license.html) are CC0, including the mesh, targets and rigs. Their program code has separate GPL/AGPL licensing. Our optional fixture converter reads graphical data and does not incorporate their program logic into Toon Pro.

`scripts/prepare_reference3d_cc0_fixture.py` converts a pinned MPFB graphical-data snapshot into an optional local GLB. It removes helper geometry, prepares a 1.75 m body, attaches 52 mapped bones, includes five actual facial targets, and regenerates normals. During this explicit offline conversion, nine vertices' original weights are reduced from five influences to four and normalized; the maximum discarded weight is 0.035. The normal importer itself continues to reject unsupported influence counts without truncation.

The resulting fixture has **13,380 vertices and 26,756 triangles**, occupies approximately 1.8 MB, and is not bundled as a Toon Pro starter. Its `.CC0.txt` and `.provenance.json` companions retain the asset license, source revision, input/output hashes, conversion details and measured counts. This is an import test, not a complete VRM conformance certification.

To reproduce it:

```sh
git clone --filter=blob:none --no-checkout https://github.com/makehumancommunity/mpfb2.git /path/to/mpfb-source
git -C /path/to/mpfb-source checkout 6ff1eb358f6c1959ced77e79e6dd2406d9a71f49
python3 scripts/prepare_reference3d_cc0_fixture.py \
  --source-root /path/to/mpfb-source --output /path/to/cc0-anatomy.glb
```

No automatic download, account subscription or external model-generation service is required by the application.

The bundled resource itself is reproducible with `scripts/prepare_reference3d_anatomy.py --source-root /path/to/mpfb-source --output resources/reference3d/anatomy-v1.bin`. Its adjacent provenance manifest records all source hashes and the output checksum. Main-surface counts are 26,756 body, 8,654 head and 3,416 hand triangles; eye/nail guides bring the actual model totals to 28,484 / 9,422 / 3,896.

## Real model review artifacts

With `TOON_BUILD_BENCHMARKS=ON`, the opt-in `toon_reference3d_asset_review` utility renders the actual final-quality review viewport for front/three-quarter/side views, supplied shape presets and original pose presets. It saves individual PNGs, contact sheets, canonical starter scenes, a three-model review scene and a JSON manifest with geometry/deformation counts and bounds. These images are model renders, not illustrative mockups.

```sh
cmake --build build-3d-review --target toon_reference3d_asset_review --parallel 4
TOON_REFERENCE3D_ASSET_REVIEW_DIR=/path/to/review-output \
TOON_REFERENCE3D_IMPORT_REVIEW_FILE=/path/to/cc0-anatomy.glb \
  ./build-3d-review/toon_reference3d_asset_review -platform offscreen
```

Set `TOON_REFERENCE3D_ASSET_REVIEW_CATALOG_ONLY=1` to render the Anime/Pets models, every anime body pose, selected supplied expressions and all animal presets. It also saves contact sheets and `anime-pets-review.toon3d` with both characters and all three pets. The import environment variable is optional. The utility uses temporary material storage and its own application identity. Opening its saved review scene in a fresh standalone review app does not alter a production drawing.

The anatomical-library baseline passed all **8 affected CTest suites in each OFF/ON build**, plus **23 workspace and 8 isolation Qt results each** on native macOS and offscreen 2×. The utility verifies all **223 non-default parameter endpoints** move actual vertices and renders **63 views**. The 12 transform cases publish **59 images during every 60-update drag**, with full-quality settling 8–12 ms after release. Shape regeneration is synchronous: body/head/hand medians were approximately 63/19/9 ms in the sweep, so production shaping latency remains acceptance work. See [the development evidence](DEVELOPMENT.md#anatomical-reference-models-and-pose-library--7-november-2026).

The Anime/Pets follow-up passes **10 affected CTest suites in each flag configuration** and **8 focused native / 8 offscreen 2× Qt results** with no failures/skips. Its final utility captures **75 real views**, verifies both anime characters against every humanoid/hand preset, and produces the five-model `anime-pets-review.toon3d`. All 18 translate/rotate/scale benchmark cases (five individual models and the combined scene) publish during interaction, settle correctly, resize successfully and add one Undo command without rendering errors. The combined scene publishes 36–43 images during each 60-update drag and settles at final quality in 20–29 ms after release on this Mac. See [the latest development evidence](DEVELOPMENT.md#anime-characters-and-pet-references--7-november-2026) for method and limits.

On macOS, start a fresh standalone instance with the generated review scene:

```sh
open -n "build-3d-review/Toon Pro 3D Review.app" --args \
  /path/to/review-output/models-review.toon3d
```

The optional scene argument belongs to the standalone review app. The normal application opens its docked library/controls through **View → 3D reference…**, independently of that argument, and stores editable reference scenes in its drawing layers.
