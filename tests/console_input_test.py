#!/usr/bin/env python3
"""Exercise actual keyboard IO in Scratch and both TurboWarp execution modes."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_resources import prepare_resources
from build_stdlib import compiler_flags
from toolchain import resolve_clang, resolve_executable


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--node", default="node")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "tests/.tmp/console-input")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"passed": False, "commands": []}

    def execute(command, timeout=240):
        command = [str(item) for item in command]
        result = subprocess.run(command, cwd=ROOT, text=True, encoding="utf-8",
                                errors="replace", capture_output=True, timeout=timeout)
        report["commands"].append({"command": command, "code": result.returncode,
                                   "stdout": result.stdout, "stderr": result.stderr})
        if result.returncode:
            raise RuntimeError(f"Command failed ({result.returncode}): {command}\n{result.stdout}\n{result.stderr}")
        return result

    try:
        compiler = resolve_executable(args.compiler, "Scratch compiler")
        clang = resolve_clang(args.clang)
        node = resolve_executable(args.node, "Node.js")
        sdk = args.sdk.resolve()
        sdk_manifest = json.loads((sdk / "manifest.json").read_text(encoding="utf-8"))
        report["compilerSha256"] = hashlib.sha256(Path(compiler).read_bytes()).hexdigest()
        pack = prepare_resources(ROOT / "include/events/sCrpp.toml", output / "resources")
        objects = []
        for relative in ("include/console/console.cpp", "include/events/events.cpp", "include/triangle/triangle.cpp", "tests/console_input.cpp"):
            source = ROOT / relative
            obj = output / (source.stem + ".bc")
            execute([clang, *compiler_flags(clang, sdk), "-std=c++17", "-O1", "-I", ROOT / "include",
                     "-emit-llvm", "-c", source, "-o", obj])
            objects.append(obj)
        sb3 = output / "console-input.sb3"
        # Console/events are built above and PTE is a recording stub here.
        core_bitcode = sdk_manifest.get("core_bitcode", sdk_manifest["bitcode"])
        execute([compiler, *objects, *[sdk / item for item in core_bitcode],
                 "--resources", pack, "--whole-program", "--passes", "default<O2>", "-o", sb3])
        report["projectSha256"] = hashlib.sha256(sb3.read_bytes()).hexdigest()
        vm_report = output / "console-input-vm-report.json"
        execute([node, ROOT / "tests/console_input_vm.cjs", sb3, vm_report], timeout=400)
        report["vm"] = json.loads(vm_report.read_text(encoding="utf-8"))
        assert report["vm"]["pass"]
        report["passed"] = True
        print("Console input: Scratch, TurboWarp compiled and interpreted; two green-flag runs each passed.")
    except Exception as exc:
        report["error"] = str(exc)
        print(str(exc), file=sys.stderr)
    finally:
        (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
