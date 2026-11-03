# Toon Pro 0.75.0 release verification

Updated 4 November 2026. The current release scope is **Windows and macOS**; Linux distribution is deferred. These are test candidates, not production approval. The artist authorized public source and test downloads in [complex1/ToonPro](https://github.com/complex1/ToonPro). No store submission or GitHub Release has been published.

## Candidate status

| Platform | Result | Package |
| --- | --- | --- |
| Apple Silicon Mac | Verified Release build, ad-hoc signed; native Cocoa and offscreen 2× checks pass, including 3D and media export. Mounted DMG installation-copy checks pass. | `dist/0.75.0/macos-arm64/` ZIP and DMG |
| Windows x86_64 | App and all test targets compile. Latest full run: **242/246 suites pass**. Short AAC export, one brush snapshot, audio-scrub timing and external 3D resource paths still fail. | Withheld until full tests and packaged runtime pass |
| Intel Mac | Cross-compiled; intermittent SIGILL during AAC export under Rosetta, including a copied DMG installation. Many checks pass, but this does not resolve the crash. | Withheld; original logs preserved |

3D rendering currently ships only on Apple Silicon Mac. Other targets report it unavailable. Mac deployment target is 13.0. These targets do not imply testing on every supported OS release.

## Recorded verification

- Mac: all **247/247 CTest suites** pass after the final save/export changes (307.81 seconds; `build/release-075-current-all-tests.log`). The subsequent test-only font diagnostic passes its focused PropertyRow check. A later short-AAC fix passes all three media suites on Mac and Linux, including audible stereo decoding and unchanged timing bounds.
- Packaging: **36/36 Python tests** pass (`build/release-075-windows-font-env-tests.log`).
- Each packaged smoke run performs 20 checks: project save/reopen, drawings/Undo, icons and brush resources, Movie review, WAV export, actual H.264/AAC export and decode, and supported 3D rendering or its explicit unavailable state.
- Mac final DMG copy: native Cocoa, all 20 checks, strict signature verification and no SDK/Homebrew library loads. ZIP integrity and matching executable checks pass. Evidence: `dist/0.75.0/macos-arm64/verification/dmg-installed-native/`.
- Windows first full run: 195/246 suites passed. [Run 41692948278](https://github.com/complex1/ToonPro/actions/runs/41692948278) improves this to **242/246** after font/save fixes. Six of seven diagnostic suites pass. Padding very short audio to one AAC block does **not** resolve the Windows encoder's missing audio track; five short-audio cases still fail. The other remaining failures are the `painted-pebble` macOS snapshot hash, an audio-scrub test observing four grains instead of three, and valid external 3D resources rejected by the source-folder check. No Windows package was produced. Full assertions are preserved in `build/release-075-windows-run5.log` and the run's diagnostic artifact.
- An earlier intermediate report incorrectly interpreted GitHub's `continue-on-error` step conclusion as a passing diagnostic result. The full job log and explicit packaging guard are authoritative; the failed candidate was withheld.

## Current artifact hashes

| File | Size (bytes) | SHA-256 |
| --- | --- | --- |
| `ToonPro-0.75.0-macos-arm64.zip` | 46,044,380 | `c7f9f6a53246f2b167de25cd7d2f2e248e52031bc428ff260d3fbc75131b7925` |
| `ToonPro-0.75.0-macos-arm64.dmg` | 51,512,372 | `bb19eca9db513f422b009d9f51a2f72e57667a950a096b702f098ea875eed84e` |

Each platform folder includes `package.json`, `SHA256SUMS` and verification reports. The matching Mac dependency-source archive is `dist/0.75.0/dependency-sources/macos-arm64.tar.gz`, with an adjacent checksum. Windows dependency-source coverage remains pending its verified package. See [packaging](packaging.md) for reproduction and dependency details.

## Deferred Linux work

Linux is excluded from the active release matrix and public download scope. Existing build recipes, artifacts and evidence are retained for a future release: 25 selected suites and six save/export follow-up suites passed with the software H.264/AAC runtime. The retained archive passed all 20 checks in offscreen 2× and native XCB modes in a clean Ubuntu 24.04 runtime container, without SDK/compiler/source or network access. This was an x86_64 container under emulation on Apple Silicon, not a physical Linux workstation.

The historical archive is `dist/0.75.0/linux-x86_64/ToonPro-0.75.0-linux-x86_64.tar.gz`, SHA-256 `041d7e2446f88c9137a1cc431272be934a34bcdaeed0a60f20c4c1051d6cc9a8`. Its dependency-source bundles `linux-x86_64.tar.gz` and `linux-codecs.tar.gz` remain under `dist/0.75.0/dependency-sources/`. These records do not make Linux a current release target.

## Remaining acceptance

Windows full-suite/package checks and Intel blockers must be resolved before offering those downloads. Physical Huion feel, production-project testing, baseline OS installations, signing/notarization, application distribution terms and final dependency/replacement review remain open. No public production-readiness claim is made. Use the [acceptance checklist](release-acceptance.md) for hands-on testing.
