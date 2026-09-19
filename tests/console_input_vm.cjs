#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {fork} = require('node:child_process');

async function worker(filename, engine) {
    console.log = console.info = console.debug = (...args) => console.error(...args);
    const VM = require(engine === 'scratch' ? 'scratch-vm' : 'turbowarp-vm');
    const Storage = require('scratch-storage');
    const vm = new VM();
    vm.attachStorage(new (Storage.ScratchStorage || Storage)());
    vm.setCompatibilityMode(true);
    vm.setTurboMode(true);
    const errors = [];
    const isTurboWarp = engine !== 'scratch';
    const compiled = engine === 'turbowarp';
    let compiledThreads = 0;
    if (isTurboWarp) {
        vm.setCompilerOptions({enabled: compiled, warpTimer: false});
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
        const variables = Object.fromEntries(Object.values(target.variables).map(v => [v.name, v]));
        const value = name => variables[name]?.value;
        async function step() {
            vm.runtime._step();
            await new Promise(resolve => setTimeout(resolve, 1));
        }
        async function until(predicate, message) {
            for (let i = 0; i < 12000; ++i) {
                if (predicate()) return;
                if (value('__scl_status') === 'done')
                    throw new Error(`${message}: exited early with ${value('exit_code')}`);
                await step();
            }
            throw new Error(`${message}: phase=${value('console_input_phase')}, status=${value('__scl_status')}`);
        }
        async function key(text) {
            // Runtime policy samples Shift in the native letter collector.
            // Hold it until that collector has had time to execute; providing
            // key: 'B' by itself does not imply the Shift state is pressed.
            const shifted = isTurboWarp && /^[A-Z]$/.test(text);
            if (shifted) {
                vm.postIOData('keyboard', {key: 'Shift', isDown: true});
                await step();
            }
            vm.postIOData('keyboard', {key: text, isDown: true});
            await step();
            vm.postIOData('keyboard', {key: text, isDown: false});
            await step();
            if (shifted) {
                vm.postIOData('keyboard', {key: 'Shift', isDown: false});
                await step();
            }
        }
        async function batch(number, keys) {
            await until(() => Number(value('console_input_phase')) === number, `phase ${number}`);
            for (const text of keys) await key(text);
            // Hats may be behind the yielding LLVM thread; wait until each
            // native collector has executed before releasing the poll gate.
            for (let i = 0; i < 5; ++i) await step();
            variables.console_input_go.value = number;
        }
        for (let round = 0; round < 2; ++round) {
            vm.greenFlag();
            await step(); // Let green-flag initialization replace the prior done state.
            const erase = isTurboWarp ? 'Backspace' : '\\';
            await batch(1, ['a', 'B', '\\', 'c', ' ', '1', 'Enter', 'z']);
            assert.equal(Number(value('console_input_tw')), isTurboWarp ? 1 : 0);
            await batch(2, [erase, 'a', 'B', 'c', erase, erase, 'D', 'Enter']);
            await batch(3, ['a', 'b', 'c', 'd', 'e', erase, 'd', 'Enter']);
            await until(() => Number(value('console_input_phase')) === 4, 'blocking read_line');
            for (const text of ['x', 'Y', 'Enter']) await key(text);
            await until(() => Number(value('console_input_phase')) === 5, 'Backspace case');
            await batch(5, ['a', 'b', 'Backspace', 'c', '\\', 'Enter']);
            await batch(6, ['a', 'Enter']);
            await until(() => value('__scl_status') === 'done', 'completion');
            assert.equal(Number(value('exit_code')), 0);
            assert.equal(Number(value('console_input_phase')), 7);
            assert.ok(value('console_input_draws').length > 0);
        }
        assert.deepEqual(errors, []);
        if (compiled) assert.ok(compiledThreads > 0, 'TurboWarp must actually compile threads');
        else assert.equal(compiledThreads, 0);
        return {engine, mode: compiled ? 'compiled' : 'interpreted', compiledThreads, pass: true, rounds: 2, assertionsPerRound: 22,
            checks: ['real keyboard hats', 'runtime platform detection', 'TurboWarp Shift-based case and literal backslash',
                'vanilla uppercase and backslash deletion', 'same-batch Enter isolation',
                'prompt protection and deletion across soft wrap', 'history-derived byte limit',
                'external output and explicit cancellation', 'blocking UI-cooperative read_line',
                'callback reentry refusal', 'TurboWarp Backspace', 'zero limit and empty line', 'green flag restart']};
    } finally { vm.stopAll(); vm.quit(); }
}

function run(filename, engine) {
    return new Promise((resolve, reject) => {
        const child = fork(__filename, ['--worker', filename, engine], {
            stdio: ['ignore', 'pipe', 'pipe', 'ipc'], windowsHide: true
        });
        let settled = false, diagnostics = '';
        const finish = (error, result) => {
            if (settled) return;
            settled = true; clearTimeout(timer); child.kill();
            error ? reject(error) : resolve(result);
        };
        const timer = setTimeout(() => finish(new Error(`${engine} timeout: ${diagnostics}`)), 120000);
        child.stdout.on('data', data => { diagnostics = (diagnostics + data).slice(-6000); });
        child.stderr.on('data', data => { diagnostics = (diagnostics + data).slice(-6000); });
        child.once('message', result => finish(result.pass ? null : new Error(result.error), result));
        child.once('error', error => finish(error));
        child.once('exit', (code, signal) => finish(new Error(`${engine} exited ${code}/${signal}: ${diagnostics}`)));
    });
}
if (process.argv[2] === '--worker') {
    worker(process.argv[3], process.argv[4]).then(
        result => process.send(result, () => process.exit(0)),
        error => process.send({pass: false, error: error.stack}, () => process.exit(1)));
} else {
    (async () => {
        const filename = path.resolve(process.argv[2]);
        const results = [];
        for (const engine of ['scratch', 'turbowarp', 'turbowarp-interpreted']) results.push(await run(filename, engine));
        const report = {project: filename, pass: true, results};
        if (process.argv[3]) fs.writeFileSync(process.argv[3], JSON.stringify(report, null, 2));
        console.log(JSON.stringify(report));
    })().catch(error => { console.error(error); process.exitCode = 1; });
}
