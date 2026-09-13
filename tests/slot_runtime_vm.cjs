#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const Zip = require('jszip');
const {runProject} = require('./vm_runner.cjs');
const {resolveCompiler} = require('./compiler_path.cjs');

async function main() {
    const directory = path.resolve(process.argv[2] || 'build/clang/test-output/slot-runtime');
    const smoke = path.join(directory, 'slot-runtime.sb3');
    const zip = await Zip.loadAsync(fs.readFileSync(smoke), {checkCRC32: true});
    const expected = JSON.parse(await zip.file('slot-runtime-expected.json').async('string'));
    assert.equal(expected.cases.reduce((sum, test) => sum + test.checks, 0), expected.bytes);
    const traps = [
        {name: 'slot-high-pointer.sb3', error: /exceeds addressable memory/},
        {name: 'slot-range-end.sb3', error: /invalid memory access/},
        {name: 'slot-code-address.sb3', error: /invalid memory access/}
    ];
    if (process.argv[3]) {
        const compiler = resolveCompiler(process.argv[3]);
        const output = path.join(directory, 'slot-backend-high-pointer.sb3');
        if (fs.existsSync(output)) fs.unlinkSync(output);
        const result = spawnSync(compiler, [path.join(__dirname, 'fixtures/slot_invalid_high_pointer.ll'), '-o', output],
            {encoding: 'utf8', timeout: 60000, windowsHide: true, maxBuffer: 8 * 1024 * 1024});
        assert.equal(result.status, 0, result.error?.message || result.stderr);
        traps.push({name: path.basename(output), error: /pointer|addressable memory|invalid memory access/});
    }
    const report = [];
    for (const vm of ['scratch', 'turbowarp']) {
        const result = await runProject(smoke, {vm, timeout: 60000});
        assert.equal(result.status, 'completed', `${vm}: ${result.error || result.diagnostics}`);
        assert.equal(Number(result.variables.__test_done), 1, `${vm}: ${result.variables.__scl_status}`);
        assert.equal(result.variables.__scl_status, 'done');
        if (vm === 'turbowarp') assert.ok(result.compiledThreads > 0, 'TurboWarp compiler was not used');
        const actual = result.lists.__test_actual.map(Number);
        const reference = result.lists.__test_expected.map(Number);
        assert.equal(actual.length, expected.bytes);
        assert.equal(reference.length, expected.bytes);
        let offset = 0;
        for (const test of expected.cases) {
            assert.deepEqual(actual.slice(offset, offset + test.checks), reference.slice(offset, offset + test.checks), `${vm}: ${test.label}`);
            offset += test.checks;
        }
        report.push({vm, kind: 'direct slots', passed: true, cases: expected.cases.length, byteChecks: expected.bytes,
            executionMs: result.executionMs, compiledThreads: result.compiledThreads});
        for (const trap of traps) {
            const execution = await runProject(path.join(directory, trap.name), {vm, timeout: 10000, listLimit: 32});
            assert.equal(execution.status, 'completed', `${vm}/${trap.name}: ${execution.error}`);
            assert.match(execution.variables.__scl_status, /^error:/, `${vm}/${trap.name}`);
            assert.match(execution.variables.__scl_status, trap.error, `${vm}/${trap.name}`);
            assert.notEqual(Number(execution.variables.__test_done), 1, `${vm}/${trap.name}: execution continued after trap`);
            report.push({vm, kind: trap.name, passed: true, diagnostic: execution.variables.__scl_status, executionMs: execution.executionMs});
        }
    }
    fs.writeFileSync(path.join(directory, 'slot-runtime-vm-report.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify(report, null, 2));
}
main().catch(error => {console.error(error); process.exitCode = 1;});
