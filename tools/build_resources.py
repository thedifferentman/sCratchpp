#!/usr/bin/env python3
"""Prepare portable Scratch resource packs from sCrpp.toml (Python 3.11+)."""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zlib

SVG = "http://www.w3.org/2000/svg"
XLINK = "http://www.w3.org/1999/xlink"
ET.register_namespace("", SVG)
ET.register_namespace("xlink", XLINK)


def _name(value, label):
    if not isinstance(value, str) or not value.strip() or "::" in value:
        raise ValueError(f"{label} must be nonempty text without '::'")
    if any(ord(c) < 32 for c in value):
        raise ValueError(f"{label} must not contain control characters")
    try:
        value.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise ValueError(f"{label} must be valid Unicode text") from exc
    return value


def load_manifest(manifest):
    try:
        import tomllib
    except ImportError as exc:
        raise ValueError("Resource manifests require Python 3.11 or newer (tomllib).") from exc
    path = Path(manifest).resolve()
    with path.open("rb") as stream:
        data = tomllib.load(stream)
    if type(data.get("version")) is not int or data["version"] != 1:
        raise ValueError("sCrpp.toml must declare version = 1")
    package = data.get("package", {})
    if not isinstance(package, dict):
        raise ValueError("sCrpp.toml requires [package] with name = '...' ")
    _name(package.get("name"), "package.name")
    if "resource_namespace" in package:
        _name(package["resource_namespace"], "package.resource_namespace")
    if not isinstance(data.get("costumes", []), list):
        raise ValueError("costumes must be an array of [[costumes]] tables")
    for field in ("lists", "events"):
        if not isinstance(data.get(field, []), list):
            raise ValueError(f"{field} must be an array of [[{field}]] tables")
    return data


def read_package_name(manifest):
    package = load_manifest(manifest)["package"]
    return package.get("resource_namespace", package["name"])


def collect_manifests(manifest):
    """Return the primary manifest followed by unique linked resource manifests."""
    result, visited, active, packages = [], set(), set(), {}

    def visit(path):
        path = Path(path).resolve()
        if path in active:
            raise ValueError(f"Resource manifest dependency cycle at {path}")
        if path in visited:
            return
        config = load_manifest(path)
        package = config["package"].get("resource_namespace", config["package"]["name"])
        if package in packages:
            raise ValueError(f"Duplicate resource package {package!r}: {packages[package]} and {path}")
        packages[package] = path
        link = config.get("link", {})
        if not isinstance(link, dict) or not isinstance(link.get("resources", []), list):
            raise ValueError("[link] resources must be an array of manifest paths")
        resources = link.get("resources", [])
        if any(not isinstance(item, str) or not item for item in resources):
            raise ValueError("[link] resources must contain nonempty manifest paths")
        result.append(path)
        visited.add(path)
        active.add(path)
        for item in resources:
            visit(path.parent / item.replace("\\", "/"))
        active.remove(path)

    visit(manifest)
    return result


def _pair(value, label, positive=False):
    if not isinstance(value, list) or len(value) != 2:
        raise ValueError(f"{label} must contain two numbers")
    if any(type(v) not in (int, float) or not math.isfinite(v) or
           (positive and v <= 0) for v in value):
        raise ValueError(f"{label} must contain finite {'positive ' if positive else ''}numbers")
    return tuple(value)


def _number(value):
    return format(value, ".15g")


