#!/usr/bin/env python3
"""Build the independent events library and test callbacks/collectors in both VMs."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

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
    parser.add_argument("--output-dir", type=Path, default=ROOT / "tests/.tmp/events")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"passed": False, "commands": []}

    def execute(command, timeout=180):
        command = [str(item) for item in command]
        result = subprocess.run(command, cwd=ROOT, text=True, encoding="utf-8",
                                errors="replace", capture_output=True, timeout=timeout)
        report["commands"].append({"command": command, "code": result.returncode,
                                   "stdout": result.stdout, "stderr": result.stderr})
        if result.returncode:
            raise RuntimeError(f"Command failed ({result.returncode}): {command}\n"
                               f"{result.stdout}\n{result.stderr}")
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
        for source in [ROOT / "include/events/events.cpp", ROOT / "tests/events_test.cpp"]:
            obj = output / (source.stem + ".bc")
            execute([clang, *compiler_flags(clang, sdk), "-std=c++17", "-O1",
                     "-I", ROOT / "include", "-emit-llvm", "-c", source, "-o", obj])
            objects.append(obj)
        sb3 = output / "events.sb3"
        # Exercise these event sources rather than the SDK's precompiled copy.
        core_bitcode = sdk_manifest.get("core_bitcode", sdk_manifest["bitcode"])
        execute([compiler, *objects, *[sdk / entry for entry in core_bitcode],
                 "--resources", pack, "--whole-program", "--passes", "default<O1>", "-o", sb3])

        with zipfile.ZipFile(sb3) as archive:
            project = json.loads(archive.read("project.json"))
        assert len(project["targets"]) == 2, "events must not add sprites"
        target = next(t for t in project["targets"] if not t["isStage"])
        blocks = target["blocks"]
        hats = [b for b in blocks.values() if b["opcode"] == "event_whenkeypressed"]
        keys = [b["fields"]["KEY_OPTION"][0] for b in hats]
        assert keys.count("up arrow") == 2 and keys.count("down arrow") == 2
        assert "enter" in keys and "space" in keys and "backspace" in keys
        for hat in hats:
            call = blocks[hat["next"]]
            assert call["opcode"] == "procedures_call"
            assert call["mutation"]["proccode"].startswith(("__scl_event_wheel_", "__scl_event_keyboard_"))
            assert str(call["mutation"]["warp"]).lower() == "true"
        vm_report = output / "events-vm-report.json"
        execute([node, ROOT / "tests/events_vm.cjs", sb3, vm_report])
        report["vm"] = json.loads(vm_report.read_text(encoding="utf-8"))
        assert report["vm"]["pass"]
        report["passed"] = True
        print("Events: native collectors and C++ callbacks passed in Scratch and TurboWarp.")
    except Exception as exc:
        report["error"] = str(exc)
        print(str(exc), file=sys.stderr)
    finally:
        (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n",
                                           encoding="utf-8")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
