"""Native AMD64 SysV reference using the selected Clang host toolchain.

The fixtures exercise the guest's AMD64 SysV va_list layout. On Windows, their
variadic definitions/calls use x86_64_sysvcc while the exported no-argument
entry uses the compiler's normal host ABI. A non-AMD64 host needs an external
AMD64 runner and is rejected explicitly, rather than testing a different ABI.
"""
from __future__ import annotations

import argparse
import ctypes
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from toolchain import (clang_default_target, host_abi, host_library_suffix,
                       host_shared_library_flags, resolve_clang, write_host_support)


def adapt_ir(source: str, target: str) -> str:
    if target.split("-", 1)[0].lower() not in ("x86_64", "amd64"):
        raise RuntimeError(f"SysV native reference requires an AMD64 host; selected target is {target}")
    abi = host_abi(target)
    lines = []
    for line in source.splitlines():
        # These fixed guest fixtures were emitted for a non-shared executable.
        # Their dso_local markings permit direct relocations that are invalid
        # in an ELF shared library; -fPIC alone cannot undo explicit IR flags.
        # Remove this host-linkage assumption only in the native reference
        # bridge. The guest fixtures and their compiled Scratch behavior stay
        # untouched, while Clang can choose proper PIC/GOT access for the .so.
        if line.startswith(("define ", "declare ", "@")):
            line = re.sub(r"\bdso_local\s+", "", line, count=1)
        if line.startswith("define ") and "..." in line:
            line = line.replace("define dso_local ", "define ").replace("define ", "define x86_64_sysvcc ", 1)
        if re.search(r"(?<![%\w])call\s+.*\.\.\.", line):
            line = re.sub(r"(?<![%\w])call\s+", "call x86_64_sysvcc ", line, count=1)
        if line.startswith("define ") and "@main(" in line:
            line = line.replace("define dso_local ", "define ")
            if abi in ("msvc", "mingw"):
                line = line.replace("define ", "define dllexport ", 1)
            line = line.replace("@main(", "@native_entry(")
        lines.append(line)
    if abi == "msvc":
        # Microsoft's floating-operation object marker has no runtime role.
        lines.append("@_fltused = global i32 0")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", help="Clang executable (otherwise SCRATCH_CLANG, CLANG, or PATH)")
    parser.add_argument("--host-linker", help="linker flavor (e.g. lld), or linker executable path")
    parser.add_argument("--host-sysroot", help="optional native test sysroot")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "tests/.tmp/varargs_native")
    args = parser.parse_args()
    clang = resolve_clang(args.clang)
    target = clang_default_target(clang)
    flags = host_shared_library_flags(target, args.host_linker, args.host_sysroot)
    work = args.output_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    support = write_host_support(target, work)
    for name, expected in (("varargs_ir", 58), ("varargs_sysv", 57)):
        source = (ROOT / "tests/fixtures" / (name + ".ll")).read_text(encoding="utf-8")
        ir = work / (name + ".ll")
        library = work / (name + host_library_suffix(target))
        ir.write_text(adapt_ir(source, target), encoding="utf-8")
        subprocess.run([clang, "-Wno-override-module", "-O1", *flags,
                        str(ir), *support, "-o", str(library)], check=True)
        native = ctypes.CDLL(str(library))
        native.native_entry.restype = ctypes.c_int
        native.native_entry.argtypes = []
        result = native.native_entry()
        if result != expected:
            raise RuntimeError(f"{name}: result {result}, expected {expected}")
        print(f"{name}: native LLVM SysV result {result} ({target})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
