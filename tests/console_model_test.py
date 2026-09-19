#!/usr/bin/env python3
"""Console state/render-plan regression using real events and a PTE draw stub.

No font package or renderer is needed: the stub records each requested glyph.
Actual pen pixels and hat collection are covered by the integration tests.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def executable(value):
    candidate = Path(value)
    if candidate.is_file():
        return str(candidate.resolve())
    found = shutil.which(value)
    if not found:
        raise RuntimeError("Executable not found: " + value)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--sdk", required=True)
    parser.add_argument("--node", default="node")
    parser.add_argument("--output-dir", default=str(ROOT / "build/validation/console-model"))
    parser.add_argument("--vm", choices=("both", "scratch", "turbowarp"), default="both")
    args = parser.parse_args()
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"test": "console-model", "assertions": 23, "font": "draw_cell recording stub",
              "events": "real library, queue injected without hats", "commands": [], "results": []}

    def run(command, timeout=180):
        report["commands"].append(command)
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=timeout)
        if result.returncode:
            raise RuntimeError("Command failed (%s): %s\n%s\n%s" %
                               (result.returncode, command[0], result.stdout, result.stderr))
        return result.stdout

    try:
        compiler, clang, node = (executable(args.compiler), executable(args.clang), executable(args.node))
        report["compiler_sha256"] = hashlib.sha256(Path(compiler).read_bytes()).hexdigest()
        sdk = Path(args.sdk).resolve()
        manifest = json.loads((sdk / "manifest.json").read_text(encoding="utf-8"))
        resource = run([clang, "-print-resource-dir"]).strip()
        flags = [clang, *manifest["compile_flags"], "-std=c++17", "-O1", "-S", "-emit-llvm",
                 "-DSCRPP_CONSOLE_STUB_FONT", "-I" + str(ROOT / "include")]
        for relative in manifest["include_dirs"]:
            flags += ["-isystem", str(sdk / relative)]
        # libc++'s stddef wrapper uses include_next, so the Clang resource
        # headers follow the SDK headers (even when running on Windows).
        flags += ["-isystem", str(Path(resource) / "include")]
        inputs = []
        for relative in ("include/console/console.cpp", "include/events/events.cpp", "include/triangle/triangle.cpp", "tests/console_model.cpp"):
            source = ROOT / relative
            ir = output / (source.stem + ".ll")
            run(flags + [str(source), "-o", str(ir)])
            inputs.append(str(ir))
        project = output / "model.sb3"
        # This fixture compiles the console/events sources and supplies a PTE
        # stub, so the SDK's precompiled sCr++ libraries must not be linked too.
        core_bitcode = manifest.get("core_bitcode", manifest["bitcode"])
        run([compiler, *inputs, *[str(sdk / name) for name in core_bitcode],
             "--whole-program", "--passes", "default<O2>", "-o", str(project)])
        report["project_sha256"] = hashlib.sha256(project.read_bytes()).hexdigest()
        with zipfile.ZipFile(project) as archive:
            data=json.loads(archive.read("project.json"))
            if any(b["opcode"]=="pen_clear" for t in data["targets"] for b in t["blocks"].values()):
                raise RuntimeError("Console must never clear the complete pen layer")
        for vm in (("scratch", "turbowarp") if args.vm == "both" else (args.vm,)):
            raw = run([node, str(ROOT / "tests/vm_runner.cjs"), str(project), "--vm", vm,
                       "--timeout", "120000", "--list-limit", "80"])
            result = json.loads(raw.strip().splitlines()[-1])
            report["results"].append({"vm": vm, **result})
            variables = result.get("variables", {})
            if result.get("status") != "completed" or variables.get("__scl_status") != "done" or int(variables.get("exit_code", -1)) != 0:
                raise RuntimeError("%s console model failed: exit_code=%s, runtime=%s" %
                                   (vm, variables.get("exit_code"), variables.get("__scl_status")))
            print("console model: %s passed (23 assertions)" % vm, flush=True)
        report["status"] = "passed"
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        report["status"] = "failed"
        report["error"] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")


if __name__ == "__main__":
    raise SystemExit(main())
