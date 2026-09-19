#!/usr/bin/env node
'use strict';

// Real Scratch/TurboWarp execution in isolated workers. This verifies costume
// selection and visibility, not rendered pixels (there is no renderer here).
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {fork} = require('node:child_process');

async function worker(filename, engine) {
    console.log = console.info = console.debug = (...args) => console.error(...args);
    const VM = require(engine === 'scratch' ? 'scratch-vm' : 'turbowarp-vm');
    const StorageModule = require('scratch-storage');
    const Storage = StorageModule.ScratchStorage || StorageModule;
    const vm = new VM();
    vm.attachStorage(new Storage());
    vm.setCompatibilityMode(true);
    vm.setTurboMode(true);
    let compiledThreads = 0;
    const errors = [];
    if (engine === 'turbowarp') {
        vm.setCompilerOptions({enabled: true});
        vm.on('COMPILE_ERROR', (_target, error) => errors.push(String(error)));
    }
    const push = vm.runtime._pushThread;
    vm.runtime._pushThread = function (...args) {
        const thread = push.apply(this, args);
        if (thread.isCompiled && !thread.updateMonitor) compiledThreads++;
        return thread;
    };
    vm.start();
    try {
        await vm.loadProject(fs.readFileSync(filename));
        const originals = vm.runtime.targets.filter(t => t.isOriginal !== false);
        assert.equal(originals.length, 2);
        assert.equal(originals.filter(t => t.isStage).length, 1);
        const target = originals.find(t => !t.isStage);
        assert.equal(target.getName(), 'Program');
        const costumes = target.getCostumes();
        assert.equal(costumes.length, 5);
        assert.deepEqual(costumes.slice(1).map(c => c.name), ['demo::tile', 'demo::copy', 'demo::123', 'demo::待机😀']);
        for (const costume of costumes) assert.ok(costume.asset && costume.asset.data, `asset ${costume.name} loaded`);
        const changes = [];
        const visibility = [];
        const setCostume = target.setCostume;
        target.setCostume = function (...args) {
            const result = setCostume.apply(this, args);
            changes.push(this.currentCostume);
            return result;
        };
        const setVisible = target.setVisible;
        target.setVisible = function (...args) {
            const result = setVisible.apply(this, args);
            visibility.push(this.visible);
            return result;
        };
        const runs = [];
        for (let iteration = 0; iteration < 2; iteration++) {
            // A subsequent green flag must restore the initial blank/hidden
            // state even when the previous execution left a resource selected.
            target.setCostume(4);
            target.setVisible(true);
            changes.length = visibility.length = 0;
            vm.greenFlag();
            await new Promise(resolve => {
                const timer = setInterval(() => {
                    if (!vm.runtime.threads.some(t => !t.updateMonitor)) {
                        clearInterval(timer);
                        resolve();
                    }
                }, 5);
            });
            assert.deepEqual(errors, []);
            const variables = Object.fromEntries(Object.values(target.variables).map(v => [v.name, v.value]));
            assert.equal(variables.__scl_status, 'done');
            assert.equal(Number(variables.exit_code), 0);
            assert.equal(variables.__scl_string, 'demo::待机😀');
            assert.equal(target.currentCostume, 4);
            assert.equal(target.visible, false);
            assert.equal(changes[0], 0, 'green flag restores blank costume');
            assert.equal(visibility[0], false, 'green flag hides Program');
            assert.deepEqual(changes, [0, 1, 2, 3, 4], 'missing name leaves costume unchanged');
            assert.deepEqual(visibility, [false, true, false]);
            runs.push({changes: [...changes], visibility: [...visibility], exitCode: Number(variables.exit_code)});
        }
        if (engine === 'turbowarp') assert.ok(compiledThreads > 0, 'compiled execution used');
        return {vm: engine, passed: true, runs, compiledThreads, rendering: false, pixelsVerified: false};
    } finally {
        vm.stopAll();
        vm.quit();
    }
}

function run(filename, engine) {
    return new Promise((resolve, reject) => {
        const child = fork(__filename, ['--worker', filename, engine], {stdio: ['ignore', 'pipe', 'pipe', 'ipc'], windowsHide: true});
        let diagnostics = '';
        let finished = false;
        const finish = (error, result) => {
            if (finished) return;
            finished = true;
            clearTimeout(timer);
            child.kill();
            if (error) reject(new Error(`${engine}: ${error}\n${diagnostics}`));
            else resolve(result);
        };
        const timer = setTimeout(() => finish('resource VM timeout'), 120000);
        child.stdout.on('data', x => { diagnostics = (diagnostics + x).slice(-12000); });
        child.stderr.on('data', x => { diagnostics = (diagnostics + x).slice(-12000); });
        child.on('message', message => finish(message.error, message.result));
        child.on('error', error => finish(error.message));
        child.on('exit', code => finish(`worker exited ${code}`));
    });
}

if (process.argv[2] === '--worker') {
    worker(process.argv[3], process.argv[4]).then(result => process.send({result}),
        error => process.send({error: error.stack || String(error)}));
} else {
    (async () => {
        const filename = path.resolve(process.argv[2]);
        const results = [];
        for (const engine of ['scratch', 'turbowarp']) results.push(await run(filename, engine));
        process.stdout.write(JSON.stringify(results, null, 2) + '\n');
    })().catch(error => { console.error(error); process.exitCode = 1; });
}
