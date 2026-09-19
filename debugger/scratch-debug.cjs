#!/usr/bin/env node
'use strict';

// The public entry point uses real LLDB. llvmdbg.cjs supplies only the browser
// transport and a small internal DAP harness used by transport/engine tests.
const fs = require('node:fs');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {Adapter, Bridge} = require('./llvmdbg.cjs');

function findExecutable(explicit, names, extra = []) {
    if (explicit) return path.isAbsolute(explicit) || !/[\\/]/.test(explicit) ? explicit : path.resolve(explicit);
    const directories = [...(process.env.PATH || '').split(path.delimiter), ...extra];
    for (const name of names) for (const directory of directories) {
        const candidate = path.join(directory, name);
        try { if (fs.statSync(candidate).isFile()) return candidate; } catch {}
    }
    return names[0];
}
function readToolchain(cwd) {
    try { return JSON.parse(fs.readFileSync(path.join(cwd, 'toolchain.local.json'), 'utf8')); } catch { return {}; }
}
function quoteCommand(value) { return '"' + String(value).replace(/\\/g, '/').replace(/"/g, '\\"') + '"'; }
function supportedCapabilities(value) {
    return {...value, supportsSetVariable: false, supportsSetExpression: false,
        supportsDataBreakpoints: false, supportsRestartFrame: false,
        supportsWriteMemoryRequest: false, supportsRestartRequest: false,
        supportsStepBack: false, supportsReverseContinue: false};
}

class LldbConnection {
    constructor(executable, onEvent, env = process.env) {
        this.seq = 1; this.pending = new Map(); this.buffer = Buffer.alloc(0); this.onEvent = onEvent;
        this.child = spawn(executable, [], {stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true, env});
        this.child.stdout.on('data', data => this.read(data));
        this.child.stderr.on('data', data => onEvent({event: 'output', body: {category: 'stderr', output: data.toString('utf8')}}));
        this.child.once('error', error => this.fail(new Error(`Cannot start LLDB DAP (${executable}): ${error.message}. Install LLDB and set launch.lldbDap.`)));
        this.child.once('exit', (code, signal) => { this.fail(new Error(`LLDB DAP exited (${signal || code})`)); onEvent({event: 'terminated', body: {}}); });
    }
    request(command, args = {}) {
        if (this.failure) return Promise.reject(this.failure);
        const seq = this.seq++;
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                this.pending.delete(seq);
                reject(new Error(`LLDB DAP request timed out: ${command}`));
            }, command === 'launch' ? 120000 : 30000);
            this.pending.set(seq, {resolve, reject, command, timeout});
            const data = Buffer.from(JSON.stringify({seq, type: 'request', command, arguments: args}));
            this.child.stdin.write(`Content-Length: ${data.length}\r\n\r\n`); this.child.stdin.write(data);
        });
    }
    read(data) {
        this.buffer = Buffer.concat([this.buffer, data]);
        while (true) {
            const end = this.buffer.indexOf('\r\n\r\n'); if (end < 0) return;
            const match = /Content-Length:\s*(\d+)/i.exec(this.buffer.subarray(0, end).toString('ascii'));
            if (!match) { this.fail(new Error('Invalid LLDB DAP response')); return; }
            const length = Number(match[1]); if (this.buffer.length < end + 4 + length) return;
            let message;
            try { message = JSON.parse(this.buffer.subarray(end + 4, end + 4 + length).toString('utf8')); }
            catch (error) { this.fail(error); return; }
            this.buffer = this.buffer.subarray(end + 4 + length);
            if (message.type === 'event') this.onEvent(message);
            else if (message.type === 'response') {
                const pending = this.pending.get(message.request_seq); if (!pending) continue;
                this.pending.delete(message.request_seq); clearTimeout(pending.timeout);
                message.success ? pending.resolve(message.body || {}) : pending.reject(new Error(message.message || `${pending.command} failed`));
            } else if (message.type === 'request') {
                // This target never launches guest machine code or a terminal.
                const response = Buffer.from(JSON.stringify({seq: this.seq++, type: 'response', request_seq: message.seq, command: message.command, success: false, message: 'Reverse terminal requests are unavailable for the LLVM virtual target'}));
                this.child.stdin.write(`Content-Length: ${response.length}\r\n\r\n`); this.child.stdin.write(response);
            }
        }
    }
    fail(error) { if (this.failure) return; this.failure = error; for (const p of this.pending.values()) { clearTimeout(p.timeout); p.reject(error); } this.pending.clear(); }
    close() { this.child.stdin.end(); this.child.kill(); this.fail(new Error('LLDB session closed')); }
}

