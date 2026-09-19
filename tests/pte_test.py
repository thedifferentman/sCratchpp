"""Link the standalone PTE manifest and compare native pen operations in both VMs."""
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


SOURCE = r'''
#include <pte/pte.hpp>
int main() {
    unsigned cps[] = {65, 0x4e2d, 0x10ffff, 32, 87, 105};
    int sum = 0;
    for (auto cp : cps) {
        scratch::pte::draw(cp, -10, 10, 20, 0x123456);
        scratch::pte::draw_cell(cp, -10, 10, 20, 10, 0x123456);
        sum += scratch::pte::measure_units(cp);
    }
    scratch::pte::draw_cell(87, -10, 10, 20, 0, 0x123456);
    scratch::pte::draw_cell(87, -10, 10, 0, 10, 0x123456);
    return sum;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--clang")
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--node", default="node")
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    report = {"schemaVersion": 1, "commands": [], "passed": False}

    def run(command):
        step = {"command": list(map(str, command))}
        report["commands"].append(step)
        result = subprocess.run(step["command"], cwd=ROOT, capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=120)
        step.update(returncode=result.returncode, stdout=result.stdout, stderr=result.stderr)
        if result.returncode:
            raise RuntimeError(f"Command failed: {step['command']}\n{result.stdout}\n{result.stderr}")

    try:
        compiler = resolve_executable(args.compiler, "Scratch compiler")
        clang = resolve_clang(args.clang)
        node = resolve_executable(args.node, "Node.js")
        report["compilerSha256"] = hashlib.sha256(Path(compiler).read_bytes()).hexdigest()
        resource = prepare_resources(ROOT / "include/pte/sCrpp.toml", output / "resources")
        source = output / "main.cpp"
        source.write_text(SOURCE, encoding="utf-8")
        flags = compiler_flags(clang, args.sdk.resolve())
        inputs = []
        for item in (source, ROOT / "include/pte/pte.cpp"):
            target = output / (item.stem + ".ll")
            run([clang, *flags, "-std=c++17", "-O1", "-I", ROOT / "include",
                 "-emit-llvm", "-S", item, "-o", target])
            inputs.append(target)
        project = output / "pte.sb3"
        run([compiler, *inputs, "--resources", resource, "-o", project])
        vm_report = output / "vm-report.json"
        run([node, ROOT / "tests/pte_vm.cjs", project, vm_report,
             ROOT / "include/pte/resources/SOURCE.json"])
        report["vms"] = json.loads(vm_report.read_text(encoding="utf-8"))
        report["passed"] = all(result["ok"] for result in report["vms"])
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if not report["passed"]:
        print(report.get("error", "PTE VM trace mismatch"), file=sys.stderr)
        return 1
    trace_count = report["vms"][0]["traceLength"]
    glyph_count = report["vms"][0]["fontRows"]
    print(f"PTE: {glyph_count} font records, {trace_count} pen/motion operations, "
          "12 glyph draws, natural/cell widths and empty cells passed in both VMs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
