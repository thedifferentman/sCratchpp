"""Generate two byte-memory arithmetic benchmarks without running either sb3."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import random
import zipfile

import generate_benchmarks as sb


ROOT = Path(__file__).resolve().parent
MAX_N = 8192  # 3 arrays * 8 bytes * 8192 elements = 196608 list entries.
DEFAULT_N = 4096
DEFAULT_PASSES = 2500
DEFAULT_SAMPLES = 5
MAX_PASSES = 100000
MAX_ITERATIONS = 100000000
MOD32 = 2**32
CASES = [(4, 'add'), (4, 'sub'), (4, 'mul'), (8, 'add'), (8, 'sub'), (8, 'mul')]
KERNEL_NAMES = [f'{op}{width * 8}' for width, op in CASES]
PROCEDURES = {
    'main': [], 'prepare': [], 'verify': [], 'diagnostics': [],
    'dispatch': ['sweeps'], 'suite': ['case'], 'report': ['row'],
    **{name: ['sweeps'] for name in KERNEL_NAMES},
}
# These are in-process builder settings, not changes to the other generator file.
sb.PROCEDURES = PROCEDURES
sb.NAMES.update(config_n='每组元素数 N', config_passes='每样本遍历次数', config_samples='样本数', status='状态')
v, arg = sb.v, sb.arg
add, sub, mul, div, mod = sb.add, sb.sub, sb.mul, sb.div, sb.mod
eq, lt, gt = sb.eq, sb.lt, sb.gt
floor, ceiling = sb.floor, sb.ceiling
setv, change, repeat, iff, until = sb.setv, sb.change, sb.repeat, sb.iff, sb.until
call, join = sb.call, sb.join


LIST_NAMES = {
    'memory': '字节内存', 'golden': '整数精确计算的参考结果',
    'results': '结果（右键可导出）', 'times': '样本时间',
}


def lf(name): return {'LIST': [LIST_NAMES[name], 'list_' + name]}
def item(name, index): return sb.Expr('data_itemoflist', {'INDEX': index}, lf(name))
def length(name): return sb.Expr('data_lengthoflist', fields=lf(name))
def append(name, value): return sb.Statement('data_addtolist', {'ITEM': value}, lf(name))
def clear(name): return sb.Statement('data_deletealloflist', fields=lf(name))
def replace(name, index, value): return sb.Statement('data_replaceitemoflist', {'INDEX': index, 'ITEM': value}, lf(name))
def insert(name, index, value): return sb.Statement('data_insertatlist', {'INDEX': index, 'ITEM': value}, lf(name))


def inputs_for(width, op, i):
    if op == 'mul':
        # Full products stay below 2**53; low 32-bit reduction is also exact.
        return 0x01020304 + i * 2053, 0x02030405 + i * 3079
    if width == 4:
        return (0xF1234567 + i * 65537) % MOD32, (0x2345FEDC + i * 104729) % MOD32
    # Both >2**32; addition is exact, subtraction stays nonnegative.
    return 2**49 + 0x01020304 + i * 65537, 2**48 + 0x89ABCDEF + i * 104729


def exact_result(a, b, width, op):
    result = {'add': lambda: a + b, 'sub': lambda: a - b, 'mul': lambda: a * b}[op]()
    return result % (256**width)


GOLDEN = [exact_result(*inputs_for(width, op, i), width, op)
          for width, op in CASES for i in range(MAX_N)]

EDGES = [
    (4, 'add', 2**32 - 1, 1, 'carry_out'),
    (4, 'sub', 0, 1, 'borrow_out'),
    (4, 'mul', 2**32 - 1, 2**32 - 1, 'full_product_precision'),
    (8, 'add', 2**53, 1, 'increment_above_53_bits'),
    (8, 'add', 2**64 - 1, 1, 'carry_out'),
    (8, 'sub', 0, 1, 'borrow_out'),
    (8, 'sub', 2**53 + 2, 1, 'subtract_above_53_bits'),
    (8, 'mul', 2**32 + 1, 2**32 + 1, 'low_bit_of_product'),
    (8, 'mul', 2**64 - 1, 2**64 - 1, 'full_product_precision'),
]


def at(offset): return v('p') if offset == 0 else add(v('p'), offset)


def decode(values):
    result = values[-1]
    for value in reversed(values[:-1]):
        result = add(mul(result, 256), value)
    return result


def arithmetic_operation(strategy, width, op):
    """One unrolled load-A/load-B/operate/store-R operation."""
    statements = []
    # Identical operand loading in both implementations: exactly 2*width reads.
    for j in range(width):
        statements.extend([setv(f'a{j}', item('memory', at(j))),
                           setv(f'b{j}', item('memory', at(width + j)))])
    if strategy == 'number':
        statements.extend([setv('av', decode([v(f'a{j}') for j in range(width)])),
                           setv('bv', decode([v(f'b{j}') for j in range(width)])),
                           setv('rv', mod({'add': add, 'sub': sub, 'mul': mul}[op](v('av'), v('bv')), 256**width))])
        for j in range(width):
            value = v('rv') if j == 0 else floor(div(v('rv'), 256**j))
            statements.append(replace('memory', at(2 * width + j), mod(value, 256)))
    else:
        statements.append(setv('carry', 0))
        for j in range(width):
            if op == 'add':
                value = add(add(v(f'a{j}'), v(f'b{j}')), v('carry'))
            elif op == 'sub':
                value = sub(sub(v(f'a{j}'), v(f'b{j}')), v('carry'))
            else:
                # Only columns 0..width-1 are required for modular multiplication.
                value = v('carry')
                for k in range(j + 1):
                    value = add(value, mul(v(f'a{k}'), v(f'b{j-k}')))
            statements.extend([setv('t', value), replace('memory', at(2 * width + j), mod(v('t'), 256))])
            if j + 1 < width:
                carry = floor(div(v('t'), 256))
                statements.append(setv('carry', sub(0, carry) if op == 'sub' else carry))
    return statements


def choose_case(bodies):
    tail = bodies[-1]
    for case_id in reversed(range(1, len(bodies))):
        tail = [iff(eq(v('case_id'), case_id), bodies[case_id - 1], tail)]
    return tail


def make_bodies(strategy):
    prepare_inputs = [iff(sb.either(eq(v('case_id'), 3), eq(v('case_id'), 6)), [
        setv('init_a', add(0x01020304, mul(v('i'), 2053))),
        setv('init_b', add(0x02030405, mul(v('i'), 3079))),
    ], [iff(lt(v('case_id'), 4), [
        setv('init_a', mod(add(0xF1234567, mul(v('i'), 65537)), MOD32)),
        setv('init_b', mod(add(0x2345FEDC, mul(v('i'), 104729)), MOD32)),
    ], [
        setv('init_a', add(2**49 + 0x01020304, mul(v('i'), 65537))),
        setv('init_b', add(2**48 + 0x89ABCDEF, mul(v('i'), 104729))),
    ])])]
    initialize_record = [*prepare_inputs]
    for source in ('init_a', 'init_b'):
        initialize_record.extend([setv('init_value', v(source)), repeat(v('width'), [
            append('memory', mod(v('init_value'), 256)),
            setv('init_value', floor(div(v('init_value'), 256))),
        ])])
    initialize_record.extend([repeat(v('width'), [append('memory', 0)]), change('i', 1)])
    prepare = [clear('memory'), setv('i', 0), repeat(v('n'), initialize_record)]

    kernels = {}
    for width, op in CASES:
        kernels[f'{op}{width*8}'] = [
            setv('started_days', sb.wall_days()),
            repeat(arg('sweeps'), [setv('p', 1), repeat(v('n'), [
                *arithmetic_operation(strategy, width, op), change('p', 3 * width),
            ])]),
            setv('elapsed', mul(sub(sb.wall_days(), v('started_days')), 86400)),
        ]
    dispatch = choose_case([[call(name, arg('sweeps'))] for name in KERNEL_NAMES])

    verify = [setv('errors', 0), setv('checksum', 0), setv('expected_sum', 0),
              setv('i', 0), setv('p', 1), repeat(v('n'), [
        iff(eq(v('width'), 4), [setv('decoded', decode([item('memory', at(8+j)) for j in range(4)]))],
            [setv('decoded', decode([item('memory', at(16+j)) for j in range(8)]))]),
        setv('expected', item('golden', add(add(mul(sub(v('case_id'), 1), MAX_N), v('i')), 1))),
        iff(eq(v('decoded'), v('expected')), [], [change('errors', 1)]),
        setv('checksum', mod(add(v('checksum'), v('decoded')), MOD32)),
        setv('expected_sum', mod(add(v('expected_sum'), v('expected')), MOD32)),
        change('p', mul(v('width'), 3)), change('i', 1),
    ]), iff(eq(v('errors'), 0), [setv('ok', 1)], [setv('ok', 0), setv('all_ok', 0), setv('case_ok', 0)])]

    report = [append('results', join(strategy, ',', v('label'), ',', arg('row'), ',', v('iterations'), ',',
        v('elapsed'), ',', v('checksum'), ',', v('expected_sum'), ',', v('errors'), ',', v('ok')))]

    suite = [setv('case_id', arg('case')), setv('case_ok', 1),
             *choose_case([[setv('label', name), setv('width', width)] for name, (width, _) in zip(KERNEL_NAMES, CASES)]),
        setv('status', join('预热 ', v('label'))), call('prepare'), call('dispatch', v('warm_sweeps')), call('verify'),
        iff(eq(v('ok'), 0), [append('results', join('WARMUP_FAILED,', strategy, ',', v('label')))]),
        clear('times'), setv('trial', 1), repeat(v('samples'), [
            setv('status', join(v('label'), ' ', v('trial'), '/', v('samples'))),
            call('prepare'), call('dispatch', v('passes')), call('verify'), call('report', v('trial')),
            iff(lt(v('elapsed'), 0.1), [setv('timing_ok', 0),
                append('results', join('TIMING_TOO_SHORT,', v('label'), ',sample=', v('trial'), ',seconds=', v('elapsed')))]),
            setv('position', 1),
            until(sb.either(gt(v('position'), length('times')), lt(v('elapsed'), item('times', v('position')))), [change('position', 1)]),
            insert('times', v('position'), v('elapsed')), change('trial', 1),
        ]), setv('middle', div(add(v('samples'), 1), 2)),
        setv('elapsed', div(add(item('times', floor(v('middle'))), item('times', ceiling(v('middle')))), 2)),
        setv('ok', v('case_ok')), call('report', 'median'),
    ]

    diagnostics = [setv('edge_passes', 0), append('results', 'EDGE,strategy,case,name,ok,actual_LE_bytes,expected_LE_bytes')]
    for width, op, a, b, label in EDGES:
        expected_bytes = list(exact_result(a, b, width, op).to_bytes(width, 'little'))
        diagnostics.append(clear('memory'))
        for byte in a.to_bytes(width, 'little') + b.to_bytes(width, 'little') + bytes(width):
            diagnostics.append(append('memory', byte))
        diagnostics.extend([setv('p', 1), *arithmetic_operation(strategy, width, op), setv('edge_ok', 1)])
        for j, expected in enumerate(expected_bytes):
            diagnostics.append(iff(eq(item('memory', 2 * width + j + 1), expected), [], [setv('edge_ok', 0)]))
        displayed = []
        for j in range(width):
            if j:
                displayed.append(' ')
            displayed.append(item('memory', 2 * width + j + 1))
        diagnostics.extend([change('edge_passes', v('edge_ok')), append('results', join(
            'EDGE,', strategy, ',', f'{op}{width*8}', ',', label, ',', v('edge_ok'), ',',
            join(*displayed), ',', ' '.join(map(str, expected_bytes))))])

    main = [*[sb.Statement('data_hidelist', fields=lf(name)) for name in LIST_NAMES],
        clear('results'), clear('times'), setv('all_ok', 1), setv('timing_ok', 1), setv('status', '准备'),
        setv('n', floor(v('config_n'))), iff(lt(v('n'), 1), [setv('n', 1)]), iff(gt(v('n'), MAX_N), [setv('n', MAX_N)]),
        setv('passes', floor(v('config_passes'))), iff(lt(v('passes'), 1), [setv('passes', 1)]),
        iff(gt(v('passes'), MAX_PASSES), [setv('passes', MAX_PASSES)]),
        iff(gt(v('passes'), floor(div(MAX_ITERATIONS, v('n')))), [setv('passes', floor(div(MAX_ITERATIONS, v('n'))))]),
        setv('samples', floor(v('config_samples'))), iff(lt(v('samples'), 1), [setv('samples', 1)]), iff(gt(v('samples'), 9), [setv('samples', 9)]),
        setv('config_n', v('n')), setv('config_passes', v('passes')), setv('config_samples', v('samples')),
        setv('iterations', mul(v('n'), v('passes'))), setv('warm_sweeps', v('passes')),
        iff(gt(v('warm_sweeps'), 5), [setv('warm_sweeps', 5)]),
        append('results', join('CONFIG,', strategy, ',version=arith1,clock=dayssince2000,N=', v('n'), ',passes=', v('passes'), ',samples=', v('samples'))),
        append('results', 'strategy,case,sample,iterations,seconds,checksum,expected,errors,ok'),
        *[call('suite', case_id) for case_id in range(1, 7)],
        setv('status', '精度边界检查'), call('diagnostics'),
        iff(eq(v('all_ok'), 1), [iff(eq(v('timing_ok'), 1),
            [setv('status', '完成：计时校验通过；边界见结果')], [setv('status', '完成：样本过短，请增加遍历次数')])],
            [setv('status', '完成：计时数据校验失败')]),
        append('results', join('DONE,timed_checks_passed=', v('all_ok'), ',timing_at_least_0.1s=', v('timing_ok'),
                               ',edge_passes=', v('edge_passes'), '/', len(EDGES))),
        sb.Statement('data_showlist', fields=lf('results')),
    ]
    return {'main': main, 'prepare': prepare, 'verify': verify, 'diagnostics': diagnostics,
            'dispatch': dispatch, 'suite': suite, 'report': report, **kernels}


def project_for(strategy):
    builder = sb.Builder()
    builder.green_flag()
    bodies = make_bodies(strategy)
    for index, name in enumerate(PROCEDURES):
        builder.procedure(name, bodies[name], index)
    variables = {}
    for block in builder.blocks.values():
        if 'VARIABLE' in block['fields']:
            name, key = block['fields']['VARIABLE']
            variables[key] = [name, 0]
    for key, value in [('config_n', DEFAULT_N), ('config_passes', DEFAULT_PASSES),
                       ('config_samples', DEFAULT_SAMPLES), ('status', '点击绿旗开始')]:
        variables['var_' + key][1] = value
    assets = {}

    def costume(name, svg, cx, cy):
        data = svg.encode('utf-8')
        digest = hashlib.md5(data).hexdigest()
        assets[digest + '.svg'] = data
        return {'name': name, 'assetId': digest, 'dataFormat': 'svg', 'md5ext': digest + '.svg',
                'bitmapResolution': 1, 'rotationCenterX': cx, 'rotationCenterY': cy}

    title = 'BYTES - NUMBER - BYTES' if strategy == 'number' else 'BYTEWISE ARITHMETIC'
    background = f'''<svg xmlns="http://www.w3.org/2000/svg" width="480" height="360">
<rect width="480" height="360" fill="#eef3fa"/>
<text x="10" y="24" font-family="sans-serif" font-size="12" font-weight="bold" fill="#183153">{title}</text>
<text x="10" y="254" font-family="sans-serif" font-size="11" fill="#4b607c">u32 / u64: add, sub, mul</text>
<text x="10" y="276" font-family="sans-serif" font-size="11" fill="#4b607c">Compare median seconds.</text>
<text x="10" y="298" font-family="sans-serif" font-size="11" fill="#4b607c">Precision edges: not timed.</text>
<text x="10" y="338" font-family="sans-serif" font-size="10" fill="#4b607c">Clock: days since 2000</text>
</svg>'''
    blank = '<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><rect width="1" height="1" fill="none"/></svg>'
    stage = {'isStage': True, 'name': 'Stage', 'variables': variables,
             'lists': {'list_' + name: [label, GOLDEN if name == 'golden' else []] for name, label in LIST_NAMES.items()},
             'broadcasts': {}, 'blocks': {}, 'comments': {}, 'currentCostume': 0,
             'costumes': [costume('Benchmark', background, 240, 180)], 'sounds': [], 'volume': 100,
             'layerOrder': 0, 'tempo': 60, 'videoTransparency': 50, 'videoState': 'off', 'textToSpeechLanguage': None}
    sprite = {'isStage': False, 'name': 'Arithmetic Benchmark', 'variables': {}, 'lists': {}, 'broadcasts': {},
              'blocks': builder.blocks, 'comments': {}, 'currentCostume': 0,
              'costumes': [costume('Invisible', blank, 0, 0)], 'sounds': [], 'volume': 100, 'layerOrder': 1,
              'visible': False, 'x': 0, 'y': 0, 'size': 100, 'direction': 90, 'draggable': False, 'rotationStyle': 'all around'}
    monitors = []
    for key, y, maximum in [('config_n', 43, MAX_N), ('config_passes', 97, MAX_PASSES), ('config_samples', 151, 9)]:
        monitors.append({'id': 'var_' + key, 'mode': 'slider', 'opcode': 'data_variable',
                         'params': {'VARIABLE': sb.NAMES[key]}, 'spriteName': None, 'value': variables['var_' + key][1],
                         'width': 0, 'height': 0, 'x': 10, 'y': y, 'visible': True,
                         'sliderMin': 1, 'sliderMax': maximum, 'isDiscrete': True})
    monitors.append({'id': 'var_status', 'mode': 'default', 'opcode': 'data_variable',
                     'params': {'VARIABLE': sb.NAMES['status']}, 'spriteName': None, 'value': '点击绿旗开始',
                     'width': 0, 'height': 0, 'x': 10, 'y': 211, 'visible': True,
                     'sliderMin': 0, 'sliderMax': 100, 'isDiscrete': True})
    monitors.append({'id': 'list_results', 'mode': 'list', 'opcode': 'data_listcontents',
                     'params': {'LIST': LIST_NAMES['results']}, 'spriteName': None, 'value': [],
                     'width': 274, 'height': 340, 'x': 200, 'y': 10, 'visible': True})
    return {'targets': [stage, sprite], 'monitors': monitors, 'extensions': [],
            'meta': {'semver': '3.0.0', 'vm': '0.2.0', 'agent': 'byte-arithmetic-benchmark-arith1'}}, assets


def check_project(project, assets):
    blocks = project['targets'][1]['blocks']
    stage = project['targets'][0]
    incoming = dict.fromkeys(blocks, 0)
    prototypes = {}
    for key, block in blocks.items():
        refs = [block['next']] if block['next'] else []
        for descriptor in block['inputs'].values():
            refs.extend(x for x in descriptor[1:] if isinstance(x, str))
        for ref in refs:
            assert ref in blocks and blocks[ref]['parent'] == key
            incoming[ref] += 1
        for field, collection in [('VARIABLE', 'variables'), ('LIST', 'lists')]:
            if field in block['fields']:
                assert block['fields'][field][1] in stage[collection]
        if block['opcode'] == 'procedures_prototype':
            mut = block['mutation']
            assert mut['warp'] == 'true'
            assert set(json.loads(mut['argumentids'])) == set(block['inputs'])
            assert mut['proccode'] not in prototypes
            prototypes[mut['proccode']] = mut
    for key, block in blocks.items():
        assert incoming[key] == (0 if block['topLevel'] else 1)
        if block['opcode'] == 'procedures_call':
            mut = block['mutation']
            assert mut['argumentids'] == prototypes[mut['proccode']]['argumentids']
            assert set(json.loads(mut['argumentids'])) == set(block['inputs'])
    assert len(prototypes) == len(PROCEDURES)
    assert sum(b['opcode'] == 'event_whenflagclicked' for b in blocks.values()) == 1
    assert sum(b['opcode'] == 'sensing_dayssince2000' for b in blocks.values()) == 12
    assert not any(b['opcode'] == 'sensing_timer' for b in blocks.values())
    assert 3 * 8 * MAX_N <= 200000
    assert len(stage['lists']['list_golden'][1]) <= 200000
    for target in project['targets']:
        for c in target['costumes']:
            assert hashlib.md5(assets[c['md5ext']]).hexdigest() == c['assetId']


def bytewise_reference(a, b, width, op):
    aa, bb = a.to_bytes(width, 'little'), b.to_bytes(width, 'little')
    output = []
    carry = 0
    for j in range(width):
        if op == 'add':
            t = aa[j] + bb[j] + carry
        elif op == 'sub':
            t = aa[j] - bb[j] - carry
        else:
            t = carry + sum(aa[k] * bb[j-k] for k in range(j + 1))
        output.append(t % 256)
        carry = -(t // 256) if op == 'sub' else t // 256
        assert abs(t) < 2**20
    return int.from_bytes(bytes(output), 'little')


def check_arithmetic():
    # Small arithmetic oracle checks only; no Scratch VM or timing workload runs.
    rng = random.Random(934731)
    for width, op in CASES:
        for i in range(MAX_N):
            a, b = inputs_for(width, op, i)
            expected = exact_result(a, b, width, op)
            assert 0 <= a < 256**width and 0 <= b < 256**width
            assert a < 2**53 and b < 2**53 and expected < 2**53
            numeric = {'add': lambda: float(a) + float(b), 'sub': lambda: float(a) - float(b),
                       'mul': lambda: float(a) * float(b)}[op]() % float(256**width)
            assert numeric == expected
            assert expected + MOD32 < 2**53
        for _ in range(100):
            a, b = rng.randrange(256**width), rng.randrange(256**width)
            assert bytewise_reference(a, b, width, op) == exact_result(a, b, width, op)
    for width, op, a, b, _ in EDGES:
        assert bytewise_reference(a, b, width, op) == exact_result(a, b, width, op)


def main():
    check_arithmetic()
    for strategy in ('number', 'bytewise'):
        project, assets = project_for(strategy)
        check_project(project, assets)
        filename = f'arithmetic-{strategy}.sb3'
        data = json.dumps(project, ensure_ascii=False, separators=(',', ':')).encode('utf-8')
        with zipfile.ZipFile(ROOT / filename, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, payload in [('project.json', data), *sorted(assets.items())]:
                info = zipfile.ZipInfo(name, date_time=(2026, 9, 12, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, payload)
        with zipfile.ZipFile(ROOT / filename) as archive:
            assert archive.testzip() is None
            check_project(json.loads(archive.read('project.json')), {name: archive.read(name) for name in assets})
        print(f'Built {filename}: {len(project["targets"][1]["blocks"])} blocks, {len(data)} JSON bytes. Static/oracle checks passed; NOT opened or run.')


if __name__ == '__main__':
    main()
