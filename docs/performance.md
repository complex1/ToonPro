# Performance workloads and acceptance

Performance checks use deterministic generated artwork and temporary project files. They do not open the artist's files, modify preferences, load external brush packs, or measure physical pen-to-screen latency. Run benchmark processes serially on an otherwise idle machine. Keep the build configuration, Qt version, resolution, device scale and workload unchanged when comparing results.

## Reproduce

Configure with `TOON_BUILD_BENCHMARKS=ON` and build `toon_production_benchmark` along with the existing benchmark targets. Use the project's normal Qt and Filament configuration. The production benchmark accepts `storage`, `audio`, `scene`, or `all` as its first argument, followed by Qt's `-platform offscreen` option. Run separate profiles when comparing process peak memory; the peak is cumulative within each process.

```sh
build/toon_production_benchmark storage -platform offscreen
build/toon_production_benchmark audio -platform offscreen
build/toon_production_benchmark scene -platform offscreen
build/toon_brush_live_benchmark -platform offscreen
build/toon_brush_live_benchmark -platform offscreen --large
QT_SCALE_FACTOR=2 build/toon_brush_live_benchmark -platform offscreen --large
build/toon_vector_edit_benchmark -platform offscreen
build/toon_selection_benchmark 3840 2160 canvas -platform offscreen
build/toon_color_selection_benchmark -platform offscreen
```

Production, live-brush, vector-edit and selection timing records are JSON Lines; the color-range benchmark prints plain text timings. Qt/GPU diagnostics may also appear in either output stream; when processing a JSON log, retain lines beginning with `{` and parse those as JSON. Repeat drawing/selection checks with `QT_SCALE_FACTOR=2` for high-DPI presentation. The scene profile requires an actual supported Filament backend: a build without Filament reports a skipped profile, not a passing GPU result. Driver/render failures return an error.

The live-brush `--large` profile compares model/workspace pairs for 128 and 256 px hard round brushes, a 256 px soft round brush, static and dynamically sized/rotated 256 px sampled textures, and Brick Surface, Cloud Edge Soft and Granular Spray forced to 256 px. Each pair must produce identical artwork. The default is 240 input events; `--events N` supports 8–2400 but changing the count changes the workload. Records include median/p95/maximum input and display times, stroke release, viewport dimensions, device scale, size-change and hover timings, outline-build counts and final cursor complexity. Size-change and hover checks are separate from stroke timings. The display measurement includes event processing and a synchronous whole-window render; it is not physical pen-to-screen latency.

## Workloads

| Profile | Content | Measurements and correctness |
| --- | --- | --- |
| Layered movie | Four 3840 × 2160 shots, 24 raster layers per shot, 768 unique 256 × 256 tiles: 192 MiB decoded artwork. Generated block-noise colors provide nontrivial PNG compression work. | Copy-on-write snapshots, shot switching, full 4K composition, a synchronous save reference, three asynchronous saves and three loads. Snapshot mutation must preserve original pixels. Saved/reloaded metadata and every tile must match. Asynchronous saves record GUI submission time and lateness of a 10 ms GUI heartbeat separately from total completion time. |
| Dense audio | 64 overlapping stereo clips sharing one two-second 48 kHz source, 256 source-position gain keys per clip, equal-power fade-in/out, static gain zero with animation enabled. | 96 consecutive 1024-frame buffers, including range-loop boundaries. First/last sample checks in every buffer compare the mixer to direct envelope evaluation. Reports median, p95 and maximum buffer work. A buffer covers 21.33 ms of sound. |
| Animated city | 24 native building instances, a ground plane and a mannequin; three immutable model resources, 1280 × 720 readback, camera animation through 25 views. | Cold initialization separately from 24 subsequent request/completion timings. Main-thread request submission is recorded separately from end-to-end GPU rendering/readback. Output must have visible geometry, correct dimensions and different first/last views. |
| Existing drawing benchmarks | Particle/texture brushes at small and large sizes; an earlier editable stroke among 48 raster-brush vector strokes; dense 4K selections/transforms; color-range selection. | Existing executables retain their deterministic fixtures and artwork checks. They complement the movie/audio/3D workload rather than duplicating it. See the source for each executable's individual fields and sample count. |

The production benchmark reports median, p95, maximum, sample count, Qt/OS/architecture and process peak resident memory where available. Unix memory uses `getrusage`: macOS reports bytes directly and other Unix targets convert KiB to bytes. A value of `-1` means that metric is unavailable on that platform. It does not include a separate GPU-memory measurement or prove absence of leaks. Three save/load samples are useful for comparison but too few for a reliable tail-latency estimate; their p95 is effectively the maximum.

## Acceptance targets

These are investigation targets for a designated reference machine, **not measured results or guarantees**. Record the actual machine and results before adopting a target as a release gate.

| Interaction | Initial target | Interpretation |
| --- | --- | --- |
| Pen event processing | p95 below 8 ms | Leaves time for presentation within a 60 Hz frame. Synthetic events cannot establish Huion driver/sample delivery or physical display latency. |
| Warm drawing/selection presentation | p95 below 16.7 ms at the chosen viewport/device scale | Inspect the worst samples as well; isolated work above the frame interval needs profiling. |
| Audio mixing | p95 below 10 ms and maximum below the 21.33 ms buffer duration | Provides scheduling headroom for this 1024-frame workload. Native device underrun tests remain necessary. |
| Snapshot save submission | Below one 16.7 ms frame | Compression and file writing should happen off the GUI thread. Total save time is reported independently. |
| GUI heartbeat during save | p95 lateness below 16.7 ms; investigate repeated gaps above 50 ms | The timer itself is not a real-time clock. Compare with an idle-machine run and inspect submission/publication separately. |
| Animated 3D camera preview | Warm completion p95 below 33.3 ms at 1280 × 720 | A 30 Hz preview target, not a video-export speed requirement. Report shader initialization separately. |

