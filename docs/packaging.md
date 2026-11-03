# Desktop packaging and verification

The current release candidate is **Toon Pro 0.75.0**, using **Qt 6.10.3**. Candidates are test builds, not signed production releases. Packaging creates local artifacts and reports; it does not publish a release, upload to a store, or install over an existing Toon Pro session. `package.json` deliberately records `published: false`, `releaseChannel: "candidate"` and `publicReleaseReady: false`.

The active distribution scope is **Windows and macOS**. Linux is deferred; its recipes and earlier verification results below are retained for future work, with no Linux target in the active CI matrix or current download offering.

The scripts stage the native application, Qt libraries/plugins, matching FFmpeg libraries, dependency notices and build-capability metadata. They relocate that installation into a temporary directory and run the app without its development SDK environment. Distribution archives and checksums are created after verification passes.

## Current platform status

| Target | Package format | 3D reference rendering | Verification status |
| --- | --- | --- | --- |
| macOS arm64, deployment target 13.0 | ZIP and optional DMG | Filament 1.78.0 / Metal | 0.75.0 package passed all 20 checks in offscreen 2× and native Cocoa modes. A copy from the mounted DMG also passed the native checks. |
| macOS x86_64, deployment target 13.0 | ZIP and optional DMG | Unavailable in this build | Cross-compiled; intermittent AAC export crashes under Rosetta. Artifact withheld pending diagnosis and actual Intel acceptance. |
| Windows x86_64, MSVC 2022 | Portable ZIP | Unavailable in this build | Application and all test targets compile in native Windows CI. All seven focused suites pass after save/font/short-AAC fixes. Full 246-suite validation is running; packages remain withheld. |

These are build targets and recorded results, not a claim of testing every OS version. The verified Apple Silicon artifacts are in `dist/0.75.0/macos-arm64/`. Their exact sizes, hashes, capabilities and checks are in `package.json`, `SHA256SUMS` and `verification/package-verification.json`. The current Mac candidate has an **ad-hoc signature**, with no Developer ID signature or notarization. Clean-machine installation and physical Huion acceptance remain separate checks.

The pinned Filament SDK supports macOS arm64 only. `TOON_ENABLE_FILAMENT=ON` fails configuration if the required target/SDK is unavailable; `OFF` makes a deliberate 2D build, and `AUTO` uses the renderer when available. Other targets must not use `--require-3d` or claim that the renderer passed. No universal Mac application is currently verified.

## Build and package

Use Python 3.10 or later, CMake, a supported C++ toolchain, the matching Qt 6.10.3 SDK and zlib. Install QtBase, QtSvg and QtMultimedia; platform-specific SDK commands and dependency packages are recorded in [the desktop workflow](../.github/workflows/build.yml). The example commands below run from the repository root and use fresh candidate destinations.

Configure before running `package_desktop.py`. It builds the application with at most four compilation jobs unless `--skip-build` is supplied. Run the regression tests separately before packaging: the script runs packaged runtime checks, not the full test suite. A package destination must be new or empty; existing artifacts are preserved.

### macOS Apple Silicon

Install the pinned Filament SDK with `scripts/setup_filament.sh`, then configure and test:

```sh
cmake -S . -B build-release-arm64 \
  -DCMAKE_PREFIX_PATH="$PWD/.deps/Qt/6.10.3/macos" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DTOON_ENABLE_FILAMENT=ON -DBUILD_TESTING=ON
cmake --build build-release-arm64 --config Release --parallel 4
ctest --test-dir build-release-arm64 -C Release --output-on-failure --parallel 2
python3 scripts/package_desktop.py \
  --build-dir build-release-arm64 --config Release --skip-build \
  --qt-root .deps/Qt/6.10.3/macos \
  --output-dir dist/0.75.0/macos-arm64-candidate \
  --require-3d --dmg
```

The script deploys Qt, adds the offscreen test plugin if needed, signs nested code and the app, then verifies the relocated copy. ZIP creation preserves framework symlinks. `--dmg` also creates and checks a disk image containing `ToonPro.app` and an Applications shortcut. Extract the ZIP, or open the DMG and copy the app into the chosen Applications folder. The image does not replace a running copy automatically.

### macOS Intel

Use the same macOS Qt SDK, which supplies the required architecture, with a separate build:

