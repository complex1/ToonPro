# Toon Pro 0.75.0 release verification

Updated 3 November 2026. These are test candidates, not production approval. The artist authorized public source and test downloads in [complex1/ToonPro](https://github.com/complex1/ToonPro/tree/codex/release-candidate-0.75.0). No store submission or GitHub Release has been published.

## Candidate status

| Platform | Result | Package |
| --- | --- | --- |
| Apple Silicon Mac | Verified Release build, ad-hoc signed; native Cocoa and offscreen 2× checks pass, including 3D and media export. Mounted DMG installation-copy checks pass. | `dist/0.75.0/macos-arm64/` ZIP and DMG |
| Linux x86_64 | Verified Release build with software H.264/AAC; native XCB and offscreen 2× checks pass. The final archive also passes all 20 checks in both modes in a clean runtime-only Ubuntu container, with no SDK/compiler/source and network disabled. | `dist/0.75.0/linux-x86_64/` portable tar.gz |
| Windows x86_64 | App and all test targets compile. Validation continues after Windows save-file handling and fontless offscreen test fixes. | Withheld until tests and packaged runtime pass |
| Intel Mac | Cross-compiled; intermittent SIGILL during AAC export under Rosetta, including a copied DMG installation. Many checks pass, but this does not resolve the crash. | Withheld; original logs preserved |

3D rendering currently ships only on Apple Silicon Mac. Other targets report it unavailable. Mac deployment target is 13.0; Linux baseline is Ubuntu 24.04/glibc 2.39. These targets do not imply testing on every supported OS release.

## Recorded verification

- Mac: all **247/247 CTest suites** pass after the final save/export changes (307.81 seconds; `build/release-075-current-all-tests.log`). The subsequent test-only font diagnostic also passes its focused PropertyRow check.
- Packaging: **31/31 Python tests** pass (`build/release-075-current-python-tests.log`).
- Linux: 25 selected regression suites pass; all six save/export follow-up suites pass with the replacement software codecs (19.16 seconds; `build/release-075-linux-io-final-tests.log`).
- Each packaged smoke run performs 20 checks: project save/reopen, drawings/Undo, icons and brush resources, Movie review, WAV export, actual H.264/AAC export and decode, and supported 3D rendering or its explicit unavailable state.
- Mac final DMG copy: native Cocoa, all 20 checks, strict signature verification and no SDK/Homebrew library loads. ZIP integrity and matching executable checks pass. Evidence: `dist/0.75.0/macos-arm64/verification/dmg-installed-native/`.
- Windows first full run: 195/246 suites passed. Subsequent diagnostics confirmed a fontless Qt offscreen environment and Windows file-handle issues; those fixes are under native CI validation. Original failure logs remain under `build/release-075-windows-run*.log`.

## Current artifact hashes

| File | SHA-256 |
| --- | --- |
| `ToonPro-0.75.0-macos-arm64.zip` | `22bdc70dd2503315a43ee6b290966d82841147eb7cbe4ba8c34bbf2c2ecad64e` |
| `ToonPro-0.75.0-macos-arm64.dmg` | `c4cc50383a78d00411414e8901c0ed2ab393d5c9bc8461867638d589f408533e` |
| `ToonPro-0.75.0-linux-x86_64.tar.gz` | `041d7e2446f88c9137a1cc431272be934a34bcdaeed0a60f20c4c1051d6cc9a8` |

Each platform folder includes `package.json`, `SHA256SUMS` and verification reports. Matching dependency-source archives are in `dist/0.75.0/dependency-sources/`: `macos-arm64.tar.gz`, `linux-x86_64.tar.gz` and `linux-codecs.tar.gz`, with adjacent checksums. See [packaging](packaging.md) for reproduction and dependency details.

## Remaining acceptance

Windows and Intel blockers must be resolved before offering those downloads. Physical Huion feel, production-project testing, baseline OS installations, signing/notarization, application distribution terms and final dependency/replacement review remain open. Linux tests run in an x86_64 container under emulation on Apple Silicon, not on a physical Linux workstation. No public production-readiness claim is made. Use the [acceptance checklist](release-acceptance.md) for hands-on testing.
