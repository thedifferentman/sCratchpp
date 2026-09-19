#!/usr/bin/env node
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');
const {upgrade} = require('./websocket.cjs');

function openExternal(target, executable) {
    return new Promise((resolve, reject) => {
        let command, args;
        if (executable) { command = executable; args = [target]; }
        else if (process.platform === 'win32') { command = 'rundll32.exe'; args = ['url.dll,FileProtocolHandler', target]; }
        else if (process.platform === 'darwin') { command = 'open'; args = [target]; }
        else { command = 'xdg-open'; args = [target]; }
        const child = spawn(command, args, {stdio: 'ignore', detached: true, windowsHide: true});
        child.once('error', reject); child.once('spawn', () => { child.unref(); resolve(); });
    });
}
function crc32(data) {
    let c = 0xffffffff;
    for (const byte of data) { c ^= byte; for (let k = 0; k < 8; k++) c = (c >>> 1) ^ (0xedb88320 & -(c & 1)); }
    return ((c ^ 0xffffffff) >>> 0).toString(16).padStart(8, '0');
}
function projectChecksum(zip) {
    // Read central-directory metadata; no ZIP library or decompression needed.
    let end = zip.length - 22;
    for (; end >= Math.max(0, zip.length - 65557); --end) if (zip.readUInt32LE(end) === 0x06054b50) break;
    if (end < Math.max(0, zip.length - 65557)) throw new Error('Invalid SB3 ZIP directory');
    let at = zip.readUInt32LE(end + 16), entries = zip.readUInt16LE(end + 10);
    for (let i = 0; i < entries; i++) {
        if (at + 46 > zip.length || zip.readUInt32LE(at) !== 0x02014b50) throw new Error('Invalid SB3 entry');
        const nameLength = zip.readUInt16LE(at + 28), extraLength = zip.readUInt16LE(at + 30), commentLength = zip.readUInt16LE(at + 32);
        if (zip.subarray(at + 46, at + 46 + nameLength).toString('utf8') === 'project.json') return zip.readUInt32LE(at + 16).toString(16).padStart(8, '0');
        at += 46 + nameLength + extraLength + commentLength;
    }
    throw new Error('SB3 has no project.json');
}

