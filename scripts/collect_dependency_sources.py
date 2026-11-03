#!/usr/bin/env python3
"""Collect audited Qt 6.10.3 dependency sources without running upstream code.

The installed SDK's SPDX manifests must match the pinned sources. This creates
a separate offline source archive; it does not certify licensing compliance or
claim that rebuilding produces byte-identical vendor binaries.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import sys
import tarfile
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
QT_VERSION = "6.10.3"
SOURCES = (
    ("qtbase", "7ddbc87d8e14ce51d2957ea72d0a6077593d5ff4", "2b68cc296ebdf466f6f5ac3de5b9c8eed9541f98a66602d6d6120574b04b432c"),
    ("qtsvg", "77765f981c067cec8f250e13688e5be75ee94a1b", "b59b4ddfd7a2821f1ec9e8e59a50cf2890cd92e384c86c31a46787564e54917c"),
    ("qtmultimedia", "df54b9d71bd5c83691b598942e19107dfc23c2a7", "88950b3d3716d172ae3f54c0aa9fc61397ec378612b64edb897ec1d011ccb355"),
    ("qtshadertools", "723bea08b53fe20acf026bce1a31168f202c5b6a", "f9a910361df4416665db43cb60b1032d15eac11fe2275e9a03f80268eab9643a"),
    ("qt5", "6417867c2da7aa2b1b94fb9ab084b514fb7009bb", "e08a1fd60954a6cb6cb8457d3d920ca9f066472e30a2aafd1648e1827dd9bd41"),
    ("FFmpeg", "n7.1.3", "e0b04c4b43d7e6d67cb6710334fb513adf13ac860532f30e1d0ac4c231f232fb"),
)
OPTIONAL_SOURCES = (
    ("qtdeclarative", "3a714efe0672ae1ba06f09864f64957560c69b91", "288d6c2e7abcb4ce91dbc0ec2c939a4f1fd037c37dd016bd27619ac84761da82"),
)
FFMPEG_VENDOR_SHA1 = "27051817deec88bed3b9652d49f9127d22268d83"
MAX_ARCHIVE_BYTES = 256 * 1024 * 1024


def digest(path: Path, algorithm: str = "sha256") -> str:
    result = hashlib.new(algorithm)
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def validate_archive(path: Path, name: str, revision: str, expected: str) -> None:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_ARCHIVE_BYTES:
        raise RuntimeError(f"Invalid source archive: {path.name}")
    if digest(path) != expected:
        raise RuntimeError(f"Source SHA-256 mismatch: {path.name}; existing cache was not replaced")
    if name == "FFmpeg" and digest(path, "sha1") != FFMPEG_VENDOR_SHA1:
        raise RuntimeError("FFmpeg source differs from Qt's provisioning checksum")
    expected_root = f"{name}-{revision}"
    source_files = 0
    expanded = 0
    with tarfile.open(path, "r:gz") as archive:
        for member in archive:
            parts = PurePosixPath(member.name).parts
            if not parts or parts[0] != expected_root or ".." in parts or member.name.startswith("/"):
                raise RuntimeError(f"Unexpected archive member: {path.name}")
            expanded += member.size
            if expanded > 2 * 1024 * 1024 * 1024:
                raise RuntimeError(f"Expanded source archive exceeds limit: {path.name}")
            source_files += member.isfile()
    if source_files < 20:
        raise RuntimeError(f"Source archive is incomplete: {path.name}")


def download_source(cache: Path, name: str, revision: str, expected: str) -> tuple[Path, str]:
    path = cache / f"{name}-{revision}.tar.gz"
    owner = "FFmpeg" if name == "FFmpeg" else "qt"
    reference = f"refs/tags/{revision}" if name == "FFmpeg" else revision
    url = f"https://codeload.github.com/{owner}/{name}/tar.gz/{reference}"
    if not path.exists() and not path.is_symlink():
        request = urllib.request.Request(url, headers={"User-Agent": "ToonPro-dependency-sources/1"})
        temporary_path = None
        try:
            with tempfile.NamedTemporaryFile(dir=cache, suffix=".download", delete=False) as temporary:
                temporary_path = Path(temporary.name)
                with urllib.request.urlopen(request, timeout=60) as response:
                    if not response.geturl().startswith("https://"):
                        raise RuntimeError("Source download redirected away from HTTPS")
                    size = 0
                    while block := response.read(1024 * 1024):
                        size += len(block)
                        if size > MAX_ARCHIVE_BYTES:
                            raise RuntimeError("Source archive exceeds download limit")
                        temporary.write(block)
            # Windows prevents renaming/removing a NamedTemporaryFile while its
            # handle is open. Close it before validation, publication or cleanup.
            validate_archive(temporary_path, name, revision, expected)
            try:
                # The cache and temporary file share a filesystem. Publish only
                # if absent, then validate any file another collector won with.
                os.link(temporary_path, path)
            except FileExistsError:
                pass
        finally:
            if temporary_path is not None:
                temporary_path.unlink(missing_ok=True)
    validate_archive(path, name, revision, expected)
    return path, url


def read_sdk_manifests(qt_root: Path) -> list[tuple[Path, dict]]:
    manifests = []
    modules = SOURCES[:3] + tuple(source for source in OPTIONAL_SOURCES
                                 if list((qt_root / "sbom").glob(f"{source[0]}-*.spdx.json")))
    for name, revision, _ in modules:
        matches = list((qt_root / "sbom").glob(f"{name}-*.spdx.json"))
        if len(matches) != 1 or matches[0].name != f"{name}-{QT_VERSION}.spdx.json":
            raise RuntimeError(f"Expected one audited Qt {QT_VERSION} SPDX manifest for {name}")
        path = matches[0]
        if path.is_symlink() or path.stat().st_size > 16 * 1024 * 1024:
            raise RuntimeError(f"Invalid SDK manifest: {path}")
        document = json.loads(path.read_text(encoding="utf-8"))
        roots = [p for p in document.get("packages", []) if p.get("name") == name]
        if len(roots) != 1:
            raise RuntimeError(f"Missing unique root package for {name}")
        package = roots[0]
        location = package.get("downloadLocation", "")
        expected = f"git://code.qt.io/qt/{name}.git@{revision}"
        release_match = name == "qtmultimedia" and location == f"git://code.qt.io/qt/{name}.git" and package.get("versionInfo") == QT_VERSION
        if location != expected and not release_match:
            raise RuntimeError(f"SDK {name} source does not match audited revision: {location}")
        if name == "qtmultimedia":
            ffmpeg = [p for p in document.get("packages", []) if p.get("name") == "FFmpeg"]
            if len(ffmpeg) != 1 or ffmpeg[0].get("versionInfo") != "7.1.3" or ffmpeg[0].get("downloadLocation") != "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.3.tar.gz":
                raise RuntimeError("SDK FFmpeg does not match audited 7.1.3 source")
        manifests.append((path, package))
    return manifests


def verify_bundle(directory: Path) -> dict:
    manifest = json.loads((directory / "SOURCE-MANIFEST.json").read_text(encoding="utf-8"))
    if manifest.get("schemaVersion") != 1 or not manifest.get("files"):
        raise RuntimeError("Unknown or empty source manifest")
    for item in manifest["files"]:
        relative = PurePosixPath(item["file"])
        if relative.is_absolute() or ".." in relative.parts:
            raise RuntimeError("Unsafe manifest path")
        path = directory / relative
        if path.is_symlink() or not path.resolve().is_relative_to(directory.resolve()) or not path.is_file() or digest(path) != item["sha256"]:
            raise RuntimeError(f"Source bundle integrity failure: {item['file']}")
    known = {name: (revision, expected) for name, revision, expected in SOURCES + OPTIONAL_SOURCES}
    supplied = manifest.get("sources", [])
    names = [item.get("name") for item in supplied]
    if len(names) != len(set(names)) or not {source[0] for source in SOURCES}.issubset(names):
        raise RuntimeError("Source manifest omits or duplicates required components")
    for item in supplied:
        name = item.get("name")
        if name not in known or (item.get("revision"), item.get("sha256")) != known[name]:
            raise RuntimeError(f"Source manifest component is not an audited revision: {name}")
        revision, expected = known[name]
        validate_archive(directory / "archives" / f"{name}-{revision}.tar.gz", name, revision, expected)
    return manifest


def collect(qt_root: Path, output: Path, cache: Path) -> Path:
    manifests = read_sdk_manifests(qt_root)
    sdk_names = {package["name"] for _, package in manifests}
    requested = SOURCES + tuple(source for source in OPTIONAL_SOURCES if source[0] in sdk_names)
    archive_path = output.with_name(output.name + ".tar.gz")
    if output.exists() or output.is_symlink() or archive_path.exists() or archive_path.is_symlink():
        raise RuntimeError("Use a fresh output directory and archive name; existing contents are preserved")
    cache.mkdir(parents=True, exist_ok=True)
    # Fetch and validate before creating the output, so failed downloads can be retried.
    collected = [(name, revision, expected, *download_source(cache, name, revision, expected)) for name, revision, expected in requested]
    output.mkdir(parents=True)
    (output / "archives").mkdir()
    (output / "sdk-sbom").mkdir()
    files = []
    sources = []
    for name, revision, expected, path, url in collected:
        destination = output / "archives" / path.name
        shutil.copy2(path, destination)
        files.append({"file": destination.relative_to(output).as_posix(), "sha256": expected})
        match = "SDK source commit" if name in ("qtbase", "qtsvg", "qtdeclarative") else "SDK release version; source commit resolved from Qt v6.10.3 tag"
        if name == "qtshadertools":
            match = "Required build dependency pinned by qtmultimedia dependencies.yaml"
        elif name == "qt5":
            match = "Qt v6.10.3 provisioning/build instructions; not an additional runtime library"
        elif name == "FFmpeg":
            match = "SDK version/source URL and Qt provisioning source SHA-1"
        sources.append({"name": name, "revision": revision, "sourceUrl": url, "sha256": expected, "matchBasis": match})
    for path, package in manifests:
        destination = output / "sdk-sbom" / path.name
        shutil.copy2(path, destination)
        files.append({"file": destination.relative_to(output).as_posix(), "sha256": digest(destination)})
    notes = output / "REBUILDING.txt"
    shutil.copy2(ROOT / "docs/DEPENDENCY-REBUILDING.txt", notes)
    files.append({"file": notes.name, "sha256": digest(notes)})
    config = qt_root / "mkspecs/qconfig.pri"
    if config.is_file():
        destination = output / "sdk-sbom/qconfig.pri"
        shutil.copy2(config, destination)
        files.append({"file": destination.relative_to(output).as_posix(), "sha256": digest(destination)})
    manifest = {"schemaVersion": 1, "qtVersion": QT_VERSION, "sources": sources, "files": files,
        "sha256Provenance": "Pinned HTTPS archive digests recorded during source audit; FFmpeg additionally checked against Qt's provisioning SHA-1. These are integrity checks, not publisher signatures.",
        "sourceRebuildVerified": False, "legalApproval": False,
        "coverage": "QtBase, QtSvg, QtMultimedia, " + ("QtDeclarative (QtQml/QtQuick), " if "qtdeclarative" in sdk_names else "")
            + "their vendored third-party trees, QtShaderTools build dependency, FFmpeg and Qt provisioning scripts. "
              "OS/runtime libraries and additional deployed modules require separate inventory review. Custom codec runtime sources are supplied separately."}
    (output / "SOURCE-MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    sums = [f"{item['sha256']}  {item['file']}" for item in files]
    sums.append(f"{digest(output / 'SOURCE-MANIFEST.json')}  SOURCE-MANIFEST.json")
    (output / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8")
    verify_bundle(output)
    with tarfile.open(archive_path, "w:gz", compresslevel=1) as archive:
        archive.add(output, arcname=output.name)
    archive_path.with_name(archive_path.name + ".sha256").write_text(f"{digest(archive_path)}  {archive_path.name}\n", encoding="utf-8")
    return archive_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qt-root", type=Path)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--cache-dir", type=Path, default=ROOT / ".cache/dependency-sources")
    parser.add_argument("--verify-only", type=Path, help="Validate an extracted source bundle without downloads")
    args = parser.parse_args()
    if args.verify_only:
        verify_bundle(args.verify_only)
        print(f"Source bundle hashes verified: {args.verify_only}")
    else:
        if not args.qt_root or not args.output_dir:
            parser.error("--qt-root and --output-dir are required when collecting sources")
        print(collect(args.qt_root.resolve(), args.output_dir.absolute(), args.cache_dir.absolute()))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, tarfile.TarError) as error:
        print(f"Dependency sources: {error}", file=sys.stderr)
        raise SystemExit(1)
