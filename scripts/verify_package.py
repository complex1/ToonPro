#!/usr/bin/env python3
"""Verify a relocated Toon Pro bundle with the build SDK removed from its environment."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import struct
import sys
import tempfile

MACHO_MAGIC = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}


def binary_architecture(executable: Path, system: str) -> str:
    """Report the payload architecture, not the packaging Python's architecture."""
    if system == "Darwin":
        architectures = subprocess.check_output(["/usr/bin/lipo", "-archs", str(executable)], text=True).split()
        if set(architectures) == {"arm64", "x86_64"}:
            return "universal2"
        if len(architectures) != 1:
            raise RuntimeError("Unsupported Mach-O architecture set: " + ", ".join(architectures))
        return architectures[0]
    with executable.open("rb") as stream:
        header = stream.read(64)
        if system == "Linux" and header[:4] == b"\x7fELF":
            order = "<" if header[5] == 1 else ">"
            machine = struct.unpack_from(order + "H", header, 18)[0]
            names = {62: "x86_64", 183: "aarch64", 3: "i686"}
        elif system == "Windows" and header[:2] == b"MZ":
            stream.seek(struct.unpack_from("<I", header, 60)[0])
            pe = stream.read(6)
            if pe[:4] != b"PE\0\0":
                raise RuntimeError("Invalid Windows PE executable.")
            machine = struct.unpack_from("<H", pe, 4)[0]
            names = {0x8664: "x86_64", 0xAA64: "arm64", 0x14C: "i686"}
        else:
            raise RuntimeError("The executable does not match the native packaging platform.")
    if machine not in names:
        raise RuntimeError(f"Unsupported executable architecture: {machine}")
    return names[machine]


def is_macho(path: Path) -> bool:
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as stream:
        return stream.read(4) in MACHO_MAGIC


def is_elf(path: Path) -> bool:
    if path.is_symlink() or not path.is_file():
        return False
    with path.open("rb") as stream:
        return stream.read(4) == b"\x7fELF"


def linux_dependencies(binary: Path, environment: dict[str, str]) -> list[dict]:
    environment = dict(environment, LC_ALL="C")
    result = subprocess.run(["ldd", str(binary)], env=environment, capture_output=True, text=True, timeout=30)
    if result.returncode:
        raise RuntimeError(f"Could not inspect ELF dependencies: {binary}: {result.stderr.strip()}")
    dependencies = []
    for line in result.stdout.splitlines():
        if "=>" not in line:
            continue
        name, target = [part.strip() for part in line.split("=>", 1)]
        dependencies.append({"name": name, "path": None if target.startswith("not found") else target.split(" (", 1)[0].strip()})
    return dependencies


def inspect_linux(stage: Path, forbidden: list[str]) -> list[dict]:
    environment = clean_environment(False, False)
    environment["LD_LIBRARY_PATH"] = str(stage / "lib")
    results = []
    for binary in sorted(stage.rglob("*")):
        if not is_elf(binary):
            continue
        dynamic = subprocess.check_output(["readelf", "--dynamic", str(binary)], env=environment, text=True)
        rpaths = []
        for value in re.findall(r"\((?:RPATH|RUNPATH)\).*?\[(.*?)\]", dynamic):
            rpaths.extend(value.split(":"))
        for value in rpaths:
            expanded = value.replace("${ORIGIN}", str(binary.parent)).replace("$ORIGIN", str(binary.parent))
            if not value.startswith(("$ORIGIN", "${ORIGIN}")) or not Path(expanded).resolve().is_relative_to(stage.resolve()):
                raise RuntimeError(f"External ELF runtime path in {binary.relative_to(stage)}: {value}")
        libraries = linux_dependencies(binary, environment)
        for dependency in libraries:
            location = dependency["path"]
            if not location:
                raise RuntimeError(f"Missing Linux dependency {dependency['name']} in {binary.relative_to(stage)}")
            inside = Path(location).resolve().is_relative_to(stage.resolve())
            if inside:
                dependency["bundled"] = True
                continue
            if any(refers_to_path(location, prefix) for prefix in forbidden):
                raise RuntimeError("A Linux binary still loads a library from the build SDK: " + location)
            if dependency["name"].startswith(("libQt6", "libavcodec", "libavformat", "libavutil", "libswscale", "libswresample")):
                raise RuntimeError("Qt/FFmpeg dependency is not bundled: " + dependency["name"])
            if not location.startswith(("/lib/", "/lib64/", "/usr/lib/", "/usr/lib64/")):
                raise RuntimeError("Non-system Linux dependency is not bundled: " + location)
            dependency["bundled"] = False
        results.append({"file": binary.relative_to(stage).as_posix(), "libraries": libraries, "rpaths": rpaths})
    if not results:
        raise RuntimeError("No ELF application binaries found in the staged package.")
    return results