class Bridge {
    constructor(args, onEvent) {
        this.args = args; this.onEvent = onEvent; this.token = crypto.randomBytes(24).toString('hex');
        this.pending = new Map(); this.nextId = 1; this.closed = false;
        this.project = fs.readFileSync(path.resolve(args.project));
        this.map = args.noDebug ? null : JSON.parse(fs.readFileSync(path.resolve(args.debugMap || args.project.replace(/\.sb3$/i, '.debug.json')), 'utf8'));
        if (this.map && this.map.schemaVersion !== 1) throw new Error('Unsupported debug map schema');
        if (this.map && (!this.map.projectCrc32 || this.map.projectCrc32.toLowerCase() !== projectChecksum(this.project))) throw new Error('Debug map does not match project.sb3. Rebuild the Debug task.');
        this.ready = new Promise((resolve, reject) => { this.readyResolve = resolve; this.readyReject = reject; });
        this.ready.catch(() => {});
    }
    async listen() {
        let port = this.args.embeddedPlayer ? 0 : (this.args.port || 8000);
        if (!this.args.embeddedPlayer && port !== 8000 && !this.args.noOpen) throw new Error('Automatic unsandboxed TurboWarp loading requires localhost port 8000.');
        const allowedOrigins = new Set([new URL(this.args.turbowarpUrl || 'https://turbowarp.org/editor').origin, 'https://turbowarp.org', 'null']);
        this.server = http.createServer((req, res) => {
            const origin = req.headers.origin;
            if (origin && !allowedOrigins.has(origin)) { res.writeHead(403); res.end(); return; }
            if (origin) res.setHeader('Access-Control-Allow-Origin', origin);
            res.setHeader('Access-Control-Allow-Private-Network', 'true');
            res.setHeader('Access-Control-Allow-Methods', 'GET, OPTIONS');
            res.setHeader('Cache-Control', 'no-store');
            if (req.method === 'OPTIONS') { res.writeHead(204); res.end(); return; }
            if (req.method !== 'GET') { res.writeHead(405); res.end(); return; }
            const resource = req.url?.split('?')[0];
            const playerFiles = {'player.html':['index.html','text/html'], 'player.js':['player.js','application/javascript'],
                'player.css':['player.css','text/css'], 'pen-resolution.js':['pen-resolution.js','application/javascript'], 'scaffolding.js':['vendor/scaffolding-with-music.js','application/javascript']};
            const relative = resource?.startsWith(`/${this.token}/`) ? resource.slice(this.token.length+2) : '';
            if (this.args.embeddedPlayer && playerFiles[relative]) {
                const [file,type]=playerFiles[relative];
                res.setHeader('Content-Type',type);
                res.setHeader('Content-Security-Policy', "default-src 'none'; script-src 'self' 'unsafe-eval'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; media-src 'self' data: blob:; connect-src 'self' ws://127.0.0.1:"+port+" blob: data:; worker-src 'self' blob:;");
                res.end(fs.readFileSync(path.join(__dirname,'player',file))); return;
            }
            if (this.args.embeddedPlayer && relative==='settings.json') {
                res.setHeader('Content-Type','application/json');
                res.end(JSON.stringify({noDebug:!!this.args.noDebug}));return;
            }
            if (this.args.embeddedPlayer && relative==='engine.js') {
                res.setHeader('Content-Type','application/javascript');res.end(fs.readFileSync(path.join(__dirname,'engine.js')));return;
            }
            if (resource === `/${this.token}/project.sb3`) { res.setHeader('Content-Type', 'application/octet-stream'); res.end(this.project); }
            else if (resource === `/${this.token}/debug.json`) { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(this.map)); }
            else if (resource === `/${this.token}/bridge.js`) {
                res.setHeader('Content-Type', 'application/javascript');
                const settings = {base: `http://localhost:${port}/${this.token}`, socket: `ws://localhost:${port}/${this.token}/socket`, noDebug: !!this.args.noDebug};
                const engine = this.args.noDebug ? '' : fs.readFileSync(path.join(__dirname, 'engine.js'), 'utf8');
                res.end(engine + '\n;(' + browserBridge.toString() + ')(Scratch, ' + JSON.stringify(settings) + ', ' + waitForCostumeImages.toString() + ');\n');
            } else { res.writeHead(404); res.end('Not found'); }
        });
        this.server.on('upgrade', (req, socket, head) => {
            if (req.url !== `/${this.token}/socket` || (req.headers.origin && !allowedOrigins.has(req.headers.origin)) || this.peer) { socket.end('HTTP/1.1 403 Forbidden\r\n\r\n'); return; }
            this.peer = upgrade(req, socket, head); if (!this.peer) return;
            this.peer.on('message', msg => {
                if (msg.event === 'ready') { clearTimeout(this.timer); this.readyResolve(); }
                else if (msg.event === 'error') { const error = new Error(msg.body?.message || 'TurboWarp bridge failed'); this.readyReject(error); this.onEvent({event: 'output', body: {category: 'stderr', output: error.message + '\n'}}); }
                else if (msg.event) this.onEvent(msg);
                else if (msg.id && this.pending.has(msg.id)) { const p = this.pending.get(msg.id); this.pending.delete(msg.id); clearTimeout(p.timer); msg.error ? p.reject(new Error(msg.error)) : p.resolve(msg.result); }
            });
            this.peer.on('fault', e => this.onEvent({event: 'output', body: {category: 'stderr', output: e.message + '\n'}}));
            this.peer.on('close', () => { if (!this.closed) { this.readyReject(new Error('TurboWarp disconnected')); this.onEvent({event: 'terminated', body: {}}); this.close(); } });
        });
        await new Promise((resolve, reject) => { this.server.once('error', reject); this.server.listen(port, '127.0.0.1', resolve); });
        port=this.server.address().port;
        if (this.args.embeddedPlayer) {
            allowedOrigins.add(`http://127.0.0.1:${port}`);
            this.url=`http://127.0.0.1:${port}/${this.token}/player.html`;
            this.timer=setTimeout(()=>this.readyReject(new Error('VS Code player did not connect.')),this.args.connectTimeout||120000);
            this.onEvent({event:'scrppPlayer',body:{url:this.url}});
            return;
        }
        this.timer = setTimeout(() => this.readyReject(new Error('TurboWarp did not connect. Allow the local debugging extension and localhost network access in the browser.')), this.args.connectTimeout || 120000);
        const url = new URL(this.args.turbowarpUrl || 'https://turbowarp.org/editor');
        if (!this.args.noDebug) url.searchParams.set('nocompile', '');
        url.searchParams.set('extension', `http://localhost:${port}/${this.token}/bridge.js`);
        this.url = url.href;
        this.onEvent({event: 'output', body: {category: 'console', output: `Opening TurboWarp: ${this.url}\n`}});
        if (!this.args.noOpen) await openExternal(this.url);
    }
    request(method, ...args) {
        if (!this.peer || this.closed) return Promise.reject(new Error('TurboWarp is not connected'));
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(`TurboWarp request timed out: ${method}`)); }, 15000);
            this.pending.set(id, {resolve, reject, timer}); this.peer.send({id, method, args});
        });
    }
    close() {
        if (this.closed) return; this.closed = true; clearTimeout(this.timer);
        this.readyReject(new Error('Debug session closed'));
        for (const p of this.pending.values()) { clearTimeout(p.timer); p.reject(new Error('Debug session closed')); }
        this.pending.clear(); this.peer?.close(); this.server?.close(); this.server?.closeAllConnections();
    }
}

