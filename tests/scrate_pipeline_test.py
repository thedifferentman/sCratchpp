#!/usr/bin/env python3
"""Exercise real source packages through a loopback registry and copied templates.

This test never contacts a public registry. It checks Release/Debug execution,
transitive package resolution, external cache compilation, locked offline
rebuilds, and projects with either no dependencies or local checkout packages.
"""
from __future__ import annotations

import argparse
from functools import partial
import hashlib
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import tomllib
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from toolchain import resolve_clang, resolve_executable

CONSOLE_SOURCE = '''#include <console/console.hpp>
#include <events/events.hpp>
#include <pte/pte.hpp>
static void on_key(int, void*) {}
int main() {
    scratch::console::Options options;
    options.columns = 8;
    options.rows = 2;
    options.history_lines = 4;
    scratch::console::init(options);
    const int token = scratch::events::on_key(on_key);
    if (token <= 0) return 1;
    scratch::events::off(token);
    scratch::console::write("Aa中");
    scratch::console::flush();
    if (scratch::console::line_count() != 1) return 2;
    if (scratch::console::cursor_column() != 4) return 3;
    if (scratch::console::cell(0, 0) != 'A') return 4;
    if (scratch::console::cell(0, 1) != 'a') return 5;
    if (scratch::console::cell(0, 2) != 0x4e2d) return 6;
    if (scratch::pte::measure_units('A') <= 0) return 7;
    if (scratch::pte::measure_units(0x4e2d) <= 0) return 8;
    return 81;
}
'''


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def inside(path, directory):
    try:
        Path(path).resolve().relative_to(Path(directory).resolve())
        return True
    except ValueError:
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--node", default="node")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--case", choices=("all", "bitcode"), default="all",
                        help="Run only the real precompiled-package case, or the complete pipeline")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    workspace = Path(tempfile.mkdtemp(prefix="scrate pipeline ", dir=output))
    report = {"schemaVersion": 1, "passed": False, "commands": [], "cases": [],
              "workspace": str(workspace), "network": "loopback only", "renderingVerified": False}
    server = None
    requests = []
    environment = os.environ.copy()
    for key in ("SCRATCH_CLANG", "SCRATCH_LLVM", "SCRATCH_STDLIB_DIR", "SCRATCH_RESOURCE_BUILDER",
                "SCRATE_REGISTRY", "SCRATE_CACHE_DIR"):
        environment.pop(key, None)

    def execute(command, label, cwd=workspace, timeout=240):
        command = list(map(str, command))
        step = {"command": command, "cwd": str(cwd), "log": label}
        report["commands"].append(step)
        result = subprocess.run(command, cwd=cwd, env=environment, capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=timeout)
        (output / (label + ".stdout.log")).write_text(result.stdout, encoding="utf-8")
        (output / (label + ".stderr.log")).write_text(result.stderr, encoding="utf-8")
        step["exitCode"] = result.returncode
        require(result.returncode == 0, f"{label}: command failed\n{result.stdout[-2000:]}\n{result.stderr[-4000:]}")
        return result

    try:
        import scrate
        compiler = resolve_executable(args.compiler, "Scratch compiler")
        clang = resolve_clang(args.clang)
        node = resolve_executable(args.node, "Node.js")
        sdk = args.sdk.resolve()
        report["compilerSha256"] = digest(compiler)
        report["sdkManifestSha256"] = digest(sdk / "manifest.json")
        registry = workspace / "registry"
        registry.mkdir()
        for name in ("events", "pte", "triangle", "console"):
            scrate.pack(ROOT / "include" / name / "sCrpp.toml", registry)
        archives = list(registry.rglob("*.zip"))
        require(len(archives) == 4, "Packing four real libraries must produce four archives")
        for archive_file in archives:
            with zipfile.ZipFile(archive_file) as archive:
                manifests = [name for name in archive.namelist() if name == "sCrpp.toml"]
                require(len(manifests) == 1, "Package archive must contain a root sCrpp.toml")
                manifest = tomllib.loads(archive.read(manifests[0]).decode("utf-8"))
                require(manifest["library"]["include_dirs"] == ["include"], "Package include path must be self-contained")
                for dependency in manifest.get("dependencies", {}).values():
                    require(isinstance(dependency, str) or "path" not in dependency,
                            "Registry package must not retain checkout dependency paths")
        report["archives"] = [{"path": str(path), "sha256": digest(path)} for path in archives]

        # A genuine Clang-produced package, not a placeholder .bc used only to
        # check metadata. The consumer must link this file and skip its source.
        bitcode_package = workspace / "bitcode-package"
        (bitcode_package / "include/answer").mkdir(parents=True)
        (bitcode_package / "lib").mkdir()
        (bitcode_package / "include/answer/answer.hpp").write_text(
            '#pragma once\nint scrate_bitcode_answer(int value);\n', encoding="utf-8")
        (bitcode_package / "answer.cpp").write_text(
            '#include <answer/answer.hpp>\nint scrate_bitcode_answer(int value) { return value * 3 + 7; }\n',
            encoding="utf-8")
        sdk_manifest = json.loads((sdk / "manifest.json").read_text(encoding="utf-8"))
        target = sdk_manifest.get("target", "x86_64-unknown-linux-gnu")
        llvm_major = sdk_manifest.get("llvm_major", 22)
        scrpp_abi = sdk_manifest.get("scrpp_abi", "1")
        require(llvm_major == 22, "Fixture requires the project's Clang/LLVM 22 toolchain")
        execute([clang, "--target=" + target, "-std=c++17", "-O1", "-fno-exceptions", "-fno-rtti",
                 "-I", bitcode_package / "include", "-emit-llvm", "-c", bitcode_package / "answer.cpp",
                 "-o", bitcode_package / "lib/answer.bc"], "bitcode-package-compile")
        (bitcode_package / "sCrpp.toml").write_text(
            'version = 1\n[package]\nname = "answer"\nversion = "0.1.0"\n'
            '[library]\nsources = ["answer.cpp"]\nbitcode = ["lib/answer.bc"]\ninclude_dirs = ["include"]\n'
            '[toolchain]\nllvm_major = 22\ntarget = ' + json.dumps(target) +
            '\nscrpp_abi = ' + json.dumps(scrpp_abi) + '\n', encoding="utf-8")
        bitcode_metadata = scrate.pack(bitcode_package / "sCrpp.toml", registry)
        report["precompiledPackage"] = {"metadata": bitcode_metadata,
                                         "bitcodeSha256": digest(bitcode_package / "lib/answer.bc")}

        class Handler(SimpleHTTPRequestHandler):
            def log_message(self, _format, *_args):
                pass

            def do_GET(self):
                requests.append(self.path)
                super().do_GET()

        server = ThreadingHTTPServer(("127.0.0.1", 0), partial(Handler, directory=str(registry)))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        registry_url = f"http://127.0.0.1:{server.server_port}/"
        report["registry"] = registry_url
        # The cache is intentionally outside the repository and the copied project.
        with tempfile.TemporaryDirectory(prefix="scrate external cache ") as cache_directory:
            cache = Path(cache_directory)
            report["cache"] = str(cache)
            require(not inside(cache, ROOT), "Cache must be outside the source checkout")
            environment["SCRATE_REGISTRY"] = registry_url
            environment["SCRATE_CACHE_DIR"] = str(cache)

            def project(name, source, dependencies=""):
                dest = workspace / name
                shutil.copytree(ROOT / "template", dest, ignore=shutil.ignore_patterns(
                    "build", "__pycache__", "toolchain.local.json", "node_modules", ".git", "*.sb3", "scrate.lock"))
                (dest / "src/main.cpp").write_text(source, encoding="utf-8")
                (dest / "sCrpp.toml").write_text(
                    'version = 1\n[package]\nname = "pipeline"\nversion = "0.1.0"\n' + dependencies,
                    encoding="utf-8")
                (dest / "toolchain.local.json").write_text(json.dumps({
                    "clang": clang, "scratch_llvm": compiler, "stdlib": str(sdk),
                    "resource_builder": str(ROOT / "tools/build_resources.py"),
                    "scrate": str(ROOT / "tools/scrate.py"), "registry": registry_url,
                    "package_cache": str(cache),
                }, ensure_ascii=False, indent=2), encoding="utf-8")
                return dest

            def inspect_build(dest, label, debug, with_packages, expected_exit, source_root):
                build = dest / "build/debug" if debug else dest / "build"
                sb3 = build / "project.sb3"
                with zipfile.ZipFile(sb3) as archive:
                    raw = archive.read("project.json")
                    data = json.loads(raw)
                sprites = [target for target in data["targets"] if not target["isStage"]]
                require(len(data["targets"]) == 2 and len(sprites) == 1, "Expected one Program sprite")
                program = sprites[0]
                lists = {row[0]: row[1] for target in data["targets"] for row in target["lists"].values()}
                hats = [block for block in program["blocks"].values() if block["opcode"] == "event_whenkeypressed"]
                database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
                library_sources = [Path(entry["file"]).resolve() for entry in database
                                   if Path(entry["file"]).name in ("console.cpp", "events.cpp", "pte.cpp")]
                if with_packages:
                    require({path.name for path in library_sources} == {"console.cpp", "events.cpp", "pte.cpp"},
                            "All transitive package implementations must be compiled")
                    require(len(library_sources) == 3 and all(inside(path, source_root) for path in library_sources),
                            "Compilation must use the selected cache or local checkout source files")
                    for name in ("font", "index"):
                        expected = (ROOT / "include/pte/resources" / (name + ".txt")).read_text(encoding="utf-8").splitlines()
                        require([str(item) for item in lists.get("__scl_pte::" + name, [])] == expected,
                                f"Published PTE {name} list did not survive dependency linking")
                    require(len(hats) >= 2, "Events dependency must provide native key/wheel collectors")
                    # Ask Clang which headers this exact main translation unit used.
                    entry = next(entry for entry in database if Path(entry["file"]).name == "main.cpp")
                    probe, skip = [], False
                    for argument in entry["arguments"]:
                        if skip:
                            skip = False
                        elif argument == "-o":
                            skip = True
                        elif argument not in ("-emit-llvm", "-c"):
                            probe.append(argument)
                    headers = execute([*probe, "-H", "-fsyntax-only"], label + "-headers", dest)
                    # Clang -H may escape Windows backslashes in diagnostics.
                    normalized = re.sub(r"[\\/]+", "/", headers.stderr).lower()
                    for source_path in library_sources:
                        header = source_path.parent / "include" / source_path.stem / (source_path.stem + ".hpp")
                        require(re.sub(r"[\\/]+", "/", str(header)).lower() in normalized,
                                f"Public package header was not used: {header}")
                else:
                    require(not library_sources and len(database) == 2,
                            "This build must compile only user main.cpp and generated scratch.cpp")
                    require(not hats, "No-dependency project unexpectedly includes event collectors")
                    require(not any(name.startswith(("__scl_pte", "__scl_console", "__scl_events")) for name in lists),
                            "No-dependency project unexpectedly carries library lists")
                if debug:
                    mapping = json.loads((build / "project.debug.json").read_text(encoding="utf-8"))
                    require(mapping["projectCrc32"] == f"{zlib.crc32(raw):08x}", "Debug map project checksum mismatch")
                result = {"name": label, "project": str(sb3), "sha256": digest(sb3),
                          "packageSources": list(map(str, library_sources)), "collectors": len(hats), "vms": []}
                report["cases"].append(result)
                for mode in ("scratch", "turbowarp"):
                    execution = execute([node, ROOT / "tests/vm_runner.cjs", sb3, "--vm", mode,
                                         "--timeout", "120000", "--list-limit", "8"], label + "-" + mode, timeout=150)
                    vm = json.loads(execution.stdout)
                    require(vm["status"] == "completed", f"{label}/{mode}: {vm.get('error')}")
                    require(vm["variables"].get("__scl_status") == "done" and
                            int(vm["variables"].get("exit_code", -1)) == expected_exit,
                            f"{label}/{mode}: wrong program result {vm['variables']}")
                    if mode == "turbowarp":
                        require(vm.get("compiledThreads", 0) > 0, "TW fixture was not compiled")
                    result["vms"].append({"vm": mode, "passed": True, "exit": expected_exit,
                                          "executionMs": vm.get("executionMs")})
                print(f"{label}: both VMs passed", flush=True)

            precompiled = project("precompiled-project",
                                  '#include <answer/answer.hpp>\n'
                                  'int main() { volatile int value = 11; return scrate_bitcode_answer(value); }\n',
                                  '\n[dependencies]\nanswer = "0.1.0"\n')
            result = execute([sys.executable, ROOT / "tools/scrate.py", "build", "--manifest",
                              precompiled / "sCrpp.toml"], "precompiled-registry")
            bitcode_lock = json.loads((precompiled / "scrate.lock").read_text(encoding="utf-8"))
            require(len(bitcode_lock["packages"]) == 1 and bitcode_lock["packages"][0]["name"] == "answer",
                    "Precompiled dependency must be locked as a registry package")
            require(bitcode_lock["packages"][0]["source"]["kind"] == "registry",
                    "Precompiled fixture must go through HTTP resolution")
            downloaded = list(cache.rglob("answer.bc"))
            require(len(downloaded) == 1 and digest(downloaded[0]) == report["precompiledPackage"]["bitcodeSha256"],
                    "Downloaded bitcode must match the genuine Clang artifact")
            normalized_build = re.sub(r"[\\/]+", "/", result.stdout).lower()
            require(re.sub(r"[\\/]+", "/", str(downloaded[0])).lower() in normalized_build,
                    "The template must pass cached package bitcode into the final compiler invocation")
            inspect_build(precompiled, "precompiled-registry", False, False, 40, cache)
            report["precompiledPackage"]["consumedFromCache"] = str(downloaded[0])
            require(any("index/answer/" in request for request in requests) and
                    any("answer-0.1.0.zip" in request for request in requests),
                    "Precompiled metadata and archive must be downloaded from the loopback registry")
            if args.case == "bitcode":
                report["registryRequests"] = list(requests)
                report["passed"] = True
                print("scrate real precompiled-package case passed", flush=True)
                return 0

            remote = project("registry-project", CONSOLE_SOURCE, '\n[dependencies]\nconsole = "0.3.0"\n')
            # A managed project must not need its own build implementation.
            require(not (remote / "build.py").exists() and not (remote / "scrate.py").exists(),
                    "Template must not contain project launch scripts")
            lock_bytes = None
            for debug in (False, True):
                label = "registry-debug" if debug else "registry-release"
                execute([sys.executable, ROOT / "tools/scrate.py", "build", "--manifest",
                         remote / "sCrpp.toml", *(["--debug", "--locked"] if debug else [])], label)
                current_lock = (remote / "scrate.lock").read_bytes()
                if lock_bytes is not None:
                    require(current_lock == lock_bytes, "Debug build changed dependency lock")
                lock_bytes = current_lock
                lock = json.loads(current_lock)
                packages = lock["packages"]
                require({item["name"] for item in packages} == {"console", "events", "pte", "triangle"} and len(packages) == 4,
                        "Lock must contain the four-package transitive graph")
                inspect_build(remote, label, debug, True, 81, cache)
            require(requests and any("index/" in request for request in requests) and
                    any(".zip" in request for request in requests), "Registry must supply metadata and archives over HTTP")
            report["registryRequests"] = list(requests)
            server.shutdown()
            server.server_close()
            server = None
            thread.join(timeout=5)
            for debug in (False, True):
                label = "offline-debug" if debug else "offline-release"
                execute([sys.executable, ROOT / "tools/scrate.py", "build", "--manifest",
                         remote / "sCrpp.toml", "--offline", "--locked",
                         *(["--debug"] if debug else [])], label)
                require((remote / "scrate.lock").read_bytes() == lock_bytes, "Offline rebuild changed lock bytes")
                inspect_build(remote, label, debug, True, 81, cache)

            bare = project("no-dependencies", "int main() { return 73; }\n")
            require(not (bare / "build.py").exists(), "Bare project must not need build.py")
            for debug in (False, True):
                label = "no-dependencies-debug" if debug else "no-dependencies-release"
                execute([sys.executable, ROOT / "tools/scrate.py", "build", "--manifest",
                         bare / "sCrpp.toml", "--offline", *(["--debug"] if debug else [])], label)
                inspect_build(bare, label, debug, False, 73, cache)

            local_dependency = json.dumps(str(ROOT / "include/console"), ensure_ascii=False)
            local = project("local-checkout", CONSOLE_SOURCE,
                            '\n[dependencies]\nconsole = { version = "0.3.0", path = ' + local_dependency + ' }\n')
            execute([sys.executable, ROOT / "tools/scrate.py", "build", "--manifest",
                     local / "sCrpp.toml", "--offline"], "local-checkout")
            inspect_build(local, "local-checkout", False, True, 81, ROOT / "include")
            require(requests == report["registryRequests"], "Offline/local/no-dependency builds contacted the registry")
            report["passed"] = True
    except (OSError, ValueError, RuntimeError, AssertionError, KeyError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
    finally:
        if server is not None:
            server.shutdown()
            server.server_close()
        (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not report["passed"]:
        print(report.get("error", "Pipeline failed"), file=sys.stderr)
        return 1
    print(f"scrate template pipeline passed: {output / 'report.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