def refers_to_path(value: str, prefix: str) -> bool:
    # QLibraryInfo uses forward slashes on Windows; pathlib uses backslashes.
    # Comparing their raw strings could silently accept a development SDK path.
    if platform.system() == "Windows":
        value = value.replace("\\", "/").casefold()
        prefix = prefix.replace("\\", "/").casefold()
    return prefix in value


def inspect_macos(stage: Path, forbidden: list[str]) -> list[dict]:
    dependencies = []
    for binary in sorted(stage.rglob("*")):
        if not is_macho(binary):
            continue
        linked = subprocess.check_output(["/usr/bin/otool", "-L", str(binary)], text=True)
        commands = subprocess.check_output(["/usr/bin/otool", "-l", str(binary)], text=True)
        rpaths = sorted(set(re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset \d+\)", commands)))
        for rpath in rpaths:
            if rpath.startswith("/") and not rpath.startswith(("/System/Library/", "/usr/lib/")):
                raise RuntimeError(f"External runtime search path in {binary.relative_to(stage)}: {rpath}")
        libraries = []
        for line in linked.splitlines():
            if not line.startswith("\t"):
                continue
            dependency = line.strip().split(" (compatibility version", 1)[0]
            if dependency.startswith("/") and not dependency.startswith(("/System/Library/", "/usr/lib/")):
                raise RuntimeError(f"External runtime dependency in {binary.relative_to(stage)}: {dependency}")
            libraries.append(dependency)
        # otool also prints the inspected binary's absolute filename. Packages
        # can legitimately be staged beneath the forbidden build directory;
        # only dependency values and runtime search paths must be independent.
        for prefix in forbidden:
            if any(prefix in value for value in libraries + rpaths):
                raise RuntimeError(f"A deployed binary still references the build SDK: {binary.relative_to(stage)}")
        dependencies.append({"file": str(binary.relative_to(stage)), "libraries": sorted(set(libraries)), "rpaths": rpaths})
    if not dependencies:
        raise RuntimeError("No Mach-O application binaries found in the staged package.")
    return dependencies


def clean_environment(offscreen: bool, high_dpi: bool) -> dict[str, str]:
    environment = {key: value for key, value in os.environ.items()
                   if not key.startswith(("QT_", "QML", "DYLD_", "LD_"))
                   and key not in {"QTDIR", "Qt6_DIR", "CMAKE_PREFIX_PATH"}}
    if platform.system() == "Windows":
        windows = environment.get("SystemRoot", r"C:\Windows")
        environment["PATH"] = os.pathsep.join([str(Path(windows) / "System32"), windows])
    else:
        environment["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
        if platform.system() == "Darwin":
            environment["DYLD_PRINT_LIBRARIES"] = "1"
        elif platform.system() == "Linux":
            environment["QT_QPA_PLATFORM"] = "xcb"
    environment["QT_MEDIA_BACKEND"] = "ffmpeg"
    environment["LC_ALL"] = "C"
    if offscreen:
        environment["QT_QPA_PLATFORM"] = "offscreen"
    if high_dpi:
        environment["QT_SCALE_FACTOR"] = "2"
    return environment


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--report-dir", type=Path, required=True)
    parser.add_argument("--forbid-path", action="append", default=[])
    parser.add_argument("--require-3d", action="store_true")
    parser.add_argument("--skip-native-smoke", action="store_true")
    args = parser.parse_args()
    system = platform.system()
    if system not in {"Darwin", "Windows", "Linux"}:
        parser.error("Native package verification supports macOS, Windows and Linux.")
    stage = args.stage.resolve()
    payload = stage / {"Darwin": "ToonPro.app/Contents/MacOS/ToonPro", "Windows": "bin/ToonPro.exe", "Linux": "bin/ToonPro"}[system]
    architecture = binary_architecture(payload, system)
    reports = args.report_dir.resolve()
    if reports.exists() and any(reports.iterdir()):
        parser.error("Verification reports need a new or empty folder.")
    reports.mkdir(parents=True, exist_ok=True)
    if not stage.is_dir():
        parser.error("The staged package does not exist.")
    for entry in stage.rglob("*"):
        if entry.is_symlink() and not entry.resolve().is_relative_to(stage):
            raise RuntimeError(f"Package symlink escapes the bundle: {entry.relative_to(stage)}")
    paths = [path.relative_to(stage).as_posix().lower() for path in stage.rglob("*") if path.is_file()]
    platform_plugin = {"Darwin": "libqcocoa.dylib", "Windows": "qwindows.dll", "Linux": "libqxcb.so"}[system]
    if not any(path.endswith(platform_plugin) for path in paths):
        raise RuntimeError(f"Native platform plugin is missing: {platform_plugin}")
    if not any("ffmpegmediaplugin" in path for path in paths):
        raise RuntimeError("FFmpeg multimedia backend is missing.")
    for library in ("avcodec", "avformat", "avutil", "swresample", "swscale"):
        if not any(library in Path(path).name and (path.endswith((".dylib", ".dll")) or ".so" in Path(path).name) for path in paths):
            raise RuntimeError(f"FFmpeg runtime library is missing: {library}")
    if not any(path.endswith("third-party/third-party-notices.txt") for path in paths):
        raise RuntimeError("Dependency notices are missing.")
    dependencies = inspect_macos(stage, args.forbid_path) if system == "Darwin" else inspect_linux(stage, args.forbid_path) if system == "Linux" else []
    smokes = []
    with tempfile.TemporaryDirectory(prefix="toon-package-relocated-") as temporary:
        relocated = Path(temporary) / "installed"
        shutil.copytree(stage, relocated, symlinks=True)
        executable = relocated / {"Darwin": "ToonPro.app/Contents/MacOS/ToonPro", "Windows": "bin/ToonPro.exe", "Linux": "ToonPro.sh"}[system]
        if not executable.is_file():
            raise RuntimeError("Installed application executable is missing.")
        runs = [("offscreen-2x", True, True)]
        if not args.skip_native_smoke:
            runs.append(("native", False, False))
        for name, offscreen, high_dpi in runs:
            destination = reports / name
            command = [str(executable), "--package-smoke", str(destination)]
            if args.require_3d:
                command.append("--require-3d")
            result = subprocess.run(command, cwd=temporary, env=clean_environment(offscreen, high_dpi),
                                    capture_output=True, text=True, timeout=60)
            (reports / (name + ".stdout.log")).write_text(result.stdout, encoding="utf-8")
            (reports / (name + ".stderr.log")).write_text(result.stderr, encoding="utf-8")
            report_path = destination / "smoke.json"
            if result.returncode or not report_path.is_file():
                raise RuntimeError(f"Relocated {name} smoke failed ({result.returncode}); inspect {reports / (name + '.stderr.log')}")
            report = json.loads(report_path.read_text(encoding="utf-8"))
            if not report.get("success"):
                raise RuntimeError(f"Relocated {name} smoke reports failed checks.")
            if high_dpi and report.get("devicePixelRatio", 0) < 1.5:
                raise RuntimeError("The offscreen high-DPI smoke did not exercise scaled rendering.")
            if args.require_3d and report.get("renderer3D") != "rendered":
                raise RuntimeError("This package requires a successful actual 3D render.")
            for prefix in args.forbid_path:
                if any(refers_to_path(path, prefix) for path in report.get("libraryPaths", [])) or refers_to_path(report.get("qtPrefix", ""), prefix):
                    raise RuntimeError(f"Relocated {name} still searches the development Qt installation.")
                if platform.system() == "Darwin":
                    for line in result.stderr.splitlines():
                        if line.startswith("dyld[") and prefix in line:
                            raise RuntimeError(f"Relocated {name} loaded a library from the build SDK.")
            smokes.append({"mode": name, "exitCode": result.returncode, "report": report})
    package_report = {"success": True, "platform": platform.system(), "architecture": architecture,
                      "verificationHostArchitecture": platform.machine(),
                      "verifiedProcessArchitectures": sorted({smoke["report"].get("architecture", "unknown") for smoke in smokes}),
                      "relocated": True, "sdkEnvironmentRemoved": True, "dependencies": dependencies, "smokes": smokes,
                      "nativeSmokeSkipped": args.skip_native_smoke}
    if system == "Linux":
        package_report["systemLibraries"] = sorted({lib["name"] for binary in dependencies for lib in binary["libraries"] if not lib.get("bundled")})
        package_report["desktopBackend"] = "X11/XCB (XWayland required on Wayland desktops)"
    (reports / "package-verification.json").write_text(json.dumps(package_report, indent=2) + "\n", encoding="utf-8")
    print(f"Verified relocated package: {reports / 'package-verification.json'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Package verification failed: {error}", file=sys.stderr)
        raise SystemExit(1)