async function waitForCostumeImages(vm, timeout = 30000) {
    // loadProject resolves after SVG skin creation, before its browser image
    // has necessarily decoded. Starting a fast warp script here can stamp an
    // empty texture permanently. Wait for known asynchronous SVG skins first.
    const skins = vm.runtime.renderer?._allSkins;
    if (!skins) return;
    const pending = new Set();
    for (const target of vm.runtime.targets) for (const costume of target.getCostumes()) {
        const skin = skins[costume.skinId];
        if (skin?._svgImage && skin._svgImageLoaded === false) pending.add(skin);
    }
    await Promise.all([...pending].map(skin => new Promise((resolve, reject) => {
        const image = skin._svgImage;
        const finish = error => {
            clearTimeout(timer); image.removeEventListener('load', loaded); image.removeEventListener('error', failed);
            error ? reject(error) : resolve();
        };
        const loaded = () => finish();
        const failed = () => finish(new Error('A costume image could not be decoded.'));
        const timer = setTimeout(() => finish(new Error('Timed out loading costume images.')), timeout);
        image.addEventListener('load', loaded); image.addEventListener('error', failed);
        if (skin._svgImageLoaded) finish();
    })));
}

function browserBridge(Scratch, settings, waitForCostumeImages) {
    if (!Scratch.extensions.unsandboxed || !Scratch.vm) throw new Error('LLVM debugger must load unsandboxed from http://localhost:8000/.');
    Scratch.extensions.register({getInfo: () => ({id: 'scratchllvmdebug', name: 'LLVM Debugger', blocks: []})});
    const socket = new WebSocket(settings.socket); let engine;
    const send = value => { if (socket.readyState === WebSocket.OPEN) socket.send(JSON.stringify(value)); };
    socket.addEventListener('open', async () => {
        try {
            const vm = Scratch.vm;
            vm.stopAll();
            if (!settings.noDebug) {
                if (typeof vm.setCompilerOptions !== 'function') throw new Error('TurboWarp compiler controls unavailable');
                vm.setCompilerOptions({enabled: false});
            }
            const [project, map] = await Promise.all([
                fetch(settings.base + '/project.sb3').then(r => { if (!r.ok) throw new Error('Project download failed'); return r.arrayBuffer(); }),
                settings.noDebug ? null : fetch(settings.base + '/debug.json').then(r => { if (!r.ok) throw new Error('Debug map download failed'); return r.json(); })
            ]);
            await vm.loadProject(project);
            await waitForCostumeImages(vm);
            if (!settings.noDebug) engine = globalThis.ScratchLLVMEngine.create(vm, map, send);
            send({event: 'ready'});
            if (settings.noDebug) vm.greenFlag();
        } catch (error) { send({event: 'error', body: {message: error.stack || String(error)}}); }
    });
    socket.addEventListener('message', async event => {
        let message;
        try {
            message = JSON.parse(event.data);
            const allowed = ['getIRState', 'setIRBreakpoints', 'stepInstruction', 'terminate', 'setBreakpoints', 'start', 'continue', 'pause', 'next', 'stepIn', 'stepOut', 'stackTrace', 'scopes', 'variables', 'readMemory', 'evaluate', 'dispose'];
            if (!allowed.includes(message.method) || !engine || typeof engine[message.method] !== 'function') throw new Error('Unsupported debugger operation: ' + message.method);
            const result = await engine[message.method](...(message.args || [])); send({id: message.id, result});
        } catch (error) { send({id: message?.id, error: error.message || String(error)}); }
    });
    socket.addEventListener('close', () => { if (engine) { Scratch.vm.stopAll(); engine.dispose(); } });
}

