import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET

SPEC = importlib.util.spec_from_file_location("release_preview", Path(__file__).resolve().parents[1] / "scripts/release_preview.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
COMMIT = "a" * 40
RUN = "1234"


class PreviewPublicationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.inputs = self.root / "inputs"
        self.output = self.root / "output"
        self.jobs = self.root / "jobs.json"
        release.write(self.jobs, {"jobs": [{"name": f"build (runner, {target}, config)", "run_id": int(RUN),
                                           "status": "completed", "conclusion": "success"} for target in release.TARGETS]})

    def candidate(self, target="windows-x86_64"):
        platform, architecture, count = release.TARGETS[target]
        root = self.inputs / ("ToonPro-candidate-" + target)
        root.mkdir(parents=True)
        (root / "verification").mkdir()
        artifacts = []
        for extension in ([".zip", ".dmg"] if platform == "Darwin" else [".zip"]):
            file = root / f"ToonPro-0.75.0-{target}{extension}"
            file.write_bytes(b"test package")
            artifacts.append({"file": file.name, "bytes": file.stat().st_size, "sha256": release.digest(file)})
        package = {"schemaVersion": 2, "version": "0.75.0", "platform": platform, "architecture": architecture,
                   "runtimeVerified": True, "nativeRuntimeVerified": True, "publicReleaseReady": False,
                   "releaseChannel": "candidate", "signature": "ad-hoc" if platform == "Darwin" else "unsigned",
                   "verification": "verification/package-verification.json", "artifacts": artifacts,
                   "capabilities": {"version": "0.75.0", "configuration": "Release", "system": platform,
                                    "architecture": architecture, "minimumOS": "Test OS", "renderer3D": "unavailable"}}
        release.write(root / "package.json", package)
        (root / "SHA256SUMS").write_text("".join(f"{a['sha256']}  {a['file']}\n" for a in artifacts))
        report = {"success": True, "relocated": True, "sdkEnvironmentRemoved": True,
                  "nativeSmokeSkipped": False, "verificationHostArchitecture": "AMD64" if platform == "Windows" else architecture,
                  "verifiedProcessArchitectures": [architecture], "smokes": [
                      {"mode": mode, "exitCode": 0, "report": {"success": True, "checks": [{"passed": True}] * 20}}
                      for mode in ("native", "offscreen-2x")]}
        release.write(root / "verification/package-verification.json", report)
        sources = root / "dependency-sources" / f"ToonPro-0.75.0-{target}-dependency-sources.tar.gz"
        sources.parent.mkdir()
        sources.write_bytes(b"audited source bundle")
        sources.with_name(sources.name + ".sha256").write_text(f"{release.digest(sources)}  {sources.name}\n")
        tests = ET.Element("testsuite")
        names = [f"suite{i}" for i in range(count)]
        for name in names:
            ET.SubElement(tests, "testcase", name=name)
        test_path = self.root / f"{target}-tests.xml"
        ET.ElementTree(tests).write(test_path)
        expected = self.root / f"{target}-expected.json"
        release.write(expected, {"tests": [{"name": n} for n in names]})
        release.attest(root, target, COMMIT, RUN, test_path, expected)
        return root, test_path, expected

    def prepare(self):
        return release.prepare(self.inputs, self.output, COMMIT, RUN, "complex1/ToonPro", self.jobs)

    def test_successful_platforms_only_and_durable_links(self):
        self.candidate()
        self.candidate("macos-arm64")
        manifest = self.prepare()
        self.assertEqual({p["target"] for p in manifest["platforms"]}, {"windows-x86_64", "macos-arm64"})
        self.assertNotIn("macos-x86_64", (self.output / "RELEASE-NOTES.md").read_text())
        self.assertTrue(all("/releases/download/v0.75.0-preview.1234/" in a["url"] for p in manifest["platforms"] for a in p["artifacts"]))
        self.assertEqual(len(list(self.output.glob("*-dependency-sources.tar.gz"))), 2)

    def test_no_artifacts_cannot_publish(self):
        with self.assertRaisesRegex(ValueError, "No successful"):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_failed_platform_job_is_omitted_even_if_an_artifact_exists(self):
        self.candidate()
        self.candidate("macos-arm64")
        jobs = release.read(self.jobs)
        jobs["jobs"][0]["conclusion"] = "failure"
        release.write(self.jobs, jobs)
        self.assertEqual([p["target"] for p in self.prepare()["platforms"]], ["windows-x86_64"])

    def test_other_run_job_results_cannot_authorize_publication(self):
        self.candidate()
        jobs = release.read(self.jobs)
        jobs["jobs"][0]["run_id"] = 555
        release.write(self.jobs, jobs)
        with self.assertRaisesRegex(ValueError, "another workflow"):
            self.prepare()

    def test_tampered_archive_is_rejected_before_output(self):
        root, _, _ = self.candidate()
        next(root.glob("*.zip")).write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "integrity"):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_missing_sources_and_stale_commit_are_rejected(self):
        root, _, _ = self.candidate()
        evidence = release.read(root / "ci-verification.json")
        evidence["sourceCommit"] = "b" * 40
        release.write(root / "ci-verification.json", evidence)
        with self.assertRaisesRegex(ValueError, "Stale"):
            self.prepare()
        evidence["sourceCommit"] = COMMIT
        release.write(root / "ci-verification.json", evidence)
        next((root / "dependency-sources").glob("*.tar.gz")).unlink()
        with self.assertRaisesRegex(ValueError, "Missing"):
            self.prepare()

    def test_failed_or_partial_ctest_cannot_be_attested(self):
        root, tests, expected = self.candidate()
        tree = ET.parse(tests)
        ET.SubElement(tree.getroot()[0], "failure")
        tree.write(tests)
        with self.assertRaisesRegex(ValueError, "failed"):
            release.attest(root, "windows-x86_64", COMMIT, RUN, tests, expected)
        ET.ElementTree(ET.Element("testsuite")).write(tests)
        with self.assertRaisesRegex(ValueError, "Full configured"):
            release.attest(root, "windows-x86_64", COMMIT, RUN, tests, expected)

    def test_skipped_native_smoke_or_failed_check_is_rejected(self):
        root, _, _ = self.candidate()
        path = root / "verification/package-verification.json"
        report = release.read(path)
        report["smokes"][0]["report"]["checks"][0]["passed"] = False
        release.write(path, report)
        with self.assertRaisesRegex(ValueError, "runtime checks"):
            self.prepare()

    def test_wrong_host_architecture_cannot_claim_native_validation(self):
        root, _, _ = self.candidate("macos-x86_64")
        path = root / "verification/package-verification.json"
        report = release.read(path)
        report["verificationHostArchitecture"] = "arm64"
        release.write(path, report)
        with self.assertRaisesRegex(ValueError, "Native target hardware"):
            self.prepare()

    def test_unsupported_platform_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Windows/Mac"):
            release.verify_package(self.root, "linux-x86_64")

    def test_evidence_path_escape_is_rejected(self):
        root, _, _ = self.candidate()
        with self.assertRaisesRegex(ValueError, "unsafe"):
            release.regular_file(root, "../outside")

    def draft(self, *, complete=True):
        assets = [{"name": p.name, "state": "uploaded", "size": p.stat().st_size,
                   "digest": "sha256:" + release.digest(p)}
                  for p in self.output.iterdir() if p.name != "RELEASE-NOTES.md"] if complete else []
        return {"tag_name": "v0.75.0-preview.1234", "draft": True, "prerelease": True,
                "target_commitish": COMMIT, "assets": assets}

    def run_publisher(self):
        return release.publish(self.output, "complex1/ToonPro", COMMIT, RUN)

    def test_remote_hash_checked_before_publication(self):
        self.candidate()
        self.prepare()
        data = self.draft()
        data["assets"][0]["digest"] = "sha256:" + "0" * 64
        replies = ["4321", json.dumps(data), json.dumps(data)]
        with mock.patch.object(release.subprocess, "run", side_effect=[
                subprocess.CompletedProcess([], 0, out) for out in replies]) as run:
            with self.assertRaisesRegex(ValueError, "upload integrity"):
                self.run_publisher()
        self.assertFalse(any("PATCH" in call.args[0] for call in run.call_args_list))

    def test_existing_public_release_is_never_modified(self):
        self.candidate()
        self.prepare()
        public = self.draft()
        public["draft"] = False
        with mock.patch.object(release.subprocess, "run", side_effect=[
                subprocess.CompletedProcess([], 0, out) for out in ("4321", json.dumps(public))]) as run:
            with self.assertRaisesRegex(ValueError, "already public"):
                self.run_publisher()
        self.assertEqual(run.call_count, 2)

    def test_complete_verified_upload_is_published_last(self):
        self.candidate()
        self.prepare()
        # GitHub's tag lookup returns 404 for a draft with an unpublished tag.
        # Listing discovers the created ID and all verification uses that ID.
        replies = ["", "", "4321", json.dumps(self.draft(complete=False)), "",
                   json.dumps(self.draft()), "{}"]
        with mock.patch.object(release.subprocess, "run", side_effect=[
                subprocess.CompletedProcess([], 0, out) for out in replies]) as run:
            url = self.run_publisher()
        calls = [call.args[0] for call in run.call_args_list]
        self.assertTrue(url.endswith("/releases/tag/v0.75.0-preview.1234"))
        self.assertIn("--draft", calls[1])
        self.assertIn("upload", calls[4])
        self.assertEqual(calls[3], ["gh", "api", "repos/complex1/ToonPro/releases/4321"])
        self.assertEqual(calls[5], calls[3])
        self.assertIn("PATCH", calls[-1])
        self.assertIn("draft=false", calls[-1])
        self.assertIn("make_latest=false", calls[-1])

    def test_resume_existing_draft_uploads_only_missing_assets(self):
        self.candidate()
        self.prepare()
        partial = self.draft()
        missing = partial["assets"].pop()["name"]
        replies = ["4321", json.dumps(partial), "", json.dumps(self.draft()), "{}"]
        with mock.patch.object(release.subprocess, "run", side_effect=[
                subprocess.CompletedProcess([], 0, out) for out in replies]) as run:
            self.run_publisher()
        calls = [call.args[0] for call in run.call_args_list]
        self.assertEqual(calls[2], ["gh", "release", "upload", "v0.75.0-preview.1234",
                                     "--repo", "complex1/ToonPro", str(self.output/missing)])
        self.assertIn("PATCH", calls[-1])


if __name__ == "__main__":
    unittest.main()
