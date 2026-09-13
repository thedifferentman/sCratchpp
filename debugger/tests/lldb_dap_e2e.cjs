'use strict';
// Actual LLDB DAP -> RSP -> WebSocket -> real TurboWarp interpreter. No browser
// renderer is needed here; browser extension loading has a separate UI check.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {PassThrough} = require('node:stream');
const {once} = require('node:events');
const {spawnSync} = require('node:child_process');
const VM = require('../../tests/node_modules/turbowarp-vm');
const {create} = require('../engine.js');
const {LldbAdapter} = require('../scratch-debug.cjs');

async function main() {
    const runningCase = process.argv.includes('--running');
    const root = path.resolve(__dirname, '../..'), directory = path.join(root, 'build/validation', runningCase ? 'debugger-running' : 'debugger');
    const source = runningCase ? path.join(directory, 'running.cpp') : path.join(root, 'tests/fixtures/debugger_program.cpp');
    const clang = process.env.SCRATCH_CLANG || path.join(root, 'build/toolchains/windows/root/clang64/bin/clang.exe');
    if (runningCase) {
        fs.mkdirSync(directory, {recursive: true});
        fs.writeFileSync(source, 'volatile int sink;\nint main() {\n    int tick = 0;\n    while (tick < 100000000) {\n        tick = tick + 1;\n        sink = tick;\n    }\n    return 0;\n}\n');
        const compiler = process.env.SCRATCH_COMPILER || path.join(root, 'build/clang/scratch-llvm' + (process.platform === 'win32' ? '.exe' : ''));
        for (const [exe, args] of [
            [clang, ['--target=x86_64-unknown-linux-gnu', '-O0', '-g', '-fstandalone-debug', '-emit-llvm', '-c', source, '-o', path.join(directory, 'program.bc')]],
            [compiler, [path.join(directory, 'program.bc'), '--whole-program', '--debug-map', path.join(directory, 'program.debug.json'), '-o', path.join(directory, 'program.sb3')]]
        ]) {
            const result = spawnSync(exe, args, {encoding: 'utf8', windowsHide: true});
            if (result.error || result.status !== 0) throw new Error(result.error?.message || result.stderr || result.stdout);
        }
    }
    const reportPath = path.join(directory, 'lldb-dap-report.json');
    const input = new PassThrough(), output = new PassThrough(), events = [], transcript = [], pending = new Map();
    let seq = 1, buffer = Buffer.alloc(0), vm, engine, socket, pump, connectionTask;
    const options = {lldbDap: process.env.SCRATCH_LLDB_DAP || (process.platform === 'win32' ? 'C:/Program Files/LLVM/bin/lldb-dap.exe' : 'lldb-dap'), lldbPython: process.env.SCRATCH_LLDB_PYTHON};
    const adapter = new LldbAdapter(input, output, options);
    const write = (command, args = {}) => {
        const id = seq++, message = Buffer.from(JSON.stringify({seq: id, type: 'request', command, arguments: args}));
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => { pending.delete(id); reject(new Error(`DAP request timed out: ${command}`)); }, 45000);
            pending.set(id, {resolve, reject, timeout}); input.write(`Content-Length: ${message.length}\r\n\r\n`); input.write(message);
        });
    };
    const waitEvent = async (name, predicate = () => true) => {
        const deadline = Date.now() + 30000;
        while (Date.now() < deadline) {
            const index = events.findIndex(e => e.event === name && predicate(e.body || {}));
            if (index >= 0) return events.splice(index, 1)[0].body || {};
            await new Promise(resolve => setTimeout(resolve, 5));
        }
        throw new Error(`Missing DAP event: ${name}`);
    };
    const connectVM = async url => {
        const extension = new URL(url).searchParams.get('extension'), base = extension.slice(0, -'/bridge.js'.length);
        const [bytes, map] = await Promise.all([fetch(base + '/project.sb3').then(r => r.arrayBuffer()), fetch(base + '/debug.json').then(r => r.json())]);
        vm = new VM(); await vm.loadProject(Buffer.from(bytes)); vm.runtime.currentStepTime = 1000 / 30;
        socket = new WebSocket(base.replace(/^http/, 'ws') + '/socket'); await once(socket, 'open');
        engine = create(vm, map, event => socket.send(JSON.stringify(event)));
        socket.addEventListener('message', async event => {
            const message = JSON.parse(event.data);
            try { const result = await engine[message.method](...(message.args || [])); socket.send(JSON.stringify({id: message.id, result})); }
            catch (error) { socket.send(JSON.stringify({id: message.id, error: error.stack || error.message})); }
        });
        socket.send(JSON.stringify({event: 'ready'}));
        pump = setInterval(() => { if (engine.running && !engine.paused) vm.runtime._step(); }, 1);
    };
    output.on('data', data => {
        buffer = Buffer.concat([buffer, data]);
        while (true) {
            const end = buffer.indexOf('\r\n\r\n'); if (end < 0) return;
            const size = Number(/Content-Length: (\d+)/i.exec(buffer.subarray(0, end).toString())[1]); if (buffer.length < end + 4 + size) return;
            const msg = JSON.parse(buffer.subarray(end + 4, end + 4 + size)); buffer = buffer.subarray(end + 4 + size); transcript.push(msg);
            if (msg.type === 'response') {
                const p = pending.get(msg.request_seq); if (!p) continue; pending.delete(msg.request_seq); clearTimeout(p.timeout);
                msg.success ? p.resolve(msg.body) : p.reject(new Error(msg.message));
            } else {
                events.push(msg);
                const match = msg.event === 'output' && /Opening TurboWarp: (\S+)/.exec(msg.body?.output || '');
                if (match) { connectionTask = connectVM(match[1]); connectionTask.catch(error => console.error(error)); }
            }
        }
    });
    let error; const measurements = {};
    try {
        await write('initialize', {adapterID: 'scratch-llvm', clientID: 'e2e', pathFormat: 'path', linesStartAt1: true, columnsStartAt1: true, supportsVariableType: true, supportsVariablePaging: true});
        const launch = write('launch', {project: path.join(directory, 'program.sb3'), debugMap: path.join(directory, 'program.debug.json'), clang, noOpen: true, port: 18842, stopOnEntry: true});
        await waitEvent('initialized');
        const bp = await write('setBreakpoints', {source: {path: source}, breakpoints: runningCase ? [] : [{line: 12}]});
        if (!runningCase) assert(bp.breakpoints[0].verified, 'source breakpoint must resolve using real LLDB');
        await write('configurationDone'); await launch; await waitEvent('stopped');
        if (runningCase) {
            await write('continue', {threadId: 1});
            await new Promise(resolve => setTimeout(resolve, 40));
            assert(engine.running && !engine.paused, 'program must actually be running before external pause');
            let began = Date.now();
            await write('pause', {threadId: 1}); await waitEvent('stopped');
            measurements.pauseMs = Date.now() - began;
            assert(measurements.pauseMs < 2000, 'pause must be delivered within a bounded interactive interval');
            await write('continue', {threadId: 1});
            await new Promise(resolve => setTimeout(resolve, 20));
            assert(engine.running && !engine.paused, 'program must be running when installing a breakpoint');
            began = Date.now();
            const added = await write('setBreakpoints', {source: {path: source}, breakpoints: [{line: 5}]});
            assert(added.breakpoints[0].verified);
            await waitEvent('stopped', body => body.reason === 'breakpoint');
            measurements.addAndHitMs = Date.now() - began;
            const trace = await write('stackTrace', {threadId: 1}); assert.equal(trace.stackFrames[0].line, 5);
            // Install a later source location, then remove it while the long
            // loop is executing. No stop/rebuild or VM mutation is involved.
            const later = await write('setBreakpoints', {source: {path: source}, breakpoints: [{line: 8}]});
            assert(later.breakpoints[0].verified);
            await write('continue', {threadId: 1});
            await new Promise(resolve => setTimeout(resolve, 20));
            assert(engine.running && !engine.paused, 'program must be running when deleting a breakpoint');
            await write('setBreakpoints', {source: {path: source}, breakpoints: []});
            assert.equal(engine.instructionBreakpoints.size, 0, 'deletion must reach the running IR target');
            await new Promise(resolve => setTimeout(resolve, 40));
            assert(engine.running && !engine.paused, 'deleting breakpoints must preserve execution');
            began = Date.now();
            await write('pause', {threadId: 1}); await waitEvent('stopped');
            measurements.pauseAfterDeleteMs = Date.now() - began;
            assert(measurements.pauseAfterDeleteMs < 2000);
            await write('disconnect', {terminateDebuggee: true});
            console.log('Live LLDB DAP: pause, breakpoint insertion and hit, removal while running, continued execution and disconnect passed.', measurements);
        } else {
        for (let i = 0; i < 3; i++) {
            await write('continue', {threadId: 1}); await waitEvent('stopped');
            const trace = await write('stackTrace', {threadId: 1}); assert.equal(trace.stackFrames[0].line, 12);
        }
        await write('setBreakpoints', {source: {path: source}, breakpoints: [{line: 14}]});
        await write('continue', {threadId: 1}); await waitEvent('stopped');
        const trace = await write('stackTrace', {threadId: 1}); assert.equal(trace.stackFrames[0].line, 14);
        const scopes = await write('scopes', {frameId: trace.stackFrames[0].id});
        const localsScope = scopes.scopes.find(s => /local/i.test(s.name)); assert(localsScope);
        const locals = await write('variables', {variablesReference: localsScope.variablesReference});
        assert(locals.variables.some(v => v.name === 'sum' && v.value === '3'), 'LLDB must read C++ local sum=3');
        await write('setBreakpoints', {source: {path: source}, breakpoints: []});
        await write('stepIn', {threadId: 1}); await waitEvent('stopped');
        const inside = await write('stackTrace', {threadId: 1}); assert.match(inside.stackFrames[0].name, /recurse/); assert(inside.stackFrames.length >= 2);
        await write('setBreakpoints', {source: {path: source}, breakpoints: [{line: 4}]});
        await write('continue', {threadId: 1}); await waitEvent('stopped');
        const recursive = await write('stackTrace', {threadId: 1});
        assert(recursive.stackFrames.length >= 3, 'recursive calls must have independent LLVM frames');
        const recursiveScopes = await write('scopes', {frameId: recursive.stackFrames[0].id});
        const recursiveLocals = await write('variables', {variablesReference: recursiveScopes.scopes.find(s => /local/i.test(s.name)).variablesReference});
        assert(recursiveLocals.variables.some(v => v.name === 'n' && v.value === '2'), 'LLDB must read n=2 in the inner recursive call');
        await write('setBreakpoints', {source: {path: source}, breakpoints: []});
        await write('stepOut', {threadId: 1}); await waitEvent('stopped');
        const recursiveOutside = await write('stackTrace', {threadId: 1}); assert.equal(recursiveOutside.stackFrames.length, 2);
        await write('stepOut', {threadId: 1}); await waitEvent('stopped');
        const outside = await write('stackTrace', {threadId: 1}); assert.match(outside.stackFrames[0].name, /main/);
        await write('next', {threadId: 1}); await waitEvent('stopped');
        await write('continue', {threadId: 1}); await waitEvent('terminated');
        const values = vm.runtime.targets.flatMap(t => Object.values(t.variables)); assert.equal(values.find(v => v.name === '__scl_status').value, 'done');
        assert.equal(Number(values.find(v => v.name === 'exit_code').value), 0);
        console.log('LLDB DAP, RSP, WebSocket and TurboWarp: dynamic source breakpoints, loop hits, local variables, source step-in/out/over and correct program exit passed.');
        }
    } catch (e) { error = e; }
    finally {
        clearInterval(pump); engine?.dispose(); vm?.stopAll(); socket?.close(); adapter.cleanup(); input.end();
        for (const p of pending.values()) { clearTimeout(p.timeout); p.reject(new Error('Test ended')); }
        fs.writeFileSync(reportPath, JSON.stringify({passed: !error, error: error?.stack, measurements, transcript}, null, 2));
    }
    if (error) throw error;
}
main().catch(error => { console.error(error); process.exitCode = 1; });