class Adapter {
    constructor(input = process.stdin, output = process.stdout) {
        this.output = output; this.seq = 1; this.buffer = Buffer.alloc(0); this.breakpoints = new Map(); this.configured = false;
        input.on('data', data => this.read(data)); input.on('end', () => this.bridge?.close());
    }
    send(message) { const body = Buffer.from(JSON.stringify({seq: this.seq++, ...message})); this.output.write(`Content-Length: ${body.length}\r\n\r\n`); this.output.write(body); }
    event(event, body = {}) { this.send({type: 'event', event, body}); }
    response(request, body, error) { this.send({type: 'response', request_seq: request.seq, command: request.command, success: !error, ...(error ? {message: error.message || String(error)} : {body: body || {}})}); }
    read(data) {
        this.buffer = Buffer.concat([this.buffer, data]);
        while (true) {
            const end = this.buffer.indexOf('\r\n\r\n'); if (end < 0) return;
            const match = /(?:^|\r\n)Content-Length:\s*(\d+)/i.exec(this.buffer.subarray(0, end).toString('ascii'));
            if (!match || Number(match[1]) > 16777216) { this.bridge?.close(); this.buffer = Buffer.alloc(0); return; }
            const size = Number(match[1]); if (this.buffer.length < end + 4 + size) return;
            const raw = this.buffer.subarray(end + 4, end + 4 + size); this.buffer = this.buffer.subarray(end + 4 + size);
            try { const request = JSON.parse(raw.toString('utf8')); if (request.type === 'request') this.handle(request).then(body => this.response(request, body), error => this.response(request, null, error)); }
            catch (error) { this.event('output', {category: 'stderr', output: error.message + '\n'}); }
        }
    }
    async handle(req) {
        const a = req.arguments || {};
        switch (req.command) {
        case 'initialize': return {supportsConfigurationDoneRequest: true, supportsTerminateRequest: true, supportsReadMemoryRequest: true, supportsVariablePaging: true, supportsEvaluateForHovers: false, supportsRestartRequest: false};
        case 'launch': {
            if (!a.project) throw new Error('launch.project is required');
            this.args = a;
            if (a.noDebug && a.turbowarp) { await openExternal(path.resolve(a.project), a.turbowarp); this.event('initialized'); setImmediate(() => this.event('terminated')); return {}; }
            this.bridge = new Bridge(a, message => this.event(message.event, message.body));
            try {
                await this.bridge.listen(); this.event('initialized'); await this.bridge.ready;
                for (const [source, lines] of this.breakpoints) await this.bridge.request('setBreakpoints', source, lines);
                this.ready = true;
                if (a.noDebug) {
                    // Embedded stages stay connected and visible until VS Code stops
                    // the session or the user closes the panel, including after main returns.
                    if (!a.embeddedPlayer) { this.bridge.close(); setImmediate(() => this.event('terminated')); }
                    return {};
                }
                if (this.configured) await this.start();
                return {};
            } catch (error) { this.bridge.close(); throw error; }
        }
        case 'setBreakpoints': {
            const source = a.source?.path; if (!source) throw new Error('Breakpoint source.path is required');
            if ((a.breakpoints || []).some(b => b.condition || b.hitCondition || b.logMessage)) throw new Error('Conditional, hit-count and log breakpoints are not supported yet');
            const lines = (a.breakpoints || []).map(b => b.line); this.breakpoints.set(source, lines);
            const breakpoints = this.ready ? await this.bridge.request('setBreakpoints', source, lines) : lines.map(line => ({verified: false, line, message: 'Waiting for TurboWarp'}));
            return {breakpoints};
        }
        case 'configurationDone': this.configured = true; if (this.ready && !this.args.noDebug) await this.start(); return {};
        case 'threads': return {threads: [{id: 1, name: 'Scratch LLVM program'}]};
        case 'stackTrace': { const frames = await this.call('stackTrace'); return {stackFrames: frames.slice(a.startFrame || 0, a.levels ? (a.startFrame || 0) + a.levels : undefined), totalFrames: frames.length}; }
        case 'scopes': return {scopes: await this.call('scopes', a.frameId)};
        case 'variables': return {variables: await this.call('variables', a.variablesReference, {start: a.start, count: a.count, filter: a.filter})};
        case 'readMemory': return await this.call('readMemory', a.memoryReference, a.offset || 0, a.count);
        case 'evaluate': return await this.call('evaluate', a.expression, a.frameId);
        case 'continue': await this.call('continue'); return {allThreadsContinued: true};
        case 'pause': case 'next': case 'stepIn': case 'stepOut': await this.call(req.command); return {};
        case 'disconnect': case 'terminate':
            if (this.bridge && this.ready && !this.args.noDebug) { try { await this.bridge.request('dispose'); } catch {} }
            this.bridge?.close(); this.event('terminated'); return {};
        case 'setExceptionBreakpoints': return {breakpoints: []};
        case 'loadedSources': return {sources: [...new Set(Object.values(this.bridge?.map?.points || {}).map(p => p.file).filter(Boolean))].map(file => ({path: file, name: path.basename(file)}))};
        default: throw new Error('Unsupported DAP request: ' + req.command);
        }
    }
    async start() {
        if (this.started) return; this.started = true;
        for (const [source, lines] of this.breakpoints) {
            const results = await this.bridge.request('setBreakpoints', source, lines);
            for (const breakpoint of results) this.event('breakpoint', {reason: 'changed', breakpoint});
        }
        await this.bridge.request('start', {stopOnEntry: this.args.stopOnEntry !== false});
    }
    call(method, ...args) { if (!this.ready) throw new Error('TurboWarp is not ready'); return this.bridge.request(method, ...args); }
}

