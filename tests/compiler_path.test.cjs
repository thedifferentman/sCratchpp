'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {resolveCompiler} = require('./compiler_path.cjs');
const {parse: parseE2e} = require('./e2e.cjs');
const {parse: parseVarargs} = require('./varargs_test.cjs');

function sandbox(t, platform = process.platform) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'scratch-compiler-path-'));
    t.after(() => fs.rmSync(root, {recursive: true, force: true}));
    const executable = relative => {
        const filename = path.join(root, relative);
        fs.mkdirSync(path.dirname(filename), {recursive: true});
        fs.writeFileSync(filename, 'fixture');
        fs.chmodSync(filename, 0o755);
        return filename;
    };
    return {root, executable, options: {root, cwd: root, env: {}, platform}};
}

test('explicit compiler and environment override default and PATH', t => {
    const {options, executable, root} = sandbox(t);
    const explicit = executable('explicit/compiler');
    const configured = executable('configured/compiler');
    executable(`build/clang/scratch-llvm${process.platform === 'win32' ? '.exe' : ''}`);
    options.env.SCRATCH_COMPILER = configured;
    assert.equal(resolveCompiler(explicit, options), explicit);
    assert.equal(resolveCompiler(undefined, options), configured);
    assert.equal(resolveCompiler('./explicit/compiler', options), path.join(root, 'explicit/compiler'));
    assert.throws(() => resolveCompiler('./missing', options), /Compiler not found/);
    options.env.SCRATCH_COMPILER = path.join(root, 'missing');
    assert.throws(() => resolveCompiler(undefined, options), /Compiler not found/);
    assert.throws(() => resolveCompiler('', options), /nonempty/);
});

for (const platform of ['linux', 'darwin', 'win32']) {
    test(`${platform}: default Clang path uses host suffix and takes priority over PATH`, t => {
        const {options, executable, root} = sandbox(t, platform);
        const name = platform === 'win32' ? 'scratch-llvm.exe' : 'scratch-llvm';
        const installed = executable(`installed/${name}`);
        options.env[platform === 'win32' ? 'Path' : 'PATH'] = path.join(root, 'installed');
        assert.equal(resolveCompiler(undefined, options), installed);
        assert.equal(resolveCompiler('scratch-llvm', options), installed);
        const local = executable(`build/clang/${name}`);
        assert.equal(resolveCompiler(undefined, options), local);
    });
}

test('legacy MSVC build is not an automatic fallback', t => {
    const {options, executable} = sandbox(t);
    executable('build/native/Release/scratch-llvm.exe');
    assert.throws(() => resolveCompiler(undefined, options), /Pass --compiler/);
});

test('compiler resolution rejects a directory', t => {
    const {options, root} = sandbox(t);
    assert.throws(() => resolveCompiler(root, options), /not executable/);
});

test('E2E output directory controls default report regardless of argument order', () => {
    const outputDir = path.resolve('build/test-output/isolation');
    const report = path.resolve('build/reports/retained.json');
    assert.equal(parseE2e(['--output-dir', outputDir]).report, path.join(outputDir, 'report.json'));
    for (const args of [
        ['--report', report, '--output-dir', outputDir],
        ['--output-dir', outputDir, '--report', report]
    ]) {
        assert.equal(parseE2e(args).report, report);
        assert.equal(parseE2e(args).outputDir, outputDir);
    }
    const defaults = parseE2e([]);
    assert(defaults.outputDir.endsWith(`${process.platform}-${process.arch}`));
    assert.equal(defaults.report, path.join(defaults.outputDir, 'report.json'));
    assert.equal(parseE2e(['--compiler', 'scratch-llvm']).compiler, 'scratch-llvm');
    assert.equal(defaults.compiler, undefined);
});

test('test CLI parsers reject missing option values and invalid options', () => {
    for (const option of ['--compiler', '--vm', '--case', '--exclude', '--timeout', '--report', '--output-dir']) {
        assert.throws(() => parseE2e([option]), /Missing value/);
        assert.throws(() => parseE2e([option, '--list']), /Missing value/);
    }
    assert.throws(() => parseE2e(['--timeout', 'NaN']), /Invalid --timeout/);
    assert.throws(() => parseE2e(['--vm', 'unknown']), /Invalid --vm/);
    assert.throws(() => parseE2e(['--unknown']), /Unknown argument/);
    for (const option of ['--compiler', '--output-dir']) {
        assert.throws(() => parseVarargs([option]), /Missing value/);
    }
    assert.throws(() => parseVarargs(['--unknown']), /Unknown argument/);
});

test('varargs parser supports compiler selection and isolated output', () => {
    const outputDir = path.resolve('build/test-output/varargs');
    assert.deepEqual(parseVarargs(['--compiler', 'scratch-llvm', '--output-dir', outputDir]),
        {compiler: 'scratch-llvm', outputDir});
    assert.equal(parseVarargs(['legacy-compiler']).compiler, 'legacy-compiler');
    assert(parseVarargs([]).outputDir.endsWith(`${process.platform}-${process.arch}`));
    assert.deepEqual(parseVarargs(['--help']), {help: true});
});
