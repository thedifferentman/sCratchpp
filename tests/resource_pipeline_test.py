"""Build PNG/SVG packs, link real C++ and execute resources in both Scratch VMs.

The VM test deliberately has no renderer. Image encoding, display dimensions and
rotation centers are checked structurally; visual acceptance is a separate test.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from build_resources import prepare_resources
from build_stdlib import compiler_flags


def png(width, height):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    # This creates valid, deterministic source pixels without a Pillow dependency.
    rows = b"".join(b"\0" + bytes((x % 256, y % 256, 127, 255)) * width for y, x in enumerate(range(height)))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--clang", required=True)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--node", default="node")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "tests/.tmp/resources")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = output / "source"
    (source / "resources").mkdir(parents=True, exist_ok=True)
    (source / "images").mkdir(parents=True, exist_ok=True)
    pixels = png(640, 480)
    (source / "resources/tile.png").write_bytes(pixels)
    (source / "images/shape.svg").write_text(
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 10">'
        '<rect width="20" height="10" fill="#123abc"/></svg>', encoding="utf-8")
    manifest_text = '''version = 1
[package]
name = "demo"
[[costumes]]
file = "tile.png"
size = [64, 48]
center = [7, 9]
[[costumes]]
file = "tile.png"
name = "copy"
size = [64, 48]
center = [0, 0]
[[costumes]]
file = "images/shape.svg"
name = "123"
[[costumes]]
file = "images/shape.svg"
name = "待机😀"
'''
    manifest = source / "sCrpp.toml"
    manifest.write_text(manifest_text, encoding="utf-8")
    pack = prepare_resources(manifest, output / "pack")
    prepared = json.loads(pack.read_text(encoding="utf-8"))
    assert len(prepared["costumes"]) == 4
    assert len(list((pack.parent / "assets").iterdir())) == 2, "physical image deduplication"
    first, second, third, fourth = prepared["costumes"]
    assert first["md5ext"] == second["md5ext"]
    assert third["md5ext"] == fourth["md5ext"]
    assert (first["rotationCenterX"], first["rotationCenterY"]) == (7, 9)
    assert (second["rotationCenterX"], second["rotationCenterY"]) == (0, 0)
    assert (third["rotationCenterX"], third["rotationCenterY"]) == (10, 5)
    svg = ET.fromstring((pack.parent / first["path"]).read_bytes())
    assert (float(svg.attrib["width"]), float(svg.attrib["height"])) == (64, 48)
    image = svg.find("{http://www.w3.org/2000/svg}image")
    assert image is not None
    encoded = image.attrib["{http://www.w3.org/1999/xlink}href"]
    assert base64.b64decode(encoded.split(",", 1)[1]) == pixels, "original PNG bytes preserved"
    defaults = source / "default.toml"
    defaults.write_text('version = 1\n[package]\nname = "default"\n[[costumes]]\nfile = "tile.png"\n', encoding="utf-8")
    default_pack = prepare_resources(defaults, output / "default-pack")
    default_costume = json.loads(default_pack.read_text(encoding="utf-8"))["costumes"][0]
    default_svg = ET.fromstring((default_pack.parent / default_costume["path"]).read_bytes())
    assert (float(default_svg.attrib["width"]), float(default_svg.attrib["height"])) == (640, 480)
    assert (default_costume["rotationCenterX"], default_costume["rotationCenterY"]) == (320, 240)
    assert default_costume["name"] == "default::tile"
    commands = []

    def execute(command, expected=0):
        result = subprocess.run([str(x) for x in command], text=True, encoding="utf-8", errors="replace",
                                capture_output=True, timeout=240)
        commands.append({"command": [str(x) for x in command], "code": result.returncode,
                         "stdout": result.stdout, "stderr": result.stderr})
        assert (result.returncode == 0) == (expected == 0), commands[-1]
        return result

    # Copy only the pack; linked objects must not depend on source image paths.
    portable = output / "portable"
    shutil.copytree(pack.parent, portable, dirs_exist_ok=True)
    portable_pack = portable / "resources.json"
    sdk = args.sdk.resolve()
    sdk_manifest = json.loads((sdk / "manifest.json").read_text(encoding="utf-8"))
    bitcode = output / "resources.bc"
    execute([args.clang, *compiler_flags(args.clang, sdk), "-std=c++17", "-O1", "-g", "-emit-llvm", "-c",
             ROOT / "tests/fixtures/resources.cpp", "-o", bitcode])
    sb3 = output / "resources.sb3"
    debug = output / "resources.debug.json"
    base_command = [args.compiler, bitcode, *[sdk / x for x in sdk_manifest["bitcode"]], "--whole-program"]
    execute([*base_command, "--resources", portable_pack, "--debug-map", debug, "-o", sb3])
    with zipfile.ZipFile(sb3) as archive:
        project_bytes = archive.read("project.json")
        project = json.loads(project_bytes)
        assert len(project["targets"]) == 2
        program = next(t for t in project["targets"] if not t["isStage"])
        assert program["name"] == "Program"
        assert program["costumes"][1:] == [{k: v for k, v in c.items() if k != "path"} for c in prepared["costumes"]]
        for c in prepared["costumes"]:
            asset = archive.read(c["md5ext"])
            assert hashlib.md5(asset).hexdigest() == c["assetId"]
        assert archive.namelist().count(first["md5ext"]) == 1
        assert sum(b["opcode"] == "event_whenflagclicked" for b in program["blocks"].values()) == 1
    mapping = json.loads(debug.read_text(encoding="utf-8"))
    assert mapping["projectCrc32"] == f"{zlib.crc32(project_bytes):08x}", "debug identity covers resource metadata"
    multiple = output / "multiple.sb3"
    execute([*base_command, "--resources", portable_pack, "--resources", default_pack, "-o", multiple])
    with zipfile.ZipFile(multiple) as archive:
        combined = json.loads(archive.read("project.json"))
        assert len(combined["targets"]) == 2, "independent packages must not add sprites"
        combined_program = next(t for t in combined["targets"] if not t["isStage"])
        assert [c["name"] for c in combined_program["costumes"]] == [
            "blank", "demo::tile", "demo::copy", "demo::123", "demo::待机😀", "default::tile"]
    duplicate = execute([*base_command, "--resources", portable_pack, "--resources", portable_pack,
                         "-o", output / "duplicate.sb3"], expected=1)
    assert any(word in duplicate.stderr.lower() for word in ("duplicate", "collision", "conflict")), duplicate.stderr
    # Resource-only metadata changes must invalidate the prior debug identity.
    modified = json.loads(portable_pack.read_text(encoding="utf-8"))
    modified["costumes"][0]["rotationCenterX"] += 1
    portable_pack.write_text(json.dumps(modified, ensure_ascii=False), encoding="utf-8")
    changed_sb3, changed_debug = output / "changed.sb3", output / "changed.debug.json"
    execute([*base_command, "--resources", portable_pack, "--debug-map", changed_debug, "-o", changed_sb3])
    changed_map = json.loads(changed_debug.read_text(encoding="utf-8"))
    with zipfile.ZipFile(changed_sb3) as archive:
        assert changed_map["projectCrc32"] == f'{zlib.crc32(archive.read("project.json")):08x}'
    assert mapping["projectCrc32"] != changed_map["projectCrc32"]
    vm = execute([args.node, ROOT / "tests/resource_vm.cjs", sb3])
    report = {"passed": True, "compilerSha256": hashlib.sha256(Path(args.compiler).read_bytes()).hexdigest(),
              "vm": json.loads(vm.stdout), "commands": commands, "pixelsVerified": False}
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(f"Resource pipeline and both VMs passed: {output / 'report.json'}")


if __name__ == "__main__":
    main()