```sh
cmake -S . -B build-release-x86_64 \
  -DCMAKE_PREFIX_PATH="$PWD/.deps/Qt/6.10.3/macos" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
  -DTOON_ENABLE_FILAMENT=OFF -DBUILD_TESTING=ON
cmake --build build-release-x86_64 --config Release --parallel 4
ctest --test-dir build-release-x86_64 -C Release --output-on-failure --parallel 2
python3 scripts/package_desktop.py \
  --build-dir build-release-x86_64 --config Release --skip-build \
  --qt-root .deps/Qt/6.10.3/macos \
  --output-dir dist/0.75.0/macos-x86_64-candidate --dmg
```

Running this target on Apple Silicon requires Rosetta. A Rosetta result must be recorded as such and does not establish acceptance on physical Intel hardware. Artifact naming reads the executable architecture rather than the packaging Python process's architecture.

### Windows

The workflow uses Qt's `msvc2022_64` SDK and a vcpkg `toon-x64-windows` triplet: x64, static zlib, shared MSVC runtime. Follow its dependency and configure steps on a Windows build machine, then:

```powershell
cmake --build build --config Release --parallel 4
$env:PATH = "$PWD\.deps\Qt\6.10.3\msvc2022_64\bin;$env:PATH"
ctest --test-dir build -C Release --output-on-failure --parallel 2
python scripts/package_desktop.py `
  --build-dir build --config Release --skip-build `
  --qt-root .deps/Qt/6.10.3/msvc2022_64 `
  --output-dir dist/0.75.0/windows-x86_64-candidate
```

The portable ZIP contains `bin/ToonPro.exe`, deployed Qt/FFmpeg libraries and plugins. CMake stages release MSVC runtime DLLs beside the executable. Verification inspects both normal and delayed DLL imports and rejects missing compiler runtimes. Windows 10/11 supplies UCRT; no Visual Studio installation should be needed, subject to actual clean-machine validation. Windows packages are unsigned: the script does not implement Authenticode signing or claim SmartScreen acceptance.

### Linux — deferred recipe and verification history

This platform is not being distributed in the current Windows/Mac release. Its earlier build passed 25 selected suites and six save/export follow-up suites; relocated offscreen 2× and native XCB checks passed with software H.264/AAC. The retained archive passed all 20 checks in each mode in a clean Ubuntu runtime container without SDK/compiler/source. These historical checks do not enable a Linux release or establish physical Linux workstation acceptance.

The current target uses Ubuntu 24.04 / glibc 2.39 and Qt's `gcc_64` SDK. Packaging requires `patchelf`, `readelf` from binutils and `ldd`. Native GUI checks require an X11 display, or Xvfb for automated checks. Build the software codec runtime with the recipe below before packaging. The dependency list is in the workflow; [the Linux release Dockerfile](../scripts/linux-release.Dockerfile) provides the same baseline for an isolated build environment.

```sh
cmake -S . -B build-release-linux -G Ninja \
  -DCMAKE_PREFIX_PATH="$PWD/.deps/Qt/6.10.3/gcc_64" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_LIBDIR=lib \
  -DTOON_ENABLE_FILAMENT=OFF -DBUILD_TESTING=ON
cmake --build build-release-linux --config Release --parallel 4
xvfb-run -a -s "-screen 0 1600x1200x24" \
  ctest --test-dir build-release-linux -C Release --output-on-failure --parallel 2
python3 scripts/package_desktop.py --xvfb \
    --build-dir build-release-linux --config Release --skip-build \
    --qt-root .deps/Qt/6.10.3/gcc_64 \
    --ffmpeg-root build/release-linux-codecs/install \
    --output-dir dist/0.75.0/linux-x86_64-candidate
```

Packaging explicitly copies the matching SDK's FFmpeg runtime, normalizes ELF runtime paths to the package, resolves its dependency closure and creates `ToonPro.sh`. Extract the `.tar.gz` and launch through that script. Qt and FFmpeg must resolve inside the package; remaining OS libraries are listed in the verification report. This is a portable directory, not an AppImage, `.deb` or `.rpm` installer. The desktop backend is X11/XCB; Wayland desktops need XWayland. Xvfb verifies the XCB runtime but does not establish real display, audio-device or tablet behavior. Other distributions and older glibc versions require separate verification.

The audited Linux SDK supplies hardware H.264 encoders but no software H.264 encoder. A machine without suitable GPU encoding therefore cannot pass the MP4 export check with that runtime. The isolated replacement recipe builds FFmpeg 7.1.3 with OpenH264 2.7.0 and native AAC, keeping Qt's library ABI and leaving GPL/nonfree FFmpeg features disabled:

```sh
# Run inside the Linux x86_64 build environment, with a fresh output directory.
TOON_CODEC_JOBS=2 bash scripts/build_linux_codecs.sh \
  build/release-linux-codecs .cache/dependency-sources
