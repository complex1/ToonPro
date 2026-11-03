"""Unit checks for packaging boundaries; native runtime smokes run separately."""
import importlib.util
import hashlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import package_desktop as package
import verify_package as verify
import collect_dependency_sources as sources


def elf(path, machine=62):
    path.parent.mkdir(parents=True, exist_ok=True)
    value = bytearray(64)
    value[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<H", value, 18, machine)
    path.write_bytes(value)


def pe_runtime(path, imports=(), delayed=()):
    path.parent.mkdir(parents=True, exist_ok=True)
    data = bytearray(2048)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, 0x8664, 1, 0, 0, 0, 240, 0x22)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<Q", data, optional + 24, 0x140000000)
    struct.pack_into("<I", data, optional + 60, 512)
    struct.pack_into("<I", data, optional + 108, 16)
    section = optional + 240
    data[section:section + 8] = b".rdata\0\0"
    struct.pack_into("<IIII", data, section + 8, 1536, 0x1000, 1536, 512)
    cursor = 0x400
    for directory, names, offset, entry_size, name_offset in ((1, imports, 0x200, 20, 12), (13, delayed, 0x300, 32, 4)):
        if not names: continue
        struct.pack_into("<II", data, optional + 112 + directory * 8, offset + 0xE00, entry_size * (len(names) + 1))
        for index, name in enumerate(names):
            entry = offset + index * entry_size
            if directory == 13: struct.pack_into("<I", data, entry, 1)
            struct.pack_into("<I", data, entry + name_offset, cursor + 0xE00)
            encoded = name.encode("ascii") + b"\0"
            data[cursor:cursor + len(encoded)] = encoded
            cursor += len(encoded)
    path.write_bytes(data)


