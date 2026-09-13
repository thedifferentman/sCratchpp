"""Build the vendored integer-only SoftFloat runtime as one LLVM module.

No network, platform SDK, llvm-link, or target standard library is needed to
produce guest bitcode: Clang's freestanding headers and pinned C sources suffice.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

from toolchain import (clang_default_target, host_library_suffix,
                       host_shared_library_flags, resolve_clang, resolve_llvm_library,
                       write_host_support)

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "runtime" / "softfloat"
SOURCE = RUNTIME / "upstream" / "source"


def verify_upstream() -> None:
    for entry in (RUNTIME / "SOURCE_SHA256SUMS").read_text().splitlines():
        expected, relative = entry.split("  ", 1)
        source = RUNTIME / relative
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Vendored SoftFloat source is missing or differs from the pinned release: {relative}")


def llvm_library(clang: str, library_path: str | None):
    candidate = resolve_llvm_library(clang, library_path)
    try:
        return ctypes.CDLL(candidate)
    except OSError as error:
        raise RuntimeError(f"Cannot load LLVM shared library {candidate}: {error}") from error


def compatible_layout(original: str, replacement: str, clang: str, library_path: str | None) -> None:
    """Validate actual LLVM layout equivalence for every runtime memory type."""
    lib = llvm_library(clang, library_path)
    ptr = ctypes.c_void_p

    def api(name, result, *arguments):
        fn = getattr(lib, name)
        fn.restype = result
        fn.argtypes = list(arguments)
        return fn

    context = api("LLVMContextCreate", ptr)()
    create_layout = api("LLVMCreateTargetData", ptr, ctypes.c_char_p)
    layouts = [create_layout(x.encode()) for x in (original, replacement)]
    byte_order = api("LLVMByteOrder", ctypes.c_int, ptr)
    pointer_size = api("LLVMPointerSize", ctypes.c_uint, ptr)
    integer = api("LLVMIntTypeInContext", ptr, ptr, ctypes.c_uint)
    struct_type = api("LLVMStructTypeInContext", ptr, ptr, ctypes.POINTER(ptr), ctypes.c_uint, ctypes.c_int)
    size = api("LLVMABISizeOfType", ctypes.c_ulonglong, ptr, ptr)
    alignment = api("LLVMABIAlignmentOfType", ctypes.c_uint, ptr, ptr)
    offset = api("LLVMOffsetOfElement", ctypes.c_ulonglong, ptr, ptr, ctypes.c_uint)
    try:
        if any(byte_order(dl) != 1 or pointer_size(dl) != 8 for dl in layouts):
            raise RuntimeError("Float runtime requires little-endian layout and 64-bit pointers")
        for width in (8, 16, 32, 64):
            ty = integer(context, width)
            if (size(layouts[0], ty), alignment(layouts[0], ty)) != (size(layouts[1], ty), alignment(layouts[1], ty)):
                raise RuntimeError(f"Incompatible runtime ABI for i{width}")
        for widths in ((8,), (16, 32), (16, 64), (64, 64), (64, 64, 64), (8, 64, 64)):
            fields = (ptr * len(widths))(*(integer(context, n) for n in widths))
            ty = struct_type(context, fields, len(widths), 0)
            signatures = [(size(dl, ty), alignment(dl, ty), *(offset(dl, ty, n) for n in range(len(widths)))) for dl in layouts]
            if signatures[0] != signatures[1]:
                raise RuntimeError(f"Incompatible runtime aggregate ABI for {widths}")
    finally:
        dispose = api("LLVMDisposeTargetData", None, ptr)
        for dl in layouts:
            dispose(dl)
        api("LLVMContextDispose", None, ptr)(context)


def emit_preserving_layout(ir: str, output: Path, clang: str, library_path: str | None) -> None:
    # Clang's IR-to-bitcode driver resets DataLayout to its target-machine
    # layout. Parse/write with LLVM-C instead when an equivalent custom layout
    # was requested; no optimization or target-machine rewrite is required.
    lib = llvm_library(clang, library_path)
    ptr = ctypes.c_void_p
    lib.LLVMContextCreate.restype = ptr
    lib.LLVMCreateMemoryBufferWithMemoryRangeCopy.restype = ptr
    lib.LLVMCreateMemoryBufferWithMemoryRangeCopy.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p]
    lib.LLVMParseIRInContext.argtypes = [ptr, ptr, ctypes.POINTER(ptr), ctypes.POINTER(ptr)]
    lib.LLVMParseIRInContext.restype = ctypes.c_int
    lib.LLVMWriteBitcodeToFile.argtypes = [ptr, ctypes.c_char_p]
    lib.LLVMWriteBitcodeToFile.restype = ctypes.c_int
    lib.LLVMDisposeModule.argtypes = [ptr]
    lib.LLVMContextDispose.argtypes = [ptr]
    lib.LLVMDisposeMessage.argtypes = [ptr]
    context, module, error = lib.LLVMContextCreate(), ptr(), ptr()
    contents = ir.encode("utf-8")
    buffer = lib.LLVMCreateMemoryBufferWithMemoryRangeCopy(contents, len(contents), b"scratch-float")
    try:
        if lib.LLVMParseIRInContext(context, buffer, ctypes.byref(module), ctypes.byref(error)):
            message = ctypes.string_at(error).decode("utf-8", "replace")
            lib.LLVMDisposeMessage(error)
            raise RuntimeError(message)
        if lib.LLVMWriteBitcodeToFile(module, str(output).encode("utf-8")):
            raise RuntimeError(f"Cannot write bitcode to {output}")
    finally:
        if module:
            lib.LLVMDisposeModule(module)
        lib.LLVMContextDispose(context)


def sources() -> list[Path]:
    # The official primitive set is portable C when no opts-GCC.h is used.
    makefile = (RUNTIME / "upstream/build/Linux-x86_64-GCC/Makefile").read_text()
    primitive_section = makefile.split("OBJS_PRIMITIVES =", 1)[1].split("OBJS_SPECIALIZE", 1)[0]
    names = set(re.findall(r"(\w+)\$\(OBJ\)", primitive_section))
    names.add("softfloat_state")
    for bits in (32, 64):
        for op in ("add", "sub", "mul", "div", "sqrt", "mulAdd", "rem", "roundToInt",
                   "eq", "lt_quiet", "le_quiet"):
            names.add(f"f{bits}_{op}")
        for integer in ("i32", "ui32", "i64", "ui64"):
            names.add(f"{integer}_to_f{bits}")
            names.add(f"f{bits}_to_{integer}_r_minMag")
        names.add(f"f{bits}_to_f{96 - bits}")
    for source in SOURCE.glob("s_*.c"):
        if re.search(r"(?:F32|F64)", source.stem):
            names.add(source.stem)
    selected = [SOURCE / (name + ".c") for name in sorted(names)]
    for source in sorted((SOURCE / "8086-SSE").glob("*.c")):
        if re.search(r"(?:F32|F64)", source.stem, re.IGNORECASE) or source.stem == "softfloat_raiseFlags":
            selected.append(source)
    missing = [str(p) for p in selected if not p.is_file()]
    if missing:
        raise RuntimeError("Missing vendored sources: " + ", ".join(missing))
    return selected


def write_amalgamation(destination: Path) -> list[Path]:
    selected = sources()
    lines = ["/* Generated by tools/build_float_runtime.py; do not edit. */"]
    for source in selected + [RUNTIME / "runtime.c"]:
        lines.append('#include "' + source.as_posix() + '"')
    destination.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return selected


def audit_ir(ir: str) -> dict:
    forbidden = re.findall(
        r"^\s*(?:%[\w.]+\s*=\s*)?(fadd|fsub|fmul|fdiv|frem|fneg|fcmp|"
        r"fptosi|fptoui|sitofp|uitofp|fptrunc|fpext)\b", ir, re.MULTILINE)
    if forbidden or re.search(r"\basm\b", ir):
        raise RuntimeError("Runtime unexpectedly contains floating-point instructions or inline assembly")
    declarations = re.findall(r"^declare\b[^\n]*?@([^ (]+)", ir, re.MULTILINE)
    external = [name for name in declarations if not name.startswith("llvm.")]
    if external:
        raise RuntimeError("Runtime has unresolved external dependencies: " + ", ".join(external))
    exports = sorted(set(re.findall(r"^define\b[^\n]*?@(__sclr_(?!sf_)\w+)", ir, re.MULTILINE)))
    if len(exports) != 50:
        raise RuntimeError(f"Expected 50 wrapper exports, found {len(exports)}")
    return {"exports": exports, "llvm_intrinsics": sorted(set(declarations)),
            "function_count": len(re.findall(r"^define\b", ir, re.MULTILINE)),
            "integer_only": True}


def default_clang() -> str:
    return resolve_clang()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang", help="Clang executable (otherwise SCRATCH_CLANG, CLANG, or PATH)")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/runtime")
    parser.add_argument("--target", default="x86_64-unknown-linux-gnu")
    parser.add_argument("--data-layout", help="equivalent module DataLayout to use; ABI checked using LLVM-C")
    parser.add_argument("--llvm-library", help="LLVM shared library for optional DataLayout validation")
    parser.add_argument("--host-test", action="store_true", help="also build a native test library and run differential tests")
    parser.add_argument("--host-linker", help="native test linker flavor (e.g. lld), or linker executable path")
    parser.add_argument("--host-sysroot", help="optional sysroot for the native test only; does not change the guest ABI")
    args = parser.parse_args()
    clang = resolve_clang(args.clang)
    # Validate an explicit library even when no custom DataLayout is requested;
    # silently accepting a stale CMake/toolchain selection hides configuration bugs.
    if args.llvm_library or os.environ.get("SCRATCH_LLVM_LIBRARY") or os.environ.get("LLVM_LIBRARY"):
        resolve_llvm_library(clang, args.llvm_library)
    verify_upstream()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    amalgamation = output / "softfloat-amalgamation.c"
    selected = write_amalgamation(amalgamation)
    flags = ["-std=c11", "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
             "-O1", "-fno-vectorize", "-fno-slp-vectorize", "-fno-unroll-loops",
             "-Werror=implicit-function-declaration", "-I", str(RUNTIME),
             "-I", str(SOURCE / "include"), "-I", str(SOURCE / "8086-SSE")]
    ll = output / "scratch-float.ll"
    bc = output / "scratch-float.bc"
    subprocess.run([clang, "--target=" + args.target, *flags, "-S", "-emit-llvm",
                    str(amalgamation), "-o", str(ll)], check=True)
    ir = ll.read_text(encoding="utf-8")
    original_layout = re.search(r'^target datalayout = "([^"]+)"', ir, re.MULTILINE).group(1)
    if args.data_layout:
        compatible_layout(original_layout, args.data_layout, clang, args.llvm_library)
        ir = ir.replace(f'target datalayout = "{original_layout}"', f'target datalayout = "{args.data_layout}"')
    # Reserve upstream globals/functions at LLVM symbol level, after C's
    # conditional implementation macros have selected their portable bodies.
    ir = re.sub(r"@((?:softfloat_\w+|(?:f(?:16|32|64|128)M?|extF80M?|u?i(?:32|64))_\w+|extF80_roundingPrecision))\b",
                r"@__sclr_sf_\1", ir)
    ll.write_text(ir, encoding="utf-8")
    audit = audit_ir(ir)
    if args.data_layout:
        emit_preserving_layout(ir, bc, clang, args.llvm_library)
    else:
        subprocess.run([clang, "--target=" + args.target, "-Wno-override-module", "-c",
                        "-emit-llvm", str(ll), "-o", str(bc)], check=True)
    audit.update({"target": args.target,
                  "data_layout": args.data_layout or original_layout,
                  "source_data_layout": original_layout,
                  "layout_requirements": {"byte_order": "little", "pointer_bits": 64,
                                          "integer_abi_align_bits": {"8": 8, "16": 16, "32": 32, "64": 64}},
                  "clang": subprocess.check_output([clang, "--version"], text=True).splitlines()[0],
                  "bitcode_sha256": hashlib.sha256(bc.read_bytes()).hexdigest(),
                  "source_files": [p.relative_to(ROOT).as_posix() for p in selected]})
    (output / "scratch-float.json").write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    # Keep the distributable license adjacent to the runtime artifact. Package
    # and install rules need not infer the original source checkout location.
    shutil.copyfile(RUNTIME / "upstream" / "COPYING.txt", output / "SoftFloat-LICENSE.txt")
    print(f"Built {bc}: {audit['function_count']} integer-only functions; {len(audit['exports'])} wrapper exports")
    print("LLVM intrinsics: " + ", ".join(audit["llvm_intrinsics"]))
    if args.host_test:
        target = clang_default_target(clang)
        library = output / ("scratch-float-test" + host_library_suffix(target))
        host_flags = host_shared_library_flags(target, args.host_linker, args.host_sysroot)
        host_flags.append("-DSCLR_TEST_EXPORT")
        print(f"Native runtime test target: {target} (guest target remains {args.target})", flush=True)
        subprocess.run([clang, *flags, *host_flags, str(amalgamation),
                        *write_host_support(target, output), "-o", str(library)], check=True)
        subprocess.run([sys.executable, str(RUNTIME / "test_runtime.py"), str(library)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
