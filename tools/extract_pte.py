#!/usr/bin/env python3
"""Extract and validate the user-supplied PTE SB3's immutable font tables.

No source project scripts are executed. The generated files preserve each
record verbatim, including the initial space in the space character's row.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def extract(source, output):
    raw = source.read_bytes()
    with zipfile.ZipFile(source) as archive:
        project_bytes = archive.read("project.json")
    project = json.loads(project_bytes.decode("utf-8"))
    matches = [value[1] for target in project["targets"]
               for value in target.get("lists", {}).values()
               if value[0] == "字体"]
    if len(matches) != 1:
        raise ValueError("Expected exactly one list named 字体")
    records = matches[0]
    indices = [0] * 65536
    previous = -1
    for index, record in enumerate(records, 1):
        if not isinstance(record, str) or len(record) < 3:
            raise ValueError(f"Invalid font record {index}")
        cp = ord(record[0])
        if cp <= previous or cp > 65535 or 0xD800 <= cp <= 0xDFFF:
            raise ValueError(f"Non-BMP, duplicate or unsorted codepoint at {index}")
        previous = cp
        if cp in (10, 13) or not re.fullmatch(r"[0-9a-fA-F]{2}(?:[0-9a-fA-F]{3}|M)*", record[1:]):
            raise ValueError(f"Malformed width/stroke encoding at {index}")
        if record[3:].startswith("M") or "MM" in record[3:] or record.endswith("M"):
            raise ValueError(f"Empty stroke at {index}")
        indices[cp] = index
    if not indices[0x25A1]:
        raise ValueError("Missing fallback square U+25A1")
    font = ("\n".join(records) + "\n").encode("utf-8")
    lookup = ("\n".join(map(str, indices)) + "\n").encode("ascii")
    output.mkdir(parents=True, exist_ok=True)
    (output / "font.txt").write_bytes(font)
    (output / "index.txt").write_bytes(lookup)
    provenance = {
        "source_filename": source.name,
        "source_sha256": digest(raw),
        "source_project_json_sha256": digest(project_bytes),
        "source_list": "字体",
        "glyph_count": len(records),
        "index_entries": len(indices),
        "fallback_codepoint": "U+25A1",
        "fallback_glyph_index": indices[0x25A1],
        "font_sha256": digest(font),
        "index_sha256": digest(lookup),
        "record_format": "BMP character + 2 hex advance units + 3 hex packed points, M separates strokes; 64 units/em",
        "provenance": "User-provided Scratch project. Font records extracted verbatim; codepoint index generated.",
        "license": "Unspecified in supplied project; no ownership or redistribution license is asserted.",
    }
    (output / "SOURCE.json").write_text(json.dumps(provenance, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return provenance


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "include/pte/resources")
    args = parser.parse_args()
    result = extract(args.source, args.output)
    print(f"Extracted {result['glyph_count']} glyphs and {result['index_entries']} index entries")
