#!/usr/bin/env node
'use strict';

// Run the events_test.cpp SB3, then exercise real mouse/keyboard IO against its
// native collectors. Workers bound a bad warp script's execution time.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {fork} = require('node:child_process');

async function worker(filename, engine) {
    console.log = console.info = console.debug = (...args) => console.error(...args);
    const VM = require(engine === 'scratch' ? 'scratch-vm' : 'turbowarp-vm');
    const StorageModule = require('scratch-storage');
    const vm = new VM();
    vm.attachStorage(new (StorageModule.ScratchStorage || StorageModule)());
    vm.setCompatibilityMode(true);
    vm.setTurboMode(true);
    const errors = [];
    let compiledThreads = 0;
    if (engine === 'turbowarp') {
        vm.setCompilerOptions({enabled: true, warpTimer: false});
        vm.on('COMPILE_ERROR', (_target, error) => errors.push(String(error)));
        const push = vm.runtime._pushThread;
        vm.runtime._pushThread = function (...args) {
            const thread = push.apply(this, args);
            if (thread.isCompiled && !thread.updateMonitor) ++compiledThreads;
            return thread;
        };
    }
    vm.start();
    try {
        await vm.loadProject(fs.readFileSync(filename));
        const target = vm.runtime.targets.find(t => !t.isStage);
        const stage = vm.runtime.targets.find(t => t.isStage);
        assert.equal(stage.sprite.costumes.filter(c => c.name.startsWith('__scl_keycase::')).length, 0);
        assert.equal(target.sprite.costumes.filter(c => c.name.startsWith('__scl_keycase::')).length, 0);
        const vars = Object.fromEntries(Object.values(target.variables).map(v => [v.name, v]));
        const queue = vars['__scl_events::wheel'];
        const keyboard = vars['__scl_events::keyboard'];
        const status = vars.__scl_status;
        const enabled = vars.__scl_events_enabled;
        const dropped = vars.__scl_events_enabled_dropped;
        assert.ok(queue && keyboard && status && enabled && dropped);
        async function drain() {
            for (let i = 0; i < 10000; ++i) {
                if (!vm.runtime.threads.some(t => !t.updateMonitor)) return;
                vm.runtime._step();
                await new Promise(resolve => setTimeout(resolve, 1));
            }
            throw new Error('Threads did not finish');
        }
        async function wheel(deltaY) {
            vm.postIOData('mouseWheel', {deltaY});
            await drain();
        }
        async function key(key, isDown) {
            vm.postIOData('keyboard', {key, isDown});
            await drain();
        }

        // Real library tests include callback mutation/reentrancy, batching,
        // capacity, float clock, run/quit and deliberate native yields.
        vm.greenFlag();
        await drain();
        assert.equal(status.value, 'done');
        assert.equal(Number(vars.exit_code.value), 0);
        assert.equal(queue.value.length, 0);
        assert.equal(keyboard.value.length, 0);

        const memory = vars.__scl_memory.value.slice();
        const stack = vars.__scl_sp.value;
        await wheel(-120); // Completed programs do not accept events.
        await key('a', true);
        await key('a', false);
        assert.equal(queue.value.length, 0);
        assert.equal(keyboard.value.length, 0);
        status.value = 'running';
        enabled.value = 0;
        await wheel(-120);
        await key('a', true);
        await key('a', false);
        assert.equal(queue.value.length, 0);
        assert.equal(keyboard.value.length, 0);
        enabled.value = 1;
        // Keyboard collection must leave user costume/backdrop state untouched.
        target.sprite.costumes.push({...target.sprite.costumes[0], name: 'user-original'});
        const originalCostume = target.sprite.costumes.length - 1;
        target.setCostume(originalCostume);
        stage.sprite.costumes.push({...stage.sprite.costumes[0], name: 'user-original-backdrop'});
        const originalBackdrop = stage.sprite.costumes.length - 1;
        stage.setCostume(originalBackdrop);

        await wheel(-120);
        await wheel(120);
        await wheel(0);
        assert.deepEqual(queue.value.map(Number), [1, -1]);
        assert.deepEqual(keyboard.value, []); // Wheel hats must not masquerade as keys.
        queue.value = [];

        await key('ArrowUp', true);
        await wheel(-120); // Documented ambiguity: held matching key suppresses wheel.
        await key('ArrowUp', false);
        await key('ArrowDown', true);
        await wheel(120);
        await key('ArrowDown', false);
        assert.deepEqual(queue.value, []);
        await wheel(-120);
        await wheel(120);
        assert.deepEqual(queue.value.map(Number), [1, -1]);
        queue.value = [];
        keyboard.value = [];

        const printable = [];
        for (let code = 32; code <= 126; ++code) {
            const text = String.fromCharCode(code);
            await key(text, true);
            await key(text, false);
            printable.push((engine === 'scratch' ? text.toUpperCase() : text.toLowerCase()).charCodeAt(0));
        }
        assert.deepEqual(keyboard.value.map(Number), printable);
        // Backslash remains ASCII 92. Console policy is not part of this library.
        assert.ok(keyboard.value.map(Number).includes(92));
        keyboard.value = [];

        for (const text of ['A', 'a', 'Z', 'z']) {
            await key(text, true);
            await key(text, false);
        }
        assert.deepEqual(keyboard.value.map(Number), engine === 'scratch' ? [65, 65, 90, 90] : [97, 97, 122, 122]);
        assert.equal(target.currentCostume, originalCostume);
        assert.equal(stage.currentCostume, originalBackdrop);
        keyboard.value = [];

        // Caps Lock is deliberately ignored: no Shift means lowercase in TW.
        await key('A', true);
        await key('A', false);
        assert.deepEqual(keyboard.value.map(Number), engine === 'scratch' ? [65] : [97]);
        keyboard.value = [];
        await key('Shift', true);
        keyboard.value = [];
        for (const text of ['A', 'a', 'Z', 'z']) {
            await key(text, true);
            await key(text, false);
        }
        await key('Shift', false);
        assert.deepEqual(keyboard.value.map(Number), [65, 65, 90, 90]);
        keyboard.value = [];

        // Distinct queued letters use their own hat codes, not last-key text.
        vm.postIOData('keyboard', {key: 'a', isDown: true});
        vm.postIOData('keyboard', {key: 'a', isDown: false});
        vm.postIOData('keyboard', {key: 'b', isDown: true});
        vm.postIOData('keyboard', {key: 'b', isDown: false});
        await drain();
        assert.deepEqual(keyboard.value.map(Number), engine === 'scratch' ? [65, 66] : [97, 98]);
        assert.equal(target.currentCostume, originalCostume);
        assert.equal(stage.currentCostume, originalBackdrop);
        keyboard.value = [];

        // Document the intentional sampling boundary: Shift was down when A
        // arrived but is released before its hat executes, so TW produces 'a'.
        await key('Shift', true);
        keyboard.value = [];
        vm.postIOData('keyboard', {key: 'A', isDown: true});
        vm.postIOData('keyboard', {key: 'A', isDown: false});
        vm.postIOData('keyboard', {key: 'Shift', isDown: false});
        await drain();
        assert.deepEqual(keyboard.value.map(Number), engine === 'scratch' ? [65] : [97]);
        keyboard.value = [];

        const basic = [['Enter', 13], ['ArrowLeft', 0x110000], ['ArrowUp', 0x110001],
            ['ArrowRight', 0x110002], ['ArrowDown', 0x110003]];
        for (const [name] of basic) {
            await key(name, true);
            await key(name, false);
        }
        assert.deepEqual(keyboard.value.map(Number), basic.map(([, code]) => code));
        assert.deepEqual(queue.value, []);
        keyboard.value = [];

        const extra = [['Backspace', 8], ['Delete', 127], ['Shift', 0x110004],
            ['CapsLock', 0x110005], ['ScrollLock', 0x110006], ['Control', 0x110007],
            ['Escape', 27], ['Insert', 0x110008], ['Home', 0x110009], ['End', 0x11000a],
            ['PageUp', 0x11000b], ['PageDown', 0x11000c]];
        for (const [name] of extra) {
            await key(name, true);
            await key(name, false);
        }
        assert.deepEqual(keyboard.value.map(Number), engine === 'scratch' ? [] : extra.map(([, code]) => code));
        keyboard.value = [];
        for (const name of ['Tab', 'F1', 'Meta']) {
            await key(name, true);
            await key(name, false);
        }
        assert.deepEqual(keyboard.value, []);

        // Host repeat notifies repeatedly; no key-up notifications are synthesized.
        await key('a', true);
        await key('a', true);
        await key('a', false);
        assert.deepEqual(keyboard.value.map(Number), engine === 'scratch' ? [65, 65] : [97, 97]);
        keyboard.value = [];
        vm.postIOData('keyboard', {key: 'z', isDown: true});
        vm.postIOData('keyboard', {key: 'z', isDown: false});
        await drain();
        assert.deepEqual(keyboard.value.map(Number), engine === 'scratch' ? [90] : [122]); // Fast release still captured.
        keyboard.value = [];

        for (let i = 0; i < 130; ++i) await wheel(i % 2 ? 120 : -120);
        assert.equal(queue.value.length, 128);
        assert.equal(Number(dropped.value), 2);
        assert.deepEqual(queue.value.map(Number), Array.from({length: 128}, (_, i) => i % 2 ? -1 : 1));
        for (let i = 0; i < 130; ++i) await key('x', true);
        await key('x', false);
        assert.deepEqual(keyboard.value.map(Number), Array(128).fill(engine === 'scratch' ? 88 : 120));
        assert.equal(Number(dropped.value), 4);
        // Collector execution cannot enter LLVM callbacks or corrupt its stack.
        assert.deepEqual(vars.__scl_memory.value, memory);
        assert.equal(vars.__scl_sp.value, stack);
        assert.equal(target.currentCostume, originalCostume);
        assert.equal(stage.currentCostume, originalBackdrop);

        // Restart clears queues, counters and C++ subscription state.
        vm.greenFlag();
        await drain();
        assert.equal(status.value, 'done');
        assert.equal(Number(vars.exit_code.value), 0);
        assert.equal(queue.value.length, 0);
        assert.equal(keyboard.value.length, 0);
        assert.equal(Number(dropped.value), 0);
        assert.deepEqual(errors, []);
        if (engine === 'turbowarp') assert.ok(compiledThreads > 2);
        return {engine, pass: true, compiledThreads, collectorCapacity: 128,
            checks: ['library callbacks and float clock', 'completed/disabled gates',
                'wheel directions', 'physical arrow suppression', 'capacity/drop count',
                '95 printable keys and VM-specific letter case', 'arrows and enter',
                'Shift-only case and quick-release boundary', 'backdrop and costume unchanged',
                'TurboWarp extra keys and vanilla compatibility', 'key repeat and rapid release',
                'LLVM memory/stack isolation', 'green flag reset']};
    } finally {
        vm.stopAll();
        vm.quit();
    }
}