def _png_size(data):
    if not data.startswith(b"\x89PNG\r\n\x1a\n"):
        raise ValueError("Invalid PNG signature")
    offset, size, seen_idat, ended = 8, None, False, False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ValueError("Truncated PNG chunk")
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 12 + length
        if end > len(data):
            raise ValueError("Truncated PNG chunk data")
        payload = data[offset + 8:end - 4]
        crc = struct.unpack_from(">I", data, end - 4)[0]
        if zlib.crc32(kind + payload) & 0xffffffff != crc:
            raise ValueError("Invalid PNG chunk CRC")
        if size is None and kind != b"IHDR":
            raise ValueError("PNG must start with IHDR")
        if kind == b"IHDR":
            if size is not None or length != 13:
                raise ValueError("Invalid PNG IHDR")
            w, h, depth, color, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            depths = {0: {1, 2, 4, 8, 16}, 2: {8, 16}, 3: {1, 2, 4, 8}, 4: {8, 16}, 6: {8, 16}}
            if not (0 < w < 2**31 and 0 < h < 2**31) or depth not in depths.get(color, set()):
                raise ValueError("Invalid PNG dimensions or pixel format")
            if compression != 0 or filtering != 0 or interlace not in (0, 1):
                raise ValueError("Unsupported PNG encoding")
            size = (w, h)
        if kind == b"IDAT":
            seen_idat = True
        if kind == b"IEND":
            if length or end != len(data) or not seen_idat:
                raise ValueError("Invalid PNG end or missing pixel data")
            ended = True
            break
        offset = end
    if not ended:
        raise ValueError("PNG is missing IEND")
    return size


def _length(text):
    if text is None:
        return None
    match = re.fullmatch(r"\s*([+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)\s*(px|in|cm|mm|pt|pc)?\s*", text)
    if not match:
        raise ValueError(f"Unsupported SVG dimension {text!r}; use absolute units or a viewBox")
    scale = {None: 1, "px": 1, "in": 96, "cm": 96 / 2.54,
             "mm": 96 / 25.4, "pt": 96 / 72, "pc": 16}[match[2]]
    value = float(match[1]) * scale
    if not math.isfinite(value) or value <= 0:
        raise ValueError("SVG dimensions must be finite and positive")
    return value


def _local(tag):
    return tag.rsplit("}", 1)[-1]


def _check_svg(root, data):
    if re.search(br"<!\s*(?:DOCTYPE|ENTITY)", data, re.I):
        raise ValueError("SVG document types and entities are unsupported")
    if re.search(br"<\?xml-stylesheet", data, re.I):
        raise ValueError("External SVG stylesheets are unsupported")
    for node in root.iter():
        if _local(node.tag).lower() in ("script", "foreignobject"):
            raise ValueError("SVG scripts and foreignObject are unsupported")
        for key, value in node.attrib.items():
            key = _local(key).lower()
            if key.startswith("on") or key == "base":
                raise ValueError("SVG event handlers and xml:base are unsupported")
            if key in ("href", "src") and not (value.startswith("#") or
                    re.match(r"data:image/(?:png|jpeg|gif|webp);base64,", value, re.I)):
                raise ValueError("SVG resources must be embedded images or local # references")
        texts = list(node.attrib.values())
        if _local(node.tag).lower() == "style":
            texts.append(node.text or "")
        for text in texts:
            if "@import" in text.lower() or "\\" in text:
                raise ValueError("SVG CSS imports and escaped resource references are unsupported")
            for match in re.finditer(r"url\s*\((.*?)\)", text, re.I | re.S):
                target = match[1].strip().strip("\"'")
                if not target.startswith("#"):
                    raise ValueError("SVG CSS URLs must reference a local # identifier")