Current safety limits remain unchanged: 1024 raster tiles per project, 64 audio clips per shot, 256 gain keys per clip, 128 MiB per decoded audio asset and 256 MiB for project audio. Profiling must identify a specific bottleneck before changing a limit or adding a cache. A faster synthetic workload does not justify removing validation or weakening atomic save/cancel behavior.

## Hardware acceptance still required

Use the actual Huion tablet to verify fast circular strokes, pressure, tilt, eraser switching, interrupted strokes and immediate tool changes while a large project saves. Compare captured input samples with the resulting stroke so missing samples are not hidden by smoothing. Observe audio during play/seek/scrub/cancel and check several high-DPI display scales.

Run the same workloads on Windows with the supported Qt/Filament build, then test the distributable on a clean machine. A macOS benchmark or a Windows build configuration cannot establish Windows runtime, device-driver, rendering, codec or installation correctness. Packaging instructions and platform-specific checks belong in the packaging document.

## Recorded results

The production workload passed on **26 October 2026**, using a **MacBook Pro (MacBookPro18,3), Apple M1 Pro, 16 GiB unified memory**, macOS Tahoe **26.5.1**, arm64, Qt **6.10.3**, a **RelWithDebInfo** build and Filament's **Metal** backend. Workloads ran serially on an otherwise idle machine, using the offscreen Qt platform. The original local output is `build/production-benchmark.jsonl`; it includes Filament diagnostics between JSON records. All snapshot/pixel persistence, sample-exact audio and changed-image rendering checks passed.

| Operation | Samples | Median | p95 | Maximum |
| --- | ---: | ---: | ---: | ---: |
| Copy-on-write movie snapshot | 101 | 0.0013 ms | 0.0013 ms | 0.0118 ms |
| Switch active shot | 25 | 0.0101 ms | 0.0156 ms | 0.0345 ms |
| Full 3840 × 2160 frame composition | 7 | 16.48 ms | 23.87 ms | 23.87 ms |
| Synchronous save reference | 1 | 2232.40 ms | 2232.40 ms | 2232.40 ms |
| Asynchronous save completion | 3 | 2135.45 ms | 2171.04 ms | 2171.04 ms |
| Asynchronous save GUI submission | 3 | 0.0129 ms | 0.0591 ms | 0.0591 ms |
| Save-time 10 ms GUI heartbeat lateness | 639 | 0.0137 ms | 1.0337 ms | 12.3799 ms |
| Load saved movie | 3 | 440.55 ms | 452.07 ms | 452.07 ms |
| 64-clip, 256-key audio mix per 1024 frames | 96 | 0.5797 ms | 0.6205 ms | 0.6671 ms |
| Warm 1280 × 720 animated city render/readback | 24 | 2.3465 ms | 3.2346 ms | 3.8235 ms |
| Warm scene GUI request submission | 24 | 0.2765 ms | 0.3128 ms | 0.3136 ms |

The city scene's first frame, including cold renderer initialization, took **146.68 ms**. The saved movie was **10,842,518 bytes** from **192 MiB** of decoded generated tiles. Compression is highly content-dependent. The small synchronous/async sample counts do not establish a save-speed improvement; the relevant result is that compression and writing completed while GUI submission and timer delays remained below the stated targets.

Process peak RSS was **315.5 MiB** through save, **669.7 MiB** after repeated loads and **713.3 MiB** after the scene profile. These values are cumulative high-water marks from this run, not independent profile allocations or additional GPU memory. The load loop temporarily retains the previous loaded project while preparing its replacement; the result should not be presented as the application's idle working set.

Audio work fit comfortably inside the **21.33 ms** buffer period, and warm scene completion met the **33.3 ms** preview target on this machine. Full 4K composition reached **23.87 ms**, above a 60 Hz **16.7 ms** frame interval. That measures complete frame rendering rather than dirty-region canvas presentation, so it does not establish a viewport frame-rate failure. If full recomposition is needed on every interactive frame, profile layer composition/cache reuse before changing the renderer or resource limits. Physical tablet timing, device audio underruns and Windows behavior remain unmeasured.

### Drawing and selection on the same build

The drawing, vector-edit and selection workloads also completed successfully on 26 October, serially on the same machine/build at **1× scale with offscreen rendering**. The live-brush workspace was 1600 × 1000 with a 1920 × 1080 document. Each profile delivered 240 synthetic mouse-move events, measuring event handling separately from event processing plus a synchronous workspace render. These timings do not include tablet-driver delivery or physical display presentation.

| Live workspace profile | Input median | Input p95 | Render median | Render p95 | Stroke release |
| --- | ---: | ---: | ---: | ---: | ---: |
| 6 px textured particle brush | 0.211 ms | 0.301 ms | 4.512 ms | 5.124 ms | 11.077 ms |
| 128 px sampled, textured brush | 1.998 ms | 2.218 ms | 10.159 ms | 11.529 ms | 10.046 ms |
| 6 px particle brush, smoothing 10 | 0.322 ms | 0.421 ms | 4.463 ms | 4.967 ms | 10.839 ms |

The first two profiles produced identical artwork SHA-256 hashes in model-only and workspace modes. Their model-only input p95 measurements were 0.601 ms and 1.448 ms respectively. The benchmark does not emit live-brush maximum input/render times, and independent p95 values should not be added together and presented as an end-to-end percentile. Smoothing intentionally changes the path; its hash is not expected to match the unsmoothed profile. Raw records are in `build/drawing-benchmark.jsonl`.

