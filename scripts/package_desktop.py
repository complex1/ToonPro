#!/usr/bin/env python3
"""Build a verified native distribution candidate; never publish or overwrite one."""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

from verify_package import binary_architecture, is_elf, is_macho, linux_dependencies

ROOT = Path(__file__).resolve().parents[1]
SPDX_TAG = "v3.27.0"


def license_cache_directory(requested: Path | None = None) -> Path:
    """Use a writable cache without requiring writes to a mounted source tree."""
    configured = requested or os.environ.get("TOON_PACKAGE_CACHE")
    roots = [Path(configured)] if configured else [ROOT / ".cache/package-license-texts",
                                                   Path.home() / ".cache/toon-pro/package-license-texts"]
    failures = []
    for root in roots:
        cache = root.expanduser().resolve() / SPDX_TAG
        try:
            cache.mkdir(parents=True, exist_ok=True)
            # os.access alone can lie for root inside read-only bind mounts.
            with tempfile.TemporaryFile(dir=cache):
                pass
            return cache
        except OSError as error:
            failures.append(f"{cache}: {error}")
    raise RuntimeError("No writable license cache. Use --cache-dir or TOON_PACKAGE_CACHE. " + "; ".join(failures))


def copy_linux_library(source: Path, library_dir: Path, needed: str | None = None) -> None:
    real = source.resolve(strict=True)
    destination = library_dir / real.name
    if destination.exists():
        if digest(destination) != digest(real):
            raise RuntimeError(f"Conflicting deployed library: {real.name}")
    else:
        shutil.copy2(real, destination)
    for name in {source.name, needed or real.name}:
        if name == real.name:
            continue
        alias = library_dir / name
        if alias.exists() or alias.is_symlink():
            if alias.resolve() != destination.resolve():
                raise RuntimeError(f"Conflicting deployed library alias: {name}")
        else:
            alias.symlink_to(real.name)


def prepare_linux(stage: Path, qt_root: Path) -> None:
    """Complete Qt's deployment, then remove SDK RPATHs from every ELF payload."""
    if not shutil.which("patchelf") or not shutil.which("readelf") or not shutil.which("ldd"):
        raise RuntimeError("Linux packaging requires patchelf, readelf (binutils), and ldd.")
    library_dir = stage / "lib"
    library_dir.mkdir(exist_ok=True)
    # Qt 6.10 does not automatically deploy FFmpeg on Linux/X11. Copy the
    # matching SDK's runtime, never an unrelated system FFmpeg installation.
    for pattern in ("libav*.so*", "libswresample.so*", "libswscale.so*"):
        for source in sorted((qt_root / "lib").glob(pattern)):
            if source.is_file():
                copy_linux_library(source, library_dir)
    environment = os.environ.copy()
    environment["LD_LIBRARY_PATH"] = os.pathsep.join([str(library_dir), str(qt_root / "lib")])
    def patch_paths() -> None:
        for binary in stage.rglob("*"):
            if is_elf(binary):
                relative = os.path.relpath(library_dir, binary.parent)
                run(["patchelf", "--set-rpath", "$ORIGIN" if relative == "." else "$ORIGIN/" + relative, str(binary)])
    for _ in range(12):
        # An old DT_RPATH can take precedence over LD_LIBRARY_PATH. Normalize
        # it before resolving, including libraries copied in the previous pass.
        patch_paths()
        added = False
        for binary in list(stage.rglob("*")):
            if not is_elf(binary):
                continue
            for dependency in linux_dependencies(binary, environment):
                name, location = dependency["name"], dependency["path"]
                if location is None:
                    source = qt_root / "lib" / name
                    if not source.is_file():
                        raise RuntimeError(f"Unresolved Linux dependency {name} in {binary.relative_to(stage)}")
                else:
                    source = Path(location)
                    if source.resolve().is_relative_to(stage.resolve()):
                        continue
                    sdk = source.resolve().is_relative_to(qt_root.resolve())
                    system = source.as_posix().startswith(("/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/"))
                    if system and not sdk and not name.startswith(("libQt6", "libavcodec", "libavformat", "libavutil", "libswscale", "libswresample")):
                        continue
                copy_linux_library(source, library_dir, name)
                added = True
        if not added:
            break
    else:
        raise RuntimeError("Linux dependency closure did not converge.")
    platforms = list(stage.rglob("libqxcb.so"))
    if len(platforms) != 1:
        raise RuntimeError("Expected one deployed Linux XCB platform plugin.")
    plugins = platforms[0].parent.parent.relative_to(stage).as_posix()
    (stage / "bin/qt.conf").write_text(f"[Paths]\nPrefix=..\nLibraries=lib\nPlugins={plugins}\nTranslations=translations\n", encoding="utf-8")
    launcher = stage / "ToonPro.sh"
    launcher.write_text('''#!/bin/sh
set -eu
TOON_APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML_IMPORT_PATH QML2_IMPORT_PATH LD_PRELOAD LD_AUDIT
export LD_LIBRARY_PATH="$TOON_APP_DIR/lib"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
exec "$TOON_APP_DIR/bin/ToonPro" "$@"
''', encoding="utf-8")
    launcher.chmod(0o755)