class PackageScriptTests(unittest.TestCase):
    def source_archive(self):
        stream = io.BytesIO()
        with tarfile.open(fileobj=stream, mode="w:gz") as archive:
            for index in range(20):
                data = b"source fixture\n"
                info = tarfile.TarInfo(f"fixture-revision/source-{index}.cpp")
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        data = stream.getvalue()
        return data, hashlib.sha256(data).hexdigest()

    def source_response(self, data):
        response = io.BytesIO(data)
        response.geturl = lambda: "https://upstream.example/fixture.tar.gz"
        return response

    def test_source_download_closes_handle_before_validation_and_publication(self):
        data, expected = self.source_archive()
        with tempfile.TemporaryDirectory() as temporary:
            cache = Path(temporary)
            handles = []
            factory, validate = tempfile.NamedTemporaryFile, sources.validate_archive
            def create_file(*args, **kwargs):
                handle = factory(*args, **kwargs)
                handles.append(handle)
                return handle
            def validate_closed(*args):
                self.assertTrue(handles and all(handle.closed for handle in handles))
                return validate(*args)
            with mock.patch.object(sources.urllib.request, "urlopen", return_value=self.source_response(data)) as download, \
                 mock.patch.object(sources.tempfile, "NamedTemporaryFile", side_effect=create_file), \
                 mock.patch.object(sources, "validate_archive", side_effect=validate_closed):
                path, url = sources.download_source(cache, "fixture", "revision", expected)
                self.assertEqual(path.read_bytes(), data)
                self.assertTrue(url.startswith("https://"))
                self.assertEqual(list(cache.iterdir()), [path])
                self.assertEqual(sources.download_source(cache, "fixture", "revision", expected)[0], path)
                download.assert_called_once()

    def test_source_download_failures_clean_temporary_files(self):
        data, expected = self.source_archive()
        for failure in ("checksum", "read", "size"):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as temporary:
                cache = Path(temporary)
                response = self.source_response(data)
                if failure == "read":
                    response.read = mock.Mock(side_effect=OSError("download interrupted"))
                with mock.patch.object(sources.urllib.request, "urlopen", return_value=response), \
                     mock.patch.object(sources, "MAX_ARCHIVE_BYTES", 4 if failure == "size" else sources.MAX_ARCHIVE_BYTES):
                    with self.assertRaises((RuntimeError, OSError)):
                        sources.download_source(cache, "fixture", "revision", "0" * 64 if failure == "checksum" else expected)
                self.assertEqual(list(cache.iterdir()), [])

    def test_source_download_never_replaces_existing_or_competing_cache_file(self):
        data, expected = self.source_archive()
        for competing in (False, True):
            with self.subTest(competing=competing), tempfile.TemporaryDirectory() as temporary:
                cache = Path(temporary)
                destination = cache / "fixture-revision.tar.gz"
                def download(*args, **kwargs):
                    destination.write_bytes(b"preserve competing cache file")
                    return self.source_response(data)
                if not competing:
                    destination.write_bytes(b"preserve existing cache file")
                with mock.patch.object(sources.urllib.request, "urlopen", side_effect=download) as request:
                    with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                        sources.download_source(cache, "fixture", "revision", expected)
                self.assertEqual(destination.read_bytes(), b"preserve competing cache file" if competing else b"preserve existing cache file")
                self.assertEqual(request.call_count, int(competing))
                self.assertEqual(list(cache.iterdir()), [destination])

    def test_linux_codec_metadata_keeps_build_provenance_and_final_hashes_separate(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            (stage / "lib").mkdir()
            codec = stage / "lib/libavcodec.so.61.19.101"
            codec.write_bytes(b"original codec build")
            provenance = {"libraries": {codec.name: package.digest(codec)}, "qtEndToEndVerified": False}
            original = json.dumps(provenance, indent=2)
            evidence = stage / "third-party/ffmpeg-runtime/sources.json"
            evidence.parent.mkdir(parents=True); evidence.write_text(original)
            codec.write_bytes(b"deployed codec with rewritten RPATH")
            (stage / "lib/libQt6Core.so.6").write_bytes(b"unrelated runtime")
            if os.name != "nt": (stage / "lib/libavcodec.so.61").symlink_to(codec.name)
            checks = [{"name": name, "passed": True} for name in
                      ["wav-export", "h264-aac-export", "h264-decode-pixels", "aac-decode-samples"]]
            report = {"success": True, "smokes": [{"mode": "offscreen-2x", "exitCode": 0,
                       "report": {"success": True, "checks": checks}}]}
            result = package.linux_ffmpeg_runtime_metadata(stage, provenance, report)
            self.assertEqual(len(result["deployedLibraries"]), 1)
            deployed = result["deployedLibraries"][0]
            self.assertEqual(deployed["file"], "lib/" + codec.name)
            self.assertEqual(deployed["sha256"], package.digest(codec))
            self.assertEqual(deployed["sourceBuildSha256"], provenance["libraries"][codec.name])
            self.assertNotEqual(deployed["sha256"], deployed["sourceBuildSha256"])
            self.assertEqual(evidence.read_text(), original)
            self.assertFalse(result["provenance"]["qtEndToEndVerified"])
            self.assertEqual(result["provenanceFileSha256"], package.digest(evidence))
            self.assertTrue(result["packagedMediaVerification"]["success"])
            self.assertEqual(result["packagedMediaVerification"]["runs"][0]["mode"], "offscreen-2x")

    def test_packaged_media_metadata_requires_every_actual_check_in_every_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary); (stage / "lib").mkdir()
            checks = [{"name": name, "passed": True} for name in
                      ["wav-export", "h264-aac-export", "h264-decode-pixels", "aac-decode-samples"]]
            valid = {"success": True, "smokes": [{"mode": "native", "exitCode": 0,
                      "report": {"success": True, "checks": checks}}]}
            cases = []
            for case in ["missing", "failed", "duplicate", "exit", "report", "overall", "empty"]:
                data = json.loads(json.dumps(valid))
                if case == "missing": data["smokes"][0]["report"]["checks"].pop()
                elif case == "failed": data["smokes"][0]["report"]["checks"][1]["passed"] = False
                elif case == "duplicate": data["smokes"][0]["report"]["checks"].append(checks[1])
                elif case == "exit": data["smokes"][0]["exitCode"] = 1
                elif case == "report": data["smokes"][0]["report"]["success"] = False
                elif case == "overall": data["success"] = False
                else: data["smokes"] = []
                cases.append((case, data))
            for name, report in cases:
                with self.subTest(name=name):
                    result = package.linux_ffmpeg_runtime_metadata(stage, None, report)
                    self.assertFalse(result["packagedMediaVerification"]["success"])

    def test_source_collection_detects_and_checks_optional_qtdeclarative_commit(self):
        with tempfile.TemporaryDirectory() as temporary:
            sdk = Path(temporary)
            (sdk / "sbom").mkdir()
            for name, revision, _ in sources.SOURCES[:3] + sources.OPTIONAL_SOURCES:
                packages = [{"name": name, "downloadLocation": f"git://code.qt.io/qt/{name}.git@{revision}"}]
                if name == "qtmultimedia":
                    packages.append({"name": "FFmpeg", "versionInfo": "7.1.3",
                        "downloadLocation": "https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n7.1.3.tar.gz"})
                (sdk / "sbom" / f"{name}-6.10.3.spdx.json").write_text(json.dumps({"packages": packages}))
            self.assertEqual([p["name"] for _, p in sources.read_sdk_manifests(sdk)],
                             ["qtbase", "qtsvg", "qtmultimedia", "qtdeclarative"])
            optional = sdk / "sbom/qtdeclarative-6.10.3.spdx.json"
            optional.write_text(optional.read_text().replace(sources.OPTIONAL_SOURCES[0][1], "different-commit"))
            with self.assertRaisesRegex(RuntimeError, "does not match audited revision"):
                sources.read_sdk_manifests(sdk)
            optional.unlink()
            self.assertEqual(len(sources.read_sdk_manifests(sdk)), 3)

    def test_source_verification_preserves_legacy_bundle_support(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            notes = folder / "REBUILDING.txt"
            notes.write_text("Existing source bundle contents remain unchanged.")
            manifest = {"schemaVersion": 1,
                "files": [{"file": notes.name, "sha256": sources.digest(notes)}],
                "sources": [{"name": name, "revision": revision, "sha256": sha} for name, revision, sha in sources.SOURCES]}
            path = folder / "SOURCE-MANIFEST.json"
            path.write_text(json.dumps(manifest))
            with mock.patch.object(sources, "validate_archive") as validate:
                sources.verify_bundle(folder)
                self.assertEqual(validate.call_count, 6)
                name, revision, sha = sources.OPTIONAL_SOURCES[0]
                manifest["sources"].append({"name": name, "revision": revision, "sha256": sha})
                path.write_text(json.dumps(manifest))
                validate.reset_mock()
                sources.verify_bundle(folder)
                self.assertEqual(validate.call_count, 7)
                manifest["sources"][-1]["sha256"] = "unreviewed"
                path.write_text(json.dumps(manifest))
                with self.assertRaisesRegex(RuntimeError, "not an audited revision"):
                    sources.verify_bundle(folder)

    def test_pe_imports_include_delayed_crt_dependencies(self):
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / "app.exe"
            pe_runtime(binary, ["KERNEL32.dll", "VCRUNTIME140.dll"], ["MSVCP140_ATOMIC_WAIT.dll"])
            self.assertEqual(verify.pe_imports(binary), ["kernel32.dll", "msvcp140_atomic_wait.dll", "vcruntime140.dll"])
            binary.write_bytes(binary.read_bytes()[:550])
            with self.assertRaisesRegex(RuntimeError, "Truncated PE"):
                verify.pe_imports(binary)

    def test_windows_requires_app_local_crt_including_delayed_variants(self):
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            pe_runtime(stage / "bin/ToonPro.exe", ["VCRUNTIME140.dll"], ["msvcp140_atomic_wait.dll"])
            with self.assertRaisesRegex(RuntimeError, "App-local MSVC runtime is missing"):
                verify.inspect_windows(stage)
            for name in ["vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll"]:
                pe_runtime(stage / "bin" / name)
            with self.assertRaisesRegex(RuntimeError, "msvcp140_atomic_wait.dll.*not bundled"):
                verify.inspect_windows(stage)
            pe_runtime(stage / "bin/msvcp140_atomic_wait.dll")
            result = verify.inspect_windows(stage)
            self.assertEqual(len(result), 5)
            self.assertEqual(result[0]["file"], "bin/ToonPro.exe")

    @unittest.skipIf(os.name == "nt", "Xvfb uses POSIX ready descriptors")
    def test_virtual_display_uses_ready_descriptor_and_cleans_up_its_process(self):
        with tempfile.TemporaryDirectory() as temporary:
            process = mock.Mock()
            process.poll.return_value = None
            def start(command, **kwargs):
                self.assertNotIn("xvfb-run", command)
                fd = int(command[command.index("-displayfd") + 1])
                self.assertEqual(kwargs["pass_fds"], (fd,))
                os.write(fd, b"103\n")
                return process
            with mock.patch.object(verify.shutil, "which", return_value="/usr/bin/Xvfb"), \
                 mock.patch.object(verify.subprocess, "Popen", side_effect=start), \
                 mock.patch.object(verify.socket, "socket") as connection:
                with verify.virtual_x_display(Path(temporary) / "xvfb.log") as display:
                    self.assertEqual(display, ":103")
                    connection.return_value.__enter__.return_value.connect.assert_called_once_with("/tmp/.X11-unix/X103")
                process.terminate.assert_called_once()
                process.wait.assert_called_once_with(timeout=5)

    @unittest.skipIf(os.name == "nt", "Xvfb uses POSIX ready descriptors")
    def test_virtual_display_rejects_bad_ready_response_and_cleans_up(self):
        with tempfile.TemporaryDirectory() as temporary:
            process = mock.Mock()
            process.poll.return_value = None
            def start(command, **kwargs):
                os.write(kwargs["pass_fds"][0], b"invalid\n")
                return process
            with mock.patch.object(verify.shutil, "which", return_value="Xvfb"), \
                 mock.patch.object(verify.subprocess, "Popen", side_effect=start):
                with self.assertRaisesRegex(RuntimeError, "invalid display"):
                    with verify.virtual_x_display(Path(temporary) / "xvfb.log"): pass
                process.terminate.assert_called_once()

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

    def test_windows_offscreen_uses_os_fonts_and_removes_sdk_overrides(self):
        for key in ("SystemRoot", "SYSTEMROOT", "systemroot"):
            with self.subTest(key=key), mock.patch.object(verify.platform, "system", return_value="Windows"), \
                 mock.patch.dict(os.environ, {key: r"D:\OperatingSystem", "QT_QPA_FONTDIR": r"E:\Qt\fonts",
                     "QT_PLUGIN_PATH": r"E:\Qt\plugins", "QT_QPA_PLATFORM": "minimal",
                     "PATH": r"E:\Qt\bin", "TOON_UNRELATED": "preserved"}, clear=True):
                environment = verify.clean_environment(True, True)
            self.assertEqual(environment["QT_QPA_FONTDIR"], str(Path(r"D:\OperatingSystem") / "Fonts"))
            self.assertEqual(environment["PATH"], os.pathsep.join([
                str(Path(r"D:\OperatingSystem") / "System32"), r"D:\OperatingSystem"]))
            self.assertEqual(environment["QT_QPA_PLATFORM"], "offscreen")
            self.assertEqual(environment["QT_SCALE_FACTOR"], "2")
            self.assertNotIn("QT_PLUGIN_PATH", environment)
            self.assertNotIn(r"E:\Qt", "\n".join(environment.values()))
            self.assertEqual(environment["TOON_UNRELATED"], "preserved")

    def test_windows_native_does_not_inherit_offscreen_or_font_overrides(self):
        with mock.patch.object(verify.platform, "system", return_value="Windows"), \
             mock.patch.dict(os.environ, {"SYSTEMROOT": r"D:\OperatingSystem",
                 "QT_QPA_PLATFORM": "offscreen", "QT_QPA_FONTDIR": r"E:\Qt\fonts",
                 "QT_SCALE_FACTOR": "4"}, clear=True):
            environment = verify.clean_environment(False, False)
        self.assertNotIn("QT_QPA_FONTDIR", environment)
        self.assertNotIn("QT_QPA_PLATFORM", environment)
        self.assertNotIn("QT_SCALE_FACTOR", environment)
        self.assertEqual(environment["QT_MEDIA_BACKEND"], "ffmpeg")

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