def _svg(data, size):
    # Reject entities before parsing, including internal entity expansion.
    if re.search(br"<!\s*(?:DOCTYPE|ENTITY)", data, re.I):
        raise ValueError("SVG document types and entities are unsupported")
    try:
        root = ET.fromstring(data)
    except ET.ParseError as exc:
        raise ValueError(f"Invalid SVG XML: {exc}") from exc
    if root.tag not in ("svg", f"{{{SVG}}}svg"):
        raise ValueError("SVG requires an svg root element")
    _check_svg(root, data)
    box = None
    if "viewBox" in root.attrib:
        try:
            box = [float(x) for x in re.split(r"[\s,]+", root.attrib["viewBox"].strip())]
        except ValueError as exc:
            raise ValueError("Invalid SVG viewBox") from exc
        if len(box) != 4 or not all(math.isfinite(v) for v in box) or min(box[2:]) <= 0:
            raise ValueError("SVG viewBox needs four finite values with positive width and height")
    width, height = _length(root.get("width")), _length(root.get("height"))
    if width is None:
        width = (height * box[2] / box[3] if height is not None else box[2]) if box else None
    if height is None:
        height = width * box[3] / box[2] if box and width is not None else None
    if width is None or height is None:
        raise ValueError("SVG requires absolute width/height or a viewBox")
    logical = size or (width, height)
    if box is None:
        root.set("viewBox", f"0 0 {_number(width)} {_number(height)}")
    root.set("width", _number(logical[0]))
    root.set("height", _number(logical[1]))
    if root.tag == "svg":
        root.set("xmlns", SVG)
    # Scratch normalizes a costume's outer viewBox into its logical size. Merely
    # changing width/height is insufficient when the source viewBox differs.
    # Keep the source viewport/aspect-ratio/styles intact inside an outer canvas
    # whose coordinates match the manifest's display dimensions and center.
    outer = ET.Element(f"{{{SVG}}}svg", {
        "width": _number(logical[0]), "height": _number(logical[1]),
        "viewBox": f"0 0 {_number(logical[0])} {_number(logical[1])}",
    })
    outer.append(root)
    return ET.tostring(outer, encoding="utf-8", xml_declaration=True), logical


def _costume(file, size):
    data = file.read_bytes()
    extension = file.suffix.lower()
    if extension == ".png":
        pixels = _png_size(data)
        logical = size or pixels
        width, height = map(_number, logical)
        encoded = base64.b64encode(data).decode("ascii")
        text = (f'<svg xmlns="{SVG}" xmlns:xlink="{XLINK}" width="{width}" height="{height}" '
                f'viewBox="0 0 {width} {height}"><image width="{width}" height="{height}" '
                f'preserveAspectRatio="none" xlink:href="data:image/png;base64,{encoded}"/></svg>')
        return text.encode("utf-8"), logical
    if extension == ".svg":
        return _svg(data, size)
    raise ValueError(f"Unsupported resource format {extension!r}; use PNG or SVG")


def _source_path(manifest, spelling):
    # Treat Windows separators identically on every build host.
    spelling = spelling.replace("\\", "/")
    relative = Path(spelling)
    return (manifest.parent / ("resources" if "/" not in spelling else "") / relative).resolve()


def _text_items(source):
    # Do not use splitlines(): U+2028, U+2029 and other Unicode characters
    # can be actual glyph data. Do not let universal-newline handling alter CR.
    text = source.read_bytes().decode("utf-8-sig")
    if "\b" in text:
        raise ValueError("List text contains U+0008, which Scratch removes when loading projects")
    if not text:
        return []
    items = text.split("\n")
    if text.endswith("\n"):
        items.pop()
    # A CR immediately before LF is a line ending. A final standalone CR is data.
    terminated = len(items) if text.endswith("\n") else len(items) - 1
    for index in range(terminated):
        if items[index].endswith("\r"):
            items[index] = items[index][:-1]
    if len(items) > 200000:
        raise ValueError("Scratch lists support at most 200000 initial items")
    return items


def _lists(config, manifest, package):
    lists, names = [], set()
    for index, item in enumerate(config.get("lists", [])):
        label = f"lists[{index}]"
        if not isinstance(item, dict) or not isinstance(item.get("file"), str) or not item["file"]:
            raise ValueError(f"{label}.file must name a UTF-8 TXT file")
        if set(item) - {"file", "name", "readonly"}:
            raise ValueError(f"Unknown {label} fields")
        source = _source_path(manifest, item["file"])
        if source.suffix.lower() != ".txt":
            raise ValueError(f"{label}.file must name a TXT file")
        stem = Path(item["file"].replace("\\", "/")).stem
        name = package + "::" + _name(item.get("name", stem), label + ".name")
        if name in names:
            raise ValueError(f"Duplicate list name: {name}")
        names.add(name)
        readonly = item.get("readonly", False)
        if type(readonly) is not bool:
            raise ValueError(f"{label}.readonly must be a boolean")
        try:
            items = _text_items(source)
        except (OSError, ValueError) as exc:
            raise ValueError(f"{label} ({item['file']}): {exc}") from exc
        lists.append({"name": name, "items": items, "readonly": readonly})
    return lists


