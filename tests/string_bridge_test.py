"""Compile UTF-8 bridge cases and compare exact strings in both real VMs."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_stdlib import compiler_flags


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--node", default="node")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "tests/.tmp/string-bridge")
    args = parser.parse_args()
    work = args.output_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    strings = ["", "AazZ09 ::_-", "123", "\0", "before\0after", "中文造型", "😀𐀀\U0010ffff",
               "\u007f\u0080\u07ff\u0800\ud7ff\ue000\ufffd\uffff", "\t\n\r ",
               "Straße İı ée\u0301", "🧑🏽‍💻", "x" * 1024, "reset"]
    cases = [(text.encode("utf-8"), text) for text in strings]
    # Policy is deliberately byte-at-a-time replacement, not maximal subparts.
    cases += [(bytes(range(128)), ''.join(chr(i) if i != 8 else '�' for i in range(128))),
              (b"\x80\xbf", "��"), (b"\xc0\xaf", "��"), (b"\xe0\x80\xaf", "���"),
              (b"\xed\xa0\x80", "���"), (b"\xf4\x90\x80\x80", "����"),
              (b"\xf0\x9f", "��"), (b"\xe2A\xa1", "�A�"), (b"\xff\xfe", "��"),
              (b"\x07\x08\x09", "\x07�\x09"), (b"", "")]
    source = '#include <scratch_string.hpp>\n#include <string>\nint main() {\n'
    for data, _ in cases:
        literal = ''.join('\\x%02x' % byte for byte in data)
        source += 'scratch::set_string(std::string("' + literal + '", ' + str(len(data)) + '));\n'
        source += 'asm volatile("data_addtolist LIST=\\\"results\\\" ITEM=(data_variable VARIABLE=\\\"__scl_string\\\")" : : : "memory");\n'
    source += 'return 0;\n}\n'
    cpp, bc, sb3 = (work / name for name in ("cases.cpp", "cases.bc", "cases.sb3"))
    cpp.write_text(source, encoding="utf-8")
    sdk = args.sdk.resolve()
    subprocess.run([args.clang, *compiler_flags(args.clang, sdk), "-std=c++17", "-O1", "-emit-llvm",
                    "-c", str(cpp), "-o", str(bc)], check=True)
    manifest = json.loads((sdk / "manifest.json").read_text(encoding="utf-8"))
    subprocess.run([args.compiler, str(bc), *(str(sdk / path) for path in manifest["bitcode"]),
                    "--whole-program", "-o", str(sb3)], check=True)
    with zipfile.ZipFile(sb3) as archive:
        project = json.loads(archive.read("project.json"))
    tables = [value[1] for target in project["targets"] for value in target["lists"].values()
              if value[0] == "__scl_unicode"]
    assert len(tables) == 1 and len(tables[0]) == 3
    assert [len(text.encode("utf-16-le")) // 2 for text in tables[0]] == [63487, 2048, 2048]
    report = {"cases": len(cases), "runs": []}
    expected = [value for _, value in cases]
    for engine in ("scratch", "turbowarp"):
        result = subprocess.run([args.node, str(ROOT / "tests/vm_runner.cjs"), str(sb3), "--vm", engine,
                                 "--timeout", "180000", "--list-limit", "128"], check=True,
                                capture_output=True, text=True, encoding="utf-8")
        run = json.loads(result.stdout)
        assert run["variables"]["__scl_status"] == "done", run
        actual = run["lists"]["results"]
        assert actual == expected, [(i, got, want) for i, (got, want) in enumerate(zip(actual, expected)) if got != want]
        if engine == "turbowarp":
            assert run["compiledThreads"]
        report["runs"].append({"vm": engine, "passed": True, "result": run})
        print(f"{engine}: {len(cases)} exact string cases passed")
    (work / "report.json").write_text(json.dumps(report, ensure_ascii=True, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
