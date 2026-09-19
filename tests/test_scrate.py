"""scrate protocol/cache/security tests; only local directories and localhost HTTP."""
from __future__ import annotations

from contextlib import contextmanager
import hashlib
import http.server
import io
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import tomllib
import unittest
from unittest import mock
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import scrate


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_args):
        pass


@contextmanager
def registry_server(root):
    from functools import partial
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), partial(QuietHandler, directory=str(root)))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        yield f"http://127.0.0.1:{server.server_port}/"
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


class ScrateTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="scrate-tests-")
        self.root = Path(self.temp.name) / "workspace with spaces 字"
        self.root.mkdir()
        self.registry = self.root / "registry"
        self.cache = self.root / "cache"

    def tearDown(self):
        self.temp.cleanup()

    def package(self, name, *, version="0.1.0", dependencies=None, location=None, extra=""):
        folder = self.root / (location or name)
        folder.mkdir(parents=True, exist_ok=True)
        content = f'version = 1\n[package]\nname = "{name}"\n'
        if version is not None:
            content += f'version = "{version}"\n'
        content += '[library]\nsources = ["main.cpp"]\ninclude_dirs = ["."]\n'
        if dependencies:
            content += "[dependencies]\n"
            for dep, value in dependencies.items():
                rendered = (json.dumps(value) if isinstance(value, str) else
                            "{ " + ", ".join(k + " = " + json.dumps(v) for k, v in value.items()) + " }")
                content += json.dumps(dep) + " = " + rendered + "\n"
        content += extra
        manifest = folder / "sCrpp.toml"
        manifest.write_text(content, encoding="utf-8")
        (folder / "main.cpp").write_text("int value = 1;\n", encoding="utf-8")
        return manifest

    def resolve(self, manifest, **kwargs):
        return scrate.resolve(manifest, registry=kwargs.pop("registry", self.registry), cache_dir=self.cache, **kwargs)

    def publish(self, name, **kwargs):
        source = self.package(name, **kwargs)
        return scrate.pack(source, self.registry)

    def malformed(self, name, entries, *, metadata_url=None):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for filename, content in entries:
                archive.writestr(filename, content)
        blob = data.getvalue()
        relative = f"packages/{name}/0.1.0/{name}-0.1.0.zip"
        archive_path = self.registry / relative
        archive_path.parent.mkdir(parents=True, exist_ok=True)
        archive_path.write_bytes(blob)
        index = self.registry / "index" / name / "0.1.0.json"
        index.parent.mkdir(parents=True, exist_ok=True)
        index.write_text(json.dumps({"schemaVersion": 1, "name": name, "version": "0.1.0",
            "url": metadata_url or relative, "sha256": hashlib.sha256(blob).hexdigest()}), encoding="utf-8")
        return index

    def test_default_registry_and_no_dependencies_make_no_network_request(self):
        self.assertEqual(scrate.DEFAULT_REGISTRY, "https://scrate.shapy.cn")
        app = self.package("app", version=None)
        with mock.patch.object(scrate, "urlopen", side_effect=AssertionError("unexpected network")):
            self.assertEqual(scrate.resolve(app, cache_dir=self.cache), [])
        self.assertTrue((app.parent / "scrate.lock").is_file())

    def test_deterministic_pack_rewrite_and_filters(self):
        dep = self.package("dep")
        app = self.package("app", dependencies={"dep": {"path": "../dep", "version": "0.1.0"}})
        for name in ["build/temp.bin", ".git/config", "__pycache__/x.pyc", ".scrate/cache"]:
            item = app.parent / name
            item.parent.mkdir(parents=True, exist_ok=True)
            item.write_text("ignored")
        (app.parent / "scrate.lock").write_text("ignored")
        metadata = scrate.pack(app, self.registry)
        self.assertEqual(metadata, scrate.pack(app, self.registry))
        with zipfile.ZipFile(self.registry / metadata["url"]) as archive:
            self.assertEqual(archive.namelist(), ["main.cpp", "sCrpp.toml"])
            config = tomllib.loads(archive.read("sCrpp.toml").decode())
            self.assertEqual(config["dependencies"], {"dep": "0.1.0"})
        self.assertTrue(dep.is_file())
        (app.parent / "main.cpp").write_text("int value = 2;")
        with self.assertRaisesRegex(ValueError, "Immutable"):
            scrate.pack(app, self.registry)

    def test_output_inside_package_is_excluded(self):
        app = self.package("app")
        output = app.parent / "local-registry"
        self.assertEqual(scrate.pack(app, output), scrate.pack(app, output))

    def test_local_graph_locked_changes_and_relocation(self):
        dep = self.package("dep")
        app = self.package("app", dependencies={"dep": {"path": "../dep", "version": "0.1.0"}})
        self.assertEqual(self.resolve(app), [dep])
        lock = (app.parent / "scrate.lock").read_bytes()
        self.assertEqual(self.resolve(app, locked=True, offline=True), [dep])
        moved = self.root / "moved"
        shutil.copytree(app.parent, moved / "app")
        shutil.copytree(dep.parent, moved / "dep")
        self.assertEqual(self.resolve(moved / "app/sCrpp.toml", locked=True), [moved / "dep/sCrpp.toml"])
        (dep.parent / "main.cpp").write_text("int value = 2;")
        with self.assertRaisesRegex(ValueError, "stale"):
            self.resolve(app, locked=True)
        self.assertEqual((app.parent / "scrate.lock").read_bytes(), lock)
        self.resolve(app)
        self.assertNotEqual((app.parent / "scrate.lock").read_bytes(), lock)

    def test_local_identity_unversioned_pack_and_scripts_rejected(self):
        self.package("dep")
        app = self.package("app", dependencies={"dep": {"path": "../dep"}})
        self.assertEqual(len(self.resolve(app)), 1)
        with self.assertRaisesRegex(ValueError, "unversioned"):
            scrate.pack(app, self.registry)
        app = self.package("app", dependencies={"wrong": {"path": "../dep", "version": "0.1.0"}})
        with self.assertRaisesRegex(ValueError, "identity"):
            self.resolve(app)
        app = self.package("app", extra='[scripts]\ninstall = "do not execute"\n')
        with self.assertRaisesRegex(ValueError, "scripts"):
            scrate.pack(app, self.registry)

    def test_local_external_source_fingerprinted_but_cannot_pack(self):
        external = self.root / "external.cpp"
        external.write_text("int x = 1;")
        dep = self.package("dep")
        dep.write_text(dep.read_text().replace('sources = ["main.cpp"]', 'sources = ["../external.cpp"]'))
        app = self.package("app", dependencies={"dep": {"path": "../dep"}})
        self.resolve(app)
        external.write_text("int x = 2;")
        with self.assertRaisesRegex(ValueError, "stale"):
            self.resolve(app, locked=True)
        with self.assertRaisesRegex(ValueError, "escapes"):
            scrate.pack(dep, self.registry)

    def test_local_registry_topology_lock_url_and_offline(self):
        self.publish("base")
        self.publish("middle", dependencies={"base": "0.1.0"})
        app = self.package("app", dependencies={"middle": "0.1.0"})
        paths = self.resolve(app)
        self.assertEqual([scrate._load(p)["package"]["name"] for p in paths], ["base", "middle"])
        lock = json.loads((app.parent / "scrate.lock").read_text())
        self.assertEqual(lock["packages"][1]["dependencies"], ["base"])
        index = self.registry / "index/middle/0.1.0.json"
        changed = json.loads(index.read_text())
        changed["url"] = "does-not-exist.zip"
        index.write_text(json.dumps(changed))
        self.assertEqual(self.resolve(app, locked=True, offline=True), paths)

    def test_http_offline_and_corrupt_cache_repair(self):
        self.publish("dep")
        app = self.package("app", dependencies={"dep": "0.1.0"})
        with registry_server(self.registry) as url:
            paths = self.resolve(app, registry=url)
        lock = (app.parent / "scrate.lock").read_bytes()
        (paths[0].parent / "main.cpp").write_text("corrupted")
        extra = paths[0].parent / ".git/injected"
        extra.parent.mkdir()
        extra.write_text("must be detected")
        self.assertEqual(self.resolve(app, registry=url, offline=True, locked=True), paths)
        self.assertFalse(extra.exists())
        self.assertEqual((paths[0].parent / "main.cpp").read_text(), "int value = 1;\n")
        self.assertEqual((app.parent / "scrate.lock").read_bytes(), lock)
        next((self.cache / "archives").glob("*.zip")).write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "Offline"):
            self.resolve(app, registry=url, offline=True)

    def test_manifest_registry_and_cache_environment(self):
        self.publish("dep")
        app = self.package("app", dependencies={"dep": "0.1.0"},
                           extra='[registry]\nurl = ' + json.dumps(str(self.registry)) + '\n')
        with mock.patch.dict("os.environ", {"SCRATE_CACHE_DIR": str(self.cache)}):
            paths = scrate.resolve(app)
        self.assertTrue(paths[0].is_relative_to(self.cache))

    def test_registry_environment_and_explicit_override(self):
        self.publish("dep")
        app = self.package("app", dependencies={"dep": "0.1.0"},
                           extra='[registry]\nurl = "https://invalid.example"\n')
        with mock.patch.dict("os.environ", {"SCRATE_REGISTRY": str(self.registry)}):
            paths = scrate.resolve(app, cache_dir=self.cache, offline=True)
        self.assertEqual(len(paths), 1)
        with mock.patch.dict("os.environ", {"SCRATE_REGISTRY": "https://invalid.example"}):
            self.assertEqual(scrate.resolve(app, registry=self.registry, cache_dir=self.cache,
                                           offline=True, locked=True), paths)

    def test_remote_registry_cannot_point_at_local_file(self):
        app = self.package("app", dependencies={"dep": "0.1.0"})
        self.malformed("dep", [("sCrpp.toml", 'version=1\n[package]\nname="dep"\nversion="0.1.0"')],
                       metadata_url=app.as_uri())
        with registry_server(self.registry) as url:
            with self.assertRaisesRegex(ValueError, "local file"):
                self.resolve(app, registry=url)

    def test_registry_package_path_dependency_forbidden(self):
        app = self.package("app", dependencies={"dep": "0.1.0"})
        self.malformed("dep", [("sCrpp.toml", 'version=1\n[package]\nname="dep"\nversion="0.1.0"\n'
                         '[dependencies]\nother={path="../other",version="0.1.0"}\n')])
        with self.assertRaisesRegex(ValueError, "path dependency"):
            self.resolve(app)

    def test_registry_path_escape_forbidden(self):
        app = self.package("app", dependencies={"dep": "0.1.0"})
        self.malformed("dep", [("sCrpp.toml", 'version=1\n[package]\nname="dep"\nversion="0.1.0"\n'
                         '[library]\nsources=["../outside.cpp"]\n')])
        with self.assertRaisesRegex(ValueError, "escapes"):
            self.resolve(app)

    def test_seen_registry_version_cannot_change(self):
        self.publish("dep")
        first = self.package("first", dependencies={"dep": "0.1.0"})
        second = self.package("second", dependencies={"dep": "0.1.0"})
        self.resolve(first)
        self.malformed("dep", [("sCrpp.toml", 'version=1\n[package]\nname="dep"\nversion="0.1.0"\n')])
        with self.assertRaisesRegex(ValueError, "Immutable"):
            self.resolve(second)
        self.assertFalse((second.parent / "scrate.lock").exists())

    def test_cycles_conflicts_and_exact_versions(self):
        self.publish("aaa", dependencies={"bbb": "0.1.0"})
        self.publish("bbb", dependencies={"aaa": "0.1.0"})
        app = self.package("app", dependencies={"aaa": "0.1.0"})
        with self.assertRaisesRegex(ValueError, "cycle"):
            self.resolve(app)
        self.publish("common", location="common-one")
        self.publish("common", version="0.2.0", location="common-two")
        self.publish("left", dependencies={"common": "0.1.0"})
        self.publish("right", dependencies={"common": "0.2.0"})
        app = self.package("app", dependencies={"left": "0.1.0", "right": "0.1.0"})
        with self.assertRaisesRegex(ValueError, "conflict"):
            self.resolve(app)
        for invalid in ("*", "^1.2.3", "1.2", "01.2.3", "1.2.3-beta"):
            with self.subTest(invalid=invalid):
                with self.assertRaisesRegex(ValueError, "exact"):
                    self.resolve(self.package("app", dependencies={"dep": invalid}))

    def test_bitcode_abi_strict_and_source_only_unaffected(self):
        dep = self.package("dep", extra='[toolchain]\nllvm_major=22\ntarget="x86_64-unknown-linux-gnu"\nscrpp_abi="1"\n')
        dep.write_text(dep.read_text().replace('sources = ["main.cpp"]', 'bitcode = ["main.bc"]'))
        (dep.parent / "main.bc").write_bytes(b"not executed")
        scrate.pack(dep, self.registry)
        app = self.package("app", dependencies={"dep": "0.1.0"})
        with self.assertRaisesRegex(ValueError, "explicit toolchain ABI"):
            self.resolve(app)
        abi = {"llvm_major": 22, "target": "x86_64-unknown-linux-gnu", "scrpp_abi": "1"}
        self.assertEqual(len(self.resolve(app, abi=abi)), 1)
        for key, value in (("llvm_major", 23), ("target", "other"), ("scrpp_abi", 1)):
            with self.subTest(key=key):
                with self.assertRaisesRegex(ValueError, "ABI mismatch"):
                    self.resolve(app, abi={**abi, key: value})

    def test_zip_unsafe_paths_collisions_and_symlinks(self):
        manifest = 'version=1\n[package]\nname="dep"\nversion="0.1.0"\n'
        names = ['../escape', '/absolute', r'a\b', 'C:/escape', 'name:stream', 'CON.txt', 'LPT1',
                 'COM¹.txt', 'trailing.', 'trailing ', 'a//b', 'a/./b']
        for name in names:
            with self.subTest(name=name):
                buffer = io.BytesIO()
                with zipfile.ZipFile(buffer, 'w') as archive:
                    archive.writestr('sCrpp.toml', manifest)
                    info = zipfile.ZipInfo('placeholder')
                    info.filename = name  # Preserve raw backslashes even on Windows.
                    archive.writestr(info, 'bad')
                with self.assertRaises(ValueError):
                    scrate._archive_files(buffer.getvalue())
        for names in [('a', 'A'), ('Foo/a', 'foo/b'), ('a', 'a/b')]:
            with self.subTest(names=names):
                buffer = io.BytesIO()
                with zipfile.ZipFile(buffer, 'w') as archive:
                    archive.writestr('sCrpp.toml', manifest)
                    for name in names:
                        archive.writestr(name, 'bad')
                with self.assertRaises(ValueError):
                    scrate._archive_files(buffer.getvalue())
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w') as archive:
            archive.writestr('sCrpp.toml', manifest)
            info = zipfile.ZipInfo('link')
            info.create_system = 3
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            archive.writestr(info, '../outside')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            scrate._archive_files(buffer.getvalue())

    def test_zip_expansion_limits_and_archive_identity(self):
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            archive.writestr('sCrpp.toml', 'x' * 1024)
        with mock.patch.object(scrate, 'MAX_FILE_BYTES', 100):
            with self.assertRaisesRegex(ValueError, 'limit'):
                scrate._archive_files(buffer.getvalue())
        app = self.package("app", dependencies={"dep": "0.1.0"})
        self.malformed("dep", [("sCrpp.toml", 'version=1\n[package]\nname="wrong"\nversion="0.1.0"\n')])
        with self.assertRaisesRegex(ValueError, 'identity'):
            self.resolve(app)

    def test_names_locked_missing_and_readonly_list(self):
        for name in ['Upper', 'trailing.', 'con', '../escape']:
            with self.subTest(name=name), self.assertRaises(ValueError):
                scrate._name(name)
        app = self.package("app")
        with self.assertRaisesRegex(ValueError, 'existing'):
            self.resolve(app, locked=True)
        self.resolve(app)
        lock = app.parent / 'scrate.lock'
        before = lock.read_bytes()
        app.write_text(app.read_text() + '\n# should not update from list\n')
        with mock.patch.object(scrate, 'resolve', side_effect=AssertionError('list must be read-only')):
            self.assertEqual(scrate.main(['list', '--manifest', str(app)]), 0)
        self.assertEqual(lock.read_bytes(), before)

    def test_pack_rejects_directory_links(self):
        app = self.package('app')
        outside = self.root / 'outside'
        outside.mkdir()
        (outside / 'secret.txt').write_text('outside package')
        link = app.parent / 'linked'
        if os.name == 'nt':
            result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(outside)], capture_output=True)
            if result.returncode:
                self.skipTest('Cannot create a test junction on this filesystem')
        else:
            link.symlink_to(outside, target_is_directory=True)
        try:
            with self.assertRaisesRegex(ValueError, 'symlink|junction'):
                scrate.pack(app, self.registry)
            self.assertEqual((outside / 'secret.txt').read_text(), 'outside package')
        finally:
            if link.is_symlink():
                link.unlink()
            else:
                link.rmdir()


if __name__ == "__main__":
    unittest.main()