def _events(config, package, lists):
    events, queues = [], set()
    readonly = {item["name"] for item in lists if item["readonly"]}
    for index, item in enumerate(config.get("events", [])):
        label = f"events[{index}]"
        if not isinstance(item, dict) or set(item) - {"type", "queue", "enabled", "capacity"}:
            raise ValueError(f"Invalid {label} fields")
        if item.get("type") not in ("wheel", "keyboard"):
            raise ValueError(f"{label}.type must be 'wheel' or 'keyboard'")
        queue = package + "::" + _name(item.get("queue"), label + ".queue")
        enabled = _name(item.get("enabled"), label + ".enabled")
        capacity = item.get("capacity", 128)
        if type(capacity) is not int or not 1 <= capacity <= 200000:
            raise ValueError(f"{label}.capacity must be an integer from 1 to 200000")
        if queue in queues or queue in readonly:
            raise ValueError(f"Duplicate or readonly event queue: {queue}")
        queues.add(queue)
        events.append({"type": item["type"], "queue": queue, "enabled": enabled, "capacity": capacity})
    return events


def prepare_resources(manifest, output_dir):
    """Create resources.json and content-addressed SVG assets; return JSON Path."""
    manifest = Path(manifest).resolve()
    output = Path(output_dir).resolve()
    config = load_manifest(manifest)
    package = config["package"].get("resource_namespace", config["package"]["name"])
    costumes, assets, names = [], {}, set()
    for index, item in enumerate(config.get("costumes", [])):
        label = f"costumes[{index}]"
        if not isinstance(item, dict) or not isinstance(item.get("file"), str) or not item["file"]:
            raise ValueError(f"{label}.file must name a PNG or SVG file")
        if set(item) - {"file", "name", "size", "center"}:
            raise ValueError(f"Unknown {label} fields: {', '.join(sorted(set(item) - {'file', 'name', 'size', 'center'}))}")
        # Use forward slash semantics consistently even when a manifest originated on Windows.
        spelling = item["file"].replace("\\", "/")
        relative = Path(spelling)
        source = _source_path(manifest, spelling)
        name = package + "::" + _name(item.get("name", relative.stem), label + ".name")
        if name in names:
            raise ValueError(f"Duplicate costume name: {name}")
        names.add(name)
        size = _pair(item["size"], label + ".size", True) if "size" in item else None
        center = _pair(item["center"], label + ".center") if "center" in item else None
        try:
            data, logical = _costume(source, size)
        except (OSError, ValueError) as exc:
            raise ValueError(f"{label} ({item['file']}): {exc}") from exc
        center = center or (logical[0] / 2, logical[1] / 2)
        digest = hashlib.md5(data).hexdigest()
        filename = digest + ".svg"
        assets[filename] = data
        costumes.append({"name": name, "assetId": digest, "md5ext": filename,
                         "dataFormat": "svg", "bitmapResolution": 1,
                         "rotationCenterX": center[0], "rotationCenterY": center[1],
                         "path": "assets/" + filename})
    lists = _lists(config, manifest, package)
    events = _events(config, package, lists)
    # Validate all input before writing anything. Existing unrelated files are never deleted.
    (output / "assets").mkdir(parents=True, exist_ok=True)
    for name, data in assets.items():
        (output / "assets" / name).write_bytes(data)
    result = output / "resources.json"
    text = json.dumps({"schemaVersion": 1, "package": package, "costumes": costumes,
                       "lists": lists, "events": events},
                      ensure_ascii=False, indent=2) + "\n"
    temporary = result.with_suffix(".json.tmp")
    temporary.write_text(text, encoding="utf-8")
    temporary.replace(result)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        print(prepare_resources(args.manifest, args.output_dir))
    except (OSError, ValueError) as exc:
        print(f"Resource preparation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
