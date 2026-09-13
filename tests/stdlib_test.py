"""Compile first-batch libc++ fixtures and execute them in both real Scratch VMs.

This is targeted integration coverage, not a C++ standard conformance suite.
The SDK is built separately with tools/build_stdlib.py or the CMake target.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import fnmatch
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_stdlib import compiler_flags
from toolchain import resolve_clang, resolve_executable


CASES = [
    {"name": name, "source": name + ".cpp", "exit": 0,
     **({"scratchTimeoutMs": 600000} if name == "associative" else {})}
    for name in ("memory", "new", "new_handler", "vector", "string", "deque", "list", "associative", "utilities")
] + [
    {"name": "finalize", "source": "finalize.cpp", "exit": 0, "lifecycle": 31452},
    {"name": "replacement_paired", "source": "replacement.cpp", "mode": 1, "exit": 0},
    {"name": "replacement_unpaired", "source": "replacement.cpp", "mode": 0, "status": "error: llvm.trap"},
    {"name": "lifecycle_return", "source": "lifecycle.cpp", "mode": 0, "exit": 0, "lifecycle": 123789},
    {"name": "lifecycle_exit", "source": "lifecycle.cpp", "mode": 1, "exit": 17, "lifecycle": 123789},
    {"name": "lifecycle_immediate", "source": "lifecycle.cpp", "mode": 2, "exit": 18, "lifecycle": 123},
    {"name": "lifecycle_quick", "source": "lifecycle.cpp", "mode": 3, "exit": 19, "lifecycle": 1236},
] + [
    {"name": "failure_" + name, "source": "failure.cpp", "mode": mode, "status": "error: llvm.trap"}
    for mode, name in enumerate(("abort", "assert", "new", "bounds", "optional"), 1)
]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(command: list[str], timeout: int) -> dict:
    start = time.monotonic()
    try:
        result = subprocess.run(command, text=True, encoding="utf-8", errors="replace",
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
        return {"command": command, "code": result.returncode, "stdout": result.stdout,
                "stderr": result.stderr, "seconds": time.monotonic() - start}
    except subprocess.TimeoutExpired as error:
        return {"command": command, "code": None, "error": f"exceeded {timeout}s",
                "seconds": time.monotonic() - start}


def check_result(result: dict, case: dict, engine: str) -> list[str]:
    errors = []
    if result.get("status") != "completed":
        errors.append(f"VM did not complete: {result.get('error', result.get('status'))}")
    variables = result.get("variables", {})
    expected_status = case.get("status", "done")
    if variables.get("__scl_status") != expected_status:
        errors.append(f"__scl_status={variables.get('__scl_status')!r}; expected {expected_status!r}")
    for name, expected in (("exit_code", case.get("exit")), ("lifecycle", case.get("lifecycle"))):
        if expected is not None:
            try:
                actual = float(variables.get(name))
            except (TypeError, ValueError):
                actual = None
            if actual != expected:
                errors.append(f"{name}={variables.get(name)!r}; expected {expected}")
    if "exit" in case:
        expected_bytes = [(case["exit"] >> (8 * i)) & 255 for i in range(4)]
        try:
            actual_bytes = [int(value) for value in result.get("lists", {}).get("return_bytes", [])]
        except (ValueError, TypeError):
            actual_bytes = None
        if actual_bytes != expected_bytes:
            errors.append(f"return_bytes={actual_bytes!r}; expected {expected_bytes}")
    if engine == "turbowarp" and not result.get("compiledThreads"):
        errors.append("TurboWarp did not execute a compiled thread")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--clang")
    parser.add_argument("--sdk", type=Path, default=ROOT / "build/stdlib")
    parser.add_argument("--node", default="node")
    parser.add_argument("--vm", choices=("both", "scratch", "turbowarp"), default="both")
    parser.add_argument("--timeout", type=int, default=180000, help="compilation/execution timeout per VM, milliseconds")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "tests/.tmp/stdlib")
    parser.add_argument("--case", action="append", default=[], help="fixture glob; repeatable")
    parser.add_argument("--include-stress", action="store_true", help="also run the large combined sequences project")
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    available = CASES + ([{"name": "sequences_stress", "source": "sequences.cpp", "exit": 0,
                          "scratchTimeoutMs": 600000}] if args.include_stress else [])
    cases = [case for case in available if not args.case or any(fnmatch.fnmatchcase(case["name"], pattern) for pattern in args.case)]
    if not cases:
        parser.error("No matching fixtures")
    if args.list:
        print("\n".join(case["name"] for case in cases))
        return 0
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    compiler = Path(resolve_executable(args.compiler, "Scratch compiler"))
    clang = resolve_clang(args.clang)
    node = resolve_executable(args.node, "Node.js")
    sdk = args.sdk.resolve()
    manifest = json.loads((sdk / "manifest.json").read_text(encoding="utf-8"))
    flags = compiler_flags(clang, sdk)
    bitcode = [str(sdk / file) for file in manifest["bitcode"]]
    work = args.output_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    engines = ("scratch", "turbowarp") if args.vm == "both" else (args.vm,)
    report = {"schemaVersion": 1, "startedAt": datetime.now(timezone.utc).isoformat(),
              "platform": platform.platform(), "compiler": str(compiler), "compilerSha256": digest(compiler),
              "clang": clang, "sdk": str(sdk), "sdkManifest": manifest,
              "runtimeSha256": {file: digest(Path(file)) for file in bitcode},
              "timeoutMs": args.timeout, "total": len(cases), "cases": []}
    def save_report() -> None:
        report["passed"] = sum(item.get("complete", False) and bool(item["passed"]) for item in report["cases"])
        report["completed"] = sum(item.get("complete", False) for item in report["cases"])
        (work / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    for case in cases:
        print(f"{case['name']}: compiling", flush=True)
        source = ROOT / "tests/stdlib" / case["source"]
        bc, sb3 = work / (case["name"] + ".bc"), work / (case["name"] + ".sb3")
        for artifact in (bc, sb3):
            artifact.unlink(missing_ok=True)
        entry = {"name": case["name"], "expected": case, "sourceSha256": digest(source),
                 "runs": [], "passed": False, "complete": False}
        report["cases"].append(entry)
        save_report()
        defines = [f"-DMODE={case['mode']}"] if "mode" in case else []
        defines.append(f"-DSCRATCH_TEST_HEAP_BYTES={manifest['heap_bytes']}")
        entry["clang"] = execute([clang, *flags, "-std=c++17", "-fsized-deallocation", "-O1", "-emit-llvm", "-c",
                                  *defines, str(source), "-o", str(bc)], 120)
        if entry["clang"]["code"] == 0:
            entry["compilerSha256"] = digest(compiler)
            entry["compiler"] = execute([str(compiler), str(bc), *bitcode, "-o", str(sb3)], 180)
        entry["passed"] = entry.get("compiler", {}).get("code") == 0
        save_report()
        if entry["passed"]:
            # Keep the exact generated project reviewable, and ensure the archive
            # contains valid JSON before attributing any later failure to a VM.
            with zipfile.ZipFile(sb3) as archive:
                project = archive.read("project.json")
                json.loads(project)
                (work / (case["name"] + ".project.json")).write_bytes(project)
            entry["sb3Sha256"] = digest(sb3)
            for engine in engines:
                timeout = max(args.timeout, case.get("scratchTimeoutMs", 0)) if engine == "scratch" else args.timeout
                run = execute([node, str(ROOT / "tests/vm_runner.cjs"), str(sb3), "--vm", engine,
                               "--timeout", str(timeout), "--list-limit", "16"], timeout // 1000 + 45)
                run["vm"] = engine
                run["timeoutMs"] = timeout
                try:
                    run["result"] = json.loads(run.pop("stdout", ""))
                    run["errors"] = check_result(run["result"], case, engine)
                except (ValueError, TypeError) as error:
                    run["errors"] = [f"Invalid VM result: {error}"]
                run["passed"] = run["code"] == 0 and not run["errors"]
                entry["runs"].append(run)
                entry["passed"] &= run["passed"]
                save_report()
                print(f"  {engine}: {'PASS' if run['passed'] else 'FAIL'} {run['errors']}", flush=True)
        if not entry["passed"] and not entry["runs"]:
            print(entry.get("compiler", entry["clang"]), flush=True)
        entry["complete"] = True
        save_report()
    print(f"{report['passed']}/{report['total']} fixtures passed; report: {work / 'report.json'}")
    return 0 if report["passed"] == len(cases) else 1


if __name__ == "__main__":
    raise SystemExit(main())
