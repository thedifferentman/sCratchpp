"""Real Clang/frontend/backend test: sidecars must not change Scratch code."""
import argparse
import json
from pathlib import Path
import subprocess
import zipfile
import zlib


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--clang', required=True)
    parser.add_argument('--output-dir', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = root / 'tests/fixtures/debugger_program.cpp'
    bitcode = output / 'program.bc'
    subprocess.run([args.clang, '--target=x86_64-unknown-linux-gnu', '-O0', '-g',
                    '-fstandalone-debug', '-fno-exceptions', '-fno-rtti',
                    '-emit-llvm', '-c', str(source), '-o', str(bitcode)], check=True)
    plain = output / 'plain.sb3'
    project = output / 'program.sb3'
    debug = output / 'program.debug.json'
    for destination, extra in [(plain, []), (project, ['--debug-map', str(debug)])]:
        subprocess.run([args.compiler, str(bitcode), '--whole-program', '-o', str(destination), *extra], check=True)
    with zipfile.ZipFile(plain) as archive:
        expected = archive.read('project.json')
    with zipfile.ZipFile(project) as archive:
        actual = archive.read('project.json')
    assert expected == actual, 'Debug sidecar altered executable Scratch project'
    mapping = json.loads(debug.read_text(encoding='utf-8'))
    assert mapping['projectCrc32'] == f'{zlib.crc32(actual):08x}'
    assert mapping['schemaVersion'] == 1
    blocks = json.loads(actual)['targets'][1]['blocks']
    positions = set()
    for block_id, point in mapping['points'].items():
        assert block_id in blocks
        function = mapping['functions'][point['function']]
        assert point['instruction'] in function['instructions']
        assert blocks[block_id]['opcode'] != 'event_whenflagclicked'
        if point.get('file') and point.get('line'):
            positions.add(point['line'])
            assert Path(point['file']).resolve() == source.resolve()
    assert len(positions) >= 9, positions
    locals_ = [v['name'] for f in mapping['functions'].values() for v in f['variables']]
    assert all(name in locals_ for name in ('sum', 'i', 'n', 'value')), locals_
    print(f'Debug map: {len(mapping["points"])} execution points, {len(positions)} source lines; SB3 unchanged')
    # Large bodies exercise outlining: debug annotations must not affect the
    # AST size budget or cause extra helper procedures to be generated.
    large = output / 'outlined.cpp'
    large.write_text('volatile int total;\nint main() {\n' +
                     '\n'.join('total += 1;' for _ in range(120)) +
                     '\nreturn total == 120 ? 0 : 1;\n}\n', encoding='utf-8')
    large_bc = output / 'outlined.bc'
    subprocess.run([args.clang, '--target=x86_64-unknown-linux-gnu', '-O0', '-g',
                    '-emit-llvm', '-c', str(large), '-o', str(large_bc)], check=True)
    for name, flags in [('outlined-plain', []), ('outlined-debug', ['--debug-map', str(output / 'outlined.debug.json')])]:
        subprocess.run([args.compiler, str(large_bc), '--whole-program', '-o',
                        str(output / (name + '.sb3')), *flags], check=True)
    with zipfile.ZipFile(output / 'outlined-plain.sb3') as a, zipfile.ZipFile(output / 'outlined-debug.sb3') as b:
        assert a.read('project.json') == b.read('project.json'), 'Debug metadata changed chunk outlining'
    print('Large outlined function: executable project unchanged')


if __name__ == '__main__':
    main()