| Edit/selection operation | Samples | Median | Maximum |
| --- | ---: | ---: | ---: |
| Warm edit of first round-brush vector stroke among 48 strokes | 6 warm previews | 8.276 ms | 8.393 ms |
| Warm edit of first sampled/texture/dual-brush stroke among 48 strokes | 6 warm previews | 7.693 ms | 7.978 ms |
| 4K selection canvas render at 1200 × 760 | 3 | 1.144 ms | 1.214 ms |
| 4K selection move preview update | 3 | 0.667 ms | 0.960 ms |
| 4K selection move preview and render | 3 | 5.832 ms | 9.273 ms |
| 4K selection rotate preview and render | 3 | 6.853 ms | 7.645 ms |

Vector edits used a 1920 × 1080 document. First-preview times were 10.268 ms for round strokes and 8.030 ms for the sampled/texture/dual profile; the six warm previews exclude those initial requests. These logs report median/minimum/maximum, not p95. The three selection iterations are too few to characterize tail latency. Raw records are `build/vector-benchmark.jsonl` and `build/selection-benchmark.jsonl`.

The separate color-range stress fixture uses **512 × 512** deterministic red/white noise, producing **177,311 selection path elements**. Single measurements were **45.777 ms** for color selection plus overlay, **28.332 ms** for four selection tile masks, **53.391 ms** for invert plus overlay and **2.790 ms** for deselect plus drawing. These are single-action timings, not frame-rate or percentile measurements; the input differs substantially from the dense rectangular 4K selection fixture. Selection construction/inversion merits profiling before expanding noisy-selection workloads. Source output is `build/color-selection-benchmark.jsonl`, whose contents are plain text despite its filename.

The measured brush processing/rendering and warm edit previews met the initial investigation targets on this fixture and display scale. This does not establish 2× display performance, sustained physical Huion input or performance for every imported texture. The full 4K composition tail remains **23.87 ms** as recorded above. No resource limits were changed in response to these measurements. Historical drawing measurements in `docs/DEVELOPMENT.md` remain separate results from their original builds.

### Tablet-flow follow-up — 0.41.1, 28 October 2026

Pen delivery now queues tile-revision discovery once per delivered batch/display pass, retains input without emitting artwork updates between actual stamps, and defers exact textured cursor contours until display. Structural regressions verify no tile scans or contour tracing during synchronous pen bursts, latest-pixel composition when a paint arrives before the queued callback, exact brush/eraser outline and pixel parity, and quiet workspace controls during contact. Native tablet tests additionally exercise real dock separators and missing-release recovery. These changes do not alter pressure curves or skip pen samples.

The same isolated 1920 × 1080, 1600 × 1000 workspace benchmark was rerun serially after testing on the same Mac, with offscreen rendering at 1× and 240 synthetic mouse events per profile. Each input sample is followed by event processing and a synchronous whole-window render, so this workload does not measure the additional benefit of batching several high-rate pen samples into a single displayed frame.

| Live workspace profile | Input median | Input p95 | Display median | Display p95 | Stroke release |
| --- | ---: | ---: | ---: | ---: | ---: |
| 6 px textured particle brush | 0.190 ms | 0.293 ms | 4.763 ms | 6.124 ms | 11.096 ms |
| 128 px sampled, textured brush | 1.331 ms | 1.517 ms | 11.267 ms | 13.090 ms | 9.934 ms |
| 6 px particle brush, smoothing 10 | 0.192 ms | 0.251 ms | 4.481 ms | 5.900 ms | 11.224 ms |

The sampled-tip input stage is shorter than the earlier recorded run, while display timings are higher; display now includes work deferred from input. Independent percentiles must not be added into an end-to-end latency claim. Both unsmoothed workspace profiles retain exactly the model-only artwork hashes. Raw evidence is `build/tablet-flow-brush-benchmark.jsonl`. Physical Huion delivery, sustained 2× presentation and driver latency remain unmeasured; no claim of Photoshop/Clip Studio performance parity is made.
## Default brush catalog workload — 0.42.0

The 112 new defaults add 1,922,048 bytes of decoded source artwork, generated once and implicitly shared. Tools and Add Tools now render only visible matching preview rows in short batches and stop while a brush stroke is active. The tool-catalog autosave timer also defers during active editing gestures. Tests verify visible/hidden rows, filtering, cache reuse, per-brush invalidation, pen priority and a held stroke spanning the save timer.

`toon_brush_live_benchmark --catalog` exercises Clean Contour, Granular Spray, Short Grass Tuft, Cloud Edge Soft, Brick Surface, Calm Ripples and Painted Leaf Cluster at their actual default sizes on a 1920 × 1080 document, with 240 delivered events each. Smoothing is disabled for a direct model/workspace comparison; normal presets retain their configured smoothing. Both modes receive corresponding view/recovered-artwork coordinates, so floating-point transform roundtrips cannot masquerade as a rendering change. All seven final hash pairs match.

The final offscreen 1× run on the existing M1 Pro setup reports workspace input p95 of 0.302–2.073 ms and separately measured display p95 of 8.060–18.344 ms across these profiles. Display includes event processing and synchronous full-window rendering. Some profiles exceed the 16.7 ms display target; this is not a universal 60 fps or physical pen-latency claim, and is not a Retina benchmark. Native Mac panel/tablet regressions pass separately. Data: `build/catalog-brush-benchmark-final.jsonl`; the earlier exploratory run is superseded by this run and its corrected coordinate comparison.

## Brush recipe corrections — 0.42.1

Continuous texture, soft and cloud presets use closer spacing with adjusted flow/edge falloff. The same seven-profile benchmark was rerun offscreen at 1× with no concurrent build or test process: all seven model/workspace artwork hashes match. Workspace input p95 is 0.153–1.121 ms; synchronous display p95 is 4.438–5.397 ms in this run. Brick Surface is the largest input cost at 1.121 ms. These are synthetic workload measurements, not a claimed speedup over the earlier run or physical Huion/Retina latency. Data: `build/recipe-brush-benchmark.jsonl`.

