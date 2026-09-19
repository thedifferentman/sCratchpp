#!/usr/bin/env python3
"""Build, run, debug and manage exact-version sCr++ packages.

Packages are data: this tool never executes package scripts. Python 3.11+.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import tomllib
import unicodedata
from urllib.parse import urljoin, urlparse
from urllib.request import urlopen
import zipfile

DEFAULT_REGISTRY = "https://scrate.shapy.cn"
VERSION = "0.2.0"
MAX_FILES = 20000
MAX_FILE_BYTES = 64 * 1024 * 1024
MAX_TOTAL_BYTES = 256 * 1024 * 1024
MAX_ARCHIVE_BYTES = 256 * 1024 * 1024
MAX_RATIO = 2000
IGNORED = {".git", ".scrate", "build", "dist", "__pycache__", "node_modules", ".idea"}
IGNORED_FILES = {"scrate.lock", ".DS_Store", "Thumbs.db"}
NAME_RE = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_.-]*\Z")
VERSION_RE = re.compile(r"(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\Z")
RESERVED_RE = re.compile(r"(?:CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\..*)?\Z", re.I)


def _json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2) + "\n").encode("utf-8")


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def _atomic_write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _immutable_write(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        try:
            # Hard-link publication is atomic and cannot overwrite a concurrent
            # publisher. Unlike rename(), its no-replace behavior is portable.
            os.link(temporary, path)
        except FileExistsError:
            if path.read_bytes() != data:
                raise ValueError(f"Immutable package version already has different content: {path}")
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _name(value):
    if (not isinstance(value, str) or not NAME_RE.fullmatch(value) or RESERVED_RE.fullmatch(value)
            or value != value.lower() or value.endswith(".")):
        raise ValueError(f"Invalid package name: {value!r}")
    return value


def _version(value):
    if not isinstance(value, str) or not VERSION_RE.fullmatch(value):
        raise ValueError(f"An exact x.y.z package version is required, got {value!r}")
    return value


def _load(manifest):
    manifest = Path(manifest)
    with manifest.open("rb") as stream:
        config = tomllib.load(stream)
    if "scripts" in config:
        raise ValueError(f"Package scripts are unsupported and never executed: {manifest}")
    if type(config.get("version")) is not int or config["version"] != 1:
        raise ValueError(f"Unsupported manifest version: {manifest}")
    package = config.get("package")
    if not isinstance(package, dict):
        raise ValueError(f"Missing [package] in {manifest}")
    _name(package.get("name"))
    if "version" in package:
        _version(package["version"])
    if not isinstance(config.get("dependencies", {}), dict):
        raise ValueError(f"[dependencies] must be a table: {manifest}")
    return config


def _dependency(name, spec):
    _name(name)
    if isinstance(spec, str):
        return {"version": _version(spec)}
    if not isinstance(spec, dict) or set(spec) - {"path", "version"}:
        raise ValueError(f"Invalid dependency {name}: expected exact version or {{path, version}}")
    result = dict(spec)
    if "path" in result and (not isinstance(result["path"], str) or not result["path"]):
        raise ValueError(f"Dependency {name} has an invalid path")
    if "version" in result:
        _version(result["version"])
    if "path" not in result and "version" not in result:
        raise ValueError(f"Dependency {name} needs path or exact version")
    return result


def validate_abi(config, abi):
    """Validate precompiled bitcode metadata; source-only packages need no ABI."""
    library = config.get("library", {})
    if not isinstance(library, dict):
        raise ValueError("[library] must be a table")
    bitcode = library.get("bitcode", [])
    if not isinstance(bitcode, list) or any(not isinstance(item, str) or not item for item in bitcode):
        raise ValueError("[library].bitcode must be an array of paths")
    if not bitcode:
        return
    expected = config.get("toolchain", {})
    if not isinstance(abi, dict):
        raise ValueError("Precompiled bitcode requires an explicit toolchain ABI (llvm_major, target, scrpp_abi)")
    if not isinstance(expected, dict):
        raise ValueError("[toolchain] must declare the precompiled package ABI")
    for key in ("llvm_major", "target", "scrpp_abi"):
        value = expected.get(key)
        valid = type(value) is int and value > 0 if key == "llvm_major" else isinstance(value, str) and bool(value)
        if not valid or key not in abi or type(value) is not type(abi[key]) or value != abi[key]:
            raise ValueError(f"Precompiled ABI mismatch for {key}: package={value!r}, consumer={abi.get(key)!r}")


def _manifest_path(path):
    path = Path(path)
    return path / "sCrpp.toml" if path.is_dir() else path


def _path_fields(config, manifest):
    library = config.get("library", {})
    if not isinstance(library, dict):
        raise ValueError("[library] must be a table")
    for field in ("sources", "include_dirs", "bitcode"):
        values = library.get(field, [])
        if not isinstance(values, list) or any(not isinstance(v, str) or not v for v in values):
            raise ValueError(f"library.{field} must be an array of paths")
        for value in values:
            yield manifest.parent / value.replace("\\", "/"), field == "include_dirs"
    link = config.get("link", {})
    if not isinstance(link, dict) or not isinstance(link.get("resources", []), list):
        raise ValueError("link.resources must be an array of paths")
    for value in link.get("resources", []):
        if not isinstance(value, str) or not value:
            raise ValueError("link.resources must contain nonempty paths")
        yield manifest.parent / value.replace("\\", "/"), False
    for field in ("costumes", "lists"):
        entries = config.get(field, [])
        if not isinstance(entries, list):
            raise ValueError(f"{field} must be an array of tables")
        for entry in entries:
            if not isinstance(entry, dict) or not isinstance(entry.get("file"), str) or not entry["file"]:
                raise ValueError(f"Every {field} entry needs a file")
            value = entry["file"].replace("\\", "/")
            base = manifest.parent / "resources" if "/" not in value else manifest.parent
            yield base / value, False


def _validate_tree(manifest, root, registry_package=True, visited=None):
    visited = set() if visited is None else visited
    manifest = Path(manifest).resolve()
    if manifest in visited:
        return
    visited.add(manifest)
    config = _load(manifest)
    for name, value in config.get("dependencies", {}).items():
        if registry_package and "path" in _dependency(name, value):
            raise ValueError(f"Registry package cannot contain path dependency: {name}")
    root = root.resolve()
    for path, directory in _path_fields(config, manifest):
        # Reject an escape before filesystem probing (notably Windows UNC/SMB).
        if not Path(os.path.abspath(path)).is_relative_to(root):
            raise ValueError(f"Package path escapes its root: {path}")
        resolved = path.resolve()
        if not resolved.is_relative_to(root):
            raise ValueError(f"Package path escapes its root: {path}")
        if not (resolved.is_dir() if directory else resolved.is_file()):
            raise ValueError(f"Missing package {'directory' if directory else 'file'}: {path}")
    for value in config.get("link", {}).get("resources", []):
        _validate_tree(_manifest_path(manifest.parent / value), root, registry_package, visited)


def _safe_name(name):
    if not isinstance(name, str) or not name or name.startswith("/") or "\\" in name:
        raise ValueError(f"Unsafe ZIP path: {name!r}")
    parts = name.rstrip("/").split("/")
    for part in parts:
        if (not part or part in (".", "..") or part[-1:] in (".", " ") or
                any(ord(c) < 32 or c in ':<>"|?*' for c in part) or RESERVED_RE.fullmatch(part)):
            raise ValueError(f"Unsafe ZIP path component: {name!r}")
    return "/".join(parts)


def _ignored(relative):
    return (any(part in IGNORED or part.startswith("cmake-build-") for part in relative.parts)
            or relative.name in IGNORED_FILES or relative.suffix in (".pyc", ".pyo"))


def _unsafe_link(path):
    if path.is_symlink():
        return True
    # Python 3.11 has no Path.is_junction(); Windows reparse attributes cover
    # junctions as well as other directory links without following their target.
    attributes = getattr(path.lstat(), "st_file_attributes", 0)
    return bool(attributes & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400))


def _tree_files(root, *, filtered=True, exclude=()):
    root = Path(root).resolve()
    files = {}
    total = 0
    seen, spellings = set(), {}
    for directory, dirs, names in os.walk(root, followlinks=False):
        base = Path(directory)
        dirs[:] = sorted(d for d in dirs if not (filtered and _ignored((base / d).relative_to(root)))
                         and not any((base / d).resolve().is_relative_to(p) for p in exclude))
        for name in dirs + sorted(names):
            path = base / name
            relative = path.relative_to(root)
            if filtered and _ignored(relative):
                continue
            if _unsafe_link(path) or not path.resolve().is_relative_to(root):
                raise ValueError(f"Package symlinks/junctions are not allowed: {path}")
            if path.is_dir():
                continue
            if not stat.S_ISREG(path.stat().st_mode):
                raise ValueError(f"Package special files are not allowed: {path}")
            safe = _safe_name(relative.as_posix())
            _check_case_path(safe, spellings)
            folded = unicodedata.normalize("NFC", safe).casefold()
            if folded in seen:
                raise ValueError(f"Case-colliding package paths: {safe}")
            seen.add(folded)
            size = path.stat().st_size
            total += size
            if size > MAX_FILE_BYTES or total > MAX_TOTAL_BYTES or len(files) >= MAX_FILES:
                raise ValueError("Package exceeds file-count or uncompressed-size limit")
            files[safe] = path.read_bytes()
    return files


def _check_case_path(name, spellings):
    parts = name.split("/")
    for count in range(1, len(parts) + 1):
        prefix = "/".join(parts[:count])
        folded = unicodedata.normalize("NFC", prefix).casefold()
        if folded in spellings and spellings[folded] != prefix:
            raise ValueError(f"Case-colliding package paths: {spellings[folded]} and {prefix}")
        spellings[folded] = prefix


def _local_digest(manifest, config):
    root = manifest.parent.resolve()
    files = _tree_files(root)
    parts = [(name, _sha(data)) for name, data in sorted(files.items())]
    # Explicit paths outside local packages are permitted and must also be
    # included in their fingerprint, without storing machine-absolute paths.
    for path, directory in _path_fields(config, manifest):
        resolved = path.resolve()
        if not resolved.exists():
            raise ValueError(f"Missing local package path: {path}")
        if resolved.is_relative_to(root):
            continue
        relative = Path(os.path.relpath(resolved, root)).as_posix()
        if directory:
            parts.extend((relative + "/" + name, _sha(data)) for name, data in sorted(_tree_files(resolved).items()))
        else:
            parts.append((relative, _sha(resolved.read_bytes())))
    return _sha(_json_bytes(parts))


def _archive_files(data):
    if len(data) > MAX_ARCHIVE_BYTES:
        raise ValueError("Package archive exceeds size limit")
    files, entries, spellings, total = {}, {}, {}, 0
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        if len(archive.infolist()) > MAX_FILES:
            raise ValueError("ZIP exceeds entry-count limit")
        for info in archive.infolist():
            # ZipInfo normalizes OS separators and truncates NULs. Validate the
            # original central-directory spelling before using its normalized one.
            _safe_name(info.orig_filename)
            name = _safe_name(info.filename)
            _check_case_path(name, spellings)
            key = unicodedata.normalize("NFC", name).casefold()
            if key in entries:
                raise ValueError(f"Duplicate or case-colliding ZIP path: {name}")
            mode = (info.external_attr >> 16) & 0o170000
            if mode not in (0, stat.S_IFREG, stat.S_IFDIR):
                raise ValueError(f"ZIP symlink/special file is forbidden: {name}")
            if info.flag_bits & 1:
                raise ValueError("Encrypted package ZIP is unsupported")
            entries[key] = info.is_dir()
            total += info.file_size
            if (info.file_size > MAX_FILE_BYTES or total > MAX_TOTAL_BYTES or
                    info.file_size > max(1024 * 1024, info.compress_size * MAX_RATIO)):
                raise ValueError("ZIP expansion limit exceeded")
            if not info.is_dir():
                content = archive.read(info)
                if len(content) != info.file_size:
                    raise ValueError(f"ZIP entry size mismatch: {name}")
                files[name] = content
    for key in entries:
        prefix = key.split("/")[:-1]
        for length in range(1, len(prefix) + 1):
            if entries.get("/".join(prefix[:length])) is False:
                raise ValueError(f"ZIP file/directory collision: {key}")
    if "sCrpp.toml" not in files:
        raise ValueError("Package ZIP must contain sCrpp.toml at its root")
    return files


def _registry(value):
    value = str(value or DEFAULT_REGISTRY)
    parsed = urlparse(value)
    if parsed.scheme in ("https", "http"):
        if not parsed.netloc or parsed.username or parsed.password or parsed.query or parsed.fragment:
            raise ValueError("Invalid registry URL")
        return value.rstrip("/") + "/"
    if parsed.scheme == "file":
        return value.rstrip("/") + "/"
    if len(parsed.scheme) > 1:
        raise ValueError(f"Unsupported registry scheme: {parsed.scheme}")
    return Path(value).resolve().as_uri().rstrip("/") + "/"


def _read_url(url, offline, limit):
    parsed = urlparse(url)
    if parsed.scheme not in ("file", "http", "https") or parsed.username or parsed.password:
        raise ValueError(f"Unsupported package URL: {url}")
    if offline and parsed.scheme != "file":
        raise ValueError(f"Offline mode: package is not cached ({url})")
    with urlopen(url, timeout=30) as response:
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ValueError(f"Download exceeds size limit: {url}")
    return data


def _metadata(value, name, version, registry):
    if (not isinstance(value, dict) or value.get("schemaVersion") != 1 or value.get("name") != name or
            value.get("version") != version or not isinstance(value.get("url"), str) or
            not isinstance(value.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", value["sha256"])):
        raise ValueError(f"Invalid registry metadata for {name}@{version}")
    result = {"name": name, "version": version, "url": urljoin(registry, value["url"]), "sha256": value["sha256"]}
    if urlparse(result["url"]).scheme not in ("http", "https", "file"):
        raise ValueError("Unsupported archive URL in registry metadata")
    if urlparse(registry).scheme in ("http", "https") and urlparse(result["url"]).scheme == "file":
        raise ValueError("A remote registry cannot reference a local file URL")
    return result


def _cached_package(name, version, registry, cache, offline, old_record):
    receipt = cache / "indexes" / _sha(registry.encode()) / name / (version + ".json")
    def check_parents(path):
        for parent in path.parents:
            if parent == cache:
                break
            if parent.exists() and (_unsafe_link(parent) or not parent.resolve().is_relative_to(cache)):
                raise ValueError(f"Unsafe package cache directory: {parent}")
    check_parents(receipt)
    known = json.loads(receipt.read_text(encoding="utf-8")) if receipt.is_file() else None
    source = old_record.get("source", {}) if old_record else {}
    if (old_record and old_record.get("version") == version and source.get("kind") == "registry"
            and source.get("registry") == registry):
        metadata = _metadata({"schemaVersion": 1, "name": name, "version": version,
                              "url": source.get("url"), "sha256": old_record.get("sha256")}, name, version, registry)
    elif offline and known:
        metadata = _metadata({"schemaVersion": 1, **known}, name, version, registry)
    else:
        value = json.loads(_read_url(urljoin(registry, f"index/{name}/{version}.json"), offline, 1024 * 1024))
        metadata = _metadata(value, name, version, registry)
    if known and known.get("sha256") != metadata["sha256"]:
        raise ValueError(f"Immutable registry version changed: {name}@{version}")
    digest = metadata["sha256"]
    archive = cache / "archives" / (digest + ".zip")
    check_parents(archive)
    if archive.exists() and _unsafe_link(archive):
        raise ValueError(f"Unsafe package cache archive: {archive}")
    data = archive.read_bytes() if archive.is_file() else b""
    if _sha(data) != digest:
        data = _read_url(metadata["url"], offline, MAX_ARCHIVE_BYTES)
        if _sha(data) != digest:
            raise ValueError(f"SHA256 mismatch for {name}@{version}")
        _atomic_write(archive, data)
    files = _archive_files(data)
    extracted = cache / "packages" / digest
    check_parents(extracted)
    intact = extracted.is_dir() and not _unsafe_link(extracted)
    if intact:
        try:
            actual = _tree_files(extracted, filtered=False)
            intact = actual == files
        except (OSError, ValueError):
            intact = False
    if not intact:
        extracted.parent.mkdir(parents=True, exist_ok=True)
        temporary = Path(tempfile.mkdtemp(prefix=digest + ".", dir=extracted.parent))
        try:
            for relative, content in files.items():
                destination = temporary / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(content)
            if extracted.exists() and _unsafe_link(extracted):
                if extracted.is_symlink():
                    extracted.unlink()
                else:
                    os.rmdir(extracted)  # Remove the junction itself, never its target.
            elif extracted.exists():
                if not extracted.resolve().is_relative_to(cache.resolve()):
                    raise ValueError("Unsafe extracted cache path")
                shutil.rmtree(extracted)
            os.replace(temporary, extracted)
        finally:
            if temporary.exists():
                shutil.rmtree(temporary)
    manifest = extracted / "sCrpp.toml"
    config = _load(manifest)
    if config["package"].get("name") != name or config["package"].get("version") != version:
        raise ValueError(f"Archive package identity mismatch for {name}@{version}")
    _validate_tree(manifest, extracted)
    _atomic_write(receipt, _json_bytes(metadata))
    return manifest, {"kind": "registry", "registry": registry, "url": metadata["url"]}, digest


def resolve(manifest: Path, *, registry=None, cache_dir=None, offline=False, locked=False, abi=None) -> list[Path]:
    """Resolve dependency manifests in topological order, excluding the root."""
    manifest = _manifest_path(manifest).resolve()
    root = manifest.parent
    config = _load(manifest)
    registry_config = config.get("registry", {})
    if not isinstance(registry_config, dict):
        raise ValueError("[registry] must be a table")
    registry = _registry(registry if registry is not None else
                         os.environ.get("SCRATE_REGISTRY") or registry_config.get("url"))
    cache = Path(cache_dir or os.environ.get("SCRATE_CACHE_DIR") or os.environ.get("SCRATE_CACHE") or
                 Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache")) / "scrate").resolve()
    lock_path = root / "scrate.lock"
    old = json.loads(lock_path.read_text(encoding="utf-8")) if lock_path.is_file() else None
    if old is not None and (not isinstance(old, dict) or old.get("schemaVersion") != 1 or not isinstance(old.get("packages"), list)):
        raise ValueError(f"Unsupported or malformed lockfile: {lock_path}")
    if locked and old is None:
        raise ValueError("--locked requires an existing scrate.lock")
    old_packages = {}
    for item in old["packages"] if old else []:
        if (not isinstance(item, dict) or not isinstance(item.get("source"), dict) or
                item["source"].get("kind") not in ("path", "registry") or
                not isinstance(item.get("sha256"), str) or not re.fullmatch(r"[0-9a-f]{64}", item["sha256"])):
            raise ValueError("Malformed package entry in scrate.lock")
        name = _name(item.get("name"))
        if name in old_packages:
            raise ValueError(f"Duplicate lockfile package: {name}")
        old_packages[name] = item
    validate_abi(config, abi)
    result, records, seen, active = [], [], {}, [config["package"]["name"]]

    def visit(name, spec, declaring):
        declaration = _dependency(name, spec)
        if name in active:
            raise ValueError("Dependency cycle: " + " -> ".join(active + [name]))
        if "path" in declaration:
            path = _manifest_path(declaring.parent / declaration["path"]).resolve()
            child = _load(path)
            version = child["package"].get("version")
            if child["package"]["name"] != name or ("version" in declaration and version != declaration["version"]):
                raise ValueError(f"Local dependency identity/version mismatch: {name} at {path}")
            source = {"kind": "path", "path": Path(os.path.relpath(path, root)).as_posix()}
            digest = _local_digest(path, child)
        else:
            version = declaration["version"]
            path, source, digest = _cached_package(name, version, registry, cache, offline, old_packages.get(name))
            child = _load(path)
        identity = (version, source)
        if name in seen:
            if seen[name] != identity:
                raise ValueError(f"Dependency version/source conflict for {name}: {seen[name]} versus {identity}")
            return
        seen[name] = identity
        validate_abi(child, abi)
        active.append(name)
        for dependency in sorted(child.get("dependencies", {})):
            visit(dependency, child["dependencies"][dependency], path)
        active.pop()
        result.append(path)
        records.append({"name": name, "version": version, "source": source, "sha256": digest,
                        "dependencies": sorted(child.get("dependencies", {}))})

    for name in sorted(config.get("dependencies", {})):
        visit(name, config["dependencies"][name], manifest)
    lock = {"schemaVersion": 1, "root": {"manifestSha256": _sha(manifest.read_bytes()),
            "dependencies": sorted(config.get("dependencies", {}))}, "packages": records}
    if locked and old != lock:
        raise ValueError("scrate.lock is stale: dependency graph, manifest or local source changed (--locked)")
    if old != lock:
        _atomic_write(lock_path, _json_bytes(lock))
    return result


def _toml(config):
    def key(value):
        return json.dumps(value, ensure_ascii=False)

    def scalar(value):
        if isinstance(value, str): return json.dumps(value, ensure_ascii=False)
        if type(value) is bool: return "true" if value else "false"
        if type(value) in (int, float): return repr(value)
        if isinstance(value, list): return "[" + ", ".join(scalar(v) for v in value) + "]"
        if isinstance(value, dict): return "{ " + ", ".join(key(k) + " = " + scalar(v) for k, v in sorted(value.items())) + " }"
        raise ValueError(f"Unsupported TOML value in package manifest: {type(value).__name__}")

    lines = []
    def table(value, prefix=()):
        for name, item in sorted(value.items()):
            if not isinstance(item, dict):
                lines.append(key(name) + " = " + scalar(item))
        for name, item in sorted(value.items()):
            if isinstance(item, dict):
                path = prefix + (name,)
                lines.extend(["", "[" + ".".join(map(key, path)) + "]"])
                table(item, path)
    table(config)
    return ("\n".join(lines) + "\n").encode("utf-8")


def pack(manifest: Path, output_dir: Path) -> dict:
    """Create packages/...zip and index/...json beneath a static registry root."""
    manifest = _manifest_path(manifest).resolve()
    config = _load(manifest)
    name = _name(config["package"]["name"])
    version = _version(config["package"].get("version"))
    validate_abi(config, config.get("toolchain"))
    for dependency, spec in list(config.get("dependencies", {}).items()):
        declaration = _dependency(dependency, spec)
        if "path" in declaration:
            if "version" not in declaration:
                raise ValueError(f"Cannot pack unversioned local dependency: {dependency}")
            local = _load(_manifest_path(manifest.parent / declaration["path"]).resolve())
            if local["package"]["name"] != dependency or local["package"].get("version") != declaration["version"]:
                raise ValueError(f"Local dependency identity/version mismatch: {dependency}")
        config["dependencies"][dependency] = declaration["version"]
    output = Path(output_dir).resolve()
    if output == manifest.parent:
        raise ValueError("Registry output directory must differ from the package root")
    files = _tree_files(manifest.parent, exclude=(output,))
    if manifest.name != "sCrpp.toml":
        files.pop(manifest.name, None)
    files["sCrpp.toml"] = _toml(config)
    # Validate the actual distributable tree (after rewriting path dependencies).
    with tempfile.TemporaryDirectory(prefix="scrate-pack-") as temp:
        temporary = Path(temp)
        for relative, content in files.items():
            target = temporary / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
        _validate_tree(temporary / "sCrpp.toml", temporary)
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for relative, content in sorted(files.items()):
            info = zipfile.ZipInfo(relative, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.create_system = 3
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            archive.writestr(info, content)
    data = buffer.getvalue()
    _archive_files(data)
    relative = f"packages/{name}/{version}/{name}-{version}.zip"
    metadata = {"schemaVersion": 1, "name": name, "version": version, "url": relative, "sha256": _sha(data)}
    _immutable_write(output / relative, data)
    _immutable_write(output / "index" / name / (version + ".json"), _json_bytes(metadata))
    return metadata


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", action="version", version="scrate " + VERSION)
    commands = parser.add_subparsers(dest="command", required=True)
    for command in ("install", "list", "pack"):
        sub = commands.add_parser(command)
        sub.add_argument("--manifest", type=Path, default=Path("sCrpp.toml"))
        if command == "pack":
            sub.add_argument("--output-dir", type=Path, default=Path("dist"))
        else:
            sub.add_argument("--registry")
            sub.add_argument("--cache-dir", type=Path)
            sub.add_argument("--offline", action="store_true")
            sub.add_argument("--locked", action="store_true")
            sub.add_argument("--llvm-major", type=int)
            sub.add_argument("--target")
            sub.add_argument("--scrpp-abi")
    for command in ("build", "run", "debug"):
        sub = commands.add_parser(command)
        sub.add_argument("--manifest", type=Path, default=Path("sCrpp.toml"))
        sub.add_argument("--registry")
        sub.add_argument("--cache-dir", type=Path)
        sub.add_argument("--offline", action="store_true")
        sub.add_argument("--locked", action="store_true")
        if command != "debug":
            mode = sub.add_mutually_exclusive_group()
            mode.add_argument("--debug", action="store_true")
            mode.add_argument("--release", action="store_true")
        if command != "build":
            sub.add_argument("--no-build", action="store_true")
            sub.add_argument("--dap", action="store_true")
            sub.add_argument("--no-open", action="store_true")
            sub.add_argument("--node")
            sub.add_argument("--debugger")
            sub.add_argument("--connect-timeout", type=int)
            if command == "run":
                sub.add_argument("--turbowarp")
            else:
                sub.add_argument("--lldb-dap")
                sub.add_argument("--lldb-python")
                sub.add_argument("--command", action="append", default=[], dest="debug_commands")
    args = parser.parse_args(argv)
    try:
        if args.command == "build":
            from scrate_build import build_project
            build_project(args.manifest, debug=args.debug, offline=args.offline, locked=args.locked,
                          registry=args.registry, cache_dir=args.cache_dir)
        elif args.command in ("run", "debug"):
            from scrate_run import launch_project
            return launch_project(args.manifest, debug=args.command == "debug",
                                  debug_build=getattr(args, "debug", False),
                                  offline=args.offline, locked=args.locked, registry=args.registry,
                                  cache_dir=args.cache_dir, no_build=args.no_build, dap=args.dap,
                                  no_open=args.no_open, node=args.node, debugger=args.debugger,
                                  connect_timeout=args.connect_timeout,
                                  turbowarp=getattr(args, "turbowarp", None),
                                  lldb_dap=getattr(args, "lldb_dap", None),
                                  lldb_python=getattr(args, "lldb_python", None),
                                  commands=getattr(args, "debug_commands", ()))
        elif args.command == "pack":
            print(json.dumps(pack(args.manifest, args.output_dir), ensure_ascii=False, indent=2))
        elif args.command == "list":
            lock = json.loads((_manifest_path(args.manifest).resolve().parent / "scrate.lock").read_text(encoding="utf-8"))
            if not isinstance(lock, dict) or lock.get("schemaVersion") != 1 or not isinstance(lock.get("packages"), list):
                raise ValueError("Invalid scrate.lock")
            for item in lock["packages"]:
                if not isinstance(item, dict) or not isinstance(item.get("source"), dict):
                    raise ValueError("Invalid scrate.lock package entry")
                print(f"{_name(item.get('name'))}@{item.get('version') or 'local'} "
                      f"{item['source'].get('path') or item['source'].get('url', '')}")
        else:
            abi = ({"llvm_major": args.llvm_major, "target": args.target, "scrpp_abi": args.scrpp_abi}
                   if any((args.llvm_major, args.target, args.scrpp_abi)) else None)
            paths = resolve(args.manifest, registry=args.registry, cache_dir=args.cache_dir,
                            offline=args.offline, locked=args.locked, abi=abi)
            for path in paths:
                config = _load(path)["package"]
                print(f"{config['name']}@{config.get('version', 'local')} {path}")
        return 0
    except subprocess.CalledProcessError as exc:
        print(f"scrate: command failed (exit {exc.returncode}): {exc.cmd[0]}", file=sys.stderr)
        return exc.returncode if exc.returncode > 0 else 1
    except KeyboardInterrupt:
        print("scrate: interrupted", file=sys.stderr)
        return 130
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile) as exc:
        print(f"scrate: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