class LldbAdapter extends Adapter {
    constructor(input, output, options = {}) {
        super(input, output); this.options = options;
        input?.on('end', () => this.cleanup());
    }
    lldb() {
        if (!this.connection) {
            const config = readToolchain(process.cwd());
            const extras = process.platform === 'win32' ? [path.join(process.env.ProgramFiles || 'C:\\Program Files', 'LLVM', 'bin')] : ['/usr/local/opt/llvm/bin', '/opt/homebrew/opt/llvm/bin', '/usr/lib/llvm-22/bin', '/usr/lib/llvm-21/bin', '/usr/lib/llvm-20/bin', '/usr/lib/llvm-19/bin', '/usr/lib/llvm-18/bin'];
            const executable = findExecutable(this.options.lldbDap || process.env.SCRATCH_LLDB_DAP || config.lldb_dap, process.platform === 'win32' ? ['lldb-dap.exe', 'lldb-vscode.exe'] : ['lldb-dap', 'lldb-vscode'], extras);
            const env = {...process.env};
            let python = this.options.lldbPython || process.env.SCRATCH_LLDB_PYTHON || config.lldb_python;
            if (!python && process.platform === 'win32') {
                python = [path.resolve('build/debugger/python311'), path.resolve('../build/debugger/python311')].find(p => fs.existsSync(path.join(p, 'python311.dll')));
            }
            if (python) { env.PATH = path.resolve(python) + path.delimiter + (env.PATH || ''); }
            this.connection = new LldbConnection(executable, message => {
                if (message.event === 'terminated') setImmediate(() => this.cleanup());
                if (message.event === 'capabilities') message.body = {capabilities: supportedCapabilities(message.body?.capabilities)};
                this.event(message.event, message.body);
            }, env);
        }
        return this.connection;
    }
    async handle(req) {
        const a = req.arguments || {};
        if (this.options.noDebug) return super.handle(req.command === 'launch' ?
            {...req, arguments: {...a, noDebug: true}} : req);
        switch (req.command) {
        case 'initialize': return supportedCapabilities(await this.lldb().request('initialize', a));
        case 'setVariable': case 'setExpression': case 'dataBreakpointInfo': case 'setDataBreakpoints':
        case 'restartFrame': case 'writeMemory': case 'restart': case 'stepBack': case 'reverseContinue':
            throw new Error(`${req.command} is not supported by the LLVM IR debug target`);
        case 'launch': return this.launch(a);
        case 'disconnect': case 'terminate': {
            let result = {};
            try { if (this.connection) result = await this.connection.request(req.command, a); }
            finally { this.cleanup(); }
            return result;
        }
        default: return this.lldb().request(req.command, a);
        }
    }
    async launch(a) {
        if (a.noDebug) throw new Error('Run Without Debugging must launch the adapter in run mode; update the Scratch LLVM VS Code extension.');
        if (!a.project) throw new Error('launch.project is required');
        let firstStopResolve, firstStopReject;
        const firstStop = new Promise((resolve, reject) => { firstStopResolve = resolve; firstStopReject = reject; });
        firstStop.catch(() => {});
        this.abortLaunch = firstStopReject;
        this.bridge = new Bridge(a, message => {
            if (message.event === 'stopped') { firstStopResolve(message.body); this.rsp?.notifyStopped(message.body); }
            else if (message.event === 'terminated') { firstStopReject(new Error('Program exited before a source execution point')); this.rsp?.notifyExited(message.body); }
            else if (message.event === 'output' || message.event === 'scrppPlayer') this.event(message.event, message.body);
        });
        try {
            const {generateSymbols} = require('./symbols.cjs');
            const {RspServer} = require('./rsp.cjs');
            const cwd = a.cwd || process.cwd(), config = readToolchain(cwd);
            const configuredClang = a.clang || process.env.SCRATCH_CLANG || config.clang;
            const clang = configuredClang ? (path.isAbsolute(configuredClang) ? configuredClang : /[\\/]/.test(configuredClang) ? path.resolve(cwd, configuredClang) : configuredClang) : findExecutable(null, process.platform === 'win32' ? ['clang.exe'] : ['clang'], [path.dirname(this.connection?.child.spawnfile || '')]);
            const output = path.resolve(a.symbols || path.join(path.dirname(a.debugMap || a.project), 'project.debug.elf'));
            this.event('output', {category: 'console', output: 'Preparing LLVM source symbols for LLDB.\n'});
            const generated = generateSymbols(this.bridge.map, {clang, output});
            this.rsp = new RspServer({request: (method, ...args) => this.bridge.request(method, ...args), symbols: generated.symbols, map: this.bridge.map});
            this.rsp.on('fault', error => this.event('output', {category: 'stderr', output: `LLVM target: ${error.message}\n`}));
            const port = await this.rsp.listen(0);
            if (this.cleaning) throw new Error('Debug session closed');
            await this.bridge.listen(); await this.bridge.ready;
            this.event('output', {category: 'console', output: 'TurboWarp connected; waiting for the first LLVM execution point.\n'});
            await this.bridge.request('start', {stopOnEntry: true, sourceEntry: true});
            let timer;
            try { await Promise.race([firstStop, new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('No source execution point reached within 60 seconds')), 60000); })]); }
            finally { clearTimeout(timer); }
            const launch = {
                name: a.name || 'Scratch LLVM', type: 'lldb-dap', request: 'launch',
                program: generated.elf, cwd, stopOnEntry: a.stopOnEntry !== false,
                initCommands: ['settings set target.load-script-from-symbol-file false'],
                launchCommands: ['target create ' + quoteCommand(generated.elf), 'gdb-remote 127.0.0.1:' + port],
                ...(a.sourceMap ? {sourceMap: a.sourceMap} : {})
            };
            this.event('output', {category: 'console', output: 'Connecting LLDB to the LLVM IR debug target.\n'});
            return await this.lldb().request('launch', launch);
        } catch (error) { this.cleanup(); throw error; }
    }
    cleanup() {
        if (this.cleaning) return; this.cleaning = true;
        this.abortLaunch?.(new Error('Debug session closed'));
        this.rsp?.close(); this.bridge?.close(); this.connection?.close();
    }
}

