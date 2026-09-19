#!/usr/bin/env python3
"""Reusable project builds for scrate (Python 3.11+)."""
import contextlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tomllib
import zipfile

def tool(value, root):
    """Accept a PATH command or a path relative to this project."""
    if not isinstance(value, str) or not value.strip():
        raise ValueError("Tool paths must be non-empty strings")
    path = Path(value).expanduser()
    if path.is_absolute() or "/" in value or "\\" in value:
        path = (root / path).resolve()
        if path.is_file():
            return str(path)
    else:
        found = shutil.which(value)
        if found:
            return str(Path(found).resolve())
    raise ValueError(f"Tool not found: {value}. Configure toolchain.local.json (see README.md).")


def run(args, root, log_to_stderr=False):
    # No shell: spaces, Unicode, and shell metacharacters in paths stay literal.
    print("+ " + " ".join(json.dumps(arg, ensure_ascii=False) for arg in args),
          file=sys.stderr if log_to_stderr else sys.stdout, flush=True)
    subprocess.run(args, cwd=root, check=True, stdout=sys.stderr if log_to_stderr else None)


def stdlib_sdk(config, compiler, root):
    explicit = os.environ.get("SCRATCH_STDLIB_DIR") or config.get("stdlib")
    if explicit:
        candidates = [(root / Path(explicit).expanduser()).resolve()]
    else:
        prefix = Path(compiler).parent
        candidates = [prefix / "stdlib", prefix.parent / "share/scratch-llvm/stdlib",
                      root.parent / "build/stdlib"]
    for directory in candidates:
        manifest = directory / "manifest.json"
        if manifest.is_file():
            return directory, json.loads(manifest.read_text(encoding="utf-8"))
    raise ValueError("Scratch standard library SDK not found. Set stdlib in toolchain.local.json "
                     "or SCRATCH_STDLIB_DIR; see README.md. Host C++ libraries cannot be used here.")


