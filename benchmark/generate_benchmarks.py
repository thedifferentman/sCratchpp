"""Build two vanilla-Scratch benchmarks. This script never runs the projects."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import struct
import zipfile


ROOT = Path(__file__).resolve().parent
INITIAL_SEED = 270_544_960  # 0x10203040
INITIAL_STEP = 65_537
WRITE_SEED = 305_419_896  # 0x12345678
DEFAULT_N = 50_000
DEFAULT_PASSES = 200
DEFAULT_SAMPLES = 5
MAX_ITERATIONS = 100_000_000
MAX_PASSES = 100_000
CHECKSUM_MODULUS = 2**32


class Expr:
    def __init__(self, opcode, inputs=None, fields=None, boolean=False):
        self.opcode = opcode
        self.inputs = inputs or {}
        self.fields = fields or {}
        self.boolean = boolean


class Statement:
    def __init__(self, opcode, inputs=None, fields=None, branches=None, mutation=None):
        self.opcode = opcode
        self.inputs = inputs or {}
        self.fields = fields or {}
        self.branches = branches or {}
        self.mutation = mutation


NAMES = {
    "config_n": "元素数 N",
    "config_passes": "每样本遍历次数",
    "config_samples": "样本数",
    "status": "状态",
}


def variable_field(key):
    return [NAMES.get(key, key), "var_" + key]


def v(key):
    return Expr("data_variable", fields={"VARIABLE": variable_field(key)})


def arg(name):
    return Expr("argument_reporter_string_number", fields={"VALUE": [name, None]})


def binary(op, a, b):
    if op in ("add", "subtract", "multiply", "divide", "mod"):
        return Expr("operator_" + op, {"NUM1": a, "NUM2": b})
    return Expr("operator_" + op, {"OPERAND1": a, "OPERAND2": b}, boolean=True)


def add(a, b): return binary("add", a, b)
def sub(a, b): return binary("subtract", a, b)
def mul(a, b): return binary("multiply", a, b)
def div(a, b): return binary("divide", a, b)
def mod(a, b): return binary("mod", a, b)
def eq(a, b): return binary("equals", a, b)
def lt(a, b): return binary("lt", a, b)
def gt(a, b): return binary("gt", a, b)
def either(a, b): return binary("or", a, b)


def mathop(op, value):
    return Expr("operator_mathop", {"NUM": value}, {"OPERATOR": [op, None]})


def floor(value): return mathop("floor", value)
def ceiling(value): return mathop("ceiling", value)
def wall_days(): return Expr("sensing_dayssince2000")


def join(*values):
    result = values[-1]
    for value in reversed(values[:-1]):
        result = Expr("operator_join", {"STRING1": value, "STRING2": result})
    return result


def listfield(key):
    return {"LIST": [{"memory": "内存（计时期间隐藏）", "results": "结果（右键可导出）", "times": "排序后的样本时间"}[key], "list_" + key]}


def item(key, index):
    return Expr("data_itemoflist", {"INDEX": index}, listfield(key))


def length(key): return Expr("data_lengthoflist", fields=listfield(key))
def append(key, value): return Statement("data_addtolist", {"ITEM": value}, listfield(key))
def clear(key): return Statement("data_deletealloflist", fields=listfield(key))
def replace(key, index, value): return Statement("data_replaceitemoflist", {"INDEX": index, "ITEM": value}, listfield(key))
def insert(key, index, value): return Statement("data_insertatlist", {"INDEX": index, "ITEM": value}, listfield(key))
def setv(key, value): return Statement("data_setvariableto", {"VALUE": value}, {"VARIABLE": variable_field(key)})
def change(key, value): return Statement("data_changevariableby", {"VALUE": value}, {"VARIABLE": variable_field(key)})
def repeat(count, body): return Statement("control_repeat", {"TIMES": count}, branches={"SUBSTACK": body})
def until(condition, body): return Statement("control_repeat_until", {"CONDITION": condition}, branches={"SUBSTACK": body})


def iff(condition, body, otherwise=None):
    branches = {"SUBSTACK": body}
    if otherwise is not None:
        branches["SUBSTACK2"] = otherwise
    return Statement("control_if_else" if otherwise is not None else "control_if", {"CONDITION": condition}, branches=branches)


PROCEDURES = {
    "main": [], "initialize": [], "read": ["sweeps"],
    "write": ["sweeps"], "rmw": ["sweeps"],
    "kernel": ["mode", "sweeps"], "verify": ["mode", "sweeps"],
    "case": ["mode"], "report": ["row"],
}


def signature(name):
    return "bench " + name + " %s" * len(PROCEDURES[name])


def argument_ids(name):
    return [f"argument_{name}_{i}" for i in range(len(PROCEDURES[name]))]


def mutation(name, prototype=False):
    result = {"tagName": "mutation", "children": [], "proccode": signature(name),
              "argumentids": json.dumps(argument_ids(name)), "warp": "true"}
    if prototype:
        result.update(argumentnames=json.dumps(PROCEDURES[name]),
                      argumentdefaults=json.dumps([""] * len(PROCEDURES[name])))
    return result


def call(name, *values):
    assert len(values) == len(PROCEDURES[name])
    return Statement("procedures_call", dict(zip(argument_ids(name), values)), mutation=mutation(name))


class Builder:
    def __init__(self):
        self.blocks = {}
        self.counter = 0

    def block(self, opcode, parent=None, shadow=False, top=False):
        self.counter += 1
        key = f"block_{self.counter:05d}"
        block = {"opcode": opcode, "next": None, "parent": parent, "inputs": {},
                 "fields": {}, "shadow": shadow, "topLevel": top}
        self.blocks[key] = block
        return key, block

    def input(self, value, parent):
        if isinstance(value, Expr):
            key, block = self.block(value.opcode, parent)
            block["fields"] = value.fields
            block["inputs"] = {name: self.input(child, key) for name, child in value.inputs.items()}
            return [2, key] if value.boolean else [3, key, [4, ""]]
        return [1, [4 if isinstance(value, (int, float)) else 10, str(value)]]

    def sequence(self, statements, parent):
        first = None
        previous = None
        for statement in statements:
            key, block = self.block(statement.opcode, previous or parent)
            if previous is not None:
                self.blocks[previous]["next"] = key
            if first is None:
                first = key
            block["fields"] = statement.fields
            block["inputs"] = {name: self.input(value, key) for name, value in statement.inputs.items()}
            if statement.mutation is not None:
                block["mutation"] = statement.mutation
            for branch, contents in statement.branches.items():
                start = self.sequence(contents, key)
                if start is not None:
                    block["inputs"][branch] = [2, start]
            previous = key
        return first

    def procedure(self, name, body, index):
        definition, block = self.block("procedures_definition", top=True)
        block.update(x=420 * (index % 3), y=220 * (index // 3) + 220)
        prototype, proto = self.block("procedures_prototype", definition, shadow=True)
        block["inputs"]["custom_block"] = [1, prototype]
        proto["mutation"] = mutation(name, True)
        for key, argname in zip(argument_ids(name), PROCEDURES[name]):
            reporter, argument = self.block("argument_reporter_string_number", prototype, shadow=True)
            argument["fields"] = {"VALUE": [argname, None]}
            proto["inputs"][key] = [1, reporter]
        block["next"] = self.sequence(body, definition)

    def green_flag(self):
        key, block = self.block("event_whenflagclicked", top=True)
        block.update(x=0, y=0)
        block["next"] = self.sequence([call("main")], key)


def build_project(layout):
    stride = 1 if layout == "compact" else 4

    def load():
        if stride == 1:
            return item("memory", v("p"))
        # Horner form, using ordinary Scratch arithmetic only.
        return add(mul(add(mul(add(mul(item("memory", add(v("p"), 3)), 256),
                                      item("memory", add(v("p"), 2))), 256),
                              item("memory", add(v("p"), 1))), 256),
                   item("memory", v("p")))

    def bytes_of(value):
        return [mod(value, 256), mod(floor(div(value, 256)), 256),
                mod(floor(div(value, 65536)), 256), floor(div(value, 16777216))]

    def store(value, initializing=False):
        values = [value] if stride == 1 else bytes_of(value)
        if initializing:
            return [append("memory", b) for b in values]
        return [replace("memory", v("p") if offset == 0 else add(v("p"), offset), b)
                for offset, b in enumerate(values)]

    initialize = [clear("memory"), setv("i", 0), repeat(v("n"), [
        setv("x", add(INITIAL_SEED, mul(v("i"), INITIAL_STEP))),
        *store(v("x"), initializing=True), change("i", 1)])]

    def kernel_body(kind):
        prefix = [setv("read_sum", 0)] if kind == "read" else [setv("value", WRITE_SEED)] if kind == "write" else []
        if kind == "read":
            body = [setv("x", load()), change("read_sum", v("x"))]
        elif kind == "write":
            body = [*store(v("value")), change("value", 1)]
        else:
            body = [setv("x", load()), change("x", 1), *store(v("x"))]
        # Reduce once per sweep, not once per load, to keep larger runs exact.
        sweep_suffix = [setv("read_sum", mod(v("read_sum"), CHECKSUM_MODULUS))] if kind == "read" else []
        return [*prefix, setv("started_days", wall_days()), repeat(arg("sweeps"), [
            setv("p", 1), repeat(v("n"), [*body, change("p", stride)]), *sweep_suffix
        ]), setv("elapsed", mul(sub(wall_days(), v("started_days")), 86400))]

    kernel = [iff(eq(arg("mode"), 1), [call("read", arg("sweeps"))], [
        iff(eq(arg("mode"), 2), [call("write", arg("sweeps"))], [call("rmw", arg("sweeps"))])])]

    verify = [iff(eq(arg("mode"), 1), [
        setv("checksum", v("read_sum")),
        setv("expected", mod(mul(mod(v("initial_sum"), CHECKSUM_MODULUS), arg("sweeps")), CHECKSUM_MODULUS))
    ], [
        setv("checksum", 0), setv("p", 1),
        repeat(v("n"), [setv("x", load()), change("checksum", v("x")), change("p", stride)]),
        iff(eq(arg("mode"), 2), [setv("expected", add(
            mul(v("n"), sub(add(WRITE_SEED, mul(v("n"), arg("sweeps"))), v("n"))),
            div(mul(v("n"), sub(v("n"), 1)), 2)))],
            [setv("expected", add(v("initial_sum"), mul(v("n"), arg("sweeps"))))])
    ]), iff(eq(v("checksum"), v("expected")), [setv("ok", 1)],
            [setv("ok", 0), setv("all_ok", 0), setv("case_ok", 0)])]

    report = [append("results", join(layout, ",", v("label"), ",", arg("row"), ",",
                                      v("iterations"), ",", v("elapsed"), ",", v("checksum"),
                                      ",", v("expected"), ",", v("ok")))]

    case = [
        setv("case_ok", 1),
        iff(eq(arg("mode"), 1), [setv("label", "read")],
            [iff(eq(arg("mode"), 2), [setv("label", "write")], [setv("label", "read_modify_write")])]),
        setv("status", join("预热 ", v("label"))),
        call("initialize"), call("kernel", arg("mode"), v("warm_sweeps")),
        call("verify", arg("mode"), v("warm_sweeps")),
        iff(eq(v("ok"), 0), [append("results", join("WARMUP_FAILED,", v("label")))]),
        clear("times"), setv("trial", 1),
        repeat(v("samples"), [
            setv("status", join(v("label"), " ", v("trial"), "/", v("samples"))),
            call("initialize"), call("kernel", arg("mode"), v("passes")),
            call("verify", arg("mode"), v("passes")), call("report", v("trial")),
            iff(lt(v("elapsed"), 0.1), [setv("timing_ok", 0),
                append("results", join("TIMING_TOO_SHORT,", v("label"), ",sample=", v("trial"), ",seconds=", v("elapsed")))]),
            setv("position", 1),
            until(either(gt(v("position"), length("times")), lt(v("elapsed"), item("times", v("position")))),
                  [change("position", 1)]),
            insert("times", v("position"), v("elapsed")), change("trial", 1)
        ]),
        setv("middle", div(add(v("samples"), 1), 2)),
        setv("elapsed", div(add(item("times", floor(v("middle"))), item("times", ceiling(v("middle")))), 2)),
        setv("ok", v("case_ok")), call("report", "median")
    ]

    main = [
        *[Statement("data_hidelist", fields=listfield(key)) for key in ("memory", "times", "results")],
        clear("results"), clear("times"), setv("all_ok", 1), setv("timing_ok", 1), setv("status", "准备"),
        setv("n", floor(v("config_n"))),
        iff(lt(v("n"), 1), [setv("n", 1)]), iff(gt(v("n"), 50000), [setv("n", 50000)]),
        setv("passes", floor(v("config_passes"))), iff(lt(v("passes"), 1), [setv("passes", 1)]),
        iff(gt(v("passes"), MAX_PASSES), [setv("passes", MAX_PASSES)]),
        iff(gt(v("passes"), floor(div(MAX_ITERATIONS, v("n")))), [setv("passes", floor(div(MAX_ITERATIONS, v("n"))))]),
        setv("samples", floor(v("config_samples"))),
        iff(lt(v("samples"), 1), [setv("samples", 1)]), iff(gt(v("samples"), 9), [setv("samples", 9)]),
        setv("config_n", v("n")), setv("config_passes", v("passes")), setv("config_samples", v("samples")),
        setv("iterations", mul(v("n"), v("passes"))),
        setv("initial_sum", add(mul(v("n"), INITIAL_SEED), div(mul(mul(INITIAL_STEP, v("n")), sub(v("n"), 1)), 2))),
        setv("warm_sweeps", v("passes")), iff(gt(v("warm_sweeps"), 5), [setv("warm_sweeps", 5)]),
        append("results", join("CONFIG,", layout, ",version=2,clock=dayssince2000,N=", v("n"), ",passes=", v("passes"), ",samples=", v("samples"))),
        append("results", "layout,case,sample,iterations,seconds,checksum,expected,ok"),
        call("case", 1), call("case", 2), call("case", 3),
        iff(eq(v("all_ok"), 1), [iff(eq(v("timing_ok"), 1),
            [setv("status", "完成：校验通过")], [setv("status", "完成：样本过短，请增加遍历次数")])],
            [setv("status", "完成：校验失败")]),
        append("results", join("DONE,all_checks_passed=", v("all_ok"), ",timing_at_least_0.1s=", v("timing_ok"))),
        Statement("data_showlist", fields=listfield("results"))
    ]

    bodies = {"main": main, "initialize": initialize, "read": kernel_body("read"),
              "write": kernel_body("write"), "rmw": kernel_body("rmw"),
              "kernel": kernel, "verify": verify, "case": case, "report": report}
    builder = Builder()
    builder.green_flag()
    for index, name in enumerate(PROCEDURES):
        builder.procedure(name, bodies[name], index)

    variables = {}
    for block in builder.blocks.values():
        if "VARIABLE" in block["fields"]:
            name, key = block["fields"]["VARIABLE"]
            variables[key] = [name, 0]
    for key, value in [("config_n", DEFAULT_N), ("config_passes", DEFAULT_PASSES),
                       ("config_samples", DEFAULT_SAMPLES), ("status", "点击绿旗开始")]:
        variables["var_" + key][1] = value

    title = "COMPACT / U32" if stride == 1 else "BYTE MEMORY / U32 LE"
    backdrop = f'''<svg xmlns="http://www.w3.org/2000/svg" width="480" height="360" viewBox="0 0 480 360">
<rect width="480" height="360" fill="#f2f5fa"/>
<text x="12" y="25" font-family="sans-serif" font-size="15" font-weight="bold" fill="#26334b">{title}</text>
<text x="12" y="253" font-family="sans-serif" font-size="11" fill="#526078">Green flag: run all 3 tests</text>
<text x="12" y="272" font-family="sans-serif" font-size="11" fill="#526078">Compare median seconds.</text>
<text x="12" y="291" font-family="sans-serif" font-size="11" fill="#526078">Initialization is not timed.</text>
<text x="12" y="310" font-family="sans-serif" font-size="11" fill="#526078">Right-click results to export.</text>
<text x="12" y="338" font-family="sans-serif" font-size="10" fill="#526078">v2 clock: days since 2000</text>
</svg>'''.encode("utf-8")
    transparent = b'<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><rect width="1" height="1" fill="none"/></svg>'

    assets = {}

    def costume(name, data, cx, cy):
        digest = hashlib.md5(data).hexdigest()
        assets[digest + ".svg"] = data
        return {"name": name, "assetId": digest, "dataFormat": "svg", "md5ext": digest + ".svg",
                "bitmapResolution": 1, "rotationCenterX": cx, "rotationCenterY": cy}

    stage = {"isStage": True, "name": "Stage", "variables": variables,
             "lists": {"list_" + key: [listfield(key)["LIST"][0], []] for key in ("memory", "results", "times")},
             "broadcasts": {}, "blocks": {}, "comments": {}, "currentCostume": 0,
             "costumes": [costume("Benchmark", backdrop, 240, 180)], "sounds": [], "volume": 100,
             "layerOrder": 0, "tempo": 60, "videoTransparency": 50, "videoState": "off", "textToSpeechLanguage": None}
    sprite = {"isStage": False, "name": "Benchmark", "variables": {}, "lists": {}, "broadcasts": {},
              "blocks": builder.blocks, "comments": {}, "currentCostume": 0,
              "costumes": [costume("Invisible", transparent, 0, 0)], "sounds": [], "volume": 100,
              "layerOrder": 1, "visible": False, "x": 0, "y": 0, "size": 100,
              "direction": 90, "draggable": False, "rotationStyle": "all around"}

    monitors = []
    for key, y, maximum in [("config_n", 43, 50000), ("config_passes", 97, MAX_PASSES), ("config_samples", 151, 9)]:
        monitors.append({"id": "var_" + key, "mode": "slider", "opcode": "data_variable",
                         "params": {"VARIABLE": NAMES[key]}, "spriteName": None,
                         "value": variables["var_" + key][1], "width": 0, "height": 0,
                         "x": 10, "y": y, "visible": True, "sliderMin": 1, "sliderMax": maximum, "isDiscrete": True})
    monitors.append({"id": "var_status", "mode": "default", "opcode": "data_variable",
                     "params": {"VARIABLE": NAMES["status"]}, "spriteName": None, "value": "点击绿旗开始",
                     "width": 0, "height": 0, "x": 10, "y": 211, "visible": True,
                     "sliderMin": 0, "sliderMax": 100, "isDiscrete": True})
    monitors.append({"id": "list_results", "mode": "list", "opcode": "data_listcontents",
                     "params": {"LIST": listfield("results")["LIST"][0]}, "spriteName": None,
                     "value": [], "width": 274, "height": 340, "x": 200, "y": 10, "visible": True})
    project = {"targets": [stage, sprite], "monitors": monitors, "extensions": [],
               "meta": {"semver": "3.0.0", "vm": "0.2.0", "agent": "u32-memory-layout-benchmark-v2"}}
    return project, assets


def validate(project, assets):
    """Check references and packaging only; do not execute Scratch code."""
    blocks = project["targets"][1]["blocks"]
    variables = project["targets"][0]["variables"]
    lists = project["targets"][0]["lists"]
    incoming = {key: 0 for key in blocks}
    definitions = {}
    for key, block in blocks.items():
        assert not block["topLevel"] or block["parent"] is None
        references = []
        if block["next"]:
            references.append(block["next"])
        for descriptor in block["inputs"].values():
            for entry in descriptor[1:]:
                if isinstance(entry, str):
                    references.append(entry)
        for reference in references:
            assert reference in blocks, (key, reference)
            assert blocks[reference]["parent"] == key, (key, reference, "wrong parent")
            incoming[reference] += 1
        if "VARIABLE" in block["fields"]:
            assert block["fields"]["VARIABLE"][1] in variables
        if "LIST" in block["fields"]:
            assert block["fields"]["LIST"][1] in lists
        if block["opcode"] == "procedures_prototype":
            mut = block["mutation"]
            assert mut["warp"] == "true"
            assert set(json.loads(mut["argumentids"])) == set(block["inputs"])
            assert mut["proccode"] not in definitions
            definitions[mut["proccode"]] = mut
    for key, block in blocks.items():
        assert incoming[key] == (0 if block["topLevel"] else 1), (key, incoming[key])
        if block["opcode"] == "procedures_call":
            mut = block["mutation"]
            assert mut["proccode"] in definitions
            assert mut["argumentids"] == definitions[mut["proccode"]]["argumentids"]
            assert set(json.loads(mut["argumentids"])) == set(block["inputs"])
    assert sum(b["opcode"] == "event_whenflagclicked" for b in blocks.values()) == 1
    assert not any(b["opcode"].startswith("event_") and b["opcode"] != "event_whenflagclicked" for b in blocks.values())
    assert set(definitions) == {signature(name) for name in PROCEDURES}
    assert not any(b["opcode"] == "sensing_timer" for b in blocks.values())
    assert sum(b["opcode"] == "sensing_dayssince2000" for b in blocks.values()) == 6
    for target in project["targets"]:
        for c in target["costumes"]:
            assert hashlib.md5(assets[c["md5ext"]]).hexdigest() == c["assetId"]


def validate_arithmetic():
    # Independent byte-layout sanity checks, not timing or executing either sb3.
    for x in (0, 1, 255, 256, 65535, 65536, INITIAL_SEED, WRITE_SEED, 2**31, 2**32 - 1):
        encoded = [x % 256, math.floor(x / 256) % 256,
                   math.floor(x / 65536) % 256, math.floor(x / 16777216)]
        assert bytes(encoded) == struct.pack("<I", x)
        assert sum(b * 256**i for i, b in enumerate(encoded)) == x
    for n, passes in ((1, MAX_PASSES), (3, 2), (DEFAULT_N, DEFAULT_PASSES),
                      (50000, MAX_ITERATIONS // 50000), (1000, MAX_PASSES)):
        initial_sum = n * INITIAL_SEED + INITIAL_STEP * n * (n - 1) // 2
        assert initial_sum == sum(INITIAL_SEED + i * INITIAL_STEP for i in range(n))
        final_write_sum = n * (WRITE_SEED + n * passes - n) + n * (n - 1) // 2
        assert final_write_sum == sum(range(WRITE_SEED + n * passes - n, WRITE_SEED + n * passes))
        assert initial_sum + CHECKSUM_MODULUS - 1 < 2**53
        assert (initial_sum % CHECKSUM_MODULUS) * passes < 2**53
        assert final_write_sum < 2**53
        assert WRITE_SEED + n * passes < 2**32
        assert INITIAL_SEED + INITIAL_STEP * (n - 1) + passes < 2**32
        # Only evaluate closed-form arithmetic. No benchmark loops are executed.
        assert int(float(initial_sum % CHECKSUM_MODULUS) * passes) % CHECKSUM_MODULUS == (initial_sum * passes) % CHECKSUM_MODULUS
    # Convert a millisecond-scale wall-clock difference without scaling the epoch first.
    start_ms, duration_ms = 842_000_000_000, 1234
    elapsed = ((start_ms + duration_ms) / 86_400_000 - start_ms / 86_400_000) * 86400
    assert abs(elapsed - duration_ms / 1000) < 0.000001


def main():
    validate_arithmetic()
    for layout, filename in (("compact", "compact-u32.sb3"), ("bytes", "bytes-u32.sb3")):
        project, assets = build_project(layout)
        validate(project, assets)
        payload = json.dumps(project, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        with zipfile.ZipFile(ROOT / filename, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in [("project.json", payload), *sorted(assets.items())]:
                info = zipfile.ZipInfo(name, date_time=(2026, 9, 12, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, data)
        # Readable source for inspecting/editing the emitted blocks.
        (ROOT / filename.replace(".sb3", ".project.json")).write_text(
            json.dumps(project, ensure_ascii=False, indent=2), encoding="utf-8")
        with zipfile.ZipFile(ROOT / filename) as archive:
            assert archive.testzip() is None
            validate(json.loads(archive.read("project.json")), {name: archive.read(name) for name in assets})
        print(f"Built and statically validated {filename}: {len(project['targets'][1]['blocks'])} blocks; NOT executed")


if __name__ == "__main__":
    main()
