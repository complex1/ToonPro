#!/usr/bin/env python3
"""Verify native CI evidence and prepare immutable, explicitly preview downloads."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
from pathlib import Path
import xml.etree.ElementTree as ET

VERSION = "0.75.0"
TARGETS = {
    "macos-arm64": ("Darwin", "arm64", 247),
    "macos-x86_64": ("Darwin", "x86_64", 247),
    "windows-x86_64": ("Windows", "x86_64", 246),
}


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            result.update(block)
    return result.hexdigest()


def read(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def write(path: Path, value: dict) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def identity(commit: str, run_id: str) -> None:
    require(bool(re.fullmatch(r"[0-9a-f]{40}", commit)), "Expected an exact source commit")
    require(bool(re.fullmatch(r"[1-9][0-9]*", str(run_id))), "Invalid CI run identity")


def regular_file(root: Path, relative: str) -> Path:
    path = root / relative
    require(not path.is_symlink() and path.is_file() and path.resolve().is_relative_to(root.resolve()),
            f"Missing or unsafe artifact: {relative}")
    return path


def verify_package(root: Path, target: str) -> dict:
    require(target in TARGETS, "Only current Windows/Mac targets may be published")
    platform, architecture, _ = TARGETS[target]
    package = read(regular_file(root, "package.json"))
    require(package.get("schemaVersion") == 2 and package.get("version") == VERSION,
            "Unexpected package version/schema")
    require((package.get("platform"), package.get("architecture")) == (platform, architecture),
            "Package target mismatch")
    require(package.get("runtimeVerified") is True and package.get("nativeRuntimeVerified") is True,
            "Both relocated and native runtime verification are required")
    require(package.get("releaseChannel") == "candidate" and package.get("publicReleaseReady") is False,
            "This publication is for explicit test candidates only")
    require(package.get("signature") == ("ad-hoc" if platform == "Darwin" else "unsigned"),
            "Unexpected signing status: update preview notes before publishing")
    capabilities = package.get("capabilities", {})
    require(capabilities.get("version") == VERSION and capabilities.get("configuration") == "Release"
            and capabilities.get("system") == platform and capabilities.get("architecture") == architecture,
            "Release capabilities do not match the package")
    report = read(regular_file(root, package["verification"]))
    require(all(report.get(key) is True for key in ("success", "relocated", "sdkEnvironmentRemoved")),
            "Relocated runtime checks failed")
    host_architecture = {"AMD64": "x86_64"}.get(report.get("verificationHostArchitecture"), report.get("verificationHostArchitecture"))
    require(report.get("nativeSmokeSkipped") is False and host_architecture == architecture,
            "Native target hardware verification is required")
    require(architecture in report.get("verifiedProcessArchitectures", []), "Wrong executable architecture")
    smokes = report.get("smokes", [])
    require({"offscreen-2x", "native"}.issubset({s.get("mode") for s in smokes}), "Missing runtime smoke mode")
    for smoke in smokes:
        checks = smoke.get("report", {}).get("checks", [])
        require(smoke.get("exitCode") == 0 and smoke.get("report", {}).get("success") is True
                and len(checks) >= 20 and all(c.get("passed") is True for c in checks),
                "Packaged runtime checks are incomplete or failed")
    artifacts = package.get("artifacts", [])
    suffixes = {".zip", ".dmg"} if platform == "Darwin" else {".zip"}
    expected_names = {f"ToonPro-{VERSION}-{target}{suffix}" for suffix in suffixes}
    require({a.get("file") for a in artifacts} == expected_names and len(artifacts) == len(expected_names),
            "Missing or unexpected distribution archives")
    sums = regular_file(root, "SHA256SUMS").read_text(encoding="utf-8").splitlines()
    for artifact in artifacts:
        path = regular_file(root, artifact["file"])
        require(path.stat().st_size == artifact["bytes"] and digest(path) == artifact["sha256"],
                f"Archive integrity failed: {path.name}")
        require(f"{artifact['sha256']}  {path.name}" in sums, "Archive checksum file disagrees")
    return package


def attest(root: Path, target: str, commit: str, run_id: str, tests: Path, expected: Path) -> dict:
    identity(commit, run_id)
    verify_package(root, target)
    expected_names = [test["name"] for test in read(expected)["tests"]]
    cases = ET.parse(tests).getroot().findall(".//testcase")
    names = [case.attrib["name"] for case in cases]
    require(len(names) >= TARGETS[target][2] and len(set(names)) == len(names)
            and set(names) == set(expected_names), "Full configured CTest suite did not run")
    require(all(case.find("failure") is None and case.find("error") is None
                and case.find("skipped") is None for case in cases), "A native CTest suite failed or was skipped")
    tests_copy = root / "full-tests.xml"
    shutil.copy2(tests, tests_copy)
    source_name = f"ToonPro-{VERSION}-{target}-dependency-sources.tar.gz"
    sources = regular_file(root, "dependency-sources/" + source_name)
    source_sum = regular_file(root, "dependency-sources/" + source_name + ".sha256")
    require(source_sum.read_text(encoding="utf-8").strip() == f"{digest(sources)}  {source_name}",
            "Dependency-source archive checksum disagrees")
    files = ["package.json", "SHA256SUMS", "full-tests.xml", "verification/package-verification.json",
             "dependency-sources/" + source_name]
    files += [a["file"] for a in read(root / "package.json")["artifacts"]]
    evidence = {"schemaVersion": 1, "version": VERSION, "target": target,
                "sourceCommit": commit, "runId": str(run_id), "workflow": ".github/workflows/build.yml",
                "fullSuiteCount": len(names), "nativeTestsPassed": True,
                "packagingUnitTestsPassed": True, "dependencySourcesVerified": True,
                "files": [{"file": name, "sha256": digest(regular_file(root, name))} for name in files]}
    write(root / "ci-verification.json", evidence)
    return evidence


def prepare(inputs: Path, output: Path, commit: str, run_id: str, repository: str, jobs: Path) -> dict:
    identity(commit, run_id)
    require(repository == "complex1/ToonPro", "Unexpected publication repository")
    require(not output.exists(), "Use a fresh output directory; published assets are never overwritten")
    candidates = sorted(inputs.glob("ToonPro-candidate-*"))
    require(bool(candidates), "No successful native candidates were uploaded; nothing can be published")
    native_jobs = [job for job in read(jobs).get("jobs", []) if job.get("name", "").startswith("build (")]
    require(bool(native_jobs) and all(str(job.get("run_id")) == str(run_id) for job in native_jobs),
            "Native job results are missing or belong to another workflow run")
    verified = []
    for root in candidates:
        target = root.name.removeprefix("ToonPro-candidate-")
        require(target in TARGETS, "Only current Windows/Mac targets may be published")
        matching = [job for job in native_jobs if re.search(r"\b" + re.escape(target) + r"\b", job["name"])]
        require(len(matching) == 1, "Expected exactly one native job for each candidate")
        if matching[0].get("status") != "completed" or matching[0].get("conclusion") != "success":
            continue
        package = verify_package(root, target)
        evidence = read(regular_file(root, "ci-verification.json"))
        require(evidence.get("schemaVersion") == 1 and evidence.get("sourceCommit") == commit
                and evidence.get("runId") == str(run_id) and evidence.get("target") == target
                and evidence.get("workflow") == ".github/workflows/build.yml", "Stale or unrelated CI evidence")
        require(all(evidence.get(key) is True for key in
                    ("nativeTestsPassed", "packagingUnitTestsPassed", "dependencySourcesVerified"))
                and evidence.get("fullSuiteCount", 0) >= TARGETS[target][2], "Incomplete native CI evidence")
        source_name = f"ToonPro-{VERSION}-{target}-dependency-sources.tar.gz"
        required = {"package.json", "SHA256SUMS", "full-tests.xml", "verification/package-verification.json",
                    "dependency-sources/" + source_name, *(a["file"] for a in package["artifacts"])}
        require({f.get("file") for f in evidence.get("files", [])} == required, "Evidence omits required release files")
        for item in evidence["files"]:
            require(digest(regular_file(root, item["file"])) == item["sha256"], "Candidate changed after verification")
        verified.append((root, target, package, source_name))
    require(bool(verified), "No native platform job succeeded; nothing can be published")
    output.mkdir(parents=True)
    tag = f"v{VERSION}-preview.{run_id}"
    platforms = []
    for root, target, package, source_name in verified:
        for artifact in package["artifacts"]:
            shutil.copy2(root / artifact["file"], output / artifact["file"])
        shutil.copy2(root / "dependency-sources" / source_name, output / source_name)
        for source, suffix in (("package.json", "package.json"), ("ci-verification.json", "ci.json"),
                               ("full-tests.xml", "tests.xml"), ("verification/package-verification.json", "verification.json")):
            shutil.copy2(root / source, output / f"ToonPro-{VERSION}-{target}-{suffix}")
        platforms.append({"target": target, "minimumOS": package["capabilities"].get("minimumOS"),
                          "renderer3D": package["capabilities"].get("renderer3D"),
                          "signature": package["signature"], "notarized": package.get("notarized", False),
                          "artifacts": [{**a, "url": f"https://github.com/{repository}/releases/download/{tag}/{a['file']}"}
                                        for a in package["artifacts"]], "dependencySources": source_name})
    manifest = {"schemaVersion": 1, "version": VERSION, "channel": "preview", "tag": tag,
                "sourceCommit": commit, "runId": str(run_id), "platforms": platforms,
                "productionReady": False, "fullNativeTestsAndRelocatedRuntimePassed": True}
    write(output / "release-manifest.json", manifest)
    assets = sorted(output.iterdir())
    (output / "SHA256SUMS").write_text("".join(f"{digest(p)}  {p.name}\n" for p in assets), encoding="utf-8")
    lines = [f"Toon Pro {VERSION} — public test preview", "", "These downloads passed the full native test suite and relocated runtime checks for the listed platform.",
             "Windows builds are unsigned. Mac builds are ad-hoc signed and are not notarized. This is an early test build; physical Huion and clean-machine acceptance remain open.", "",
             "Only verified platforms are included. Linux is deferred. 3D rendering is currently available only on Apple Silicon Mac.", ""]
    for platform in platforms:
        lines.append(f"- **{platform['target']}** ({platform['minimumOS']}): " + ", ".join(f"[{a['file']}]({a['url']})" for a in platform["artifacts"]))
    lines += ["", "Matching dependency-source bundles, verification reports and SHA-256 checksums are attached.",
              f"Source commit: `{commit}`. [Native CI evidence](https://github.com/{repository}/actions/runs/{run_id}).", ""]
    (output / "RELEASE-NOTES.md").write_text("\n".join(lines), encoding="utf-8")
    return manifest


def publish(output: Path, repository: str, commit: str, run_id: str) -> str:
    identity(commit, run_id)
    require(repository == "complex1/ToonPro", "Unexpected publication repository")
    manifest = read(output / "release-manifest.json")
    tag = f"v{VERSION}-preview.{run_id}"
    require(manifest.get("tag") == tag and manifest.get("sourceCommit") == commit
            and manifest.get("runId") == str(run_id) and manifest.get("platforms")
            and manifest.get("channel") == "preview" and manifest.get("productionReady") is False,
            "Prepared publication identity mismatch")
    assets = sorted(p for p in output.iterdir() if p.name != "RELEASE-NOTES.md")
    require(all(p.is_file() and not p.is_symlink() for p in assets), "Unsafe publication asset")
    sums = (output / "SHA256SUMS").read_text(encoding="utf-8").splitlines()
    require(set(sums) == {f"{digest(p)}  {p.name}" for p in assets if p.name != "SHA256SUMS"},
            "Prepared assets changed before upload")

    def gh(*args: str, check: bool = True) -> subprocess.CompletedProcess:
        return subprocess.run(["gh", *args], check=check, capture_output=True, text=True)

    endpoint = f"repos/{repository}/releases/tags/{tag}"
    existing = gh("api", endpoint, check=False)
    if existing.returncode == 0:
        release = json.loads(existing.stdout)
        require(release.get("draft") is True and release.get("prerelease") is True
                and release.get("target_commitish") == commit,
                "A different or already public release exists; it will not be changed")
    else:
        gh("release", "create", tag, "--repo", repository, "--target", commit,
           "--draft", "--prerelease", "--latest=false", "--title", f"Toon Pro {VERSION} preview ({run_id})",
           "--notes-file", str(output / "RELEASE-NOTES.md"))
    # Retry only the same private draft. Public releases/assets are immutable.
    gh("release", "upload", tag, "--repo", repository, "--clobber", *(str(p) for p in assets))
    release = json.loads(gh("api", endpoint).stdout)
    require(release.get("draft") is True and release.get("target_commitish") == commit,
            "Draft changed during upload")
    uploaded = release.get("assets", [])
    require({a.get("name") for a in uploaded} == {p.name for p in assets}
            and len(uploaded) == len(assets), "Remote release asset set is incomplete")
    for asset in uploaded:
        local = output / asset["name"]
        require(asset.get("state") == "uploaded" and asset.get("size") == local.stat().st_size
                and asset.get("digest") == "sha256:" + digest(local), "Remote upload integrity failed")
    gh("release", "edit", tag, "--repo", repository, "--draft=false", "--prerelease", "--latest=false")
    return f"https://github.com/{repository}/releases/tag/{tag}"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("attest")
    check.add_argument("--package", type=Path, required=True)
    check.add_argument("--target", required=True, choices=TARGETS)
    check.add_argument("--tests", type=Path, required=True)
    check.add_argument("--expected", type=Path, required=True)
    bundle = commands.add_parser("prepare")
    bundle.add_argument("--inputs", type=Path, required=True)
    bundle.add_argument("--output", type=Path, required=True)
    bundle.add_argument("--repository", required=True)
    bundle.add_argument("--jobs", type=Path, required=True)
    release = commands.add_parser("publish")
    release.add_argument("--output", type=Path, required=True)
    release.add_argument("--repository", required=True)
    for command in (check, bundle, release):
        command.add_argument("--commit", required=True)
        command.add_argument("--run-id", required=True)
    args = parser.parse_args()
    if args.command == "attest":
        print(json.dumps(attest(args.package, args.target, args.commit, args.run_id, args.tests, args.expected)))
    elif args.command == "prepare":
        print(json.dumps(prepare(args.inputs, args.output, args.commit, args.run_id, args.repository, args.jobs)))
    else:
        print(publish(args.output, args.repository, args.commit, args.run_id))


if __name__ == "__main__":
    main()
