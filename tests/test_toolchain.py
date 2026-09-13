"""Host-independent checks for tool selection and target-specific link flags."""
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import toolchain
from varargs_native import adapt_ir


class ToolDiscoveryTests(unittest.TestCase):
    def setUp(self):
        environment = patch.dict(toolchain.os.environ, {}, clear=True)
        environment.start()
        self.addCleanup(environment.stop)
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.clang = self.root / "bin/clang"
        self.clang.parent.mkdir()
        self.clang.touch()

    def test_explicit_clang_is_used(self):
        self.assertEqual(toolchain.resolve_clang(str(self.clang)), str(self.clang.resolve()))

    def test_invalid_explicit_clang_does_not_fall_back(self):
        with patch.object(toolchain.shutil, "which", return_value=None), self.assertRaisesRegex(RuntimeError, "Selected Clang"):
            toolchain.resolve_clang(str(self.root / "absent/clang"))

    def test_environment_precedence(self):
        with patch.dict(toolchain.os.environ, {"SCRATCH_CLANG": str(self.clang), "CLANG": "missing"}):
            self.assertEqual(toolchain.resolve_clang(), str(self.clang.resolve()))

    def test_invalid_environment_clang_does_not_fall_back(self):
        with patch.dict(toolchain.os.environ, {"SCRATCH_CLANG": "missing-clang"}), \
                patch.object(toolchain.shutil, "which", return_value=None), \
                self.assertRaisesRegex(RuntimeError, "Selected Clang"):
            toolchain.resolve_clang()

    def test_versioned_path_clang_precedes_generic_clang(self):
        with patch.object(toolchain.shutil, "which", side_effect=lambda name: str(self.clang) if name == "clang-22" else None):
            self.assertEqual(toolchain.resolve_clang(), str(self.clang.resolve()))

    def test_missing_clang_has_actionable_error(self):
        with patch.object(toolchain.shutil, "which", return_value=None), self.assertRaisesRegex(RuntimeError, "--clang"):
            toolchain.resolve_clang()

    def test_platform_library_names(self):
        names = ("LLVM-C.dll", "libLLVM-22.dll", "libLLVM-22.so", "libLLVM-22.so.1",
                 "libLLVM.so.22.1", "libLLVM.dylib", "libLLVM-22.dylib")
        for name in names:
            with self.subTest(name=name):
                library = self.root / "lib" / name
                library.parent.mkdir(exist_ok=True)
                library.touch()
                self.assertEqual(toolchain.resolve_llvm_library(str(self.clang)), str(library.resolve()))
                library.unlink()

    def test_invalid_explicit_library_does_not_fall_back(self):
        (self.clang.parent / "LLVM-C.dll").touch()
        with self.assertRaisesRegex(RuntimeError, "Selected LLVM shared library"):
            toolchain.resolve_llvm_library(str(self.clang), str(self.root / "missing.dll"))

    def test_invalid_environment_library_does_not_fall_back(self):
        with patch.dict(toolchain.os.environ, {"SCRATCH_LLVM_LIBRARY": str(self.root / "missing.so")}), \
                self.assertRaisesRegex(RuntimeError, "Selected LLVM shared library"):
            toolchain.resolve_llvm_library(str(self.clang))

    def test_system_library_loader_fallback(self):
        with patch.object(toolchain.ctypes.util, "find_library", return_value="libLLVM-22.so.1"):
            self.assertEqual(toolchain.resolve_llvm_library(str(self.clang)), "libLLVM-22.so.1")

    def test_clang_default_target_is_queried(self):
        with patch.object(toolchain.subprocess, "check_output", return_value="x86_64-w64-windows-gnu\n") as run:
            self.assertEqual(toolchain.clang_default_target(str(self.clang)), "x86_64-w64-windows-gnu")
        run.assert_called_once_with([str(self.clang), "-dumpmachine"], text=True)


