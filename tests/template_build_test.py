#!/usr/bin/env python3
"""Check template build modes without requiring a host toolchain."""
import importlib.util
import contextlib
import io
import json
import os
from pathlib import Path
import tempfile
import subprocess
import unittest
from unittest import mock
import zipfile


SPEC = importlib.util.spec_from_file_location(
    "scratch_template_build", Path(__file__).resolve().parents[1] / "tools/scrate_build.py")
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
        (self.sdk / "include/c++/v1").mkdir(parents=True)
        (self.sdk / "lib").mkdir()
        (self.sdk / "lib/scratch-stdlib.bc").write_bytes(b"mock bitcode")
        self.sdk_manifest = {"bitcode": ["lib/scratch-stdlib.bc"], "compile_flags": []}
        self.commands = []

    def run_build(self, args):
        def run(command, root, log_to_stderr=False):
            self.commands.append(command)
            if command[0] == "scratch-llvm":
                output = Path(command[command.index("-o") + 1])
                with zipfile.ZipFile(output, "w") as archive:
                    archive.writestr("project.json", "{}")
                if "--debug-map" in command:
                    Path(command[command.index("--debug-map") + 1]).write_text("{}", encoding="utf-8")

        with mock.patch.object(BUILD, "tool", side_effect=lambda value, root: value), \
                mock.patch.object(BUILD, "stdlib_sdk", return_value=(self.sdk, self.sdk_manifest)), \
                mock.patch.object(BUILD.subprocess, "check_output", return_value="/resource\n"), \
                mock.patch.object(BUILD, "run", side_effect=run), \
                mock.patch.dict(BUILD.os.environ, {}, clear=True), \
                mock.patch("builtins.print"):
            return BUILD.build_project(self.root / "sCrpp.toml", debug="--debug" in args,
                                       offline="--offline" in args, locked="--locked" in args)

    def test_release_preserves_optimization_and_notices(self):
        (self.root / "toolchain.local.json").write_text(json.dumps({
            "lldb_dap": "/debug/lldb-dap", "lldb_python": "/debug/python311"
        }), encoding="utf-8")
        self.run_build([])
        self.assertEqual(len(self.commands), 4)
        self.assertIn("-O2", self.commands[0])
        self.assertNotIn("-g", self.commands[0])
        self.assertIn("default<O2>", self.commands[-1])
        self.assertIn("--whole-program", self.commands[-1])
        self.assertNotIn("--debug-map", self.commands[-1])
        with zipfile.ZipFile(self.root / "build/project.sb3") as archive:
            self.assertEqual(archive.read("licenses/LICENSE.txt"), b"test notice")

    def test_plain_cpp_project_needs_no_template_header_or_build_script(self):
        self.assertFalse((self.root / "include").exists())
        self.assertFalse((self.root / "build.py").exists())
        self.assertFalse((self.root / "sCrpp.toml").exists())
        self.run_build([])
        source = (self.root / "build/generated/scratch.cpp").read_text(encoding="utf-8")
        self.assertIn("#include <string>", source)
        self.assertIn("void set_string(const std::string&);", source)
        self.assertNotIn('#include "scratch.hpp"', source)

    def test_turbowarp_settings_reach_compiler_before_project_serialization(self):
        (self.root / "sCrpp.toml").write_text('version = 1\n[package]\nname = "test"\n'
            '[turbowarp]\nframerate = 72.5\nhigh_quality_pen = false\n', encoding="utf-8")
        self.run_build([])
        command = self.commands[-1]
        settings = Path(command[command.index("--turbowarp-settings") + 1])
        self.assertEqual(json.loads(settings.read_text()), {"framerate": 72.5, "high_quality_pen": False})

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
        self.assertEqual(len({entry["output"] for entry in database}), 3)
        self.assertEqual((debug_dir / "compile_commands.json").read_bytes(),
                         (self.root / "build/compile_commands.json").read_bytes())

    def test_resource_packages_generate_one_wrapper_in_both_modes(self):
        manifest = self.root / "sCrpp.toml"
        manifest.write_text('[package]\nname="demo"\n', encoding="utf-8")
        imported = self.root / "other/sCrpp.toml"
        builder = mock.Mock()
        builder.collect_manifests.return_value = [manifest, imported]
        builder.load_manifest.return_value = {}
        builder.read_package_name.return_value = '角色"包'
        builder.prepare_resources.side_effect = lambda _manifest, directory: directory / "resources.json"
        with mock.patch.object(BUILD, "resource_builder", return_value=builder):
            for options in ([], ["--debug"]):
                self.commands.clear()
                self.run_build(options)
                build = self.root / "build" / "debug" if options else self.root / "build"
                flags = self.commands[-1]
                self.assertEqual(flags.count("--resources"), 2)
                self.assertIn(str(build / "resources/0/resources.json"), flags)
                self.assertIn(str(build / "resources/1/resources.json"), flags)
                wrappers = [command for command in self.commands[:-1]
                            if str(build / "generated/scratch.cpp") in command]
                self.assertEqual(len(wrappers), 1)
                source = (build / "generated/scratch.cpp").read_text(encoding="utf-8")
                self.assertIn('std::string("角色\\"包::")', source)
                self.assertIn('looks_switchcostumeto COSTUME=(data_variable', source)
                self.assertIn('"memory"', source)
                self.assertEqual((self.root / "src/main.cpp").read_text(encoding="utf-8"),
                                 "int main() { return 0; }")

    def test_resource_manifest_requires_python311(self):
        (self.root / "sCrpp.toml").write_text('[package]\nname="demo"\n', encoding="utf-8")
        with mock.patch.object(BUILD.sys, "version_info", (3, 10, 0)):
            with self.assertRaisesRegex(ValueError, "Python 3.11"):
                self.run_build([])

    def test_linked_library_sources_paths_dedup_and_both_build_modes(self):
        manifest = self.root / "sCrpp.toml"
        manifest.write_text("", encoding="utf-8")
        first = self.root / "libraries/first/sCrpp.toml"
        second = self.root / "libraries/second/sCrpp.toml"
        for directory in (first.parent, second.parent):
            (directory / "headers").mkdir(parents=True)
            (directory / "common.cpp").write_text("void library() {}", encoding="utf-8")
        builder = mock.Mock()
        builder.collect_manifests.return_value = [manifest, first, second]
        builder.read_package_name.return_value = "main"
        builder.prepare_resources.side_effect = lambda _manifest, directory: directory / "resources.json"
        declarations = {
            manifest: {},
            first: {"library": {"sources": ["common.cpp", "./common.cpp", "../../src/main.cpp"],
                                "include_dirs": ["headers", "./headers"]}},
            second: {"library": {"sources": ["common.cpp", "../first/common.cpp"],
                                 "include_dirs": ["headers", "../first/headers"]}},
        }
        builder.load_manifest.side_effect = lambda path: declarations[path]
        with mock.patch.object(BUILD, "resource_builder", return_value=builder):
            for args in ([], ["--debug"]):
                self.commands.clear()
                self.run_build(args)
                build = self.root / "build/debug" if args else self.root / "build"
                database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
                self.assertEqual(len(database), 5)  # Two user files, wrappers, two libraries.
                self.assertEqual(len({entry["output"] for entry in database}), 5)
                for source in (first.parent / "common.cpp", second.parent / "common.cpp"):
                    command = next(entry for entry in database if entry["file"] == str(source))
                    self.assertIn(str(build / "libraries"), command["output"])
                    self.assertEqual(Path(command["output"]).name, "common.bc")
                    self.assertIn(command["output"], self.commands[-1])
                for entry in database:
                    for directory in (first.parent / "headers", second.parent / "headers"):
                        self.assertEqual(entry["arguments"].count(str(directory)), 1)
                self.assertEqual(self.commands[-1].count("--resources"), 3)

    def test_rejects_invalid_library_configuration(self):
        manifest = self.root / "sCrpp.toml"
        manifest.write_text("", encoding="utf-8")
        builder = mock.Mock()
        builder.collect_manifests.return_value = [manifest]
        builder.read_package_name.return_value = "main"
        for library in ([], {"unknown": []}, {"sources": "main.cpp"},
                        {"sources": [3]}, {"sources": ["missing.cpp"]},
                        {"sources": ["sCrpp.toml"]}, {"include_dirs": ["missing"]},
                        {"include_dirs": ["src/main.cpp"]}):
            with self.subTest(library=library):
                builder.load_manifest.return_value = {"library": library}
                with mock.patch.object(BUILD, "resource_builder", return_value=builder):
                    with self.assertRaises(ValueError):
                        self.run_build([])

    def add_precompiled_sdk(self):
        (self.sdk / "lib/scrpp-stdlib.bc").write_bytes(b"mock precompiled libraries")
        (self.sdk / "public").mkdir()
        self.sdk_manifest.update({
            "bitcode": ["lib/scratch-stdlib.bc", "lib/scrpp-stdlib.bc"],
            "core_bitcode": ["lib/scratch-stdlib.bc"],
            "include_dirs": ["public", "include/c++/v1", "include"],
            "resources": [f"resources/{name}/resources.json" for name in ("console", "events", "pte")],
            "precompiled_packages": ["__scl_console", "__scl_events", "__scl_pte"],
        })
        for entry in self.sdk_manifest["resources"]:
            path = self.sdk / entry
            path.parent.mkdir(parents=True)
            path.write_text("{}", encoding="utf-8")

    def test_sdk_only_links_core_without_project_manifest(self):
        self.add_precompiled_sdk()
        for options in ([], ["--debug"]):
            with self.subTest(options=options):
                self.commands.clear()
                with mock.patch.object(BUILD, "resource_builder") as builder, \
                        mock.patch.object(BUILD, "package_manager") as manager:
                    self.run_build(options)
                    builder.assert_not_called()
                    manager.assert_not_called()
                link = self.commands[-1]
                self.assertNotIn("--resources", link)
                for entry in self.sdk_manifest["core_bitcode"]:
                    self.assertEqual(link.count(str(self.sdk / entry)), 1)
                self.assertNotIn(str(self.sdk / "lib/scrpp-stdlib.bc"), link)
                for command in self.commands[:-1]:
                    self.assertIn(str(self.sdk / "public"), command)
                    self.assertIn("-O0" if options else "-O2", command)

    def test_sdk_does_not_skip_explicit_legacy_packages(self):
        self.add_precompiled_sdk()
        manifest = self.root / "sCrpp.toml"
        manifest.write_text("", encoding="utf-8")
        builtin = self.root / "console/sCrpp.toml"
        builtin.parent.mkdir()
        (builtin.parent / "console.cpp").write_text("void console() {}", encoding="utf-8")
        third_party = self.root / "extra/sCrpp.toml"
        third_party.parent.mkdir()
        (third_party.parent / "extra.cpp").write_text("void extra() {}", encoding="utf-8")
        builder = mock.Mock()
        builder.collect_manifests.return_value = [manifest, builtin, third_party]
        builder.read_package_name.return_value = "app"
        builder.load_manifest.side_effect = lambda path: {
            manifest: {"package": {"name": "app"}},
            builtin: {"package": {"name": "__scl_console"},
                      "library": {"sources": ["console.cpp"]}},
            third_party: {"package": {"name": "extra"}, "library": {"sources": ["extra.cpp"]}},
        }[path]
        builder.prepare_resources.side_effect = lambda _manifest, directory: directory / "resources.json"
        with mock.patch.object(BUILD, "resource_builder", return_value=builder):
            self.run_build([])
        self.assertEqual(self.commands[-1].count("--resources"), 3)  # Project, explicit console, extra.
        prepared = [call.args[0] for call in builder.prepare_resources.call_args_list]
        self.assertEqual(prepared, [manifest, builtin, third_party])
        self.assertTrue(any(str(third_party.parent / "extra.cpp") in args for args in self.commands[:-1]))
        self.assertTrue(any(str(builtin.parent / "console.cpp") in args for args in self.commands[:-1]))

    def test_sdk_rejects_missing_or_nonportable_paths(self):
        self.add_precompiled_sdk()
        for bitcode in (["lib/missing.bc"], ["../outside.bc"], [str(self.root / "absolute.bc")]):
            with self.subTest(bitcode=bitcode):
                self.sdk_manifest["core_bitcode"] = bitcode
                with self.assertRaisesRegex(ValueError, "SDK core_bitcode"):
                    self.run_build([])

    def test_scrate_dependencies_link_bitcode_and_sources_once(self):
        manifest = self.root / "sCrpp.toml"
        manifest.write_text("", encoding="utf-8")
        source_manifest = self.root / "packages/source/sCrpp.toml"
        binary_manifest = self.root / "packages/binary/sCrpp.toml"
        for path in (source_manifest, binary_manifest):
            (path.parent / "headers").mkdir(parents=True)
        (source_manifest.parent / "source.cpp").write_text("void source() {}", encoding="utf-8")
        (binary_manifest.parent / "library.bc").write_bytes(b"bitcode")
        configs = {
            manifest: {"package": {"name": "app"}, "dependencies": {"binary": "0.1.0"}},
            source_manifest: {"package": {"name": "source"},
                              "library": {"sources": ["source.cpp"], "include_dirs": ["headers"]}},
            binary_manifest: {"package": {"name": "binary"},
                              "dependencies": {"source": "0.1.0"},
                              "library": {"sources": ["not-shipped.cpp"], "bitcode": ["library.bc"],
                                          "include_dirs": ["headers"]},
                              "toolchain": {"llvm_major": 22, "target": "x86_64-unknown-linux-gnu", "scrpp_abi": "1"}},
        }
        builder = mock.Mock()
        builder.load_manifest.side_effect = lambda path: configs[path]
        builder.read_package_name.return_value = "app"
        builder.collect_manifests.side_effect = lambda path: ([manifest, source_manifest]
                                                             if path == manifest else [path])
        builder.prepare_resources.side_effect = lambda _manifest, directory: directory / "resources.json"
        manager = mock.Mock()
        manager.resolve.return_value = [source_manifest, binary_manifest]
        (self.root / "toolchain.local.json").write_text(json.dumps({
            "scrate": "tools/scrate.py", "registry": "registry", "package_cache": "cache",
        }), encoding="utf-8")
        with mock.patch.object(BUILD, "resource_builder", return_value=builder), \
                mock.patch.object(BUILD, "package_manager", return_value=manager):
            for options in (["--offline", "--locked"], ["--debug", "--offline", "--locked"]):
                self.commands.clear()
                builder.prepare_resources.reset_mock()
                self.run_build(options)
                self.assertEqual(self.commands[-1].count(str(binary_manifest.parent / "library.bc")), 1)
                self.assertEqual(self.commands[-1].count("--resources"), 3)
                self.assertEqual(len(self.commands), 5)  # Three user/generated, one source dependency, link.
                self.assertFalse(any("not-shipped.cpp" in " ".join(command) for command in self.commands))
                for command in self.commands[:-1]:
                    self.assertIn(str(source_manifest.parent / "headers"), command)
                    self.assertIn(str(binary_manifest.parent / "headers"), command)
                manager.resolve.assert_called_with(manifest, registry=str(self.root / "registry"),
                    cache_dir=self.root / "cache", offline=True, locked=True,
                    abi={"llvm_major": 22, "target": "x86_64-unknown-linux-gnu", "scrpp_abi": "1"})
                manager.validate_abi.assert_called_with(configs[binary_manifest],
                    {"llvm_major": 22, "target": "x86_64-unknown-linux-gnu", "scrpp_abi": "1"})

    def test_empty_dependencies_with_lock_still_resolve_and_unresolved_legacy_dependencies_fail(self):
        manifest = self.root / "sCrpp.toml"
        manifest.write_text("", encoding="utf-8")
        (self.root / "scrate.lock").write_text("{}", encoding="utf-8")
        builder = mock.Mock()
        builder.load_manifest.return_value = {"package": {"name": "app"}}
        builder.collect_manifests.return_value = [manifest]
        builder.read_package_name.return_value = "app"
        builder.prepare_resources.side_effect = lambda _manifest, directory: directory / "resources.json"
        manager = mock.Mock()
        manager.resolve.return_value = []
        with mock.patch.object(BUILD, "resource_builder", return_value=builder), \
                mock.patch.object(BUILD, "package_manager", return_value=manager):
            self.run_build(["--locked"])
            manager.resolve.assert_called_once()
            linked = self.root / "legacy/sCrpp.toml"
            builder.collect_manifests.return_value = [manifest, linked]
            builder.load_manifest.side_effect = lambda path: ({"package": {"name": "app"}} if path == manifest else
                {"package": {"name": "legacy"}, "dependencies": {"nested": "0.1.0"}})
            with self.assertRaisesRegex(ValueError, "declare this package"):
                self.run_build([])

    def test_two_projects_do_not_share_paths_or_configuration(self):
        first = self.root
        first_info = self.run_build([])
        second = first / "second"
        (second / "src").mkdir(parents=True)
        (second / "src/main.cpp").write_text("int main() { return 2; }", encoding="utf-8")
        self.root = second
        self.commands.clear()
        second_info = self.run_build(["--debug"])
        self.assertEqual(second_info["root"], second)
        self.assertEqual(second_info["project"], second / "build/debug/project.sb3")
        database = json.loads((second / "build/debug/compile_commands.json").read_text(encoding="utf-8"))
        for entry in database:
            self.assertEqual(entry["directory"], str(second))
            self.assertIn(str(second / "include"), entry["arguments"])
            self.assertNotIn(str(first / "include"), entry["arguments"])
        self.assertEqual(first_info["root"], first)
        self.assertEqual(first_info["project"], first / "build/project.sb3")
        self.root = first
        third_info = self.run_build([])
        self.assertEqual(third_info["project"], first_info["project"])

    def test_custom_build_layout_excludes_output_tree_and_updates_database(self):
        (self.root / "sCrpp.toml").write_text('''[build]
source_dir = "."
include_dirs = ["api", "shared include"]
output_dir = "out/artifacts"
cpp_standard = "c++20"
''', encoding="utf-8")
        (self.root / "out/artifacts/generated").mkdir(parents=True)
        (self.root / "out/artifacts/generated/old.cpp").write_text("stale", encoding="utf-8")
        builder = mock.Mock()
        builder.load_manifest.return_value = {}
        builder.collect_manifests.return_value = [self.root / "sCrpp.toml"]
        builder.read_package_name.return_value = ""
        builder.prepare_resources.side_effect = lambda _manifest, directory: directory / "resources.json"
        with mock.patch.object(BUILD, "resource_builder", return_value=builder):
            info = self.run_build(["--debug"])
        self.assertEqual(info["build_dir"], self.root / "out/artifacts/debug")
        database = json.loads((info["build_dir"] / "compile_commands.json").read_text(encoding="utf-8"))
        self.assertEqual(len(database), 3)
        for entry in database:
            self.assertNotIn("old.cpp", entry["file"])
            self.assertIn("-std=c++20", entry["arguments"])
            self.assertIn(str(self.root / "api"), entry["arguments"])
            self.assertIn(str(self.root / "shared include"), entry["arguments"])
        self.assertEqual((info["build_dir"] / "compile_commands.json").read_bytes(),
                         (self.root / "out/artifacts/compile_commands.json").read_bytes())

    def test_project_info_does_not_start_tools_and_rejects_invalid_build_configuration(self):
        config = {"node": "node", "turbowarp": "TurboWarp", "debugger": "debug/adapter.cjs", "python": "python"}
        (self.root / "toolchain.local.json").write_text(json.dumps(config), encoding="utf-8")
        with mock.patch.object(BUILD, "tool") as tool, mock.patch.object(BUILD, "package_manager") as manager:
            info = BUILD.project_info(self.root, debug=True)
            self.assertEqual(info["config"], config)
            self.assertEqual(info["project"], self.root / "build/debug/project.sb3")
            tool.assert_not_called()
            manager.assert_not_called()
        for text in ('unknown = true', 'cpp_standard = "c++11"', 'include_dirs = "include"',
                     'source_dir = 3', 'output_dir = "."', 'output_dir = "../outside"'):
            with self.subTest(text=text):
                (self.root / "sCrpp.toml").write_text("[build]\n" + text, encoding="utf-8")
                with self.assertRaises(ValueError):
                    BUILD.project_info(self.root)

    def test_rejects_output_symlink_escape(self):
        outside = self.root / "outside"
        outside.mkdir()
        output = self.root / "build"
        output.mkdir()
        try:
            (output / "generated").symlink_to(outside, target_is_directory=True)
        except OSError:
            self.skipTest("OS does not permit directory symlink creation")
        with self.assertRaisesRegex(ValueError, "symlink or reparse"):
            BUILD.project_info(self.root)

    @unittest.skipUnless(os.name == "nt", "NTFS junction test only applies to Windows")
    def test_rejects_real_output_junctions_without_following_targets(self):
        output = self.root / "build"
        output.mkdir()
        internal = output / "ordinary"
        internal.mkdir()
        external = self.root / "external"
        external.mkdir()
        sentinel = external / "sentinel.txt"
        sentinel.write_text("untouched", encoding="utf-8")
        for target in (internal, external, output):
            with self.subTest(target=target):
                junction = output / "generated"
                result = subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", "mklink", "/J",
                                         str(junction), str(target)], capture_output=True, timeout=10)
                if result.returncode:
                    self.skipTest("NTFS directory junction creation unavailable")
                try:
                    self.assertTrue(BUILD._is_link_or_reparse(junction))
                    with self.assertRaisesRegex(ValueError, "symlink or reparse"):
                        BUILD.project_info(self.root)
                finally:
                    # Remove only the junction entry, never recurse into its target.
                    junction.rmdir()
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "untouched")
        # Also reject a junction in output_dir's ancestor chain, even if its
        # target is inside the project and canonical resolution looks harmless.
        junction = self.root / "alias"
        result = subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", "mklink", "/J",
                                 str(junction), str(output)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0)
        try:
            (self.root / "sCrpp.toml").write_text('[build]\noutput_dir="alias/nested"\n', encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "symlink or reparse"):
                BUILD.project_info(self.root)
        finally:
            junction.rmdir()

    def test_build_log_to_stderr_preserves_protocol_stdout(self):
        stdout, stderr = io.StringIO(), io.StringIO()
        def perform(*args, **kwargs):
            print("resolver output")
            self.assertTrue(kwargs["log_to_stderr"])
            return {"project": "test.sb3"}
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr), \
                mock.patch.object(BUILD, "_build_project", side_effect=perform):
            result = BUILD.build_project(self.root, log_to_stderr=True)
        self.assertEqual(stdout.getvalue(), "")
        self.assertIn("resolver output", stderr.getvalue())
        self.assertEqual(result["project"], "test.sb3")


if __name__ == "__main__":
    unittest.main()