Full-size 256 px rendering checks additionally cover line-pattern transparency after three passes, texture-edge overlap at 25%, 60% and full pressure, and within-stroke foliage variation at 15%, 50% and full pressure. The renderer and pen delivery code are unchanged; recipe tuning does not alter captured vector strokes or old raster pixels. The current catalog's source artwork remains 1,922,048 decoded bytes. Legacy recipes are generated lazily only when needed for an existing catalog's one-time upgrade.

## Large-brush stroke work — 0.44.1, 29 October 2026

The artist reported lag during strokes at large sizes. A saved 0.44.0 benchmark executable and the updated build ran the same eight `--large` profiles serially, without concurrent builds/tests, on the M1 Pro/Qt 6.10.3/RelWithDebInfo setup above. Each profile uses 240 synthetic events, a fixed seed, a 1920 × 1080 document, a 1600 × 1000 logical workspace and a 1028 × 915 logical canvas. Runs at 1× and 2× render 1600 × 1000 and 3200 × 2000 physical workspace images respectively. The benchmark records pen-event handling separately from event processing plus synchronous whole-window rendering.

The largest display cost came from stroking detailed cursor paths: the final static and dynamic sampled-tip outlines contain 21,416 and 15,604 path elements. Detailed cursors now rasterize their existing silhouette at display resolution, derive a centered two-tone border, and reuse a local overlay when its exact geometry/subpixel phase is unchanged. A viewport halo preserves offscreen coverage at clipped edges. Allocation/filter-work caps retain a direct-edge fallback at extreme zoom. Simple round cursors retain their ordinary path rendering. This is a display-only change: cursor edge shading can differ from the former path stroker, while tip geometry, holes and brush artwork stay separate.

Live painting also skips the unused local preview of continuous texture before applying that texture at destination pixels. Normal unlocked premultiplied compositing uses equivalent integer arithmetic; other blend modes, alpha lock and erasing retain their existing paths. Flow/opacity/color values are prepared once per dab. No samples, brush detail, dynamics, spacing or document budgets were reduced.

All table values are **milliseconds at p95**, with independently measured stages:

| Workspace profile | Input 1× before | Input 1× after | Display 1× before | Display 1× after | Display 2× before | Display 2× after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Hard round, 128 px | 2.14 | 1.36 | 7.05 | 5.72 | 12.63 | 10.69 |
| Hard round, 256 px | 4.46 | 2.74 | 7.18 | 6.00 | 12.74 | 11.52 |
| Soft round, 256 px | 4.29 | 2.75 | 6.69 | 5.97 | 12.17 | 11.26 |
| Sampled continuous texture, 256 px | 4.33 | 2.01 | 122.72 | 18.11 | 309.24 | 32.75 |
| Sampled texture with size/angle variation, 256 px | 4.68 | 2.67 | 101.18 | 16.64 | 304.95 | 29.61 |
| Brick Surface, 256 px | 2.70 | 1.34 | 10.77 | 7.45 | 15.77 | 12.99 |
| Cloud Edge Soft, 256 px | 1.14 | 0.84 | 7.35 | 5.95 | 12.85 | 10.92 |
| Granular Spray, 256 px | 0.66 | 0.63 | 7.18 | 5.81 | 18.04 | 10.58 |

Updated input p95 ranges from 0.628–2.748 ms at 1× and 0.616–2.776 ms at 2×. Maximum display samples are 19.106 ms and 38.109 ms respectively. The two extreme sampled-texture cases still exceed the 16.7 ms display investigation target, especially at 2×; this improvement does not establish universal 60 fps, physical Huion latency or parity with other painting applications. Independent input/display percentiles must not be added into an end-to-end latency figure.

All **32 before/after artwork hashes** match, and each model/workspace pair matches within each run. Input configuration, viewport/device-scale metadata and final cursor path-element counts also match. Regression coverage compares the optimized compositor against its previous floating-point formula and painting-only dabs against preview dabs across texture transforms, colored tips, selections, blend modes, alpha lock and erasing. Cursor regressions cover geometry, clear holes, cache invalidation, clipping and unchanged artwork/history.

Final evidence: `build/large-brush-before-{1x,2x}.jsonl`, `build/large-brush-raster-{1x,2x}.jsonl` and `build/large-brush-comparison.json`. The `after` and `final` intermediate logs are exploratory contour-batching/direct-edge candidates, not the shipped raster-overlay result. Baseline executable: `build/toon_brush_live_baseline_044`; source fingerprints: `build/large-brush-baseline-sources.sha256`.


## Advanced tip sets and larger stamps — 0.45.0, 29 October 2026

Six model/workspace pairs were measured separately at explicit offscreen 1× and 2× scale, after the regression jobs had finished. Each follows the existing 1,920 × 1,080 artwork path with 120 delivered movement samples, a 1,600 × 1,000 logical workspace, constant pressure and fixed seed. The new six-tip brushes retain their actual authored spacing and dynamics; Painted Cloud is deliberately increased to 1,024 px with 1.05 spacing to exercise the upper size bound. This is an isolated-stamp workload, not a dense 1,024-pixel airbrush test.

| Brush and nominal size | Input p95, 1× | Input p95, 2× | Display p95, 1× | Display p95, 2× |
| --- | ---: | ---: | ---: | ---: |
| Meadow Tuft Set, 115 px | 0.41 ms | 0.40 ms | 5.80 ms | 10.54 ms |
| Broadleaf Crown Set, 360 px | 1.06 ms | 1.02 ms | 9.66 ms | 14.02 ms |
| Cumulus Mass Set, 480 px | 4.09 ms | 4.01 ms | 8.61 ms | 16.60 ms |
| Distant Bird Flock, 36 px | 0.17 ms | 0.16 ms | 5.64 ms | 10.04 ms |
| Painted Tree, 420 px | 0.14 ms | 0.19 ms | 6.74 ms | 14.40 ms |
| Painted Cloud (maximum-size test), 1024 px | 0.13 ms | 0.14 ms | 8.87 ms | 23.38 ms |

