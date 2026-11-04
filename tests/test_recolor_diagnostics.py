"""The crash collector must stay selective and cannot hang a release job."""
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("recolor_diagnostics", ROOT / "scripts/diagnose_recolor_crash.py")
diagnostics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(diagnostics)


class RecolorDiagnosticsTests(unittest.TestCase):
    def test_exact_failure_record_only(self):
        self.assertTrue(diagnostics.failed_recolor("17:layers\n216:character_recolor_workspace\n"))
        for text in ("216:character_recolor_workspace_extra", "215:character_recolor_dialog",
                     "character_recolor_workspace passed", ""):
            self.assertFalse(diagnostics.failed_recolor(text))

    def test_success_or_unrelated_failure_never_launches_debugger(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            failure = root / "failures.log"
            failure.write_text("4:unrelated\n")
            with mock.patch.object(diagnostics, "run_bounded") as run:
                self.assertEqual(diagnostics.main(["--build-dir", str(root), "--failure-log", str(failure)]), 0)
                run.assert_not_called()
            result = json.loads((root / "test-artifacts/recolor-diagnostics/summary.json").read_text())
            self.assertEqual(result["status"], "skipped")
            self.assertTrue(result["diagnosticDoesNotOverrideFailure"])

    def test_debugger_disables_qt_handler_and_retains_crash_trace(self):
        command = diagnostics.debugger_command("lldb", Path("/build/test"))
        self.assertIn("--no-lldbinit", command)
        self.assertIn("-nocrashhandler", command)
        self.assertIn("thread backtrace all", command)
        self.assertEqual(command[command.index("--") + 1], "/build/test")

    def test_debugger_success_cannot_erase_original_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            binary = root / diagnostics.BINARY
            binary.write_bytes(b"exact failing executable")
            symbols = root / (diagnostics.BINARY + ".dSYM")
            symbols.mkdir()
            (symbols / "symbols").write_bytes(b"exact symbols")
            failure = root / "failures.log"
            failure.write_text("216:character_recolor_workspace\n")
            with (mock.patch.object(diagnostics.platform, "system", return_value="Darwin"),
                  mock.patch.object(diagnostics.shutil, "which", return_value="lldb"),
                  mock.patch.object(diagnostics, "retain_crash_reports", return_value=[]),
                  mock.patch.object(diagnostics, "run_bounded", return_value={"exitCode": 0, "timedOut": False})):
                self.assertEqual(diagnostics.main(["--build-dir", str(root), "--failure-log", str(failure)]), 0)
            report = root / "test-artifacts/recolor-diagnostics"
            result = json.loads((report / "summary.json").read_text())
            self.assertTrue(result["originalTestFailureRecorded"])
            self.assertTrue(result["diagnosticDoesNotOverrideFailure"])
            self.assertEqual((report / diagnostics.BINARY).read_bytes(), binary.read_bytes())
            self.assertEqual(result["binary"]["sha256"], diagnostics.digest(binary))
            self.assertEqual((report / symbols.name / "symbols").read_bytes(), b"exact symbols")

    def test_timeout_targets_only_debugger_descendants_even_in_separate_groups(self):
        with (mock.patch.object(diagnostics.subprocess, "check_output", return_value="100 1\n101 100\n102 101\n200 1\n"),
              mock.patch.object(diagnostics.os, "kill") as kill,
              mock.patch.object(diagnostics.os, "killpg") as killpg):
            diagnostics.kill_debugger_tree(100)
            self.assertEqual(kill.call_args_list, [mock.call(102, diagnostics.signal.SIGKILL), mock.call(101, diagnostics.signal.SIGKILL)])
            killpg.assert_called_once_with(100, diagnostics.signal.SIGKILL)

    def test_crash_reports_are_limited_to_owned_test_and_current_run(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            reports = root / "reports"
            reports.mkdir()
            name = diagnostics.BINARY + "-2026-11-04.ips"
            (reports / name).write_text("owned")
            (reports / "OtherArtistApp-2026-11-04.ips").write_text("private")
            old = reports / (diagnostics.BINARY + "-old.crash")
            old.write_text("old")
            os.utime(old, (1, 1))
            self.assertEqual(diagnostics.retain_crash_reports(reports, root / "saved", 2), [name])
            self.assertEqual([p.name for p in (root / "saved").iterdir()], [name])

    @unittest.skipUnless(os.name == "posix", "The collector runs only on macOS")
    def test_debugger_deadline_kills_its_owned_process(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            result = diagnostics.run_bounded([sys.executable, "-c", "import time; time.sleep(30)"],
                                             root / "log", root, timeout=.1)
            self.assertTrue(result["timedOut"])
            self.assertLess(result["elapsedSeconds"], 5)
            self.assertLess(result["exitCode"], 0)


if __name__ == "__main__":
    unittest.main()