```

The recipe requires the codec build dependencies listed in the Linux Dockerfile and Python 3.12 or later. It installs under `build/release-linux-codecs/install`, records source/configuration/library hashes and checks the ABI, codec discovery and FFmpeg license configuration. Supply `--ffmpeg-root build/release-linux-codecs/install` to the Linux packaging command to deploy it into the fresh stage. The final relocated Qt MP4 encode/decode checks must still pass; merely finding an encoder does not establish working export. The current Linux candidate passes the actual Qt H.264/AAC encode/decode checks with this replacement.

The recipe preserves both source archives and rebuild inputs under `build/release-linux-codecs/dependency-sources`. OpenH264 is distributed under BSD-2-Clause, but Cisco's royalty coverage for its downloaded binary does not apply to this separately built/redistributed library; codec patent and distribution review remains a public-release requirement. [Cisco's OpenH264 FAQ](https://www.openh264.org/faq.html)

## Signing and candidate CI

Mac builds default to ad-hoc signing. When an existing Developer ID Application certificate and notarytool keychain profile are available, the script supports:

```sh
python3 scripts/package_desktop.py \
  --build-dir build-release-arm64 --config Release --skip-build \
  --qt-root .deps/Qt/6.10.3/macos \
  --output-dir dist/0.75.0/macos-arm64-signed-candidate \
  --require-3d --dmg \
  --sign-identity "Developer ID Application: YOUR IDENTITY" \
  --notarize-profile "YOUR EXISTING KEYCHAIN PROFILE"
```

These options are macOS-only. `--notarize-profile` requires `--sign-identity`, uploads the newly created application archive and optional DMG to Apple, checks acceptance, staples tickets and validates them. Secrets remain in the existing keychain profile. This path is implemented but has not been exercised for the current candidate; no suitable signing identity has been provided. Signing/notarization does not publish the app.

The GitHub workflow builds/tests macOS arm64, macOS x86_64 and Windows x86_64; Linux is inactive. Distribution artifacts are produced only after successful tests and packaged-runtime verification, through a manual `workflow_dispatch`, an application/CI change on `main`, or a push to `codex/release-candidate-*` (Windows only). Other application push/PR runs perform tests. Website-only changes do not trigger desktop builds. Windows diagnostics collect all seven results and the full suite even if an early diagnostic fails; an explicit failure gate prevents packaging in that case. Diagnostic artifacts are uploaded even after failure.

The artist authorized public source and test downloads in `complex1/ToonPro`. Repository visibility also controls artifact visibility; [GitHub Actions artifact downloads require a GitHub sign-in and expire](https://docs.github.com/en/actions/how-tos/manage-workflow-runs/download-workflow-artifacts). The workflow does not create a GitHub Release; its artifact retention is 30 days for candidates and 14 days for diagnostics. No failed or unverified platform should be offered by the download page.

## Runtime checks and reports

`package.json` records actual executable architecture, version, artifact sizes/hashes, signing/notarization state, build capabilities, verification status and remaining release gates. `SHA256SUMS` covers the distributable archives. `verification/package-verification.json` contains actual relocated smoke results and dependency inspection. Each smoke saves a project, workspace image, movie preview, WAV, MP4, decoded video image, optional 3D image and logs.

The smoke uses a fresh supplied output directory and a separate workspace/profile. It does not load an artist's projects, brush library, palettes or settings. Current checks cover:

- A three-shot project save/reopen with raster artwork, embedded Symbol content, character palettes/variants and audio; rendered artwork remains identical after reopening.
- Drawing workspace, embedded icons and default brush library; raster stroke commit followed by Undo restores the original pixels and clean history.
- Scene ordering, movie global seek/preview, a Symbol-backed shot thumbnail, continuous playback across cuts and return to the original drawing/frame/history. This check may briefly play its generated test audio.
- WAV export with the expected sample count/header; H.264/AAC encoding and independent video-pixel/audio-sample decoding.
- Actual transparent 3D reference rendering when available; `--require-3d` rejects an unavailable renderer instead of treating it as a passed render.
- Offscreen startup at forced 2× scale and a native launch at the screen's actual scale, with separate profiles. Native Retina scale is measured rather than multiplied again.
- Relocated runtime paths. macOS rejects SDK/build references and checks loaded-library logs; Linux inspects ELF paths and dependency resolution; Windows launches with only system paths in `PATH`.

The app accepts `--package-smoke OUTPUT_DIRECTORY` and optional `--require-3d`. It rejects a nonempty directory. Asynchronous checks share a **45-second deadline**; the launcher imposes a **60-second process limit**. `--skip-native-smoke` is available when there is no window server; the skipped check is recorded, and `nativeRuntimeVerified` remains false.

An existing staged package can be checked again with a fresh report destination:

```sh
python3 scripts/verify_package.py \
  --stage dist/0.75.0/macos-arm64/stage \
  --report-dir build/package-recheck \
  --forbid-path "$PWD/.deps/Qt/6.10.3/macos" \
  --forbid-path "$PWD/build-release-arm64" --require-3d