All **12 model/workspace pairs** have identical artwork hashes within each run. Event delivery, size, spacing, selection mode, viewport scale and seed are recorded in the raw output. The largest colored stamp is sparse: most pointer events do not deposit a new dab, so its low input p95 does not describe a stamp’s worst cost. At 2×, its maximum input time is **24.70 ms**, display p95 **23.38 ms**, and maximum display **31.70 ms**. Broadleaf Crown and Cumulus also have individual display samples above 16.7 ms. These results do not establish universal 60 fps or physical Huion latency.

Constructing the isolated MainWindow with the full library took approximately **1.07–1.08 seconds** in these two fresh processes, separately from process/Qt startup. The catalog’s decoded source images total **29,332,480 bytes**, plus the unchanged starter pack; images are shared through QImage and existing resource accounting. Cropped frames use a bounded 16 MiB cache. Library/project image and tile budgets are unchanged. A primary nominal size of 1,024 is a bounded capability, not a guarantee of responsiveness for every density, texture or symmetry combination.

Evidence: `build/advanced-brush-final-benchmark-{1x,2x}.jsonl`, corresponding `.log` files, and `build/advanced-brush-final-benchmark-summary.json`. The earlier `advanced-brush-benchmark-2x` run overlapped regression work and is not the accepted timing run. Physical tablet feel and real-artwork acceptance remain open.

## Connected patterns and preview cost — 0.49.0, 31 October 2026

Large colored thumbnail previews now reduce immutable source images before per-stroke HSV/tint work. The comparison uses eight fresh 2,048 × 1,024 RGBA resources and a 200 × 60 thumbnail, with the same settings in the before/after probe. Median cold generation fell from **33.97 ms to 3.21 ms**. Warm cached generation was **2.46 ms before and 2.58 ms after**; this does not show a warm-cache speedup. The temporary probe and results are `build/advanced-dynamics-preview/preview-benchmark.cpp` and `build/advanced-preview-comparison.log`.

An isolated 1,024 × 1,024 tone-surface application, measured across three fresh processes, took a median **45.62 ms cold / 1.92 ms cached**. The explicit perspective case took **39.44 / 1.83 ms**; its outside-quad pixels are transparent, so it is a different workload. Results: `build/surface-pattern-benchmark.log`. These component measurements exclude full stroke compositing, event delivery and workspace display. They do not establish end-to-end pen latency or a frame-rate guarantee.

Derived resources remain bounded: world surface tiles use 16 MiB, image mipmaps 8 MiB, reduced thumbnail sources 8 MiB, connected source images 8 MiB, connected minification 8 MiB and connected color results 16 MiB. Large cursor geometry traces a 384-pixel coverage mask and scales the outline back into artwork coordinates; full-resolution painting is unchanged. Fine connected linework is filtered before minification to avoid missing thin cables as a path bends. Connected modes do not start the stationary-airbrush timer.

The 200 catalog designs use **31,888,384 decoded source bytes**, including independent surface images, plus the 19 unchanged starters. Source images are shared by QImage; bounded derived caches add to that total. Very large connected strips, surfaces, dense colored stamps and multiple symmetry copies still require substantial pixel work. Physical Huion drawing on real production artwork remains an artist acceptance check.


## Large connected brushes — 0.49.2, 31 October 2026

The new opt-in `toon_brush_live_benchmark --connected --events 120` workload exercises Chain Border at 128/512/1,024 px, Fence Posts at 512 px and Smoke Trail at 512 px. Each follows the same curved 1,920 × 1,080 artwork path with 120 delivered movement events, a fixed seed and pressure, zero stabilization, and separate Document/model and full-workspace passes. Workspace events are synthetic mouse events, not physical tablet measurements. Input processing and event-processing plus synchronous whole-window drawing are timed separately. Both 1× and 2× runs use isolated settings and serial execution without competing build/test jobs.

A large diagonal two-pixel strip occupies only a small fraction of its axis-aligned tile rectangle. Previously, mask sampling and the brush compositor processed that mostly empty rectangle. The renderer now bounds work to 16-row bands using the actual triangle edges, including the shared diagonal and original coarse tile clipping at sharp turns. Geometry, source resolution, pen samples, dynamics, alpha accumulation and selection behavior are unchanged. Temporary images remain tile-bounded, and no additional persistent cache is introduced. The internal full-tile path remains available for pixel-equivalence regression checks.

Workspace input-processing **p95, milliseconds**:

| Brush | Before 1× | After 1× | Before 2× | After 2× |
| --- | ---: | ---: | ---: | ---: |
| Chain Border, 128 px | 2.20 | 0.86 | 2.23 | 0.93 |
| Chain Border, 512 px | 21.64 | 2.32 | 22.58 | 2.22 |
| Chain Border, 1,024 px | 50.31 | 4.23 | 54.46 | 4.20 |
| Fence Posts, 512 px | 22.55 | 2.88 | 23.16 | 2.83 |
| Smoke Trail, 512 px | 24.16 | 3.21 | 24.80 | 3.21 |

All **20 before/after artwork hashes match**, and model/workspace results match within each profile/run. Source brush size, motif spacing and path geometry were retained. Regression comparisons additionally exercise 128/512/1,024 px with translucent color/tint, transforms, selections, texture, tones, symmetry and both 8-bit output and 16-bit accumulation.