function run(filename, engine) {
    return new Promise((resolve, reject) => {
        const child = fork(__filename, ['--worker', filename, engine], {
            stdio: ['ignore', 'pipe', 'pipe', 'ipc'], windowsHide: true
        });
        let diagnostics = '';
        child.stdout.on('data', data => { diagnostics = (diagnostics + data).slice(-4000); });
        child.stderr.on('data', data => { diagnostics = (diagnostics + data).slice(-4000); });
        const timer = setTimeout(() => { child.kill(); reject(new Error(`${engine} timeout: ${diagnostics}`)); }, 60000);
        child.once('message', result => {
            clearTimeout(timer);
            child.kill();
            if (result.pass) resolve(result);
            else reject(new Error(result.error));
        });
        child.once('error', reject);
        child.once('exit', code => {
            clearTimeout(timer);
            if (code && code !== 0) reject(new Error(`${engine} exited ${code}: ${diagnostics}`));
        });
    });
}

if (process.argv[2] === '--worker') {
    worker(process.argv[3], process.argv[4]).then(
        result => process.send(result, () => process.exit(0)),
        error => process.send({pass: false, error: error.stack}, () => process.exit(1))
    );
} else {
    (async () => {
        if (!process.argv[2]) throw new Error('Usage: node tests/events_vm.cjs events.sb3 [report.json]');
        const filename = path.resolve(process.argv[2]);
        const results = [];
        for (const engine of ['scratch', 'turbowarp']) results.push(await run(filename, engine));
        const report = {project: filename, pass: true, results};
        if (process.argv[3]) fs.writeFileSync(process.argv[3], JSON.stringify(report, null, 2) + '\n');
        console.log(JSON.stringify(report, null, 2));
    })().catch(error => { console.error(error); process.exitCode = 1; });
}
