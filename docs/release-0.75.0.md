# Toon Pro 0.75.0 release verification

Updated 4 November 2026. The public preview targets Windows x64, Apple Silicon Mac and Intel Mac. The [downloads page](https://complex1.github.io/ToonPro/download/) lists packages available to artists.

## Native verification

| Platform | Native application tests | Package verification |
| --- | --- | --- |
| Apple Silicon Mac | 247/247 passed plus three repeated recolor workspace checks. | The hosted native and Retina checks passed, including 3D and media export. Both the local extracted ZIP and the app copied from a mounted DMG passed all 20 native checks. |
| Intel Mac | 247/247 passed plus three repeated recolor workspace checks. | The first hosted packaging attempt stopped while downloading a license notice. Reviewed license texts now ship with the packaging tools. Native package verification is running again. |
| Windows x64 | 246/246 passed, including the audio export, audio scrubbing, brush catalog and external model path regressions. All seven focused diagnostics passed. | A packaging test assumed Windows enumerated DLLs in a fixed order. It now checks all DLL records by name. Native package verification is running again. |

The Apple Silicon preview is public with a DMG, ZIP, checksums and matching sources. [Apple Silicon release](https://github.com/complex1/ToonPro/releases/tag/v0.75.0-preview.41758527843)

The [full native run](https://github.com/complex1/ToonPro/actions/runs/41758527843) tested commit `da5dc454`. The follow-up [packaging run](https://github.com/complex1/ToonPro/actions/runs/41772075808) includes the packaging corrections and identifies GitHub drafts by their release ID before publishing.

## Local installation checks

The independently built Apple Silicon package at `dist/0.75.0/macos-arm64-da5-preview/` passed signature checks, ZIP extraction, installation from the mounted disk image, project save/reopen, drawing/Undo, movie review, H.264/AAC export and 3D rendering. Audio exported from the installed app kept the selected 0.500 s duration and the opening stereo samples. Each platform's public checksums belong to the files attached to its GitHub release.

The Mac builds target macOS 13 and later. 3D rendering uses Metal on Apple Silicon. Intel Mac and Windows support the 2D workflow. Mac previews are ad-hoc signed without notarization. Windows previews use an unsigned portable ZIP. See [packaging](packaging.md) for dependency and build details.

## Deferred Linux work

Linux is excluded from the active release matrix and public download scope. Existing build recipes, artifacts and evidence are retained for a future release: 25 selected suites and six save/export follow-up suites passed with the software H.264/AAC runtime. The retained archive passed all 20 checks in offscreen 2× and native XCB modes in a clean Ubuntu 24.04 runtime container, without SDK/compiler/source or network access. This was an x86_64 container under emulation on Apple Silicon, not a physical Linux workstation.

The historical archive is `dist/0.75.0/linux-x86_64/ToonPro-0.75.0-linux-x86_64.tar.gz`, SHA-256 `041d7e2446f88c9137a1cc431272be934a34bcdaeed0a60f20c4c1051d6cc9a8`. Its dependency-source bundles `linux-x86_64.tar.gz` and `linux-codecs.tar.gz` remain under `dist/0.75.0/dependency-sources/`. These records do not make Linux a current release target.

## Remaining acceptance

Current priorities include physical Huion testing, production projects, clean installations on the minimum OS versions and signing credentials for production packages. See the [hands-on acceptance checklist](release-acceptance.md).

## Earlier diagnostic builds

[Native probe 41701154425](https://github.com/complex1/ToonPro/actions/runs/41701154425) tested a bounded two-block feed for very short AAC inputs and an MP4 presentation-duration correction. Audio tracks now exist, but five audible-onset cases still fail for 400–1024-sample selections; 32 media cases pass. The remaining problem is Windows encoder priming/leading silence, now measurable in retained MP4 and decoded PCM files. Four Mac media suites pass, but those results do not establish native Windows success. Missing audio is rejected before publishing the destination; no application package is produced by this probe.

The subsequent [native probe 41754255566](https://github.com/complex1/ToonPro/actions/runs/41754255566), commit `5460d982ab2a710cea74209fd256654fbe13070b`, **passes all 38 media cases**, including all ten audible stereo onset/correlation cases. Explicit silent encoder preroll is removed from the final presentation without shifting selected artwork/audio. The scene and catalog suites also pass. Retained MP4, source/decoded PCM and logs are in artifact `13287990843`, SHA-256 `ae9b891ba1180bd2cd38e29b75890aef84654b661c1a284700d1e41d739f7c65`. Full Windows CTest and packaged-runtime validation remain required before downloads.

The earlier main build [41698764292](https://github.com/complex1/ToonPro/actions/runs/41698764292) produced no downloadable candidates: Apple Silicon passed 244/247 suites, Intel passed 243/247 and Windows passed 242/246. In addition to the now-corrected Windows media/resource/catalog and scrub issues, it exposed a recolor-workspace crash, thumbnail initialization timing and Symbol picker focus checks on Mac. These are being corrected before the combined native rerun; the earlier local Mac pass is retained historical evidence, not a replacement for this failed CI run.

External model resources now compare Qt canonical paths with the same slash convention on every platform, retaining sibling-folder escape rejection. The scene suite passes on Mac and all 28 native Windows cases pass. Scrub scheduling now samples elapsed time once and has deterministic checks on either side of the 30 ms boundary; three affected Mac suites, 11 Retina checks and 80 repeated deadline cases pass, with native Windows confirmation still pending. Original brush settings, shape, masks and alpha remain exact; a compressed reference verified against all 112 immutable original hashes permits only tightly bounded RGB rounding variation in colored stamps. The revised catalog suite passes on Mac and Windows. Windows differs only in two colored-stamp channel values by one level; the follow-up bound is at most one level and eight total channel units. Comparison images are retained in the probe artifact.

The isolated Windows probe compiles the production exporter and runs real encode/decode plus resource/recipe checks, retaining MP4, PCM and diagnostic logs. A final guard uses actual step outcomes, and this workflow cannot package or publish an application. Full Windows validation and packaged-runtime checks are still required after the probe passes.