Full-window drawing remains separate from these gains: at 2×, 1,024 px Chain Border display p95 is **18.30 ms**, with a **59.20 ms maximum** observed in this run; maximum input is **17.01 ms**. Its baseline display p95 was 19.80 ms. These figures do not establish universal 60 fps or physical Huion latency. Dense paths, many symmetry copies and complex documents can cost more. Independent input/display percentiles must not be added into an end-to-end latency figure.

Evidence: `build/connected-perf-before.jsonl`, `build/connected-perf-before-2x.jsonl`, `build/connected-perf-accepted-{1x,2x}.jsonl` and `build/connected-perf-comparison.json`. Preserved baseline executable: `build/toon_connected_baseline_0491`; renderer source: `build/connected-before-0491.cpp`. Candidate/final intermediate logs without `accepted` in their names are exploratory bounds implementations, not the accepted results. `TOON_BRUSH_BENCHMARK_IMAGES` optionally exports each profile's rendered artwork for comparisons.


## Retina connected-brush redraw — 0.49.3, 31 October 2026

Detailed cursor masks previously rebuilt on fractional translations because their local coordinates depended on integer viewport clipping. They now cache the complete translation-independent shape within the existing pixel/work limits, move the overlay at its exact fractional origin, and interpolate only that display overlay. Normalized mask coordinates are rounded to 1/4096 physical pixel to remove floating-point cancellation noise; artwork coordinates and raster output are untouched. Oversized shapes retain the bounded clipped/fallback path. Offscreen halos prevent false borders along viewport edges.

Connected cursors now use distance-based repeat indices instead of input-event indices, matching multi-tip strip rendering. Fixed-shape outlines ignore pressure, tilt, wheel and speed changes only when those sensors cannot affect the outline; enabled shape dynamics still rebuild. Dual-brush inputs remain conservative. The canvas declares its fully painted background opaque so Qt can skip painting its parent's background underneath it.

The existing connected workload ran serially at **2× scale**, with 120 synthetic movement events per profile, a 1,600 × 1,000 logical window (3,200 × 2,000 rendered pixels), fixed pressure/seed and zero stabilization. Baseline is an instrumented 0.49.2 build. The final measurements below use three repetitions; p95 is the median of the three per-run p95 values, and maximum is the worst observed across all three. Display includes event processing and forced whole-window capture, not only the live canvas.

| Brush | Before display p95 (ms) | After display p95 (ms) | Before maximum (ms) | Worst after maximum (ms) | Cursor raster builds per 120 events, before → after |
| --- | ---: | ---: | ---: | ---: | --- |
| Chain Border, 128 px | 10.36 | 10.08 | 17.29 | 16.47 | 120 → 1 |
| Chain Border, 512 px | 13.56 | 11.15 | 37.86 | 16.07 | 120 → 0 |
| Chain Border, 1,024 px | 18.51 | 12.88 | 21.08 | 42.90 | 120 → 0 |
| Fence Posts, 512 px | 15.57 | 11.50 | 50.25 | 15.89 | 120 → 0 |
| Smoke Trail, 512 px | 16.52 | 12.41 | 48.35 | 15.50 | 120 → 2 |

Zero cursor builds means the outline prepared before the measured movement sequence remained reusable. Across all three repetitions, **30/30 before/after model and workspace artwork hashes match**. The largest chain's cursor p95 falls from 4.49 ms to at most 0.36 ms across these runs. This does not remove every timing outlier: the 42.90 ms whole-window sample remains in the reported results.

A subsequent diagnostic run separated event processing from forced capture. Live canvas painting p95 was 1.37/2.51/3.94/2.60/3.63 ms for the five profiles above. Maximum live canvas painting was 8.04 ms across them. A separate 50.35 ms combined outlier for the 512 px chain contained 48.47 ms in forced whole-window capture; that profile's live canvas maximum was 4.72 ms. This locates that particular outlier outside ordinary canvas painting, but does not prove the cause of the earlier 42.90 ms sample. Input processing still has occasional outliers (up to 16.79 ms in this diagnostic run), and Smoke Trail still rebuilds at actual tip changes. These are offscreen synthetic measurements, not native presentation timing, end-to-end pen latency or a universal 60 fps guarantee. Physical Huion acceptance remains open.

Validation: nine targeted CTest suites pass across the main regression and overlay runs: cursor, connected brush, tablet input, live-stroke workspace, canvas smoothing, drawing, advanced dynamics, selection canvas and selection visuals. All 22 cursor results also pass natively on macOS and at explicit 2× offscreen scale. Tests cover repeated fractional movement, clipped edges and holes, repeat-based cursors during varying tablet pressure/tilt, enabled pressure-size response, view changes and preserved artwork.

Evidence: `build/retina-before.jsonl`, `build/retina-final.jsonl`, `build/retina-repeat-{2,3}.jsonl`, `build/retina-comparison.json`, `build/retina-stages.jsonl`; regression logs `build/retina-regression.log`, `build/retina-overlay-tests.log`, `build/retina-native-tests.log`, `build/retina-final-cursor-2x.log`. Preserved baseline: `build/toon_retina_baseline_0492` and `build/canvas-retina-before.cpp`. Cursor captures: `build/retina-cursor-review/` and `build/retina-native-review/`. `TOON_PROFILE_CANVAS=1` enables optional cache/cursor/canvas stage diagnostics; normal app sessions leave profiling disabled. The benchmark additionally reports event-processing and forced-capture stages separately.


## Connected join quality check — 0.49.4, 31 October 2026

The quality fixes retain bounded raster bands and the 0.49.3 cursor cache. Shared segment edges now have one owner, sharp cross-section resets bridge their outer wedge, and repeated source artwork includes its boundary-crossing details. These intentionally correct some painted output; this release does not claim pixel identity with 0.49.3.