function main(args = process.argv.slice(2)) {
    if (args[0] === '--dap' || args.length === 0) {
        const options = {};
        for (let i = 1; i < args.length; i++) {
            if (args[i] === '--lldb-dap') options.lldbDap = args[++i];
            else if (args[i] === '--lldb-python') options.lldbPython = args[++i];
            else if (args[i] === '--no-debug') options.noDebug = true;
            else throw new Error('Unknown adapter argument: ' + args[i]);
        }
        const adapter = new LldbAdapter(process.stdin, process.stdout, options);
        const stop = () => { adapter.cleanup(); process.stdin.pause(); process.stdin.unref?.(); };
        process.once('SIGINT', stop); process.once('SIGTERM', stop);
        if (process.platform === 'win32') process.once('SIGBREAK', stop);
        return;
    }
    if (args[0] === '--run' || args[0] === '--help') {
        const child = spawn(process.execPath, [path.join(__dirname, 'llvmdbg.cjs'), ...args], {stdio: 'inherit', windowsHide: true});
        child.once('exit', code => { process.exitCode = code || 0; }); child.once('error', error => { console.error(error.message); process.exitCode = 1; }); return;
    }
    throw new Error('Use scratch-debug --dap or scratch-debug --run project.sb3');
}
if (require.main === module) { try { main(); } catch (error) { console.error(error.stack || error); process.exitCode = 1; } }
module.exports = {LldbAdapter, LldbConnection, findExecutable, supportedCapabilities, main};
