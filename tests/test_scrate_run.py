"""Dispatch/lifecycle tests; real LLDB and VM execution have separate coverage."""
import contextlib
import io
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import scrate
import scrate_build
import scrate_run


class RunTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="scrate run ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = self.root / "sCrpp.toml"
        runtime = self.root / "runtime"
        runtime.mkdir()
        for name in ("scratch-debug.cjs", "cli.cjs"):
            (runtime / name).write_text("", encoding="utf-8")
        (self.root / "toolchain.local.json").write_text(json.dumps({
            "node": sys.executable, "debugger": "runtime", "clang": sys.executable}), encoding="utf-8")
        for relative in ("build/project.sb3", "build/debug/project.sb3", "build/debug/project.debug.json"):
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("{}", encoding="utf-8")

    def run_command(self, **kwargs):
        with mock.patch.object(scrate_run.subprocess, "Popen") as popen:
            popen.return_value.wait.return_value = 0
            result = scrate_run.launch_project(self.manifest, no_build=True, **kwargs)
            self.assertEqual(result, 0)
            return popen.call_args.args[0], popen.call_args.kwargs

    def test_run_uses_existing_public_bridge_without_lldb(self):
        command, options = self.run_command()
        self.assertEqual(Path(command[0]).resolve(), Path(sys.executable).resolve())
        self.assertEqual(Path(command[1]).name, "scratch-debug.cjs")
        self.assertEqual(command[2:], ["--run", str(self.root / "build/project.sb3")])
        self.assertEqual(options["cwd"], self.root)
        self.assertNotIn("shell", options)

    def test_debug_build_can_run_without_debugger(self):
        command, _ = self.run_command(debug_build=True)
        self.assertIn("--run", command)
        self.assertNotIn("--lldb-dap", command)
        self.assertEqual(command[-1], str(self.root / "build/debug/project.sb3"))

    def test_debug_uses_interactive_lldb_client(self):
        command, _ = self.run_command(debug=True, commands=["break main.cpp:4", "continue", "quit"],
                                      no_open=True, lldb_dap=sys.executable, connect_timeout=3000)
        self.assertEqual(Path(command[1]).name, "cli.cjs")
        self.assertIn(str(self.root / "build/debug/project.debug.json"), command)
        self.assertEqual(command.count("--command"), 3)
        self.assertIn("--no-open", command)
        self.assertIn("--lldb-dap", command)

    def test_dap_does_not_build_and_keeps_stdout_clean(self):
        for debug in (False, True):
            with self.subTest(debug=debug), mock.patch.object(scrate_build, "build_project") as build:
                output = io.StringIO()
                with contextlib.redirect_stdout(output):
                    command, options = self.run_command(debug=debug, dap=True)
                build.assert_not_called()
                self.assertEqual(output.getvalue(), "")
                self.assertEqual(Path(command[1]).name, "scratch-debug.cjs")
                self.assertIn("--dap", command)
                self.assertEqual("--no-debug" in command, not debug)
                self.assertIs(options["stdout"], output)
                self.assertIs(options["stdin"], sys.stdin)
                self.assertIs(options["stderr"], sys.stderr)
        with self.assertRaisesRegex(ValueError, "--no-build"):
            scrate_run.launch_project(self.manifest, dap=True)

    def test_dap_pipes_survive_real_launcher_child_process(self):
        # Use Python as a small adapter so this regression needs no Node/LLDB.
        # Implicit stdio inheritance loses redirected handles on Windows.
        entry = self.root / "runtime" / "scratch-debug.cjs"
        entry.write_text("import sys\nsys.stdout.buffer.write(sys.stdin.buffer.read())\n"
                         "sys.stdout.buffer.flush()\n", encoding="utf-8")
        payload = b'Content-Length: 2\r\n\r\n{}'
        for mode in ("run", "debug"):
            with self.subTest(mode=mode):
                result = subprocess.run([sys.executable, str(Path(scrate.__file__)), mode,
                    "--dap", "--no-build", "--manifest", str(self.manifest)],
                    input=payload, capture_output=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, payload)

    def test_regular_launch_builds_once(self):
        info = scrate_build.project_info(self.manifest, debug=True)
        with mock.patch.object(scrate_build, "build_project", return_value=info) as build, \
                mock.patch.object(scrate_run.subprocess, "Popen") as popen:
            popen.return_value.wait.return_value = 0
            self.assertEqual(scrate_run.launch_project(self.manifest, debug=True, offline=True, locked=True), 0)
            self.assertEqual(build.call_count, 1)
            self.assertTrue(build.call_args.kwargs["debug"])
            self.assertTrue(build.call_args.kwargs["offline"])
            self.assertTrue(build.call_args.kwargs["locked"])

    def test_missing_artifact_and_runtime_fail_before_spawn(self):
        (self.root / "build/project.sb3").unlink()
        with mock.patch.object(scrate_run.subprocess, "Popen") as popen:
            with self.assertRaisesRegex(ValueError, "scrate build"):
                scrate_run.launch_project(self.manifest, no_build=True)
            popen.assert_not_called()
        with self.assertRaisesRegex(ValueError, "runtime not found"):
            scrate_run.debugger_entry(scrate_build.project_info(self.manifest), "missing.cjs")

    def test_interrupt_requests_graceful_child_cleanup(self):
        with mock.patch.object(scrate_run.subprocess, "Popen") as popen:
            child = popen.return_value
            child.wait.side_effect = [KeyboardInterrupt, 0]
            child.poll.return_value = None
            self.assertEqual(scrate_run.launch_project(self.manifest, no_build=True), 130)
            child.send_signal.assert_called_once_with(signal.CTRL_BREAK_EVENT if os.name == "nt" else signal.SIGINT)
            child.kill.assert_not_called()

    def test_cli_preserves_compiler_failure_without_traceback(self):
        errors = io.StringIO()
        with mock.patch.object(scrate_build, "build_project", side_effect=subprocess.CalledProcessError(7, ["clang"])), \
                contextlib.redirect_stderr(errors):
            code = scrate.main(["build", "--manifest", str(self.manifest)])
        self.assertEqual(code, 7)
        self.assertNotIn("Traceback", errors.getvalue())


if __name__ == "__main__":
    unittest.main()