An isolated paired run of the same five connected profiles used 120 movement events, 2× offscreen scale and the 1,600 × 1,000 logical workspace, without concurrent build/test jobs. Both baseline and final executable were run serially. Workspace timings, milliseconds:

| Brush | Input p95 before → after | Full-window display p95 before → after | Final maximum input / display |
| --- | ---: | ---: | ---: |
| Chain Border, 128 px | 0.85 → 0.82 | 9.73 → 9.81 | 0.98 / 15.54 |
| Chain Border, 512 px | 2.24 → 2.33 | 10.80 → 11.02 | 5.74 / 15.16 |
| Chain Border, 1,024 px | 4.05 → 4.55 | 12.61 → 12.62 | 19.82 / 15.81 |
| Fence Posts, 512 px | 2.86 → 2.93 | 11.11 → 11.17 | 6.83 / 15.10 |
| Smoke Trail, 512 px | 3.12 → 3.28 | 14.00 → 12.16 | 8.98 / 15.17 |

All five final model/workspace artwork hashes match. The largest chain's full-window p95 is essentially unchanged at 12.62 ms; its input p95 increases by 0.50 ms in this paired run. The fixes add corner coverage and boundary checks, so they are not a further speed optimization. Maximum input still reaches 19.82 ms. Display timing includes event processing plus synchronous whole-window capture; it is not physical tablet latency or a guarantee of a frame rate. Physical Huion drawing remains unmeasured.

Evidence: `build/quality-perf-before.jsonl`, `build/quality-perf-after.jsonl`, `build/quality-performance-comparison.json`; preserved baseline executable `build/toon_quality_baseline_0493`. Quality checks and all 16 affected suites pass across `build/quality-regression.log` and `build/quality-storage-recheck.log`; all 38 connected results pass in `build/quality-native.log`. Native gallery: `build/quality-final/`.


## Workspace interaction regression check — 0.56.0, 1 November 2026

The final UX pass changes focus, numeric-entry and pen-contact handling. A paired check compares the preserved 0.55.0 baseline with 0.56.0 using the five connected-brush profiles, 120 synthetic movement events, fixed pressure/seed and zero stabilization. Runs execute sequentially without competing build/test jobs at explicit 2× offscreen scale, with a 1600 × 1000 logical window (3200 × 2000 pixels), a 1068 × 915 canvas and Qt 6.10.3 on macOS arm64.

| Brush | Workspace input p95 before → after (ms) | Full-window display p95 before → after (ms) |
| --- | ---: | ---: |
| Chain Border, 128 px | 0.87 → 0.84 | 10.74 → 10.63 |
| Chain Border, 512 px | 2.36 → 2.27 | 11.80 → 12.06 |
| Chain Border, 1024 px | 4.22 → 4.23 | 13.75 → 13.51 |
| Fence Posts, 512 px | 2.93 → 2.92 | 12.22 → 12.26 |
| Smoke Trail, 512 px | 3.23 → 3.33 | 13.15 → 13.17 |

All **10 before/after model and workspace artwork hashes match**, as do all five final model/workspace pairs. These single paired runs show comparable timings with no material extra cost in this workload; they do not establish a speedup. Display includes event processing and synchronous whole-window capture. Synthetic mouse events do not measure native tablet presentation, physical Huion latency or a guaranteed frame rate. Physical pen acceptance remains open.

Evidence: `build/ux-pass6-perf-before.jsonl`, `build/ux-pass6-perf-after.jsonl` and `build/ux-pass6-performance-comparison.json`. Preserved baseline: `build/toon_ux_baseline_0550`. The 20 targeted regression suites also pass, including live-stroke checks that document-wide updates and control-disable transitions stay deferred during contact.


## Effects interaction performance — 0.57.0, 1 November 2026

Paired measurements compare the preserved 0.56.0 renderer with 0.57.0 using the same benchmark, original varied/translucent pixels, nonwhite tiled mask and three increasing settings per profile. This is three different settings, not three repetitions of one setting; the table reports their median and maximum, not a percentile. Runs execute sequentially without competing builds/tests, on macOS arm64 / Qt 6.10.3. Workspace profiles use a 1600 × 1000 logical window at explicit 2× offscreen scale and 1920 × 1080 artwork. Workspace time includes event processing and a forced whole-window render; it is not native display or physical pen latency. Core profiles render the full-resolution image.

| Workload | Size | Median before → after (ms) | Final maximum (ms) |
| --- | --- | ---: | ---: |
| Workspace blur | 1920 × 1080 | 282.5 → 117.0 | 137.2 |
| Workspace shadow | 1920 × 1080 | 332.3 → 77.7 | 84.7 |
| Workspace exposure | 1920 × 1080 | 64.4 → 41.8 | 42.1 |
| Full render: blur | 1920 × 1080 | 256.8 → 85.4 | 104.9 |
| Full render: shadow | 1920 × 1080 | 310.8 → 56.3 | 61.5 |
| Full render: glow | 1920 × 1080 | 334.2 → 61.7 | 69.8 |
| Full render: color, including hue/saturation | 1920 × 1080 | 160.0 → 130.3 | 132.3 |
| Full render: blur + shadow + color | 1920 × 1080 | 1360.9 → 371.8 | 465.7 |
| Full render: blur | 3840 × 2160 | 839.5 → 267.1 | 322.2 |
| Full render: shadow | 3840 × 2160 | 1023.0 → 180.3 | 196.7 |
| Full render: glow | 3840 × 2160 | 1097.3 → 186.2 | 205.9 |
| Full render: color, including hue/saturation | 3840 × 2160 | 609.1 → 504.1 | 534.0 |
| Full render: blur + shadow + color | 3840 × 2160 | 4794.2 → 1386.7 | 1494.9 |

