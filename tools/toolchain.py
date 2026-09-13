"""Discover host tools and construct native test flags without assuming an SDK.

The Scratch guest ABI is selected separately by callers. A Clang executable's
default target, not Python's operating system, determines its native link ABI.
"""
from __future__ import annotations

import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess


def resolve_executable(value: str, description: str) -> str:
    candidate = Path(value).expanduser()
    if candidate.is_file():
        return str(candidate.resolve())
    found = shutil.which(value)
    if found:
        return str(Path(found).resolve())
    raise RuntimeError(f"{description} does not exist or is not executable: {value}")


def resolve_clang(explicit: str | None = None) -> str:
    requested = explicit or os.environ.get("SCRATCH_CLANG") or os.environ.get("CLANG")
    if requested:
        # A typo in a selected toolchain must not silently select another one.
        return resolve_executable(requested, "Selected Clang")
    for name in ("clang-22", "clang"):
        found = shutil.which(name)
        if found:
            return str(Path(found).resolve())
    raise RuntimeError("Clang is not on PATH; pass --clang or set SCRATCH_CLANG")


def resolve_llvm_library(clang: str, explicit: str | None = None) -> str:
    requested = (explicit or os.environ.get("SCRATCH_LLVM_LIBRARY")
                 or os.environ.get("LLVM_LIBRARY"))
    if requested:
        candidate = Path(requested).expanduser()
        if not candidate.is_file():
            raise RuntimeError(f"Selected LLVM shared library does not exist: {requested}")
        return str(candidate.resolve())

    binary_dir = Path(clang).resolve().parent
    # Accommodate upstream Windows, LLVM-MinGW-adjacent installations, distro
    # ELF packages, and Homebrew/other macOS LLVM installations. Versioned SONAME
    # files need not have an unversioned developer-package symlink.
    patterns = (
        "LLVM-C.dll", "libLLVM-22.dll", "libLLVM.dll",
        "libLLVM-22.so", "libLLVM-22.so.*", "libLLVM.so.22*", "libLLVM.so",
        "libLLVM-22.dylib", "libLLVM.22*.dylib", "libLLVM.dylib",
    )
    for directory in (binary_dir, binary_dir.parent / "lib", binary_dir.parent / "lib64"):
        for pattern in patterns:
            for candidate in sorted(directory.glob(pattern)):
                if candidate.is_file():
                    return str(candidate.resolve())
    for name in ("LLVM-22", "LLVM"):
        candidate = ctypes.util.find_library(name)
        if candidate:
            return candidate
    raise RuntimeError("LLVM shared library was not found; pass --llvm-library or set SCRATCH_LLVM_LIBRARY")


def clang_default_target(clang: str) -> str:
    target = subprocess.check_output([clang, "-dumpmachine"], text=True).strip()
    if not target or any(character.isspace() for character in target):
        raise RuntimeError(f"Clang returned an invalid default target: {target!r}")
    return target


def host_abi(target: str) -> str:
    target = target.lower()
    if "windows" in target or "mingw" in target:
        if "msvc" in target:
            return "msvc"
        if "gnu" in target or "mingw" in target:
            return "mingw"
        raise RuntimeError(f"Unsupported Windows host ABI: {target}")
    if "darwin" in target or "apple" in target:
        return "darwin"
    if any(part in target for part in ("linux", "freebsd", "netbsd", "openbsd", "dragonfly")):
        return "elf"
    raise RuntimeError(f"Unsupported native test target: {target}")


def host_library_suffix(target: str) -> str:
    return {"msvc": ".dll", "mingw": ".dll", "darwin": ".dylib", "elf": ".so"}[host_abi(target)]


def host_shared_library_flags(target: str, linker: str | None = None,
                              sysroot: str | None = None) -> list[str]:
    abi = host_abi(target)
    flags = {
        "msvc": ["-shared", "-nostdlib", "-Wl,/noentry"],
        "mingw": ["-shared", "-nostdlib", "-Wl,--entry,__sclr_native_dll_entry"],
        "darwin": ["-dynamiclib", "-nostdlib", "-fPIC"],
        "elf": ["-shared", "-nostdlib", "-fPIC", "-Wl,-z,defs"],
    }[abi]
    if linker:
        # --ld-path selects an exact executable; -fuse-ld selects a driver flavor.
        if Path(linker).is_file() or "/" in linker or "\\" in linker:
            flags.append("--ld-path=" + resolve_executable(linker, "Selected host linker"))
        else:
            flags.append("-fuse-ld=" + linker)
    elif abi in ("msvc", "mingw"):
        # Both independent Windows toolchains can link these freestanding test
        # DLLs with LLD. No Microsoft import library, CRT, or SDK is requested.
        flags.append("-fuse-ld=lld")
    if sysroot:
        directory = Path(sysroot).expanduser()
        if not directory.is_dir():
            raise RuntimeError(f"Selected host sysroot does not exist: {sysroot}")
        flags.append("--sysroot=" + str(directory.resolve()))
    return flags


def write_host_support(target: str, directory: Path) -> list[str]:
    """Supply a minimal CRT-free DLL loader entry for native MinGW tests only.

    MinGW LLD interprets --entry=0 as a symbol, not a numeric address. Supplying
    a successful no-op loader callback keeps these native reference libraries
    independent of CRT startup code without using MSVC linker arguments.
    """
    if host_abi(target) != "mingw":
        return []
    support = directory / "scratch-native-dll-entry.c"
    support.write_text(
        "/* Native test loader glue only; never linked into guest bitcode. */\n"
        "#if defined(__i386__)\n"
        "#define SCLR_DLL_CALL __attribute__((stdcall))\n"
        "#else\n"
        "#define SCLR_DLL_CALL\n"
        "#endif\n"
        "int SCLR_DLL_CALL __sclr_native_dll_entry(void *module, unsigned long reason, void *reserved) {\n"
        "  (void)module; (void)reason; (void)reserved; return 1;\n"
        "}\n", encoding="utf-8")
    return [str(support)]
