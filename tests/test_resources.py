"""Resource preparation checks; no image libraries or browser are required."""
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import xml.etree.ElementTree as ET
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("build_resources", ROOT / "tools/build_resources.py")
resources = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(resources)


def png(width=2, height=1):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress((b"\0" + b"\xff\0\0\xff" * width) * height)) + chunk(b"IEND", b""))


class ResourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="scrpp-resources-")
        self.root = Path(self.temp.name)
        (self.root / "resources").mkdir()
        self.manifest = self.root / "sCrpp.toml"

    def tearDown(self):
        self.temp.cleanup()

    def config(self, tail="", name="demo"):
        self.manifest.write_text(f'version = 1\n[package]\nname = "{name}"\n{tail}', encoding="utf-8")

    def build(self):
        result = resources.prepare_resources(self.manifest, self.root / "output")
        return json.loads(result.read_text(encoding="utf-8"))

    def asset(self, item):
        return (self.root / "output" / item["path"]).read_bytes()

    def test_package_identity_and_resource_namespace_are_independent(self):
        (self.root / "resources/data.txt").write_text("42\n", encoding="utf-8")
        self.config('version="0.1.0"\nresource_namespace="__scl_demo"\n'
                    '[[lists]]\nfile="data.txt"\n', name="demo")
        self.assertEqual(resources.load_manifest(self.manifest)["package"]["name"], "demo")
        self.assertEqual(resources.read_package_name(self.manifest), "__scl_demo")
        data = self.build()
        self.assertEqual(data["package"], "__scl_demo")
        self.assertEqual(data["lists"][0]["name"], "__scl_demo::data")

    def test_png_preserves_pixels_and_default_dimensions(self):
        original = png()
        (self.root / "resources" / "image.dark.png").write_bytes(original)
        self.config('[[costumes]]\nfile = "image.dark.png"\n')
        data = self.build()
        item = data["costumes"][0]
        self.assertEqual(item["name"], "demo::image.dark")
        self.assertEqual((item["rotationCenterX"], item["rotationCenterY"]), (1, 0.5))
        self.assertEqual(item["dataFormat"], "svg")
        self.assertEqual(item["bitmapResolution"], 1)
        text = self.asset(item)
        root = ET.fromstring(text)
        self.assertEqual((root.get("width"), root.get("height")), ("2", "1"))
        image = root.find("{http://www.w3.org/2000/svg}image")
        encoded = image.get("{http://www.w3.org/1999/xlink}href").split(",", 1)[1]
        self.assertEqual(base64.b64decode(encoded), original)
        self.assertEqual(item["assetId"], hashlib.md5(text).hexdigest())
        self.assertNotIn(str(self.root), json.dumps(data))

    def test_custom_size_center_name_and_paths(self):
        (self.root / "images").mkdir()
        (self.root / "images" / "a.png").write_bytes(png())
        self.config('[[costumes]]\nfile = "images/a.png"\nname = "待机😀"\nsize = [480, 270]\ncenter = [-2.5, 99]\n', "角色包")
        item = self.build()["costumes"][0]
        self.assertEqual(item["name"], "角色包::待机😀")
        self.assertEqual((item["rotationCenterX"], item["rotationCenterY"]), (-2.5, 99))
        self.assertEqual(ET.fromstring(self.asset(item)).get("viewBox"), "0 0 480 270")
        self.assertEqual(resources.read_package_name(self.manifest), "角色包")

    def test_identical_content_deduplicates_but_keeps_centers(self):
        (self.root / "resources" / "a.png").write_bytes(png())
        self.config('[[costumes]]\nfile = "a.png"\nname = "a"\n[[costumes]]\nfile = "a.png"\nname = "b"\ncenter = [0, 0]\n')
        items = self.build()["costumes"]
        self.assertEqual(items[0]["assetId"], items[1]["assetId"])
        self.assertNotEqual(items[0]["rotationCenterX"], items[1]["rotationCenterX"])
        self.assertEqual(len(list((self.root / "output/assets").glob("*.svg"))), 1)

    def test_deterministic(self):
        (self.root / "resources" / "a.png").write_bytes(png())
        self.config('[[costumes]]\nfile = "a.png"\n')
        first = self.build()
        second = self.build()
        self.assertEqual(first, second)

    def test_svg_viewbox_and_size(self):
        (self.root / "resources/a.svg").write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="-10 -20 100 200"><rect width="100" height="200"/></svg>')
        self.config('[[costumes]]\nfile = "a.svg"\nsize = [25, 50]\n')
        item = self.build()["costumes"][0]
        root = ET.fromstring(self.asset(item))
        self.assertEqual(root.get("viewBox"), "0 0 25 50")
        self.assertEqual(root.get("width"), "25")
        inner = root.find("{http://www.w3.org/2000/svg}svg")
        self.assertEqual(inner.get("viewBox"), "-10 -20 100 200")
        self.assertEqual((inner.get("width"), inner.get("height")), ("25", "50"))
        self.assertEqual((item["rotationCenterX"], item["rotationCenterY"]), (12.5, 25))

    def test_svg_outer_canvas_matches_logical_size_and_preserves_source_attributes(self):
        source = (b'<svg xmlns="http://www.w3.org/2000/svg" width="80" height="40" '
                  b'viewBox="10 20 20 10" preserveAspectRatio="xMinYMin slice" '
                  b'style="fill:red" transform="translate(1 2)"><rect x="10" y="20" '
                  b'width="20" height="10"/></svg>')
        data, size = resources._svg(source, None)
        root = ET.fromstring(data)
        inner = root.find("{http://www.w3.org/2000/svg}svg")
        self.assertEqual(size, (80, 40))
        self.assertEqual(root.get("viewBox"), "0 0 80 40")
        self.assertEqual(inner.get("viewBox"), "10 20 20 10")
        self.assertEqual(inner.get("preserveAspectRatio"), "xMinYMin slice")
        self.assertEqual(inner.get("style"), "fill:red")
        self.assertEqual(inner.get("transform"), "translate(1 2)")
        self.assertEqual(len(inner), 1)

    def test_svg_absolute_units_and_inferred_dimension(self):
        data, size = resources._svg(b'<svg width="1in" height="72pt"/>', None)
        self.assertEqual(size, (96, 96))
        self.assertEqual(ET.fromstring(data).get("viewBox"), "0 0 96 96")
        _, size = resources._svg(b'<svg width="50" viewBox="0 0 100 200"/>', None)
        self.assertEqual(size, (50, 100))

    def test_local_svg_references(self):
        source = b'<svg width="2" height="2"><defs><linearGradient id="a"/></defs><rect fill="url(#a)"/></svg>'
        resources._svg(source, None)

    def test_reject_external_or_executable_svg(self):
        bad = [b'<image href="https://example.com/a.png"/>', b'<script/>',
               b'<foreignObject/>', b'<rect onclick="run()"/>',
               b'<style>@import "remote.css"</style>', b'<rect fill="url(https://example.com/a)"/>']
        for body in bad:
            with self.subTest(body=body), self.assertRaises(ValueError):
                resources._svg(b'<svg width="1" height="1">' + body + b'</svg>', None)
        with self.assertRaises(ValueError):
            resources._svg(b'<!DOCTYPE svg [<!ENTITY x "x">]><svg width="1" height="1"/>', None)

    def test_invalid_svg_geometry(self):
        for attrs in (b'width="100%" height="1"', b'viewBox="0 0 0 1"',
                      b'width="NaN" height="1"', b'', b'viewBox="0 0 inf 1"'):
            with self.subTest(attrs=attrs), self.assertRaises(ValueError):
                resources._svg(b'<svg ' + attrs + b'/>', None)

    def test_invalid_png(self):
        good = png()
        for bad in (b'not png', good[:-1], good[:20] + b'X' + good[21:], good + b'junk'):
            with self.subTest(bad=bad[:20]), self.assertRaises(ValueError):
                resources._png_size(bad)

    def test_duplicate_and_invalid_names(self):
        (self.root / "resources/a.png").write_bytes(png())
        self.config('[[costumes]]\nfile = "a.png"\n[[costumes]]\nfile = "a.png"\n')
        with self.assertRaisesRegex(ValueError, "Duplicate costume"):
            self.build()
        self.config('', 'bad::package')
        with self.assertRaises(ValueError):
            self.build()
        self.config('[[costumes]]\nfile = "a.png"\nname="bad::name"\n')
        with self.assertRaises(ValueError):
            self.build()

    def test_empty_pack(self):
        self.config()
        self.assertEqual(self.build()["costumes"], [])

    def test_list_preserves_font_rows_and_only_splits_lf(self):
        original = "\ufeff first \r\n\n\u2028\u2029\u0085\v\f\r inside \nlast\r"
        (self.root / "resources/font.txt").write_bytes(original.encode("utf-8"))
        self.config('[[lists]]\nfile="font.txt"\nreadonly=true\n')
        data = self.build()
        self.assertEqual(data["lists"], [{"name": "demo::font", "readonly": True,
            "items": [" first ", "", "\u2028\u2029\u0085\v\f\r inside ", "last\r"]}])
        self.assertNotIn(str(self.root), json.dumps(data))
        (self.root / "resources/font.txt").unlink()
        prepared = json.loads((self.root / "output/resources.json").read_text(encoding="utf-8"))
        self.assertEqual(prepared, data)

    def test_list_empty_and_final_line_endings(self):
        for text, expected in [("", []), ("\n", [""]), ("a\n", ["a"]),
                               ("a\n\n", ["a", ""]), ("a\r\n", ["a"]),
                               ("a\r\r\n", ["a\r"]), ("a\r", ["a\r"])]:
            with self.subTest(text=text):
                (self.root / "resources/items.txt").write_bytes(text.encode("utf-8"))
                self.config('[[lists]]\nfile="items.txt"\n')
                self.assertEqual(self.build()["lists"][0],
                                 {"name": "demo::items", "items": expected, "readonly": False})

    def test_list_explicit_path_and_name(self):
        (self.root / "tables").mkdir()
        (self.root / "tables/data.TXT").write_text("123\n中文😀", encoding="utf-8")
        self.config('[[lists]]\nfile="tables/data.TXT"\nname="字库"\n')
        self.assertEqual(self.build()["lists"][0],
                         {"name": "demo::字库", "items": ["123", "中文😀"], "readonly": False})

    def test_invalid_list_configuration(self):
        (self.root / "resources/items.txt").write_text("ok", encoding="utf-8")
        for tail in ('[[lists]]\nfile="items.txt"\nreadonly=1',
                     '[[lists]]\nfile="items.txt"\nunknown=true',
                     '[[lists]]\nfile="items.png"', '[[lists]]\nname="missing"',
                     '[[lists]]\nfile="items.txt"\n[[lists]]\nfile="items.txt"'):
            with self.subTest(tail=tail), self.assertRaises(ValueError):
                self.config(tail)
                self.build()
        for raw in (b"\xff", b"before\x08after"):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                (self.root / "resources/items.txt").write_bytes(raw)
                self.config('[[lists]]\nfile="items.txt"\n')
                self.build()

    def test_list_capacity(self):
        source = self.root / "resources/items.txt"
        self.config('[[lists]]\nfile="items.txt"\n')
        source.write_bytes(b"\n" * 200000)
        self.assertEqual(len(self.build()["lists"][0]["items"]), 200000)
        source.write_bytes(b"\n" * 200001)
        with self.assertRaisesRegex(ValueError, "200000"):
            self.build()

    def test_wheel_event_configuration(self):
        self.config('[[events]]\ntype="wheel"\nqueue="wheel"\nenabled="__scl_console_enabled"\n')
        self.assertEqual(self.build()["events"], [{"type": "wheel", "queue": "demo::wheel",
                         "enabled": "__scl_console_enabled", "capacity": 128}])
        self.config('[[events]]\ntype="keyboard"\nqueue="keys"\nenabled="enabled"\ncapacity=64\n')
        self.assertEqual(self.build()["events"], [{"type": "keyboard", "queue": "demo::keys",
                         "enabled": "enabled", "capacity": 64}])
        for tail in ('type="key"\nqueue="wheel"\nenabled="enabled"',
                     'type="wheel"\nqueue="wheel"\nenabled="enabled"\ncapacity=0',
                     'type="wheel"\nqueue="wheel"\nenabled="enabled"\ncapacity=true',
                     'type="wheel"\nqueue="wheel"',
                     'type="wheel"\nqueue="wheel"\nenabled="enabled"\ncallback="main"'):
            with self.subTest(tail=tail), self.assertRaises(ValueError):
                self.config('[[events]]\n' + tail)
                self.build()
        (self.root / "resources/wheel.txt").write_text("", encoding="utf-8")
        self.config('[[lists]]\nfile="wheel.txt"\nreadonly=true\n'
                    '[[events]]\ntype="wheel"\nqueue="wheel"\nenabled="enabled"\n')
        with self.assertRaisesRegex(ValueError, "readonly"):
            self.build()

    def test_invalid_config(self):
        for tail in ('[[costumes]]\nfile="a.png"\nsize=[0,1]',
                     '[[costumes]]\nfile="a.png"\nsize=[true,1]',
                     '[[costumes]]\nfile="a.png"\ncenter=[nan,1]',
                     '[[costumes]]\nfile="a.png"\nszie=[1,1]',
                     '[[costumes]]\nname="missingfile"'):
            self.config(tail)
            with self.subTest(tail=tail), self.assertRaises(ValueError):
                self.build()
        self.manifest.write_text('[package]\nname="demo"\n', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "version"):
            self.build()

    def test_link_graph_and_cycles(self):
        child = self.root / "shared"
        child.mkdir()
        (child / "sCrpp.toml").write_text('version=1\n[package]\nname="shared"\n', encoding="utf-8")
        self.config('[link]\nresources=["shared/sCrpp.toml", "shared/sCrpp.toml"]\n')
        self.assertEqual(resources.collect_manifests(self.manifest), [self.manifest, child / "sCrpp.toml"])
        (child / "sCrpp.toml").write_text('version=1\n[package]\nname="shared"\n[link]\nresources=["../sCrpp.toml"]\n', encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "cycle"):
            resources.collect_manifests(self.manifest)

    def test_duplicate_package_names(self):
        (self.root / "other.toml").write_text('version=1\n[package]\nname="demo"\n', encoding="utf-8")
        self.config('[link]\nresources=["other.toml"]\n')
        with self.assertRaisesRegex(ValueError, "Duplicate resource package"):
            resources.collect_manifests(self.manifest)


if __name__ == "__main__":
    unittest.main()