def sign_macos(app: Path, identity: str | None) -> None:
    options = ["--force", "--sign", identity or "-"]
    if identity:
        options += ["--options", "runtime", "--timestamp"]
    # Sign from the inside out. --deep is for verification, not a replacement
    # for signing nested code with the intended identity and hardened runtime.
    binaries = sorted((p for p in app.rglob("*") if is_macho(p)), key=lambda p: len(p.parts), reverse=True)
    for binary in binaries:
        run(["/usr/bin/codesign", *options, str(binary)])
    for framework in sorted(app.rglob("*.framework"), key=lambda p: len(p.parts), reverse=True):
        if not framework.is_symlink():
            run(["/usr/bin/codesign", *options, str(framework)])
    run(["/usr/bin/codesign", *options, str(app)])
    run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)])


def notarize(path: Path, profile: str, report: Path) -> None:
    # Credentials stay in the caller's existing keychain profile; never accept
    # Apple passwords/API private keys as command-line arguments or log them.
    result = subprocess.run(["/usr/bin/xcrun", "notarytool", "submit", str(path),
                             "--keychain-profile", profile, "--wait", "--output-format", "json"],
                            check=True, capture_output=True, text=True, timeout=1800)
    document = json.loads(result.stdout)
    report.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    if document.get("status") != "Accepted":
        raise RuntimeError("Apple notarization was not accepted; see " + str(report))


def run(args: list[str], **kwargs) -> None:
    print("+", " ".join(map(str, args)), flush=True)
    subprocess.run([str(x) for x in args], check=True, **kwargs)


def digest(path: Path) -> str:
    sha = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            sha.update(block)
    return sha.hexdigest()


def relocate_macos_offscreen(plugin: Path, qt_root: Path) -> None:
    # This plugin is copied after macdeployqt, so it still has the SDK layout.
    # Keep its own Qt lookup relative to the deployed bundle, even when loading
    # succeeds because QtCore/Gui were already loaded by the application.
    commands = subprocess.check_output(["/usr/bin/otool", "-l", str(plugin)], text=True)
    rpaths = set(re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset \d+\)", commands))
    deployed = "@loader_path/../../Frameworks"
    for old in sorted(rpaths & {"@loader_path/../../lib", str(qt_root / "lib")}):
        run(["/usr/bin/install_name_tool", "-delete_rpath", old, str(plugin)])
    if deployed not in rpaths:
        run(["/usr/bin/install_name_tool", "-add_rpath", deployed, str(plugin)])


def download_license(license_id: str, cache: Path) -> tuple[str, str]:
    # License IDs originate in the bounded official SDK manifests, not paths.
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+-]*", license_id):
        raise ValueError(f"Unexpected SPDX license identifier: {license_id}")
    target = cache / (license_id + ".txt")
    url = f"https://raw.githubusercontent.com/spdx/license-list-data/{SPDX_TAG}/text/{license_id}.txt"
    if not target.exists():
        request = urllib.request.Request(url, headers={"User-Agent": "ToonPro-package-notices/1"})
        with urllib.request.urlopen(request, timeout=30) as response:
            data = response.read(512 * 1024 + 1)
        if not data or len(data) > 512 * 1024:
            raise RuntimeError(f"Invalid license text size: {license_id}")
        temporary = target.with_suffix(".download")
        temporary.write_bytes(data)
        temporary.replace(target)
    return license_id, url


