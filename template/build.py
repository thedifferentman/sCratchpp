#!/usr/bin/env python3
"""Build every src/**/*.cpp into one Scratch project (Python 3.9+)."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
FLAGS = [
    "--target=x86_64-unknown-linux-gnu", "-std=c++17",
    "-fno-exceptions", "-fno-rtti",
    "-fno-stack-protector",
    "-fno-color-diagnostics", "-I", str(ROOT / "include"),
]


def tool(value):
    """Accept a PATH command or a path relative to this template."""
    if not isinstance(value, str) or not value.strip():
        raise ValueError("Tool paths must be non-empty strings")
    path = Path(value).expanduser()
    if path.is_absolute() or "/" in value or "\\" in value:
        path = (ROOT / path).resolve()
        if path.is_file():
            return str(path)
    else:
        found = shutil.which(value)
        if found:
            return str(Path(found).resolve())
    raise ValueError(f"Tool not found: {value}. Configure toolchain.local.json (see README.md).")


def run(args):
    # No shell: spaces, Unicode, and shell metacharacters in paths stay literal.
    print("+ " + " ".join(json.dumps(arg, ensure_ascii=False) for arg in args), flush=True)
    subprocess.run(args, cwd=ROOT, check=True)


def stdlib_sdk(config, compiler):
    explicit = os.environ.get("SCRATCH_STDLIB_DIR") or config.get("stdlib")
    if explicit:
        candidates = [(ROOT / Path(explicit).expanduser()).resolve()]
    else:
        prefix = Path(compiler).parent
        candidates = [prefix / "stdlib", prefix.parent / "share/scratch-llvm/stdlib",
                      ROOT.parent / "build/stdlib"]
    for directory in candidates:
        manifest = directory / "manifest.json"
        if manifest.is_file():
            return directory, json.loads(manifest.read_text(encoding="utf-8"))
    raise ValueError("Scratch standard library SDK not found. Set stdlib in toolchain.local.json "
                     "or SCRATCH_STDLIB_DIR; see README.md. Host C++ libraries cannot be used here.")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--debug", action="store_true",
                        help="Build unoptimized user code and a source debug map in build/debug")
    options = parser.parse_args(argv)
    config = {}
    local = ROOT / "toolchain.local.json"
    if local.exists():
        config = json.loads(local.read_text(encoding="utf-8-sig"))
        if not isinstance(config, dict) or set(config) - {"clang", "scratch_llvm", "stdlib", "lldb_dap", "lldb_python"}:
            raise ValueError("toolchain.local.json accepts only clang, scratch_llvm, stdlib, lldb_dap and lldb_python")
    clang = tool(os.environ.get("SCRATCH_CLANG") or config.get("clang", "clang++"))
    compiler = tool(os.environ.get("SCRATCH_LLVM") or config.get("scratch_llvm", "scratch-llvm"))
    sdk, manifest = stdlib_sdk(config, compiler)
    resource_dir = subprocess.check_output([clang, "-print-resource-dir"], text=True, encoding="utf-8").strip()
    library_flags = ["-nostdinc", "-isystem", str(sdk / "include/c++/v1"),
                     "-isystem", str(sdk / "include"),
                     "-isystem", str(Path(resource_dir) / "include")]
    library_flags += manifest.get("compile_flags", [])
    sources = sorted((ROOT / "src").rglob("*.cpp"))
    if not sources:
        raise ValueError("No src/**/*.cpp files found")

    build = BUILD / "debug" if options.debug else BUILD
    build.mkdir(parents=True, exist_ok=True)
    output = build / "project.sb3"
    debug_map = build / "project.debug.json"
    # A failed rebuild must not leave an old project looking like a new result.
    output.unlink(missing_ok=True)
    if options.debug:
        debug_map.unlink(missing_ok=True)
    mode_flags = ["-O0", "-g", "-fstandalone-debug"] if options.debug else ["-O2"]
    commands = []
    for source in sources:
        bitcode = build / "ir" / source.relative_to(ROOT / "src").with_suffix(".bc")
        bitcode.parent.mkdir(parents=True, exist_ok=True)
        args = [clang, *FLAGS, *library_flags, *mode_flags,
                "-emit-llvm", "-c", str(source), "-o", str(bitcode)]
        commands.append({"directory": str(ROOT), "file": str(source),
                         "arguments": args, "output": str(bitcode)})
    database = json.dumps(commands, indent=2, ensure_ascii=False) + "\n"
    (build / "compile_commands.json").write_text(database, encoding="utf-8")
    # IntelliSense follows the most recently selected build mode.
    if options.debug:
        (BUILD / "compile_commands.json").write_text(database, encoding="utf-8")
    for entry in commands:
        run(entry["arguments"])
    libraries = [str(sdk / path) for path in manifest["bitcode"]]
    run([
        compiler,
        *(entry["output"] for entry in commands),
        *libraries,
        *(["--debug-map", str(debug_map)] if options.debug else ["--passes", "default<O2>"]),
        "--whole-program",
        "-o", str(output),
    ])
    # Keep upstream notices with the distributed Scratch program.
    with zipfile.ZipFile(output, "a", compression=zipfile.ZIP_DEFLATED) as archive:
        for license_file in sorted((sdk / "licenses").glob("*")):
            if license_file.is_file():
                archive.write(license_file, "licenses/" + license_file.name)
    print(f"\nBuilt: {output}")
    if options.debug:
        print(f"Debug map: {debug_map}\nUse the Scratch Debug launch configuration in VS Code.")
    else:
        print("Open it in Scratch or TurboWarp, then click the green flag.")


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    try:
        main()
    except subprocess.CalledProcessError as error:
        sys.exit(error.returncode if error.returncode > 0 else 1)
    except (OSError, ValueError) as error:
        print(f"Build error: {error}", file=sys.stderr)
        sys.exit(1)