def _sibling_module(name):
    # Never resolve executable Python from the project or toolchain.local.json.
    path = Path(__file__).resolve().with_name(name + ".py")
    existing = sys.modules.get(name)
    if existing and Path(getattr(existing, "__file__", "")).resolve() == path:
        return existing
    key = "_scrate_build_" + name
    cached = sys.modules.get(key)
    if cached and Path(getattr(cached, "__file__", "")).resolve() == path:
        return cached
    spec = importlib.util.spec_from_file_location(key, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[key] = module
    spec.loader.exec_module(module)
    return module


def resource_builder():
    return _sibling_module("build_resources")


def package_manager():
    return _sibling_module("scrate")


def sdk_paths(directory, manifest, field, default=(), directories=False):
    """SDK manifests must remain relocatable, including all declared inputs."""
    entries = manifest.get(field, default)
    if not isinstance(entries, (list, tuple)) or any(not isinstance(entry, str) or not entry for entry in entries):
        raise ValueError(f"SDK {field} must be an array of relative paths")
    paths = []
    for entry in entries:
        relative = Path(entry.replace("\\", "/"))
        if relative.is_absolute() or ".." in relative.parts or ":" in entry:
            raise ValueError(f"SDK {field} paths must be relative to the SDK: {entry}")
        path = (directory / relative).resolve()
        try:
            path.relative_to(directory.resolve())
        except ValueError:
            raise ValueError(f"SDK {field} path escapes the SDK: {entry}") from None
        if not (path.is_dir() if directories else path.is_file()):
            raise ValueError(f"SDK {field} path does not exist: {path}")
        paths.append(path)
    return paths


def prepare_project_resources(manifest, config, build, abi, offline=False, locked=False,
                              registry=None, cache_dir=None):
    root = manifest.parent
    if not manifest.is_file():
        return [], "", [], [], []
    if sys.version_info < (3, 11):
        raise ValueError("sCrpp.toml requires Python 3.11 or newer (tomllib).")
    builder = resource_builder()
    manager = None
    root_config = builder.load_manifest(manifest)
    dependencies = root_config.get("dependencies", {})
    if not isinstance(dependencies, dict):
        raise ValueError("[dependencies] must be a table")
    resolved = []
    if dependencies or (root / "scrate.lock").exists():
        manager = package_manager()
        registry = registry or os.environ.get("SCRATE_REGISTRY") or config.get("registry")
        registry = str(registry) if registry is not None else None
        if registry and "://" not in registry:
            registry = str((root / Path(registry).expanduser()).resolve())
        cache = cache_dir or os.environ.get("SCRATE_CACHE_DIR") or config.get("package_cache")
        cache = (root / Path(cache).expanduser()).resolve() if cache else None
        resolved = manager.resolve(manifest, registry=registry, cache_dir=cache,
                                   offline=offline, locked=locked, abi=abi)
    resolved_paths = {Path(path).resolve() for path in resolved}
    manifests, seen_manifests = [], set()
    for entry in [*resolved, manifest]:
        for resource_manifest in builder.collect_manifests(entry):
            resource_manifest = Path(resource_manifest).resolve()
            if resource_manifest not in seen_manifests:
                manifests.append(resource_manifest)
                seen_manifests.add(resource_manifest)
    flags, sources, include_dirs, bitcodes = [], [], [], []
    seen_sources, seen_includes, seen_bitcodes, packages = set(), set(), set(), {}
    for index, resource_manifest in enumerate(manifests):
        package_config = builder.load_manifest(resource_manifest)
        if (resource_manifest != manifest.resolve() and resource_manifest not in resolved_paths
                and package_config.get("dependencies")):
            raise ValueError(f"{resource_manifest}: a legacy [link] package declares [dependencies]; "
                             "declare this package in the project's [dependencies] instead")
        package_name = package_config.get("package", {}).get("name")
        if package_name is not None:
            if package_name in packages:
                raise ValueError(f"Duplicate resource package {package_name}: {packages[package_name]} and {resource_manifest}")
            packages[package_name] = resource_manifest
        library = package_config.get("library", {})
        if not isinstance(library, dict) or set(library) - {"sources", "include_dirs", "bitcode"}:
            raise ValueError(f"{resource_manifest}: [library] accepts only sources, bitcode and include_dirs")
        if library.get("bitcode"):
            manager = manager or package_manager()
            manager.validate_abi(package_config, abi)
        for field, result, seen in (("sources", sources, seen_sources),
                                    ("include_dirs", include_dirs, seen_includes),
                                    ("bitcode", bitcodes, seen_bitcodes)):
            entries = library.get(field, [])
            if not isinstance(entries, list) or any(not isinstance(item, str) or not item.strip() for item in entries):
                raise ValueError(f"{resource_manifest}: library.{field} must be an array of nonempty paths")
            if field == "sources" and library.get("bitcode"):
                continue
            for entry in entries:
                path = (resource_manifest.parent / entry.replace("\\", "/")).resolve()
                if field == "sources":
                    if not path.is_file() or path.suffix.lower() not in {".cpp", ".cc", ".cxx"}:
                        raise ValueError(f"{resource_manifest}: library.sources requires an existing C++ file: {path}")
                elif field == "bitcode":
                    if not path.is_file() or path.suffix.lower() not in {".bc", ".ll"}:
                        raise ValueError(f"{resource_manifest}: library.bitcode requires an existing LLVM file: {path}")
                elif not path.is_dir():
                    raise ValueError(f"{resource_manifest}: library.include_dirs requires an existing directory: {path}")
                if path not in seen:
                    seen.add(path)
                    result.append(path)
        prepared = Path(builder.prepare_resources(resource_manifest, build / "resources" / str(index)))
        flags += ["--resources", str(prepared)]
    return flags, builder.read_package_name(manifest), sources, include_dirs, bitcodes


def generate_scratch_source(build, package):
    # Numeric character escapes are not needed: Clang reads this UTF-8 source.
    prefix = json.dumps(package + "::" if package else "", ensure_ascii=False)
    source = build / "generated/scratch.cpp"
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_text('''// Generated by scrate; edit sCrpp.toml instead.
#include <string>
namespace scratch {
void set_string(const std::string&);
void clear() { __asm__ volatile("pen_clear"); }
void pen_up() { __asm__ volatile("pen_penUp"); }
void pen_down() { __asm__ volatile("pen_penDown"); }
void go_to(int x, int y) {
    __asm__ volatile("motion_gotoxy X=%0 Y=%1" : : "r"(x), "r"(y));
}
void point_in_direction(int degrees) {
    __asm__ volatile("motion_pointindirection DIRECTION=%0" : : "r"(degrees));
}
void move(int steps) { __asm__ volatile("motion_movesteps STEPS=%0" : : "r"(steps)); }
void turn_right(int degrees) { __asm__ volatile("motion_turnright DEGREES=%0" : : "r"(degrees)); }
void pen_size(int size) { __asm__ volatile("pen_setPenSizeTo SIZE=%0" : : "r"(size)); }
void show() { __asm__ volatile("looks_show"); }
void hide() { __asm__ volatile("looks_hide"); }
void stamp() { __asm__ volatile("pen_stamp"); }
void set_costume_qualified(const std::string& name) {
    set_string(name);
    __asm__ volatile("looks_switchcostumeto COSTUME=(data_variable VARIABLE=\\"__scl_string\\")" : : : "memory");
}
void set_costume(const std::string& name) {
    set_costume_qualified(std::string(''' + prefix + ''') + name);
}
} // namespace scratch
''', encoding="utf-8")
    return source


def _is_link_or_reparse(path):
    """lstat handles NTFS junctions on Python 3.11 without following their target."""
    try:
        details = os.lstat(path)
    except FileNotFoundError:
        return False
    return (stat.S_ISLNK(details.st_mode) or
            bool(getattr(details, "st_file_attributes", 0) &
                 getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)))