class HostFlagsTests(unittest.TestCase):
    def test_msvc_has_no_sdk_or_crt(self):
        flags = toolchain.host_shared_library_flags("x86_64-pc-windows-msvc")
        self.assertIn("-Wl,/noentry", flags)
        self.assertIn("-nostdlib", flags)
        self.assertIn("-fuse-ld=lld", flags)

    def test_mingw_never_uses_microsoft_linker_options(self):
        flags = toolchain.host_shared_library_flags("x86_64-w64-windows-gnu")
        self.assertIn("-Wl,--entry,__sclr_native_dll_entry", flags)
        self.assertIn("-nostdlib", flags)
        self.assertNotIn("-Wl,/noentry", flags)
        self.assertFalse(any(flag.startswith("--target=") for flag in flags))
        self.assertEqual(toolchain.host_library_suffix("aarch64-w64-mingw32"), ".dll")

    def test_darwin(self):
        flags = toolchain.host_shared_library_flags("arm64-apple-darwin24.0.0")
        self.assertIn("-dynamiclib", flags)
        self.assertNotIn("-shared", flags)
        self.assertEqual(toolchain.host_library_suffix("arm64-apple-darwin24.0.0"), ".dylib")

    def test_elf(self):
        flags = toolchain.host_shared_library_flags("aarch64-unknown-linux-gnu", "lld")
        self.assertIn("-shared", flags)
        self.assertIn("-fPIC", flags)
        self.assertIn("-fuse-ld=lld", flags)
        self.assertIn("-Wl,-z,defs", flags)
        self.assertEqual(toolchain.host_library_suffix("x86_64-unknown-linux-gnu"), ".so")

    def test_explicit_linker_and_sysroot(self):
        with tempfile.TemporaryDirectory() as directory:
            linker = Path(directory) / "ld.lld"
            linker.touch()
            flags = toolchain.host_shared_library_flags("x86_64-unknown-linux-gnu", str(linker), directory)
            self.assertIn("--ld-path=" + str(linker.resolve()), flags)
            self.assertIn("--sysroot=" + str(Path(directory).resolve()), flags)

    def test_invalid_explicit_linker(self):
        with self.assertRaisesRegex(RuntimeError, "host linker"):
            toolchain.host_shared_library_flags("x86_64-unknown-linux-gnu", "/missing/linker")

    def test_invalid_sysroot(self):
        with self.assertRaisesRegex(RuntimeError, "sysroot"):
            toolchain.host_shared_library_flags("x86_64-unknown-linux-gnu", sysroot="/missing/sysroot")

    def test_unknown_target_is_not_guessed_as_elf(self):
        with self.assertRaisesRegex(RuntimeError, "Unsupported native test target"):
            toolchain.host_shared_library_flags("wasm32-unknown-unknown")

    def test_mingw_loader_glue_is_native_only(self):
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(toolchain.write_host_support("x86_64-unknown-linux-gnu", Path(directory)), [])
            paths = toolchain.write_host_support("x86_64-w64-windows-gnu", Path(directory))
            self.assertEqual(len(paths), 1)
            contents = Path(paths[0]).read_text(encoding="utf-8")
            self.assertIn("__sclr_native_dll_entry", contents)
            self.assertNotIn("#include", contents)


class VarargsAdapterTests(unittest.TestCase):
    SOURCE = "define i32 @variadic(i32 %n, ...) {\n}\ndefine i32 @main() {\n %r = call i32 (i32, ...) @variadic(i32 0)\n}\n"

    def test_windows_uses_sysv_for_varargs_only(self):
        ir = adapt_ir(self.SOURCE, "x86_64-w64-windows-gnu")
        self.assertIn("define x86_64_sysvcc i32 @variadic", ir)
        self.assertIn("call x86_64_sysvcc i32", ir)
        self.assertIn("define dllexport i32 @native_entry", ir)
        self.assertNotIn("_fltused", ir)

    def test_linux_uses_no_dll_annotations(self):
        ir = adapt_ir(self.SOURCE, "x86_64-unknown-linux-gnu")
        self.assertIn("define i32 @native_entry", ir)
        self.assertNotIn("dllexport", ir)
        self.assertNotIn("_fltused", ir)

    def test_guest_dso_local_removed_only_in_reference_copy(self):
        source = "@indirect = dso_local global ptr @variadic\n" + self.SOURCE.replace("define ", "define dso_local ")
        ir = adapt_ir(source, "x86_64-unknown-linux-gnu")
        self.assertNotIn("dso_local", ir)
        self.assertIn("@indirect = global ptr @variadic", ir)
        self.assertIn("dso_local", source)

    def test_msvc_adds_floating_marker(self):
        self.assertIn("@_fltused", adapt_ir(self.SOURCE, "x86_64-pc-windows-msvc"))

    def test_arm_is_explicitly_rejected_for_amd64_fixture(self):
        with self.assertRaisesRegex(RuntimeError, "AMD64"):
            adapt_ir(self.SOURCE, "aarch64-apple-darwin")


if __name__ == "__main__":
    unittest.main()