async function main(args) {
    if (!args.length || args[0] === '--dap') { new Adapter(); return; }
    if (args[0] === '--help') { console.log('llvmdbg --dap\nllvmdbg --run project.sb3 [--turbowarp executable]\nllvmdbg --debug project.sb3 [--map project.debug.json] [--no-open]'); return; }
    if (!['--run', '--debug'].includes(args[0]) || !args[1]) throw new Error('Use llvmdbg --help for usage');
    const options = {project: args[1], noDebug: args[0] === '--run'};
    for (let i = 2; i < args.length; i++) {
        if (args[i] === '--turbowarp') options.turbowarp = args[++i];
        else if (args[i] === '--map') options.debugMap = args[++i];
        else if (args[i] === '--no-open') options.noOpen = true;
        else if (args[i] === '--connect-timeout') {
            options.connectTimeout = Number(args[++i]);
            if (!Number.isSafeInteger(options.connectTimeout) || options.connectTimeout <= 0)
                throw new Error('--connect-timeout must be a positive integer');
        }
        else throw new Error('Unknown option: ' + args[i]);
    }
    if (options.noDebug && options.turbowarp) return openExternal(path.resolve(options.project), options.turbowarp);
    const bridge = new Bridge(options, msg => { if (msg.event === 'output') process.stderr.write(msg.body.output); else process.stderr.write(JSON.stringify(msg) + '\n'); });
    const stop = () => { bridge.close(); process.stdin.pause(); process.stdin.unref?.(); };
    process.once('SIGINT', stop); process.once('SIGTERM', stop);
    if (process.platform === 'win32') process.once('SIGBREAK', stop);
    try {
        await bridge.listen(); await bridge.ready;
        if (!options.noDebug) { console.error('Standalone debug smoke mode: stopping at entry. Use the VS Code launch configuration for interactive controls.'); await bridge.request('start', {stopOnEntry: true}); }
    } catch (error) { bridge.close(); throw error; }
}
if (require.main === module) main(process.argv.slice(2)).catch(error => { console.error(error.stack || error); process.exitCode = 1; });
module.exports = {Adapter, Bridge, openExternal, projectChecksum, crc32, browserBridge, waitForCostumeImages};