def _validate_output(root, path):
    # Normalize dot segments without resolving reparse points first: resolving
    # would hide an internal junction, or encounter a link cycle before checking.
    output = Path(os.path.abspath(path))
    if output == root or not output.is_relative_to(root):
        raise ValueError("build.output_dir must be a directory strictly inside the project")
    current = root
    for part in output.relative_to(root).parts:
        current = current / part
        if _is_link_or_reparse(current):
            raise ValueError(f"Build output must not contain a symlink or reparse point: {current}")

    def fail(error):
        raise error

    if not output.exists():
        return output
    # os.walk yields each directory before descending. Inspect every directory
    # entry (including junctions) before the next iteration, so no link target is
    # enumerated even on Windows versions where junctions are not is_symlink().
    for directory, dirs, files in os.walk(output, topdown=True, onerror=fail, followlinks=False):
        for name in [*dirs, *files]:
            child = Path(directory) / name
            if _is_link_or_reparse(child):
                raise ValueError(f"Build output must not contain a symlink or reparse point: {child}")
    return output


def _project_sources(source_dir, output_dir):
    sources = []
    if source_dir.resolve().is_relative_to(output_dir):
        return sources
    for directory, dirs, files in os.walk(source_dir, topdown=True, followlinks=False):
        current = Path(directory)
        dirs[:] = [name for name in dirs if not _is_link_or_reparse(current / name)
                   and not (current / name).resolve().is_relative_to(output_dir)]
        for name in files:
            path = current / name
            if path.suffix == ".cpp" and path.is_file() and not path.resolve().is_relative_to(output_dir):
                sources.append(path)
    return sorted(sources)


