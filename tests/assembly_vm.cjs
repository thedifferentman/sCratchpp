#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const Zip = require('jszip');
const {runProject} = require('./vm_runner.cjs');

async function main() {
    const directory = path.resolve(process.argv[2] || 'cmake-build-debug/assembly-test');
    const smoke = path.join(directory, 'assembly-smoke.sb3');
    const pen = path.join(directory, 'assembly-pen.sb3');
    const zip = await Zip.loadAsync(fs.readFileSync(smoke), {checkCRC32: true});
    const expected = JSON.parse(await zip.file('assembly-test-expected.json').async('string'));
    const penZip = await Zip.loadAsync(fs.readFileSync(pen), {checkCRC32: true});
    const penProject = JSON.parse(await penZip.file('project.json').async('string'));
    assert.deepEqual(penProject.extensions, ['pen']);
    const report = await Promise.all(['scratch', 'turbowarp'].flatMap(vm => [
        {file: smoke, kind: 'numeric'}, {file: pen, kind: 'pen-load-and-execution'}
    ].map(async ({file, kind}) => {
        const result = await runProject(file, {vm, timeout: 10000});
        assert.equal(result.status, 'completed', `${vm}/${kind}: ${result.error}`);
        assert.equal(result.variables.__status, 'done');
        if (vm === 'turbowarp') assert.ok(result.compiledThreads > 0, 'TurboWarp compiler was not used');
        if (kind === 'numeric') {
            for (const [name, value] of Object.entries(expected)) {
                assert.equal(Number(result.variables[name]), value, `${vm}: ${name}`);
            }
            const clock = Number(result.variables.case_f64_clock_native);
            assert.ok(Number.isFinite(clock) && clock > 0, `${vm}: days_since_2000 is invalid`);
            const bytes = Buffer.alloc(8);
            bytes.writeDoubleLE(clock);
            for (let i = 0; i < bytes.length; i++) {
                assert.equal(Number(result.variables[`case_f64_clock_${i}`]), bytes[i], `${vm}: clock byte ${i}`);
            }
        }
        return {vm, kind, passed: true, cases: kind === 'numeric' ? Object.keys(expected).length : 1,
            compiledThreads: result.compiledThreads, executionMs: result.executionMs,
            rendering: result.rendering, pixelsVerified: false};
    })));
    fs.writeFileSync(path.join(directory, 'assembly-vm-report.json'), JSON.stringify(report, null, 2) + '\n');
    console.log(JSON.stringify(report));
}
main().catch(error => { console.error(error); process.exitCode = 1; });
