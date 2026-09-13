#!/usr/bin/env python3
"""Check template build modes without requiring a host toolchain."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import zipfile


SPEC = importlib.util.spec_from_file_location(
    "scratch_template_build", Path(__file__).resolve().parents[1] / "template/build.py")
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class TemplateBuildTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="scratch template ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "src/nested").mkdir(parents=True)
        (self.root / "src/main.cpp").write_text("int main() { return 0; }", encoding="utf-8")
        (self.root / "src/nested/main.cpp").write_text("int other() { return 1; }", encoding="utf-8")
        self.sdk = self.root / "sdk"
        (self.sdk / "licenses").mkdir(parents=True)
        (self.sdk / "licenses/LICENSE.txt").write_text("test notice", encoding="utf-8")
        self.commands = []

    def run_build(self, args):
        def run(command):
            self.commands.append(command)
            if command[0] == "scratch-llvm":
                output = Path(command[command.index("-o") + 1])
                with zipfile.ZipFile(output, "w") as archive:
                    archive.writestr("project.json", "{}")
                if "--debug-map" in command:
                    Path(command[command.index("--debug-map") + 1]).write_text("{}", encoding="utf-8")

        with mock.patch.multiple(BUILD, ROOT=self.root, BUILD=self.root / "build"), \
                mock.patch.object(BUILD, "tool", side_effect=lambda value: value), \
                mock.patch.object(BUILD, "stdlib_sdk", return_value=(self.sdk, {
                    "bitcode": ["lib/scratch-stdlib.bc"], "compile_flags": []})), \
                mock.patch.object(BUILD.subprocess, "check_output", return_value="/resource\n"), \
                mock.patch.object(BUILD, "run", side_effect=run), \
                mock.patch.dict(BUILD.os.environ, {}, clear=True), \
                mock.patch("builtins.print"):
            BUILD.main(args)

    def test_release_preserves_optimization_and_notices(self):
        (self.root / "toolchain.local.json").write_text(json.dumps({
            "lldb_dap": "/debug/lldb-dap", "lldb_python": "/debug/python311"
        }), encoding="utf-8")
        self.run_build([])
        self.assertEqual(len(self.commands), 3)
        self.assertIn("-O2", self.commands[0])
        self.assertNotIn("-g", self.commands[0])
        self.assertIn("default<O2>", self.commands[-1])
        self.assertIn("--whole-program", self.commands[-1])
        self.assertNotIn("--debug-map", self.commands[-1])
        with zipfile.ZipFile(self.root / "build/project.sb3") as archive:
            self.assertEqual(archive.read("licenses/LICENSE.txt"), b"test notice")

    def test_debug_separate_outputs_source_info_and_no_o2(self):
        self.run_build([])
        release = (self.root / "build/project.sb3").read_bytes()
        debug_dir = self.root / "build/debug"
        debug_dir.mkdir()
        (debug_dir / "project.debug.json").write_text("stale", encoding="utf-8")
        self.commands.clear()
        self.run_build(["--debug"])
        for command in self.commands[:-1]:
            self.assertIn("-O0", command)
            self.assertIn("-g", command)
            self.assertIn("-fstandalone-debug", command)
            self.assertNotIn("-O2", command)
        command = self.commands[-1]
        self.assertIn("--whole-program", command)
        self.assertNotIn("--passes", command)
        self.assertEqual(command[command.index("--debug-map") + 1], str(debug_dir / "project.debug.json"))
        self.assertEqual((self.root / "build/project.sb3").read_bytes(), release)
        self.assertEqual((debug_dir / "project.debug.json").read_text(encoding="utf-8"), "{}")
        database = json.loads((debug_dir / "compile_commands.json").read_text(encoding="utf-8"))
        self.assertEqual(len({entry["output"] for entry in database}), 2)
        self.assertEqual((debug_dir / "compile_commands.json").read_bytes(),
                         (self.root / "build/compile_commands.json").read_bytes())


if __name__ == "__main__":
    unittest.main()