def project_info(manifest: Path, debug=False) -> dict:
    """Read project layout/configuration without finding tools or resolving packages."""
    manifest = Path(manifest).expanduser().resolve()
    if manifest.is_dir():
        manifest = manifest / "sCrpp.toml"
    root = manifest.parent
    if not root.is_dir():
        raise ValueError(f"Project directory does not exist: {root}")
    data = tomllib.loads(manifest.read_text(encoding="utf-8")) if manifest.is_file() else {}
    layout = data.get("build", {})
    if not isinstance(layout, dict) or set(layout) - {"source_dir", "include_dirs", "output_dir", "cpp_standard", "memory_bytes"}:
        raise ValueError("[build] accepts only source_dir, include_dirs, output_dir, cpp_standard and memory_bytes")
    memory_bytes = layout.get("memory_bytes")
    if memory_bytes is not None and (type(memory_bytes) is not int or not 1024 <= memory_bytes <= 200000):
        raise ValueError("build.memory_bytes must be an integer between 1024 and 200000")
    for field in ("source_dir", "output_dir"):
        if field in layout and (not isinstance(layout[field], str) or not layout[field].strip()):
            raise ValueError(f"build.{field} must be a nonempty path")
    includes = layout.get("include_dirs", ["include"])
    if not isinstance(includes, list) or any(not isinstance(value, str) or not value.strip() for value in includes):
        raise ValueError("build.include_dirs must be an array of nonempty paths")
    standard = layout.get("cpp_standard", "c++17")
    if standard not in ("c++17", "c++20", "c++23"):
        raise ValueError("build.cpp_standard must be c++17, c++20 or c++23")
    source = (root / layout.get("source_dir", "src")).resolve()
    output = _validate_output(root, root / layout.get("output_dir", "build"))
    build = output / "debug" if debug else output
    config = {}
    local = root / "toolchain.local.json"
    if local.exists():
        config = json.loads(local.read_text(encoding="utf-8-sig"))
        allowed = {"clang", "scratch_llvm", "stdlib", "lldb_dap", "lldb_python", "resource_builder",
                   "scrate", "registry", "package_cache", "node", "turbowarp", "debugger", "python"}
        if not isinstance(config, dict) or set(config) - allowed:
            raise ValueError("Unknown toolchain.local.json fields; allowed: " + ", ".join(sorted(allowed)))
        if any(not isinstance(value, str) or not value.strip() for value in config.values()):
            raise ValueError("toolchain.local.json values must be nonempty strings")
    return {"root": root, "manifest": manifest, "project": build / "project.sb3",
            "debug_map": build / "project.debug.json" if debug else None, "build_dir": build,
            "output_dir": output, "source_dir": source,
            "include_dirs": [(root / value).resolve() for value in includes], "cpp_standard": standard,
            "config": config, "turbowarp": data.get("turbowarp", {}), "memory_bytes": memory_bytes,
            "compiler": os.environ.get("SCRATCH_LLVM") or config.get("scratch_llvm", "scratch-llvm"),
            "clang": os.environ.get("SCRATCH_CLANG") or config.get("clang", "clang++")}


def build_project(manifest: Path, *, debug=False, offline=False, locked=False, registry=None,
                  cache_dir=None, log_to_stderr=False) -> dict:
    """Compile a project without retaining mutable project state between calls."""
    # Python dependency tools and all native child stdout share the selected log
    # stream; DAP users keep stdout exclusively for protocol frames.
    with contextlib.redirect_stdout(sys.stderr) if log_to_stderr else contextlib.nullcontext():
        return _build_project(manifest, debug=debug, offline=offline, locked=locked, registry=registry,
                              cache_dir=cache_dir, log_to_stderr=log_to_stderr)


