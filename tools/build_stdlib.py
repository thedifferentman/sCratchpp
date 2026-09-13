"""Build the portable, no-exception Scratch libc++ SDK (no host C/C++ SDK).

The installed manifest is also consumed by the VS Code template.  All paths in
it are relative to the SDK, so copying/installing the directory is sufficient.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from toolchain import resolve_clang, resolve_executable

ROOT = Path(__file__).resolve().parents[1]
LIBCXX = ROOT / "third_party" / "libcxx"
RUNTIME = ROOT / "runtime" / "stdlib"
TARGET = "x86_64-unknown-linux-gnu"
COMMON_FLAGS = ["--target=" + TARGET, "-fno-exceptions", "-fno-rtti",
                "-fno-stack-protector", "-fno-builtin",
                "-nostdinc", "-nostdinc++"]
SOURCES = ["string", "vector", "memory", "functional", "optional", "variant",
           "hash", "algorithm"]


def verify_sources() -> None:
    for line in (LIBCXX / "SOURCE_SHA256SUMS").read_text(encoding="utf-8").splitlines():
        expected, relative = line.split("  ", 1)
        source = LIBCXX / relative
        if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise RuntimeError("Vendored libc++ input differs from the pinned release/configuration: " + relative)


def compiler_flags(clang: str, sdk: Path) -> list[str]:
    manifest = json.loads((sdk / "manifest.json").read_text(encoding="utf-8"))
    return _compiler_flags(clang, sdk, manifest)


def _compiler_flags(clang: str, sdk: Path, manifest: dict) -> list[str]:
    resource = Path(subprocess.check_output([clang, "-print-resource-dir"], text=True, encoding="utf-8").strip())
    flags = list(manifest["compile_flags"])
    for relative in manifest["include_dirs"]:
        flags += ["-isystem", str((sdk / relative).resolve())]
    flags += ["-isystem", str(resource / "include")]
    return flags


def build(clang: str, output: Path, llvm_link: str | None = None,
          heap_bytes: int = 16384) -> None:
    output = output.resolve()
    # This file is the build-system completion marker. Never leave a successful
    # marker for a partially rebuilt SDK (including failed source compilation).
    manifest_path = output / "manifest.json"
    manifest_path.unlink(missing_ok=True)
    verify_sources()
    version = subprocess.check_output([clang, "--version"], text=True, encoding="utf-8")
    if "clang version 22." not in version:
        raise RuntimeError("The pinned Scratch libc++ SDK requires Clang 22")
    link = llvm_link or os.environ.get("SCRATCH_LLVM_LINK")
    if not link:
        suffix = ".exe" if Path(clang).suffix.lower() == ".exe" else ""
        sibling = Path(clang).parent / ("llvm-link" + suffix)
        link = str(sibling) if sibling.is_file() else "llvm-link-22"
    link = resolve_executable(link, "LLVM bitcode linker")
    link_version = subprocess.check_output([link, "--version"], text=True, encoding="utf-8")
    if "LLVM version 22." not in link_version:
        raise RuntimeError("The pinned Scratch libc++ SDK requires llvm-link 22")
    headers = output / "include" / "c++" / "v1"
    headers.mkdir(parents=True, exist_ok=True)
    shutil.copytree(LIBCXX / "include", headers, dirs_exist_ok=True)
    shutil.copytree(RUNTIME / "include", output / "include", dirs_exist_ok=True)
    (output / "lib").mkdir(exist_ok=True)
    (output / "licenses").mkdir(exist_ok=True)
    shutil.copyfile(LIBCXX / "LICENSE.TXT", output / "licenses" / "libcxx-LICENSE.TXT")
    manifest = {"version": 1, "libcxx_version": "22.1.8", "target": TARGET,
                "compile_flags": COMMON_FLAGS, "include_dirs": ["include/c++/v1", "include"],
                "bitcode": ["lib/scratch-stdlib.bc"], "licenses": ["licenses/libcxx-LICENSE.TXT"],
                "heap_bytes": heap_bytes,
                "features": {"exceptions": False, "rtti": False, "threads": False,
                             "localization": False, "filesystem": False}}
    objects = output / "objects"
    objects.mkdir(exist_ok=True)
    flags = _compiler_flags(clang, output, manifest)
    inputs = [LIBCXX / "src" / (name + ".cpp") for name in SOURCES]
    inputs += sorted((RUNTIME / "src").glob("*.cpp"))
    bitcode = []
    for index, source in enumerate(inputs):
        artifact = objects / (str(index) + "-" + source.stem + ".bc")
        command = [clang, *flags, "-ffreestanding", "-std=c++20", "-O1", "-fno-vectorize", "-fno-slp-vectorize",
                   "-D_LIBCPP_BUILDING_LIBRARY", "-DSCRATCH_HEAP_BYTES=" + str(heap_bytes),
                   "-I", str(LIBCXX / "src"), "-emit-llvm", "-c", str(source), "-o", str(artifact)]
        print("Compiling " + source.name, flush=True)
        subprocess.run(command, check=True)
        bitcode.append(str(artifact))
    subprocess.run([link, *bitcode, "-o", str(output / manifest["bitcode"][0])], check=True)
    temporary_manifest = output / "manifest.json.tmp"
    temporary_manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    temporary_manifest.replace(manifest_path)
    print("Scratch libc++ SDK: " + str(output), flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clang")
    parser.add_argument("--llvm-link")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build" / "stdlib")
    parser.add_argument("--heap-bytes", type=int, default=16384)
    args = parser.parse_args()
    if args.heap_bytes < 1024 or args.heap_bytes > 196608:
        parser.error("--heap-bytes must be between 1024 and 196608")
    try:
        build(resolve_clang(args.clang), args.output_dir, args.llvm_link, args.heap_bytes)
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print("error: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

