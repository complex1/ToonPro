# RIFE in-betweens

This is a local drawing-cel workflow. RIFE generates raster suggestions between two existing pose drawings. It does not create editable vector strokes, choose acting, or preserve the poses' internal layer separation.

## Use the panel

1. Select two distinct drawing cels on the same animation track. The order of selection does not matter.
2. Click the sparkle button in the timeline, or open **Panels → In-betweens**. The Start and End selectors can also choose exposures directly.
3. Choose **White background** for regular artwork or **Transparent line art** for single-color outlines. A temporary white background is added automatically for RIFE; source drawings are not changed. Choose **Every frame** or **On twos**. The existing endpoint frames determine the number of generated drawings; the ending pose does not move.
4. Choose Linear, Ease in, Ease out or Ease in-out, or drag the easing graph's handles. Keyboard editing follows the shared easing graph behavior.
5. Click **Generate**. This prepares a full-resolution preview without editing the document. Cancel stops the job and leaves the timeline untouched.
6. Select a thumbnail, scrub with the Drawing slider and exact-entry field, or play the preview. Scroll to zoom and drag to pan. Fit restores the full drawing view.
7. Click **Insert drawings**. The batch creates ordinary raster cels in one Undo operation. Undo restores the original exposures, and Redo restores the generated drawings.

The original pose artwork and ending exposure remain unchanged. The starting exposure's hold becomes one or two frames according to the chosen frequency. New drawings occupy the interval until the ending pose. A final drawing can have a shorter hold when the interval is not divisible by two. Other drawings inside the interval block generation rather than being replaced.

## Background and line-art output

Source drawings may be transparent. Their own layer hierarchies are rendered with alpha, then composited over temporary white model inputs. No base layer is inserted into either original pose, and paper or another track is never used as an input background. **White background** keeps the generated white matte and retains the model's full RGB output.

**Transparent line art** is the first-use default. Your last selected output mode is saved in application preferences and restored across panels, document windows and app restarts. This preference does not change project data or create an Undo step.

**Transparent line art** is intended for single-color outlines. It detects the strongest dark source ink, then estimates soft alpha by projecting each generated pixel's distance from white onto that ink's distance from white. It writes premultiplied ink pixels, preserving antialiased edge coverage without a hard white cutoff. The preview uses a checkerboard, and inserted cels/save files retain alpha.

This is single-ink reconstruction, not general foreground segmentation or native RGBA inference. White marks and fills become transparent; colored marks are reconstructed in the detected ink color. Use White background for multicolor illustrations, white highlights or colored fills that must remain intact. The original endpoint artwork is preserved; transparent-mode preview endpoints retain their original alpha.

Sources are rendered in output coordinates from the selected drawing group. Paper, guides, Notes, other tracks and the shot camera are excluded. Track/ancestor masks, effects, blending or reduced opacity are unsupported, as are animated layer properties and 3D/Symbol source content. Supported source sublayers are composited into one raster result. Generated drawings cover the output canvas; source overscan outside it is not generated.

The easing curve maps timeline progress to RIFE's interpolation time. Every output is generated from the two original poses, rather than recursively from generated frames. Easing changes spacing through the transition, not its motion path. Use additional breakdown drawings for a different trajectory or pose transition.

## Runtime setup

Prepare the optional pinned runtime before configuring the build:

```sh
python3 scripts/setup_rife.py
cmake -S . -B build -DCMAKE_PREFIX_PATH="/path/to/Qt/6.10.3/platform"
cmake --build build --target ToonPro --parallel 3
```

The script downloads the official `rife-ncnn-vulkan` 20221029 release and keeps only its executable, compact RIFE v4.6 model, upstream README and license. The model is approximately 10.6 MB; the upstream download is larger because it includes other models. The macOS archive checksum is pinned. An extracted release can be reused with `--from-existing /path/to/extracted/release`.

`TOON_RIFE_ROOT` is the CMake runtime directory, defaulting to `.deps/rife`. CMake copies it into the app's Resources/rife directory on macOS or beside the executable on other platforms. No download occurs at build time or when opening the panel. Without the runtime, Generate is disabled with a setup message. `TOON_RIFE_ROOT` may also be set as an environment variable to locate an extracted runtime during development or tests.

The native app invokes the helper through QProcess with separate arguments. Each generated drawing currently starts a helper process; model persistence across a batch is a future optimization. Jobs run on a worker with temporary inputs/outputs, progress and cancellation, and a two-minute timeout per drawing. This workflow does not require Python at runtime or upload artwork.

## Bounds and stale previews

Generation is limited to 64 in-betweens per batch, 16-megapixel source canvases, available project tiles, shot drawing/layer/exposure limits, and a conservative 60 MiB generated-tile budget for one Undo command. These bounds can limit a batch well below 64 drawings at larger resolutions. The existing document history budget still applies.

Changing the pose selection, output background, frequency or curve discards the preview. Document content changes, Undo/Redo and changing shots invalidate it and cancel a pending job. Insertion rechecks document revision, sources, timing, dimensions, mode-appropriate opacity and project validation before making one atomic change. Changing the playhead alone does not invalidate a preview.

The initial local runtime target is macOS Apple Silicon. Other release archives are supported by the setup script, but this does not establish Windows/Linux runtime or packaged-release verification. General multicolor RGBA generation, persistent inference, automatic low-resolution curve previews, multiple pose segments and regeneration recipes stored in project files are outside this implementation.

Upstream: [RIFE ncnn Vulkan](https://github.com/nihui/rife-ncnn-vulkan), [RIFE research implementation](https://github.com/hzwer/ECCV2022-RIFE).