```

For other targets, supply their stage and SDK/build paths and omit `--require-3d`. Run Linux checks under Xvfb when a normal X11 display is unavailable.

## Notices and dependency sources

Packages retain Lucide and Filament notices, the installed Qt SDK's SPDX manifests, component copyright/source attribution and license texts. SDK manifests also describe tools and optional components that may not be deployed. Standard license texts come from the pinned SPDX `v3.27.0` data release, are cached under `.cache/package-license-texts/`, and have recorded URLs/hashes. Missing notices or failed retrieval stop packaging.

The accompanying macOS source bundle is:

- `dist/0.75.0/dependency-sources/macos-arm64.tar.gz`
- Size: **111,694,875 bytes**.
- SHA-256: `e09f2e63bfd56656d110f093b72833dbb10a76b5f42e3e4ac1506c6fa7892145`.

It contains six archives: the SDK's exact patched QtBase/QtSvg commits, QtMultimedia 6.10.3, its required QtShaderTools revision, Qt's 6.10.3 provisioning/build scripts and FFmpeg 7.1.3. It also includes SDK manifests/configuration, `SOURCE-MANIFEST.json`, `SHA256SUMS` and `REBUILDING.txt`. QtMultimedia's SDK manifest records a release version rather than a commit; the collected commit is resolved from its release tag. FFmpeg additionally matches Qt's provisioning source checksum. The collector records these distinctions instead of claiming a byte-identical vendor rebuild.

Create a fresh bundle from an SDK matching the audited revisions, or verify the extracted bundle without downloading:

```sh
python3 scripts/collect_dependency_sources.py \
  --qt-root .deps/Qt/6.10.3/macos \
  --output-dir dist/0.75.0/dependency-sources/macos-arm64-new
python3 scripts/collect_dependency_sources.py \
  --verify-only dist/0.75.0/dependency-sources/macos-arm64
```

The collector produces an adjacent `<output-dir>.tar.gz` and `.tar.gz.sha256`, preserves existing destinations, checks actual SDK manifests and verifies pinned archive hashes. The current bundle passes those technical checks. It does not certify complete license compliance. The Linux bundle additionally includes the exact QtDeclarative source revision for deployed QtQml/QtQuick dependencies; its separate codec bundle includes the OpenH264/FFmpeg sources and build recipe. Windows source coverage remains pending its verified candidate. Modified-library rebuilding/replacement has not yet been verified. See [the rebuild notes](DEPENDENCY-REBUILDING.txt) for supplied material, replacement paths and known limits.

Public distribution still requires final application terms, complete notice/source-delivery and library-replacement review, target-specific installation/signing acceptance, and clean-machine/tablet results. This work does not choose Toon Pro's application license, create an EULA or make a written source offer.

## Official references

- [Qt deployment API](https://doc.qt.io/qt-6.10/qt-generate-deploy-app-script.html) and [Windows deployment](https://doc.qt.io/qt-6.10/windows-deployment.html).
- [Qt Multimedia 6.10.3](https://doc.qt.io/qt-6.10/qtmultimedia-index.html) and [FFmpeg attribution/build references](https://doc.qt.io/qt-6.10/qtmultimedia-attribution-ffmpeg.html).
- [Qt open-source obligations](https://www.qt.io/development/open-source-lgpl-obligations) and [Qt source builds](https://doc.qt.io/qt-6.10/build-sources.html).
- [Apple Developer ID and notarization](https://developer.apple.com/developer-id/).
- [SPDX license data v3.27.0](https://github.com/spdx/license-list-data/tree/v3.27.0).
