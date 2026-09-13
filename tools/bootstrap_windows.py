#!/usr/bin/env python3
"""Prepare a pinned native Clang SDK without Visual Studio, MSYS or an installer.

Python 3.14+ is required for its standard-library Zstandard support. Packages
come from the official MSYS2 CLANG64 repository, but only native Windows files
are extracted. No package install scripts are executed or system settings edited.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request


def digest(path: Path) -> str:
    checksum = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(chunk)
    return checksum.hexdigest()


def download(item: dict, cache: Path) -> Path:
    destination = cache / item["url"].rsplit("/", 1)[1]
    if destination.is_file() and digest(destination) == item["sha256"]:
        return destination
    temporary = destination.with_suffix(destination.suffix + ".part")
    for attempt in range(3):
        try:
            print("Downloading", destination.name, flush=True)
            request = urllib.request.Request(item["url"], headers={"User-Agent": "scratch-llvm-bootstrap/1"})
            with urllib.request.urlopen(request, timeout=120) as response:
                with temporary.open("wb") as output:
                    shutil.copyfileobj(response, output)
            actual = digest(temporary)
            if actual != item["sha256"]:
                raise RuntimeError(f"SHA256 mismatch for {destination.name}: {actual}")
            temporary.replace(destination)
            return destination
        except (OSError, TimeoutError):
            if attempt == 2:
                raise
            time.sleep(2 ** attempt)
    raise AssertionError("unreachable")


def checked_path(root: Path, name: str) -> Path:
    # Accept POSIX archive names only. Reject drive paths, traversal, ADS and
    # Windows separator tricks before resolving the destination or link target.
    if "\\" in name or ":" in name:
        raise ValueError(f"Invalid archive path: {name}")
    posix = PurePosixPath(name)
    if posix.is_absolute() or ".." in posix.parts:
        raise ValueError(f"Unsafe archive path: {name}")
    destination = root.joinpath(*posix.parts).resolve()
    if not destination.is_relative_to(root):
        raise ValueError(f"Archive path escapes SDK: {name}")
    return destination


def extract(archive: Path, root: Path, links: list[tuple[Path, Path]]) -> None:
    print("Extracting", archive.name, flush=True)
    with tarfile.open(archive, "r:*") as bundle:
        for member in bundle:
            # Package metadata and .INSTALL scripts are deliberately ignored.
            name = member.name.removeprefix("./")
            if name != "clang64" and not name.startswith("clang64/"):
                if name.startswith(".") and "/" not in name:
                    continue
                raise ValueError(f"Unexpected package root: {name}")
            destination = checked_path(root, name)
            if member.isdir():
                destination.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                destination.parent.mkdir(parents=True, exist_ok=True)
                with bundle.extractfile(member) as source, destination.open("wb") as output:
                    shutil.copyfileobj(source, output)
            elif member.issym() or member.islnk():
                target_name = member.linkname
                if "\\" in target_name or ":" in target_name:
                    raise ValueError(f"Invalid archive link: {target_name}")
                if member.islnk() or target_name.startswith("/"):
                    target = checked_path(root, target_name.lstrip("/"))
                else:
                    target = (destination.parent / target_name).resolve()
                    if not target.is_relative_to(root):
                        raise ValueError(f"Archive link escapes SDK: {name}")
                links.append((destination, target))
            else:
                raise ValueError(f"Unsupported archive entry: {name}")


def materialize_links(links: list[tuple[Path, Path]]) -> None:
    # Windows developer mode/admin rights are not required: links become copies.
    pending = links
    while pending:
        remaining = []
        for destination, target in pending:
            if not target.exists():
                remaining.append((destination, target))
                continue
            destination.parent.mkdir(parents=True, exist_ok=True)
            if target.is_dir():
                shutil.copytree(target, destination, dirs_exist_ok=True)
            elif destination != target:
                shutil.copyfile(target, destination)
        if len(remaining) == len(pending):
            raise RuntimeError("Unresolved SDK links: " + ", ".join(str(p[0]) for p in remaining[:10]))
        pending = remaining


def run(arguments: list[str], env: dict[str, str], cwd: Path | None = None,
        echo: bool = True) -> str:
    if echo:
        print("+", subprocess.list2cmdline(arguments), flush=True)
    result = subprocess.run(arguments, env=env, cwd=cwd, check=True,
                            text=True, encoding="utf-8", errors="replace", capture_output=True)
    if echo and result.stdout.strip():
        print(result.stdout.strip(), flush=True)
    if result.stderr.strip():
        print(result.stderr.strip(), flush=True)
    return result.stdout


def verify(sdk: Path, prefix: Path, library: Path) -> dict:
    # Intentionally exclude all developer tool directories and INCLUDE/LIB.
    # The probe may use Windows system DLLs (including OS UCRT), not MSVC SDKs.
    env = dict(os.environ)
    for key in list(env):
        if key.upper() in {"INCLUDE", "LIB", "LIBPATH", "CL", "_CL_", "LINK", "CFLAGS", "CXXFLAGS", "LDFLAGS"}:
            del env[key]
    system = Path(env.get("SystemRoot", r"C:\Windows"))
    env["PATH"] = os.pathsep.join(str(p) for p in (sdk / "bin", system / "System32", system))
    versions = {}
    for name in ("clang", "ld.lld", "cmake", "ninja"):
        versions[name] = run([str(sdk / "bin" / (name + ".exe")), "--version"], env).splitlines()[0]
    probe = prefix / "verify"
    probe.mkdir(exist_ok=True)
    source = probe / "sdk_probe.cpp"
    source.write_text('''#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <llvm-c/Core.h>
int main() {
  const auto path = std::filesystem::current_path();
  auto context = LLVMContextCreate();
  auto module = LLVMModuleCreateWithNameInContext("sdk_probe", context);
  LLVMDisposeModule(module);
  LLVMContextDispose(context);
  try { throw std::runtime_error("libc++ exception OK"); }
  catch (const std::exception& error) { std::cout << error.what() << "\\n"; }
  if (path.empty()) return 1;
  std::cout << "LLVM C API + filesystem OK\\n";
}
''', encoding="utf-8")
    executable = probe / "sdk_probe.exe"
    run([str(sdk / "bin/clang++.exe"), "-std=c++17", "-stdlib=libc++", "-fuse-ld=lld",
         str(source), str(library), "-I" + str(sdk / "include"), "-o", str(executable)], env)
    output = run([str(executable)], env, prefix)
    pending = [executable, sdk / "bin/clang.exe", sdk / "bin/ld.lld.exe",
               sdk / "bin/cmake.exe", sdk / "bin/ninja.exe"]
    imports = {}
    while pending:
        image = pending.pop()
        if image.name.lower() in imports:
            continue
        listing = run([str(sdk / "bin/llvm-readobj.exe"), "--coff-imports", str(image)], env, echo=False)
        dependencies = re.findall(r"^\s*Name: (.+\.dll)\s*$", listing, re.MULTILINE | re.IGNORECASE)
        for dependency in dependencies:
            if re.match(r"(?:msvcp\d|msvcr\d|vcruntime\d|msys-)", dependency, re.IGNORECASE):
                raise RuntimeError(f"Unexpected MSVC/MSYS runtime: {image.name} -> {dependency}")
            local = sdk / "bin" / dependency
            if local.is_file() and dependency.lower() not in imports:
                pending.append(local)
        imports[image.name.lower()] = dependencies
    versions["imports"] = imports
    print("Verified native DLL closure:", len(imports), "images; no MSVC C++/MSYS runtime", flush=True)
    versions["probe"] = output.strip()
    versions["sanitized_path"] = env["PATH"]
    versions["compiler_target"] = run([str(sdk / "bin/clang.exe"), "-dumpmachine"], env).strip()
    return versions


def activate(prefix: Path, sdk: Path, library: Path) -> None:
    values = {
        "SCRATCH_CLANG": str(sdk / "bin/clang.exe"),
        "SCRATCH_LLVM_LIBRARY": str(sdk / "bin/libLLVM-22.dll"),
        "CC": str(sdk / "bin/clang.exe"),
        "CXX": str(sdk / "bin/clang++.exe"),
        "SCRATCH_WINDOWS_SDK": str(sdk),
        "LLVM_ROOT": str(sdk),
        "SCRATCH_TOOLCHAIN_ROOT": str(sdk),
        "SCRATCH_STDLIB_ROOT": str(sdk),
    }
    powershell = ["# Dot-source this file: . ./build/toolchains/windows/activate.ps1"]
    powershell.append("$env:PATH = '" + str(sdk / "bin").replace("'", "''") + ";' + $env:PATH")
    powershell.extend("$env:" + k + " = '" + v.replace("'", "''") + "'" for k, v in values.items())
    (prefix / "activate.ps1").write_text("\n".join(powershell) + "\n", encoding="utf-8-sig")
    batch = ['@echo off', 'set "PATH=' + str(sdk / "bin") + ';%PATH%"']
    batch.extend('set "' + k + '=' + v + '"' for k, v in values.items())
    (prefix / "activate.cmd").write_text("\n".join(batch) + "\n", encoding="utf-8")
    toolchain = [
        "# Generated by tools/bootstrap_windows.py; native Windows GNU ABI.",
        'set(CMAKE_C_COMPILER "' + (sdk / "bin/clang.exe").as_posix() + '" CACHE FILEPATH "")',
        'set(CMAKE_CXX_COMPILER "' + (sdk / "bin/clang++.exe").as_posix() + '" CACHE FILEPATH "")',
        'set(CMAKE_MAKE_PROGRAM "' + (sdk / "bin/ninja.exe").as_posix() + '" CACHE FILEPATH "")',
        'set(CMAKE_CXX_FLAGS_INIT "-stdlib=libc++")',
        'set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")',
        'set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")',
        'set(CMAKE_PREFIX_PATH "' + sdk.as_posix() + '" CACHE STRING "")',
    ]
    (prefix / "clang-toolchain.cmake").write_text("\n".join(toolchain) + "\n", encoding="utf-8")
    metadata = dict(prefix=str(sdk), clang=values["SCRATCH_CLANG"],
                    llvm_root=values["LLVM_ROOT"],
                    toolchain_root=values["SCRATCH_TOOLCHAIN_ROOT"],
                    stdlib_root=values["SCRATCH_STDLIB_ROOT"],
                    llvm_shared_library=values["SCRATCH_LLVM_LIBRARY"],
                    llvm_import_library=str(library),
                    llvm_include_dir=str(sdk / "include"),
                    cmake=str(sdk / "bin/cmake.exe"), ninja=str(sdk / "bin/ninja.exe"))
    (prefix / "toolchain.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    # PowerShell/piped execution should receive UTF-8 even on a legacy ACP host.
    sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    sys.stderr.reconfigure(encoding="utf-8", errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path, default=Path(__file__).resolve().parents[1] / "build/toolchains/windows")
    parser.add_argument("--skip-verify", action="store_true", help="Extract only; do not execute the SDK probe")
    options = parser.parse_args()
    if platform.system() != "Windows" or platform.machine().lower() not in ("amd64", "x86_64"):
        parser.error("this SDK targets native Windows x86_64 only")
    if sys.version_info < (3, 14):
        parser.error("Python 3.14+ is required for standard-library .tar.zst extraction")
    lock_path = Path(__file__).with_name("toolchain-lock-windows.json")
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    prefix = options.prefix.expanduser().resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    cache = prefix / "downloads"
    cache.mkdir(exist_ok=True)
    with ThreadPoolExecutor(max_workers=6) as executor:
        archives = list(executor.map(lambda item: download(item, cache), lock["packages"]))
    root = prefix / "root"
    root.mkdir(exist_ok=True)
    marker = prefix / "extracted-lock.sha256"
    lock_hash = digest(lock_path)
    if marker.is_file() and marker.read_text().strip() != lock_hash:
        raise RuntimeError("This prefix contains a different SDK lock; choose a new empty --prefix")
    if not marker.is_file() or marker.read_text().strip() != lock_hash:
        links = []
        for archive in archives:
            extract(archive, root, links)
        materialize_links(links)
        marker.write_text(lock_hash + "\n", encoding="ascii")
    sdk = root / "clang64"
    candidates = [sdk / "lib/libLLVM.dll.a", sdk / "lib/libLLVM-22.dll.a"]
    library = next((p for p in candidates if p.is_file()), None)
    if library is None:
        raise RuntimeError("SDK does not contain the LLVM C API import library")
    activate(prefix, sdk, library)
    if not options.skip_verify:
        verification = verify(sdk, prefix, library)
        (prefix / "verification.json").write_text(json.dumps(verification, indent=2) + "\n", encoding="utf-8")
    print("SDK ready:", sdk)
    print("PowerShell activation: . '" + str(prefix / "activate.ps1") + "'")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        if isinstance(error, subprocess.CalledProcessError):
            print(error.stdout or "", file=sys.stderr)
            print(error.stderr or "", file=sys.stderr)
        raise SystemExit(1)
