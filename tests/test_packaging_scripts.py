"""Unit checks for packaging boundaries; native runtime smokes run separately."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import package_desktop as package
import verify_package as verify


def elf(path, machine=62):
    path.parent.mkdir(parents=True, exist_ok=True)
    value = bytearray(64)
    value[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<H", value, 18, machine)
    path.write_bytes(value)


class PackageScriptTests(unittest.TestCase):
    def test_sdk_notices_follow_deployed_qt_modules(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk, stage = root / "sdk", root / "stage"
            (sdk / "sbom").mkdir(parents=True); (stage / "lib").mkdir(parents=True)
            for module, libraries in {"qtbase": ["Core"], "qtsvg": ["Svg"], "qtmultimedia": ["Multimedia"],
                                      "qtdeclarative": ["Qml", "Quick"], "qtcharts": ["Charts"]}.items():
                (sdk / "sbom" / f"{module}-6.10.3.spdx.json").write_text(json.dumps({
                    "files": [{"fileName": f"./lib/libQt6{name}.so.6.10.3"} for name in libraries]}))
            for name in ["Core", "Quick", "Qml"]:
                (stage / "lib" / f"libQt6{name}.so.6.10.3").write_bytes(b"runtime")
            selected = package.sdk_notice_manifests(sdk, stage)
            self.assertEqual({p.name.split("-")[0] for p, _ in selected},
                             {"qtbase", "qtsvg", "qtmultimedia", "qtdeclarative"})
            (sdk / "sbom/qtdeclarative-6.10.3.spdx.json").unlink()
            with self.assertRaisesRegex(RuntimeError, "no SDK SPDX attribution.*Qml.*Quick"):
                package.sdk_notice_manifests(sdk, stage)

    def test_qt_library_matching_supports_native_layouts(self):
        for path, expected in [("QtCore", "Core"), ("libQt6Quick.so.6.10.3", "Quick"),
                               ("Qt6Multimedia.dll", "Multimedia"), ("libQt6Qml.6.dylib", "Qml")]:
            self.assertEqual(package.qt_library_name(path), expected)
        for path in ["QtCore.framework", "QtCore.prl", "Qt6CoreConfig.cmake", "libother.so"]:
            self.assertIsNone(package.qt_library_name(path))

    def test_icu_notice_requires_matching_version_and_verified_complete_text(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage, cache, destination = root / "stage", root / "cache", root / "licenses"
            for path in [stage, cache, destination]: path.mkdir()
            (stage / "libicuuc.so.73.2").write_bytes(b"library")
            documents = [{"packages": [{"name": "ICU", "versionInfo": "73.2", "licenseConcluded": "NOASSERTION"}]}]
            with self.assertRaisesRegex(RuntimeError, "version-matched"):
                package.collect_icu_notice(stage, [], destination, cache)
            cached = cache / "ICU-73.2-LICENSE.txt"
            cached.write_bytes(b"truncated license")
            with self.assertRaisesRegex(RuntimeError, "checksum"):
                package.collect_icu_notice(stage, documents, destination, cache)
            with mock.patch.dict(package.ICU_NOTICES, {"73.2": ("https://upstream.example/icu/LICENSE", package.digest(cached))}):
                result = package.collect_icu_notice(stage, documents, destination, cache)
            self.assertEqual(result["version"], "73.2")
            self.assertEqual(result["libraries"], ["libicuuc.so.73.2"])
            self.assertEqual((destination / cached.name).read_bytes(), cached.read_bytes())

    def test_cache_can_be_explicit_or_environment_configured(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with mock.patch.dict(os.environ, {"TOON_PACKAGE_CACHE": str(root / "environment")}), \
                 mock.patch.object(package, "ROOT", root / "untouched-source"):
                expected = (root / "environment" / package.SPDX_TAG).resolve()
                self.assertEqual(package.license_cache_directory(), expected)
                explicit = root / "explicit"
                self.assertEqual(package.license_cache_directory(explicit), (explicit / package.SPDX_TAG).resolve())
                self.assertFalse((root / "untouched-source").exists())

    def test_read_only_source_cache_falls_back_to_writable_home_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source-is-not-writable"
            source.write_bytes(b"fixture mount boundary")
            home = root / "home"
            with mock.patch.dict(os.environ, {}, clear=True), mock.patch.object(package, "ROOT", source), \
                 mock.patch.object(Path, "home", return_value=home):
                actual = package.license_cache_directory()
            self.assertEqual(actual, (home / ".cache/toon-pro/package-license-texts" / package.SPDX_TAG).resolve())
            self.assertTrue(actual.is_dir())
            self.assertEqual(source.read_bytes(), b"fixture mount boundary")

    def test_requested_unwritable_cache_is_not_silently_redirected(self):
        with tempfile.TemporaryDirectory() as temporary:
            blocked = Path(temporary) / "blocked"
            blocked.write_bytes(b"do not replace")
            with self.assertRaisesRegex(RuntimeError, "No writable license cache"):
                package.license_cache_directory(blocked)
            self.assertEqual(blocked.read_bytes(), b"do not replace")

    def test_architecture_comes_from_elf_or_pe_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "ToonPro"
            elf(binary, 183)
            self.assertEqual(package.binary_architecture(binary, "Linux"), "aarch64")
            elf(binary, 62)
            self.assertEqual(package.binary_architecture(binary, "Linux"), "x86_64")
            pe = bytearray(134)
            pe[:2] = b"MZ"
            struct.pack_into("<I", pe, 60, 128)
            pe[128:132] = b"PE\0\0"
            struct.pack_into("<H", pe, 132, 0x8664)
            binary.write_bytes(pe)
            self.assertEqual(package.binary_architecture(binary, "Windows"), "x86_64")
            with self.assertRaises(RuntimeError):
                package.binary_architecture(binary, "Linux")

    def test_universal_mac_architecture_is_not_host_python_architecture(self):
        with mock.patch.object(package.subprocess, "check_output", return_value="x86_64 arm64\n"):
            self.assertEqual(package.binary_architecture(Path("app"), "Darwin"), "universal2")

    def test_verification_report_distinguishes_rosetta_payload_and_host(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage, reports = root / "stage", root / "reports"
            files = ["ToonPro.app/Contents/MacOS/ToonPro", "libqcocoa.dylib", "libffmpegmediaplugin.dylib",
                     "libavcodec.dylib", "libavformat.dylib", "libavutil.dylib", "libswresample.dylib", "libswscale.dylib",
                     "third-party/THIRD-PARTY-NOTICES.txt"]
            for name in files:
                path = stage / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            def smoke(command, **kwargs):
                destination = Path(command[command.index("--package-smoke") + 1])
                destination.mkdir(parents=True)
                (destination / "smoke.json").write_text(json.dumps({"success": True, "devicePixelRatio": 2,
                    "architecture": "x86_64", "libraryPaths": [], "qtPrefix": "/relocated"}))
                return subprocess.CompletedProcess(command, 0, "", "")
            with mock.patch.object(verify.platform, "system", return_value="Darwin"), \
                 mock.patch.object(verify.platform, "machine", return_value="arm64"), \
                 mock.patch.object(verify, "binary_architecture", return_value="x86_64"), \
                 mock.patch.object(verify, "inspect_macos", return_value=[]), \
                 mock.patch.object(verify.subprocess, "run", side_effect=smoke), \
                 mock.patch.object(sys, "argv", ["verify_package.py", "--stage", str(stage), "--report-dir", str(reports), "--skip-native-smoke"]):
                self.assertEqual(verify.main(), 0)
            report = json.loads((reports / "package-verification.json").read_text())
            self.assertEqual(report["architecture"], "x86_64")
            self.assertEqual(report["verificationHostArchitecture"], "arm64")
            self.assertEqual(report["verifiedProcessArchitectures"], ["x86_64"])

    @unittest.skipIf(os.name == "nt", "Linux symlink layout is tested on POSIX")
    def test_linux_library_aliases_stay_internal_and_conflicts_fail(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, stage = root / "sdk", root / "stage/lib"
            source.mkdir(); stage.mkdir(parents=True)
            real = source / "libtest.so.2.3"
            real.write_bytes(b"fixture")
            alias = source / "libtest.so.2"
            alias.symlink_to(real.name)
            package.copy_linux_library(alias, stage)
            self.assertTrue((stage / alias.name).is_symlink())
            self.assertEqual((stage / alias.name).read_bytes(), b"fixture")
            self.assertTrue((stage / alias.name).resolve().is_relative_to(stage.resolve()))
            package.copy_linux_library(alias, stage)  # Idempotent.
            real.write_bytes(b"conflicting fixture")
            with self.assertRaisesRegex(RuntimeError, "Conflicting"):
                package.copy_linux_library(alias, stage)

    def test_dependency_parser_preserves_missing_libraries(self):
        result = subprocess.CompletedProcess([], 0,
            "linux-vdso.so.1 (0x123)\nlibQt6Core.so.6 => /sdk/lib/libQt6Core.so.6 (0x456)\nlibmissing.so => not found\n/lib64/ld-linux-x86-64.so.2 (0x789)\n", "")
        with mock.patch.object(verify.subprocess, "run", return_value=result):
            self.assertEqual(verify.linux_dependencies(Path("ToonPro"), {}), [
                {"name": "libQt6Core.so.6", "path": "/sdk/lib/libQt6Core.so.6"},
                {"name": "libmissing.so", "path": None}])

    def test_clean_environment_cannot_inherit_loader_injection(self):
        with mock.patch.dict(os.environ, {"LD_PRELOAD": "/bad/preload.so", "LD_LIBRARY_PATH": "/sdk/lib",
                 "LD_AUDIT": "/bad/audit.so", "QT_PLUGIN_PATH": "/sdk/plugins", "DYLD_LIBRARY_PATH": "/sdk/lib"}), \
             mock.patch.object(verify.platform, "system", return_value="Linux"):
            environment = verify.clean_environment(True, True)
        self.assertFalse(any(key.startswith(("LD_", "DYLD_")) for key in environment))
        self.assertNotIn("QT_PLUGIN_PATH", environment)
        self.assertEqual(environment["QT_QPA_PLATFORM"], "offscreen")
        self.assertEqual(environment["QT_SCALE_FACTOR"], "2")
        self.assertEqual(environment["PATH"], "/usr/bin:/bin:/usr/sbin:/sbin")

    def test_linux_verification_rejects_sdk_runtime_and_escaping_rpath(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            binary = stage / "bin/ToonPro"
            elf(binary)
            with mock.patch.object(verify.subprocess, "check_output", return_value="(RUNPATH) Library runpath: [/sdk/lib]\n"):
                with self.assertRaisesRegex(RuntimeError, "External ELF"):
                    verify.inspect_linux(stage, ["/sdk"])
            with mock.patch.object(verify.subprocess, "check_output", return_value="(RUNPATH) Library runpath: [$ORIGIN/../../outside]\n"):
                with self.assertRaisesRegex(RuntimeError, "External ELF"):
                    verify.inspect_linux(stage, [])
            with mock.patch.object(verify.subprocess, "check_output", return_value="(RUNPATH) Library runpath: [$ORIGIN/../lib]\n"), \
                 mock.patch.object(verify, "linux_dependencies", return_value=[{"name": "libQt6Core.so.6", "path": "/usr/lib/libQt6Core.so.6"}]):
                with self.assertRaisesRegex(RuntimeError, "not bundled"):
                    verify.inspect_linux(stage, [])

    def test_linux_verification_records_system_prerequisites(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            elf(stage / "bin/ToonPro")
            deps = [{"name": "libQt6Core.so.6", "path": str(stage / "lib/libQt6Core.so.6")},
                    {"name": "libc.so.6", "path": "/lib/x86_64-linux-gnu/libc.so.6"}]
            with mock.patch.object(verify.subprocess, "check_output", return_value="(RUNPATH) Library runpath: [$ORIGIN/../lib]\n"), \
                 mock.patch.object(verify, "linux_dependencies", return_value=deps):
                result = verify.inspect_linux(stage, [str(stage.parent)])
            self.assertTrue(result[0]["libraries"][0]["bundled"])
            self.assertFalse(result[0]["libraries"][1]["bundled"])

    @unittest.skipIf(os.name == "nt", "Linux shell launcher is tested on POSIX")
    def test_linux_launcher_is_relative_executable_and_quotes_arguments(self):
        with tempfile.TemporaryDirectory(prefix="toon package spaces ") as temporary:
            stage = Path(temporary) / "stage with spaces"
            qt = Path(temporary) / "sdk"
            (stage / "bin").mkdir(parents=True)
            (stage / "plugins/platforms").mkdir(parents=True)
            (stage / "plugins/platforms/libqxcb.so").write_bytes(b"plugin")
            app = stage / "bin/ToonPro"
            app.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\nprintf "%s\\n" "$QT_QPA_PLATFORM" "$LD_LIBRARY_PATH"\n')
            app.chmod(0o755)
            with mock.patch.object(package.shutil, "which", return_value="tool"), mock.patch.object(package, "run"):
                package.prepare_linux(stage, qt)
            result = subprocess.check_output([str(stage / "ToonPro.sh"), "an image.toonproj", "literal $(no command)"],
                                             env={"PATH": "/usr/bin:/bin", "QT_QPA_PLATFORM": "offscreen"}, text=True)
            self.assertEqual(result.splitlines(), ["an image.toonproj", "literal $(no command)", "offscreen", str((stage / "lib").resolve())])
            self.assertIn("Prefix=..", (stage / "bin/qt.conf").read_text())

    def test_signing_is_inside_out_and_default_is_ad_hoc(self):
        with tempfile.TemporaryDirectory() as temporary:
            app = Path(temporary) / "ToonPro.app"
            inner = app / "Contents/Frameworks/Sample.framework/Versions/A/Sample"
            inner.parent.mkdir(parents=True)
            inner.write_bytes(next(iter(verify.MACHO_MAGIC)))
            executable = app / "Contents/MacOS/ToonPro"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(next(iter(verify.MACHO_MAGIC)))
            with mock.patch.object(package, "run") as run:
                package.sign_macos(app, None)
            commands = [call.args[0] for call in run.call_args_list]
            self.assertIn(str(inner), commands[0])
            self.assertEqual(commands[-2][-1], str(app))
            self.assertIn("--verify", commands[-1])
            self.assertTrue(all("--timestamp" not in command for command in commands))
            self.assertTrue(all("--deep" not in command for command in commands[:-1]))

    def test_mac_only_flags_are_rejected_on_linux_before_mutation(self):
        with mock.patch.object(package.platform, "system", return_value="Linux"), \
             mock.patch.object(sys, "argv", ["package_desktop.py", "--qt-root", "sdk", "--output-dir", "never-created", "--dmg"]), \
             mock.patch.object(package, "run") as run, mock.patch("sys.stderr"):
            with self.assertRaises(SystemExit) as error:
                package.main()
            self.assertEqual(error.exception.code, 2)
            run.assert_not_called()

    def test_existing_output_is_never_removed_or_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            marker = output / "keep-this-package.zip"
            marker.write_bytes(b"existing distribution")
            with mock.patch.object(sys, "argv", ["package_desktop.py", "--qt-root", "sdk", "--output-dir", str(output)]), \
                 mock.patch.object(package, "run") as run, mock.patch("sys.stderr"):
                with self.assertRaises(SystemExit) as error:
                    package.main()
                self.assertEqual(error.exception.code, 2)
                run.assert_not_called()
            self.assertEqual(marker.read_bytes(), b"existing distribution")

    @unittest.skipIf(os.name == "nt", "Linux library deployment is tested on POSIX")
    def test_linux_explicitly_deploys_matching_sdk_ffmpeg(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage, qt = Path(temporary) / "stage", Path(temporary) / "sdk"
            elf(stage / "bin/ToonPro")
            elf(stage / "plugins/platforms/libqxcb.so")
            elf(qt / "lib/libavcodec.so.61.1.2")
            (qt / "lib/libavcodec.so.61").symlink_to("libavcodec.so.61.1.2")
            with mock.patch.object(package.shutil, "which", return_value="tool"), \
                 mock.patch.object(package, "linux_dependencies", return_value=[]), mock.patch.object(package, "run") as run:
                package.prepare_linux(stage, qt)
            self.assertTrue((stage / "lib/libavcodec.so.61").is_file())
            commands = [call.args[0] for call in run.call_args_list]
            self.assertIn(["patchelf", "--set-rpath", "$ORIGIN", str(stage / "lib/libavcodec.so.61.1.2")], commands)
            self.assertIn(["patchelf", "--set-rpath", "$ORIGIN/../../lib", str(stage / "plugins/platforms/libqxcb.so")], commands)

    def test_custom_ffmpeg_requires_abi_and_attribution_before_deployment(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            qt, runtime = root / "sdk", root / "runtime"
            for component, abi in [("avcodec", 61), ("avformat", 61), ("avutil", 59), ("swresample", 5), ("swscale", 8)]:
                elf(qt / "lib" / f"lib{component}.so.{abi}")
                elf(runtime / "lib" / f"lib{component}.so.{abi}")
            with self.assertRaisesRegex(RuntimeError, "attribution is missing"):
                package.validate_ffmpeg_override(runtime, qt)
            notices = runtime / "third-party"
            (notices / "licenses").mkdir(parents=True)
            (notices / "THIRD-PARTY-NOTICES.txt").write_text("FFmpeg and OpenH264")
            (notices / "sources.json").write_text('{"version":"7.1.3"}')
            with self.assertRaisesRegex(RuntimeError, "license texts"):
                package.validate_ffmpeg_override(runtime, qt)
            (notices / "licenses/LGPL.txt").write_text("fixture license")
            self.assertEqual(package.validate_ffmpeg_override(runtime, qt), {"version": "7.1.3"})
            (runtime / "lib/libavcodec.so.61").unlink()
            with self.assertRaisesRegex(RuntimeError, "avcodec ABI"):
                package.validate_ffmpeg_override(runtime, qt)

    @unittest.skipIf(os.name == "nt", "Linux library deployment is tested on POSIX")
    def test_custom_ffmpeg_replaces_only_new_staged_codec_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stage, qt, runtime = root / "stage", root / "sdk", root / "runtime"
            elf(stage / "bin/ToonPro"); elf(stage / "plugins/platforms/libqxcb.so")
            for component, abi in [("avcodec", 61), ("avformat", 61), ("avutil", 59), ("swresample", 5), ("swscale", 8)]:
                name = f"lib{component}.so.{abi}"
                elf(qt / "lib" / name); elf(stage / "lib" / name); elf(runtime / "lib" / name)
                with (runtime / "lib" / name).open("ab") as stream: stream.write(b"custom runtime")
            elf(stage / "lib/libQt6Core.so.6")
            elf(runtime / "lib/libopenh264.so.8")
            notices = runtime / "third-party"
            (notices / "licenses").mkdir(parents=True)
            (notices / "THIRD-PARTY-NOTICES.txt").write_text("FFmpeg and OpenH264")
            (notices / "sources.json").write_text('{"version":"7.1.3"}')
            (notices / "licenses/LGPL.txt").write_text("fixture license")
            sdk_hash = package.digest(qt / "lib/libavcodec.so.61")
            with mock.patch.object(package.shutil, "which", return_value="tool"), \
                 mock.patch.object(package, "linux_dependencies", return_value=[]), mock.patch.object(package, "run"):
                package.prepare_linux(stage, qt, runtime)
            self.assertEqual(package.digest(qt / "lib/libavcodec.so.61"), sdk_hash)
            self.assertNotEqual(package.digest(stage / "lib/libavcodec.so.61"), sdk_hash)
            self.assertTrue((stage / "lib/libQt6Core.so.6").is_file())
            self.assertTrue((stage / "lib/libopenh264.so.8").is_file())

    def test_custom_ffmpeg_option_is_linux_only(self):
        with mock.patch.object(package.platform, "system", return_value="Darwin"), \
             mock.patch.object(sys, "argv", ["package_desktop.py", "--qt-root", "sdk", "--output-dir", "never-created", "--ffmpeg-root", "custom"]), \
             mock.patch.object(package, "run") as run, mock.patch("sys.stderr"):
            with self.assertRaises(SystemExit) as error:
                package.main()
            self.assertEqual(error.exception.code, 2)
            run.assert_not_called()

    def test_notary_rejection_is_recorded_and_never_treated_as_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "notary.json"
            result = subprocess.CompletedProcess([], 0, '{"status":"Invalid","id":"fixture"}', "")
            with mock.patch.object(package.subprocess, "run", return_value=result) as run:
                with self.assertRaisesRegex(RuntimeError, "not accepted"):
                    package.notarize(Path("candidate.zip"), "existing-profile", report)
            self.assertIn('"Invalid"', report.read_text())
            command = run.call_args.args[0]
            self.assertIn("--keychain-profile", command)
            self.assertNotIn("--password", command)


if __name__ == "__main__":
    unittest.main()