All **39 before/after image hashes match**. Each profile verifies one Undo entry, and full-render profiles also verify exact preview/commit/Undo/Redo and unchanged source pixels. The renderer reference suite passes 81 Qt results, including extreme/fractional color values, translucency, color quantization boundaries, mask tile edges, fractional light offsets, blur radii through 128, nested folders, negative origins, grouped tiles and smaller-region fallback.

The improvement comes from contiguous blur passes, alpha-only lighting, span-based mask lookup, reuse of equal lighting blurs, shared 1024/512-pixel regions when all constituent tiles are visible, and fewer color conversions. Hue/saturation-neutral exposure/contrast uses a guarded lookup table that retains the previous calculation around ambiguous Qt quantization boundaries. The 64 MiB image cache and existing processing limits remain unchanged; public tiles remain 256 pixels. Crop allocation failure reports an error rather than silently dropping rendered pixels.

**Remaining limit:** this is still synchronous full-resolution rendering. The heaviest measured 4K combination takes about 1.39 seconds at the median and 1.49 seconds at the maximum. These changes reduce the pause but do not provide continuous, background or 60 fps previews for every effect. A cancellable/coalesced background-preview pipeline remains future work, along with physical Huion acceptance.

Evidence: `build/ux-pass7-effects-before.jsonl`, `build/ux-pass7-effects-final.jsonl`, `build/ux-pass7-effects-comparison.json`; baseline executable `build/toon_effects_baseline_0560`; benchmark source `tests/benchmark_effects.cpp` and target `toon_effects_bench` under `TOON_BUILD_BENCHMARKS`. Reproduce with `QT_SCALE_FACTOR=2 ./build/toon_effects_bench all 3 -platform offscreen` while no build, capture or other benchmark is running. First integration and final guard regressions are recorded in `build/ux-pass7-final-regressions.log` and `build/ux-pass7-guard-regressions.log`.


## Background Effects interaction — 0.58.0, 1 November 2026

This paired workload compares the preserved 0.57.0 executable with 0.58.0 on the same reference Mac/Qt 6.10.3 setup. It opens generated 3840 × 2160 translucent artwork with a tiled grayscale mask, blur, shadow and color correction in the actual MainWindow/Effects panel. Twelve slider changes are requested 40 ms apart, followed by release. Qt paints naturally; no forced screenshot or export runs inside the measured interval. A 10 ms timer measures GUI heartbeat intervals. Both runs use a 1600 × 1000 logical workspace, a 1068 × 915 logical / 2136 × 1830 pixel Canvas, and explicit offscreen 2× scale. Navigator-visible uses a separate floating 300 × 320 panel to ensure it actually paints. Runs are sequential, without a competing build or test process.

| Profile | Longest GUI heartbeat interval, before → after | Delivery of all 12 edits, before → after | Drafts published during drag | Full-quality settling after release |
| --- | ---: | ---: | ---: | ---: |
| Navigator hidden | 1804.54 → **23.33 ms** | 16190.74 → **500.61 ms** | 4 | 1888.02 ms |
| Navigator visible | 1846.13 → **24.45 ms** | 16325.91 → **501.47 ms** | 3 | 1929.81 ms |

The 12 edits nominally occupy 480 ms. The old renderer delayed their delivery to more than 16 seconds; the new renderer delivers them in about half a second. Final-run p95 heartbeat lateness above the requested 10 ms interval is 1.18 ms hidden / 1.66 ms visible, with maxima 13.33 / 14.45 ms. Setting dispatch takes at most 5.00 ms hidden / 4.84 ms visible. Release takes 11.92 / 12.19 ms. These intervals include timer scheduling and GUI work, not only paint time.

Accepted Canvas publications—not just paint requests—establish preview progress. The first draft arrives at 155.03 ms hidden / 155.57 ms visible from the start of the gesture. Subsequent published drafts are about 101–120 ms apart. Both profiles use one-eighth source resolution during interaction; the latest settled image is full resolution. The visible Navigator paints 17 times during the measured interval. These are single scripted interactions per profile, not a claim of 60 fps or physical Huion latency. The final expensive render still takes roughly two seconds; the improvement is that it no longer blocks editing. The settling column includes an 80 ms quiet check.

Both profiles retain **byte-identical Canvas and full-resolution artwork hashes against 0.57.0**, and the final Canvas also matches a freshly painted synchronous reference. Source tiles and masks are unchanged. Release creates exactly one Undo entry; Undo and Redo restore exact artwork and settled viewport images. The opaque GUI-prepared sheet background and original Canvas region clip are necessary for this strict parity, including header text and antialiased guides.

An initial quarter-resolution trial kept GUI intervals below 25 ms but published only one draft while dragging; it was not accepted as the final preview cadence. The one-eighth draft improves progress without changing authoritative pixels. Large vector/text, animation, onion-skin, 3D and transform compositions use full-quality asynchronous rendering and can update less frequently. Multi-layer production work and physical tablet interaction remain separate acceptance work.

Reproduce with `QT_SCALE_FACTOR=2 build/toon_effects_preview_bench all -platform offscreen`. Build with `TOON_BUILD_BENCHMARKS=ON`. The benchmark uses temporary settings/projects, validates the settled image after timing, and exits on incorrect pixels/history or a finite timeout. `TOON_EFFECTS_DIAGNOSTICS` optionally names a directory for mismatch PNGs. Raw evidence: `build/effects-preview-baseline.jsonl`, `build/effects-preview-verified.jsonl`, their linked log files, and `build/effects-preview-comparison.json`. The preserved original executable is `build/toon_effects_preview_baseline_0570`. Failed/intermediate attempts remain separately named; only the verified pair supplies the results above.