def collect_notices(qt_root: Path, destination: Path, cache: Path) -> None:
    destination.mkdir(parents=True)
    texts = destination / "licenses"
    texts.mkdir()
    cache.mkdir(parents=True, exist_ok=True)
    manifests = []
    licenses: set[str] = {"Zlib"}
    extracted: dict[str, str] = {}
    attribution = [
        "Toon Pro dependency notices",
        "==========================",
        "This directory retains the installed Qt SDK's component attribution and source locations.",
        "The SDK manifests also list build tools and components that may not be deployed.",
        "Their presence does not change the license of Toon Pro's own application code.",
        "Qt is dynamically linked; Filament is linked only in supported macOS arm64 builds.",
        "Source/build instructions: https://doc.qt.io/qt-6.10/build-sources.html",
        "Qt deployment: https://doc.qt.io/qt-6.10/qt-generate-deploy-app-script.html",
        "Qt multimedia/FFmpeg: https://doc.qt.io/qt-6.10/qtmultimedia-index.html",
        "Matching source bundles, when supplied, are in the release's dependency-sources directory.",
        "Each source bundle contains SOURCE-MANIFEST.json, SHA256SUMS and REBUILDING.txt.",
        "The package manifest records remaining release checks; notices alone are not license approval.",
        "",
    ]
    sbom = destination / "sdk-sbom"
    sbom.mkdir()
    for module in ("qtbase", "qtsvg", "qtmultimedia"):
        matches = sorted((qt_root / "sbom").glob(f"{module}-*.spdx.json"))
        if len(matches) != 1:
            raise RuntimeError(f"Expected one official SPDX manifest for {module} in {qt_root / 'sbom'}")
        source = matches[0]
        if source.stat().st_size > 16 * 1024 * 1024:
            raise RuntimeError(f"SDK manifest exceeds 16 MiB: {source.name}")
        document = json.loads(source.read_text(encoding="utf-8"))
        shutil.copy2(source, sbom / source.name)
        manifests.append({"file": source.name, "sha256": digest(source)})
        for license_info in document.get("hasExtractedLicensingInfos", []):
            extracted[license_info["licenseId"]] = license_info["extractedText"]
        for package in document.get("packages", []):
            name = package.get("name", "Unnamed component")
            attribution += [name, "-" * len(name),
                "Version: " + package.get("versionInfo", "not recorded"),
                "License: " + package.get("licenseConcluded", "NOASSERTION"),
                "Source: " + package.get("downloadLocation", "NOASSERTION"),
                package.get("copyrightText", "Copyright attribution is recorded in the upstream source."), ""]
            for field in ("licenseConcluded", "licenseDeclared"):
                licenses.update(re.findall(r"[A-Za-z0-9][A-Za-z0-9.+-]*", package.get(field, "")))
    licenses -= {"AND", "OR", "WITH", "NONE", "NOASSERTION"}
    unknown = {entry for entry in licenses if entry.startswith("LicenseRef-")} - extracted.keys()
    if unknown:
        raise RuntimeError("SDK contains license references without text: " + ", ".join(sorted(unknown)))
    for name, body in extracted.items():
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.+-]*", name):
            raise RuntimeError("Invalid extracted license name")
        (texts / (name + ".txt")).write_text(body, encoding="utf-8")
    sources = {}
    with ThreadPoolExecutor(max_workers=4) as pool:
        for license_id, url in pool.map(lambda name: download_license(name, cache), sorted(licenses - extracted.keys())):
            sources[license_id] = url
            shutil.copy2(cache / (license_id + ".txt"), texts / (license_id + ".txt"))
    for source, name in ((ROOT / "resources/icons/lucide/LICENSE", "Lucide-LICENSE.txt"),
                         (ROOT / "resources/icons/lucide/NOTICE.md", "Lucide-NOTICE.md"),
                         (ROOT / "resources/scene3d/licenses/Filament-LICENSE.txt", "Filament-LICENSE.txt"),
                         (ROOT / "resources/scene3d/licenses/Filament-THIRD-PARTY-NOTICES.txt", "Filament-THIRD-PARTY-NOTICES.txt")):
        shutil.copy2(source, destination / name)
    (destination / "THIRD-PARTY-NOTICES.txt").write_text("\n".join(attribution), encoding="utf-8")
    (destination / "sources.json").write_text(json.dumps({"sdkManifests": manifests, "licenseTextSources": sources,
        "licenseTextSHA256": {path.name: digest(path) for path in sorted(texts.glob("*.txt"))}}, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--qt-root", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True, help="Fresh output folder; existing contents are never removed")
    parser.add_argument("--cache-dir", type=Path, help="Writable license-text cache root; also configurable with TOON_PACKAGE_CACHE")
    parser.add_argument("--config", default="RelWithDebInfo")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--require-3d", action="store_true")
    parser.add_argument("--skip-native-smoke", action="store_true", help="CI without a window server; offscreen smoke is still required")
    parser.add_argument("--dmg", action="store_true", help="Also create a macOS drag-to-Applications disk image")
    parser.add_argument("--sign-identity", help="macOS Developer ID Application identity from the existing keychain")
    parser.add_argument("--notarize-profile", help="Existing notarytool keychain profile; requires --sign-identity and uploads to Apple")
    args = parser.parse_args()
    system = platform.system()
    if system not in {"Darwin", "Windows", "Linux"}:
        parser.error("This packaging workflow supports native macOS, Windows and Linux builds.")
    if (args.dmg or args.sign_identity or args.notarize_profile) and system != "Darwin":
        parser.error("DMG, Developer ID signing and notarization options require macOS.")
    if args.notarize_profile and not args.sign_identity:
        parser.error("Notarization requires --sign-identity; ad-hoc code cannot be notarized.")
    if args.sign_identity and not args.sign_identity.startswith("Developer ID Application:"):
        parser.error("Use a Developer ID Application identity for distribution outside the Mac App Store.")
    build = args.build_dir.resolve()
    qt_root = args.qt_root.resolve()
    output = args.output_dir.resolve()
    if output.exists() and any(output.iterdir()):
        parser.error("Output folder is not empty. Choose a new destination; existing packages are preserved.")
    if not (build / "CMakeCache.txt").is_file():
        parser.error("Configure the build before packaging.")
    cache = license_cache_directory(args.cache_dir)
    if not args.skip_build:
        run(["cmake", "--build", str(build), "--config", args.config, "--target", "ToonPro", "--parallel", "4"])
    output.mkdir(parents=True, exist_ok=True)
    stage = output / "stage"
    run(["cmake", "--install", str(build), "--config", args.config, "--prefix", str(stage)])
    app = stage / "ToonPro.app"
    executable = stage / {"Darwin": "ToonPro.app/Contents/MacOS/ToonPro", "Windows": "bin/ToonPro.exe", "Linux": "bin/ToonPro"}[system]
    if not executable.is_file():
        raise RuntimeError(f"Application installation is missing: {executable}")
    # The native deployment tool may omit the optional offscreen platform used
    # for the first clean smoke. It depends on the already deployed QtCore/Gui.
    architecture = binary_architecture(executable, system)
    offscreen_name = {"Darwin": "libqoffscreen.dylib", "Windows": "qoffscreen.dll", "Linux": "libqoffscreen.so"}[system]
    if not any(stage.rglob(offscreen_name)):
        if system == "Darwin":
            plugin_folder = app / "Contents/PlugIns/platforms"
        else:
            native_plugins = list(stage.rglob("qwindows.dll" if system == "Windows" else "libqxcb.so"))
            if len(native_plugins) != 1:
                raise RuntimeError("Could not locate the deployed native platform plugin folder.")
            plugin_folder = native_plugins[0].parent
        plugin_folder.mkdir(parents=True, exist_ok=True)
        copied_plugin = plugin_folder / offscreen_name
        shutil.copy2(qt_root / "plugins/platforms" / offscreen_name, copied_plugin)
        if system == "Darwin":
            relocate_macos_offscreen(copied_plugin, qt_root)
    if system == "Linux":
        prepare_linux(stage, qt_root)
    notices = app / "Contents/Resources/third-party" if app.exists() else stage / "third-party"
    collect_notices(qt_root, notices, cache)
    if system == "Darwin":
        sign_macos(app, args.sign_identity)
    verification = [sys.executable, str(ROOT / "scripts/verify_package.py"), "--stage", str(stage),
                    "--report-dir", str(output / "verification"), "--forbid-path", str(qt_root), "--forbid-path", str(build)]
    if args.require_3d:
        verification.append("--require-3d")
    if args.skip_native_smoke:
        verification.append("--skip-native-smoke")
    run(verification)
    report = json.loads((output / "verification/package-verification.json").read_text(encoding="utf-8"))
    version = report["smokes"][0]["report"]["applicationVersion"]
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){1,3}(?:[-+][A-Za-z0-9.-]+)?", version):
        raise RuntimeError("The packaged application returned an invalid version identifier.")
    name = f"ToonPro-{version}-{'macos' if system == 'Darwin' else system.lower()}-{architecture}"
    notarized = False
    if args.notarize_profile:
        upload = output / (name + "-notary-upload.zip")
        run(["/usr/bin/ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(upload)])
        notarize(upload, args.notarize_profile, output / "notarization-app.json")
        run(["/usr/bin/xcrun", "stapler", "staple", str(app)])
        run(["/usr/bin/xcrun", "stapler", "validate", str(app)])
        run(["/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)])
        run(["/usr/sbin/spctl", "--assess", "--type", "execute", "--verbose=2", str(app)])
        notarized = True
        upload.unlink()  # Only the newly-created, unstapled upload intermediate.
    archive = output / (name + (".tar.gz" if system == "Linux" else ".zip"))
    if system == "Darwin":
        run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(app), str(archive)])
    elif system == "Linux":
        with tarfile.open(archive, "w:gz") as tar:
            tar.add(stage, arcname=name, recursive=True)
    else:
        shutil.make_archive(str(archive.with_suffix("")), "zip", root_dir=stage)
    artifacts = [archive]
    if args.dmg:
        disk_image = output / (name + ".dmg")
        with tempfile.TemporaryDirectory(prefix="toon-dmg-") as folder:
            image_root = Path(folder)
            run(["/usr/bin/ditto", str(app), str(image_root / app.name)])
            (image_root / "Applications").symlink_to("/Applications")
            run(["/usr/bin/hdiutil", "create", "-volname", "Toon Pro", "-srcfolder", str(image_root), "-format", "UDZO", str(disk_image)])
        if args.sign_identity:
            run(["/usr/bin/codesign", "--force", "--sign", args.sign_identity, "--timestamp", str(disk_image)])
        if args.notarize_profile:
            notarize(disk_image, args.notarize_profile, output / "notarization-dmg.json")
            run(["/usr/bin/xcrun", "stapler", "staple", str(disk_image)])
            run(["/usr/bin/xcrun", "stapler", "validate", str(disk_image)])
        run(["/usr/bin/hdiutil", "verify", str(disk_image)])
        artifacts.append(disk_image)
    capability_files = list(stage.rglob("build-capabilities.json"))
    capabilities = json.loads(capability_files[0].read_text(encoding="utf-8")) if len(capability_files) == 1 else {}
    signature = "developer-id" if args.sign_identity else "ad-hoc" if system == "Darwin" else "unsigned"
    blockers = ["Owner release/license approval and corresponding-source/relinking obligations", "Clean-machine and physical tablet acceptance"]
    if system == "Darwin" and not notarized:
        blockers.append("Developer ID signing and Apple notarization")
    elif system == "Windows":
        blockers.append("Windows Authenticode signing and SmartScreen acceptance")
    if args.skip_native_smoke:
        blockers.append("Native desktop runtime smoke was skipped")
    manifest = {"schemaVersion": 2, "application": "Toon Pro", "version": version, "platform": system, "architecture": architecture,
                "archive": archive.name, "sha256": digest(archive), "bytes": archive.stat().st_size,
                "signature": signature, "notarized": notarized, "published": False,
                "releaseChannel": "candidate", "publicReleaseReady": False, "releaseBlockers": blockers,
                "runtimeVerified": True, "nativeRuntimeVerified": not args.skip_native_smoke, "capabilities": capabilities,
                "artifacts": [{"file": path.name, "sha256": digest(path), "bytes": path.stat().st_size} for path in artifacts],
                "verification": "verification/package-verification.json"}
    (output / "package.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    (output / "SHA256SUMS").write_text("".join(f"{digest(path)}  {path.name}\n" for path in artifacts), encoding="utf-8")
    print(f"Verified package: {archive}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Packaging failed: {error}", file=sys.stderr)
        raise SystemExit(1)