def _build_project(manifest, *, debug, offline, locked, registry, cache_dir, log_to_stderr):
    info = project_info(manifest, debug)
    root, config = info["root"], info["config"]
    compiler, clang = tool(info["compiler"], root), tool(info["clang"], root)
    info.update(compiler=compiler, clang=clang)
    sdk, sdk_manifest = stdlib_sdk(config, compiler, root)
    includes = sdk_paths(sdk, sdk_manifest, "include_dirs", ["include/c++/v1", "include"], directories=True)
    core_field = "core_bitcode" if "core_bitcode" in sdk_manifest else "bitcode"
    core_manifest = sdk_manifest
    if core_field == "bitcode" and isinstance(sdk_manifest.get("bitcode"), list):
        core_manifest = dict(sdk_manifest, bitcode=[entry for entry in sdk_manifest["bitcode"]
                             if not isinstance(entry, str) or Path(entry).name != "scrpp-stdlib.bc"])
    libraries = [str(path) for path in sdk_paths(sdk, core_manifest, core_field)]
    abi = {"llvm_major": sdk_manifest.get("llvm_major", 22),
           "target": sdk_manifest.get("target", "x86_64-unknown-linux-gnu"),
           "scrpp_abi": sdk_manifest.get("scrpp_abi", "1")}
    resource_dir = subprocess.check_output([clang, "-print-resource-dir"], text=True, encoding="utf-8").strip()
    library_flags = ["-nostdinc"]
    library_flags += [value for path in includes for value in ("-isystem", str(path))]
    library_flags += ["-isystem", str(Path(resource_dir) / "include")]
    library_flags += sdk_manifest.get("compile_flags", [])
    source_dir, output_dir = info["source_dir"], info["output_dir"]
    sources = _project_sources(source_dir, output_dir)
    if not sources:
        raise ValueError(f"No C++ sources outside the output directory: {source_dir}/**/*.cpp")
    build, output, debug_map = info["build_dir"], info["project"], info["debug_map"]
    build.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    if debug_map:
        debug_map.unlink(missing_ok=True)
    resource_flags, package, package_sources, include_dirs, package_bitcode = prepare_project_resources(
        info["manifest"], config, build, abi, offline, locked, registry, cache_dir)
    libraries += [str(path) for path in package_bitcode]
    project_includes = list(dict.fromkeys([*info["include_dirs"], *include_dirs]))
    include_flags = [value for path in project_includes for value in ("-I", str(path))]
    generated = generate_scratch_source(build, package)
    units = [(source, build / "ir" / source.relative_to(source_dir).with_suffix(".bc")) for source in sources]
    units.append((generated, build / "generated/scratch.bc"))
    seen_sources = {source.resolve() for source, _ in units}
    for index, source in enumerate(package_sources):
        if source not in seen_sources:
            units.append((source, build / "libraries" / str(index) / (source.stem + ".bc")))
            seen_sources.add(source)
    mode_flags = ["-O0", "-g", "-fstandalone-debug"] if debug else ["-O2"]
    flags = ["--target=" + abi["target"], "-std=" + info["cpp_standard"], "-fno-exceptions", "-fno-rtti",
             "-fno-stack-protector", "-fno-color-diagnostics"]
    commands = []
    settings = info["turbowarp"]
    if not isinstance(settings, dict):
        raise ValueError("[turbowarp] must be a table")
    settings_path = build / "turbowarp-settings.json"
    settings_path.write_text(json.dumps(settings, allow_nan=False), encoding="utf-8")
    for source, bitcode in units:
        bitcode.parent.mkdir(parents=True, exist_ok=True)
        args = [clang, *flags, *include_flags, *library_flags, *mode_flags,
                "-emit-llvm", "-c", str(source), "-o", str(bitcode)]
        commands.append({"directory": str(root), "file": str(source), "arguments": args, "output": str(bitcode)})
    database = json.dumps(commands, indent=2, ensure_ascii=False) + "\n"
    (build / "compile_commands.json").write_text(database, encoding="utf-8")
    if debug:
        (output_dir / "compile_commands.json").write_text(database, encoding="utf-8")
    for entry in commands:
        run(entry["arguments"], root, log_to_stderr)
    run([compiler, *(entry["output"] for entry in commands), *libraries,
         "--memory", str(info["memory_bytes"] or sdk_manifest.get("memory_bytes", 65536)),
         *(["--debug-map", str(debug_map)] if debug else ["--passes", "default<O2>"]),
         "--whole-program", "--turbowarp-settings", str(settings_path), *resource_flags, "-o", str(output)], root, log_to_stderr)
    with zipfile.ZipFile(output, "a", compression=zipfile.ZIP_DEFLATED) as archive:
        for license_file in sorted((sdk / "licenses").glob("*")):
            if license_file.is_file():
                archive.write(license_file, "licenses/" + license_file.name)
    print(f"\nBuilt: {output}", flush=True)
    if debug_map:
        print(f"Debug map: {debug_map}", flush=True)
    return info


