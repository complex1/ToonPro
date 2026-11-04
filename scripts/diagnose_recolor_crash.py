#!/usr/bin/env python3
"""Capture a failed hosted-Mac recolor test without changing its release result.

The caller must keep the original CTest failure gate. A successful debugger
rerun is diagnostic evidence, never permission to package a failed candidate.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import time


SUITE = "character_recolor_workspace"
BINARY = "toon_character_recolor_workspace_tests"


def failed_recolor(text: str) -> bool:
    """Accept only CTest's exact number:name failure records."""
    return bool(re.search(r"^\s*\d+:" + SUITE + r"\s*$", text, re.MULTILINE))


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(chunk)
    return value.hexdigest()


def debugger_command(debugger: str, binary: Path) -> list[str]:
    return [debugger, "--no-lldbinit", "--batch",
            "--one-line-on-crash", "thread backtrace all",
            "--one-line-on-crash", "image list -o -f",
            "--one-line", "run", "--", str(binary),
            "-platform", "offscreen", "-nocrashhandler"]


def kill_debugger_tree(pid: int) -> None:
    # A debugger can put its inferior in a different process group. Capture
    # descendants before killing LLDB so those children cannot be left behind.
    descendants = []
    try:
        listing = subprocess.check_output(["ps", "-axo", "pid=,ppid="], text=True, timeout=2)
        pairs = [tuple(map(int, row.split())) for row in listing.splitlines() if len(row.split()) == 2]
        parents = {pid}
        while children := {child for child, parent in pairs if parent in parents} - parents:
            descendants.extend(children)
            parents.update(children)
    except (OSError, ValueError, subprocess.SubprocessError):
        pass
    for child in reversed(descendants):
        try:
            os.kill(child, signal.SIGKILL)
        except ProcessLookupError:
            pass
    try:
        os.killpg(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass


def run_bounded(command: list[str], output: Path, cwd: Path, timeout: float = 60) -> dict:
    """Terminate the debugger and its owned target together at the deadline."""
    started = time.monotonic()
    environment = dict(os.environ, QT_QPA_OFFSCREEN_NO_GLX="1")
    with output.open("wb") as log:
        process = subprocess.Popen(command, cwd=cwd, env=environment, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        timed_out = False
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            # LLDB may still be waiting in a modal; kill its process group so
            # neither it nor the test continues after this diagnostic step.
            kill_debugger_tree(process.pid)
            process.wait(timeout=5)
        return {"exitCode": process.returncode, "timedOut": timed_out,
                "elapsedSeconds": round(time.monotonic() - started, 3)}


def retain_crash_reports(directory: Path, destination: Path, since: float) -> list[str]:
    """Keep recent reports for this test only, never other apps' crash data."""
    candidates = []
    for folder in (directory, directory / "Retired"):
        if not folder.is_dir():
            continue
        for path in folder.iterdir():
            if (path.is_file() and not path.is_symlink()
                    and path.name.startswith(BINARY + "-")
                    and path.suffix in {".ips", ".crash"}
                    and path.stat().st_mtime >= since):
                candidates.append(path)
    copied = []
    for path in sorted(candidates, key=lambda p: p.stat().st_mtime, reverse=True)[:16]:
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination / path.name)
        copied.append(path.name)
    return copied


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--report-dir", type=Path)
    parser.add_argument("--failure-log", type=Path)
    args = parser.parse_args(argv)
    build = args.build_dir.resolve()
    report = (args.report_dir or build / "test-artifacts/recolor-diagnostics").resolve()
    failure = (args.failure_log or build / "Testing/Temporary/LastTestsFailed.log").resolve()
    report.mkdir(parents=True, exist_ok=True)
    summary = {"suite": SUITE, "originalTestFailureRecorded": False,
               "diagnosticDoesNotOverrideFailure": True, "timeoutSeconds": 60}
    code = 0
    try:
        text = failure.read_text(errors="replace") if failure.is_file() else ""
        if not failed_recolor(text):
            summary.update(status="skipped", reason="No matching CTest failure record.")
        else:
            summary["originalTestFailureRecorded"] = True
            shutil.copy2(failure, report / "original-ctest-failures.log")
            since = failure.stat().st_mtime - 600
            if platform.system() != "Darwin":
                raise RuntimeError("This diagnostic requires the hosted macOS test environment.")
            crashes = Path.home() / "Library/Logs/DiagnosticReports"
            summary["crashReports"] = retain_crash_reports(crashes, report / "crash-reports", since)
            binary = build / BINARY
            if not binary.is_file():
                raise RuntimeError(f"The failing test executable is missing: {binary}")
            saved = report / BINARY
            shutil.copy2(binary, saved)
            summary["binary"] = {"file": saved.name, "bytes": saved.stat().st_size,
                                 "sha256": digest(saved)}
            if digest(binary) != summary["binary"]["sha256"]:
                raise RuntimeError("The test executable changed while it was being retained.")
            symbols = build / (BINARY + ".dSYM")
            if symbols.is_dir():
                shutil.copytree(symbols, report / symbols.name, dirs_exist_ok=True)
                summary["symbols"] = symbols.name
            debugger = shutil.which("lldb")
            if not debugger:
                raise RuntimeError("LLDB is unavailable; the original binary has been retained.")
            command = debugger_command(debugger, binary)
            summary["command"] = command
            try:
                summary["debugger"] = run_bounded(command, report / "lldb.log", build)
                summary["status"] = "captured"
            finally:
                summary["crashReports"] = retain_crash_reports(crashes, report / "crash-reports", since)
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        summary.update(status="capture-failed", error=str(error))
        code = 1
    (report / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"Recolor diagnostics: {summary['status']} ({report})")
    return code


if __name__ == "__main__":
    raise SystemExit(main())
