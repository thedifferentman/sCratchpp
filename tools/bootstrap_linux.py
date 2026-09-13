#!/usr/bin/env python3
"""Prepare pinned Ubuntu 24.04 x86_64 LLVM packages without sudo or apt changes.

This is an optional reproducible validation toolchain, not a Linux distribution
installer. The host supplies Python 3, dpkg-deb, CMake, Ninja, glibc development
files and the ordinary Ubuntu LLVM shared-library dependencies. Other Linux
distributions can use their own LLVM installation with the project's CMake API.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
import tarfile
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
    print("Downloading", destination.name, flush=True)
    with urllib.request.urlopen(item["url"], timeout=120) as response:
        with temporary.open("wb") as output:
            shutil.copyfileobj(response, output)
    actual = digest(temporary)
    if actual != item["sha256"]:
        raise RuntimeError(f"SHA256 mismatch for {destination.name}: {actual}")
    temporary.replace(destination)
    return destination


def run(arguments: list[str], env: dict[str, str] | None = None) -> None:
    print("+", shlex.join(arguments), flush=True)
    subprocess.run(arguments, check=True, env=env)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prefix", type=Path,
                        default=Path.home() / ".cache/scratch-llvm/llvm-22.1.8-noble")
    parser.add_argument("--with-node", action="store_true",
                        help="Also extract the pinned Node.js test runtime")
    options = parser.parse_args()
    if platform.system() != "Linux" or platform.machine() not in ("x86_64", "amd64"):
        parser.error("this optional bootstrap supports Ubuntu 24.04 x86_64 only")
    release = platform.freedesktop_os_release()
    if release.get("ID") != "ubuntu" or release.get("VERSION_ID") != "24.04":
        parser.error("these pinned packages require Ubuntu 24.04; use a native LLVM package on other hosts")
    for command in ("dpkg-deb", "cmake", "ninja"):
        if not shutil.which(command):
            parser.error(f"required host command is missing: {command}")

    lock_path = Path(__file__).with_name("linux-toolchain.lock.json")
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    prefix = options.prefix.expanduser().resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    downloads = prefix / "downloads"
    downloads.mkdir(exist_ok=True)
    items = lock["packages"] + ([lock["node"]] if options.with_node else [])
    with ThreadPoolExecutor(max_workers=4) as executor:
        archives = list(executor.map(lambda item: download(item, downloads), items))
    root = prefix / "root"
    root.mkdir(exist_ok=True)
    for item, archive in zip(lock["packages"], archives):
        marker = prefix / (item["name"] + ".sha256")
        if not marker.is_file() or marker.read_text().strip() != item["sha256"]:
            run(["dpkg-deb", "--extract", str(archive), str(root)])
            marker.write_text(item["sha256"] + "\n")

    # Debian packages occasionally contain absolute symlinks. Relocate only
    # links whose target actually exists inside this private extraction root.
    for link in root.rglob("*"):
        if link.is_symlink():
            target = Path(os.readlink(link))
            private = root / str(target).lstrip("/")
            if target.is_absolute() and private.exists():
                replacement = os.path.relpath(private, link.parent)
                link.unlink()
                link.symlink_to(replacement)

    llvm = root / "usr/lib/llvm-22"
    libraries = [llvm / "lib", root / "usr/lib/x86_64-linux-gnu"]
    node = prefix / ("node-v" + lock["node"]["version"] + "-linux-x64")
    if options.with_node and not (node / "bin/node").is_file():
        with tarfile.open(archives[-1], "r:xz") as archive:
            archive.extractall(prefix, filter="data")
    binary_paths = [llvm / "bin", root / "usr/bin"]
    if (node / "bin/node").is_file():
        binary_paths.insert(0, node / "bin")
    llvm_library = root / "usr/lib/x86_64-linux-gnu/libLLVM.so.22.1"
    if not llvm_library.is_file():
        candidates = sorted((root / "usr/lib/x86_64-linux-gnu").glob("libLLVM*.so*"))
        llvm_library = next((path for path in candidates if path.is_file()), llvm_library)
    if not llvm_library.is_file():
        raise RuntimeError("the pinned LLVM C API shared library was not extracted")
    # The large llvm-dev package normally supplies these developer aliases.
    # Only the shared C API is required here; expose it in LLVM_ROOT/lib without
    # downloading unrelated static backend archives or changing the binary.
    for name in ("libLLVM.so", "libLLVM.so.22.1"):
        link = llvm / "lib" / name
        if not link.exists() and not link.is_symlink():
            link.symlink_to(os.path.relpath(llvm_library, link.parent))
    values = {
        "LLVM_ROOT": str(llvm),
        "SCRATCH_STDLIB_ROOT": str(llvm),
        "CC": str(llvm / "bin/clang"),
        "CXX": str(llvm / "bin/clang++"),
        "SCRATCH_CLANG": str(llvm / "bin/clang"),
        "SCRATCH_LLVM_LIBRARY": str(llvm_library),
    }
    environment = os.environ.copy()
    environment.update(values)
    for name, paths in (("PATH", binary_paths), ("LD_LIBRARY_PATH", libraries)):
        parts = list(map(str, paths))
        if environment.get(name):
            parts.append(environment[name])
        environment[name] = ":".join(parts)
    activation = ["# Generated private LLVM toolchain. Source this file in a POSIX shell."]
    activation.extend(f"export {name}={shlex.quote(value)}" for name, value in values.items())
    for name, paths in (("PATH", binary_paths), ("LD_LIBRARY_PATH", libraries)):
        activation.append(f"export {name}={shlex.quote(':'.join(map(str, paths)))}\"${{{name}:+:${name}}}\"")
    (prefix / "activate.sh").write_text("\n".join(activation) + "\n")
    shutil.copyfile(lock_path, prefix / "toolchain.lock.json")
    run([str(llvm / "bin/clang"), "--version"], environment)
    run([str(llvm / "bin/ld.lld"), "--version"], environment)
    smoke = prefix / "libcxx-smoke.cpp"
    smoke.write_text('#include <string>\n#include <iostream>\n'
                     'int main() { std::cout << std::string("libc++ OK") << "\\n"; }\n')
    run([str(llvm / "bin/clang++"), "-std=c++17", "-stdlib=libc++",
         "-fuse-ld=lld", "--rtlib=compiler-rt", "--unwindlib=libunwind",
         str(smoke), "-lc++abi", "-o",
         str(prefix / "libcxx-smoke")], environment)
    run([str(prefix / "libcxx-smoke")], environment)
    if options.with_node:
        run([str(node / "bin/node"), "--version"], environment)
    print("Prepared:", prefix)
    print("Activate: .", shlex.quote(str(prefix / "activate.sh")))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"bootstrap_linux: {error}", file=sys.stderr)
        raise SystemExit(1)
