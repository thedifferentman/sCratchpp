#!/usr/bin/env node
'use strict';

// Human-readable terminal UI. The same real LLDB adapter used by VS Code owns
// source symbols, RSP and the TurboWarp bridge; no DAP framing reaches stdout.
const fs = require('node:fs');
const path = require('node:path');
const readline = require('node:readline');
const {PassThrough} = require('node:stream');
const {EventEmitter} = require('node:events');
const {LldbAdapter} = require('./scratch-debug.cjs');

const HELP = `Commands:
  break file:line       Set a source breakpoint (Windows drive paths supported)
  delete file:line      Remove a breakpoint; clear without arguments removes all
  continue | c         Continue execution
  next | n             Step over one source statement
  step | s             Step into a source call
  finish | out         Step out of the current function
  pause                Pause a running program
  bt                   Show the source call stack
  locals               Show variables in the current frame
  print expression     Read a C expression (no calls, assignments or LLDB commands)
  help                 Show this help
  quit                 Close LLDB and the TurboWarp debug connection
`;

function parseArgs(args) {
    const options = {commands: [], connectTimeout: 120000};
    const fields = {'--project': 'project', '--map': 'debugMap', '--clang': 'clang',
        '--lldb-dap': 'lldbDap', '--lldb-python': 'lldbPython'};
    for (let i = 0; i < args.length; ++i) {
        const arg = args[i];
        if (arg === '--help' || arg === '-h') options.help = true;
        else if (arg === '--no-open') options.noOpen = true;
        else if (arg in fields || arg === '--command' || arg === '--connect-timeout' || arg === '--port') {
            if (i + 1 >= args.length) throw new Error(`Missing value for ${arg}`);
            const value = args[++i];
            if (arg === '--command') options.commands.push(value);
            else if (arg === '--connect-timeout' || arg === '--port') {
                const number = Number(value);
                if (!Number.isSafeInteger(number) || number < 1 || (arg === '--port' && number > 65535))
                    throw new Error(`Invalid ${arg}: ${value}`);
                options[arg === '--port' ? 'port' : 'connectTimeout'] = number;
            } else options[fields[arg]] = value;
        } else throw new Error(`Unknown argument: ${arg}`);
    }
    if (!options.help && !options.project) throw new Error('--project is required');
    if (options.project) options.project = path.resolve(options.project);
    if (options.debugMap) options.debugMap = path.resolve(options.debugMap);
    return options;
}

function parseLocation(text) {
    const match = /^(.*):([1-9]\d*)$/.exec(text.trim());
    if (!match || !match[1].trim()) throw new Error('Expected a source location: file:line');
    const filename = match[1].trim().replace(/^"(.*)"$/, '$1');
    const line = Number(match[2]);
    if (!Number.isSafeInteger(line)) throw new Error('Source line is too large');
    return {path: /^[A-Za-z]:[\\/]/.test(filename) ? path.win32.normalize(filename) : path.resolve(filename), line};
}

// Validate a deliberately small side-effect-free expression language before
// passing it to LLDB's watch evaluator. In particular, never expose its REPL
// command prefix or arbitrary host commands through `print`.
function validateExpression(expression) {
    if (!expression || /\+\+|--/.test(expression)) throw new Error('Expected a read-only C expression');
    const pattern = /\s*(0[xX][\da-fA-F]+|(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?|[A-Za-z_]\w*|::|->|<<|>>|<=|>=|==|!=|&&|\|\||[()[\].+*/%&|^~!<>?:-])/y;
    const tokens = [];
    let offset = 0;
    while (offset < expression.length) {
        if (!expression.slice(offset).trim()) break;
        pattern.lastIndex = offset;
        const token = pattern.exec(expression);
        if (!token) throw new Error('Only read-only C expressions are allowed');
        tokens.push(token[1]); offset = pattern.lastIndex;
    }
    let cursor = 0;
    const peek = () => tokens[cursor];
    const take = token => { if (peek() !== token) throw new Error('Invalid read-only expression'); ++cursor; };
    const identifier = () => { if (!/^[A-Za-z_]\w*$/.test(peek() || '')) throw new Error('Expected an identifier'); ++cursor; };
    const priority = {'||': 1, '&&': 2, '|': 3, '^': 4, '&': 5, '==': 6, '!=': 6,
        '<': 7, '>': 7, '<=': 7, '>=': 7, '<<': 8, '>>': 8, '+': 9, '-': 9, '*': 10, '/': 10, '%': 10};
    function atom() {
        if (['+', '-', '!', '~', '*', '&'].includes(peek())) { ++cursor; atom(); return; }
        if (peek() === '(') { ++cursor; expr(0); take(')'); }
        else if (/^(?:\d|\.)/.test(peek() || '')) ++cursor;
        else { identifier(); while (peek() === '::') { ++cursor; identifier(); } }
        while (true) {
            if (peek() === '[') { ++cursor; expr(0); take(']'); }
            else if (peek() === '.' || peek() === '->') { ++cursor; identifier(); }
            else break;
        }
    }
    function expr(minimum) {
        atom();
        while (priority[peek()] >= minimum) { const p = priority[tokens[cursor++]]; expr(p + 1); }
        if (minimum === 0 && peek() === '?') { ++cursor; expr(0); take(':'); expr(0); }
    }
    expr(0);
    if (cursor !== tokens.length) throw new Error('Function calls, casts and assignments are not supported');
    return expression;
}

class TerminalDebugger extends EventEmitter {
    constructor(options, {input = process.stdin, output = process.stdout, adapterFactory} = {}) {
        super();
        this.options = {connectTimeout: 120000, ...options}; this.input = input; this.output = output;
        this.state = 'connecting'; this.threadId = 1; this.breakpoints = new Map();
        this.stopSerial = 0; this.waiters = new Set(); this.commandQueue = Promise.resolve();
        this.eventWork = Promise.resolve(); this.closed = false; this.ready = false;
        this.dapInput = new PassThrough(); this.dapOutput = new PassThrough();
        this.dapOutput.resume();
        this.adapter = (adapterFactory || ((i, o, opt) => new LldbAdapter(i, o, opt)))(this.dapInput, this.dapOutput, options);
        this.adapter.event = (event, body = {}) => this.onEvent(event, body);
        this.done = new Promise(resolve => { this.finish = resolve; });
    }
    write(text) { this.output.write(text); }
    prompt() { if (this.rl && !this.closed) this.rl.prompt(); }
    request(command, args = {}) {
        if (this.closed) return Promise.reject(new Error('Debug session closed'));
        return this.adapter.handle({seq: 1, type: 'request', command, arguments: args});
    }
    waitFor(predicate, timeout = 120000) {
        if (predicate()) return Promise.resolve();
        if (this.closed) return Promise.reject(new Error('Debug session closed'));
        return new Promise((resolve, reject) => {
            const waiter = {check: () => { if (predicate()) finish(); }, reject: error => finish(error)};
            const finish = error => { clearTimeout(timer); this.waiters.delete(waiter); error ? reject(error) : resolve(); };
            const timer = setTimeout(() => finish(new Error('Timed out waiting for the debug target')), timeout);
            this.waiters.add(waiter);
        });
    }
    onEvent(event, body) {
        if (this.closed) return;
        if (event === 'output') this.write(body.output || '');
        else if (event === 'initialized') this.initialized = true;
        else if (event === 'continued') this.state = 'running';
        else if (event === 'stopped') {
            this.state = 'stopped'; this.threadId = body.threadId || this.threadId; ++this.stopSerial;
            this.eventWork = this.eventWork.then(() => this.showStop(body)).catch(error => {
                if (!this.closed) this.write(`Cannot read stopped location: ${error.message}\n`);
            });
        } else if (event === 'exited') this.write(`Program exited with code ${body.exitCode}.\n`);
        else if (event === 'terminated') {
            this.state = 'terminated'; this.write('Debug session ended.\n');
            // Notify waiters before closing so a final continue may complete.
            for (const waiter of [...this.waiters]) waiter.check();
            this.close();
        }
        for (const waiter of [...this.waiters]) waiter.check();
        this.emit('debugEvent', {event, body});
    }
    async showStop(body) {
        if (this.closed) return;
        const trace = await this.request('stackTrace', {threadId: this.threadId, startFrame: 0, levels: 1});
        const frame = trace.stackFrames?.[0]; this.frame = frame;
        this.write(`Stopped (${body.reason || 'pause'})${frame ? `: ${frame.name} at ${frame.source?.path || '?'}:${frame.line}` : ''}\n`);
        if (frame?.source?.path && frame.line > 0) {
            try {
                const source = fs.readFileSync(frame.source.path, 'utf8').split(/\r?\n/)[frame.line - 1];
                if (source !== undefined) this.write(`  ${frame.line} | ${source.trimEnd()}\n`);
            } catch {}
        }
        this.prompt();
    }
    async start() {
        this.write('sCr++ terminal debugger (LLDB). Type help for commands; quit also works while connecting.\n');
        if (!(this.options.commands || []).length) {
            this.rl = readline.createInterface({input: this.input, output: this.output,
                terminal: Boolean(this.input.isTTY && this.output.isTTY), prompt: 'scrate> '});
            this.rl.on('line', line => this.submit(line));
            this.rl.once('close', () => this.close());
            this.rl.on('SIGINT', () => this.close());
            this.prompt();
        }
        try {
            await this.request('initialize', {adapterID: 'scratch-llvm', clientID: 'scrate-cli',
                pathFormat: 'path', linesStartAt1: true, columnsStartAt1: true,
                supportsVariableType: true, supportsVariablePaging: true});
            if (this.closed) return;
            const launch = this.request('launch', {...this.options, stopOnEntry: true});
            // Attach a rejection handler immediately: cancellation can happen
            // while waiting for the initialized notification.
            launch.catch(error => { if (!this.closed) this.fail(error); });
            await this.waitFor(() => this.initialized, this.options.connectTimeout + 120000);
            for (const source of this.breakpoints.keys()) await this.applyBreakpoints(source);
            await this.request('configurationDone');
            await launch;
            this.ready = true;
            await this.waitFor(() => this.stopSerial > 0 || this.state === 'terminated');
            await this.eventWork;
            if (this.closed) return;
            this.write('Ready.\n'); this.prompt(); this.emit('ready');
            if (this.options.commands?.length) {
                for (const command of this.options.commands) {
                    if (this.closed) break;
                    this.write(`scrate> ${command}\n`);
                    await this.execute(command, {waitForStop: true});
                }
                this.close();
            }
        } catch (error) { if (!this.closed) this.fail(error); }
    }
    submit(line) {
        if (/^(?:quit|q|exit)\s*$/.test(line.trim())) { this.close(); return; }
        this.commandQueue = this.commandQueue.then(() => this.execute(line)).catch(error => {
            if (!this.closed) { this.write(`Error: ${error.message}\n`); this.prompt(); }
        });
    }
    async applyBreakpoints(source) {
        if (!this.initialized) return;
        const lines = [...(this.breakpoints.get(source) || [])].sort((a, b) => a - b);
        const result = await this.request('setBreakpoints', {source: {path: source}, breakpoints: lines.map(line => ({line}))});
        for (let i = 0; i < (result.breakpoints || []).length; ++i) {
            const bp = result.breakpoints[i];
            this.write(`Breakpoint ${bp.verified ? 'set' : 'pending'}: ${source}:${bp.line || lines[i]}${bp.message ? ` (${bp.message})` : ''}\n`);
        }
    }
    requireStopped() {
        if (!this.ready) throw new Error('Still connecting; help, break, clear and quit are available');
        if (this.state !== 'stopped') throw new Error('Program is running; use pause first');
    }
    async execute(line, {waitForStop = false} = {}) {
        if (this.closed || !line.trim()) return;
        const match = /^(\S+)(?:\s+([\s\S]*))?$/.exec(line.trim());
        const command = match[1].toLowerCase(), arg = match[2] || '';
        if (['quit', 'q', 'exit'].includes(command)) { this.close(); return; }
        if (command === 'help' || command === '?') { this.write(HELP); this.prompt(); return; }
        if (['break', 'b', 'delete', 'clear'].includes(command)) {
            if (command === 'clear' && !arg) {
                for (const source of this.breakpoints.keys()) {
                    this.breakpoints.set(source, new Set()); await this.applyBreakpoints(source);
                }
                this.write('All breakpoints cleared.\n');
            } else {
                const location = parseLocation(arg), lines = this.breakpoints.get(location.path) || new Set();
                if (command === 'break' || command === 'b') lines.add(location.line); else lines.delete(location.line);
                this.breakpoints.set(location.path, lines);
                await this.applyBreakpoints(location.path);
                if (!this.initialized) this.write(`Breakpoint queued: ${location.path}:${location.line}\n`);
                else if (command === 'delete' || command === 'clear') this.write(`Breakpoint removed: ${location.path}:${location.line}\n`);
            }
            this.prompt(); return;
        }
        const controls = {continue: 'continue', c: 'continue', next: 'next', n: 'next',
            step: 'stepIn', s: 'stepIn', finish: 'stepOut', out: 'stepOut', pause: 'pause'};
        if (command in controls) {
            if (!this.ready) throw new Error('Still connecting; use quit to cancel');
            if (command !== 'pause') this.requireStopped();
            else if (this.state === 'stopped') { this.write('Already stopped.\n'); return; }
            const before = this.stopSerial;
            const previousState = this.state;
            if (command !== 'pause') this.state = 'running';
            try { await this.request(controls[command], {threadId: this.threadId}); }
            catch (error) { if (this.stopSerial === before) this.state = previousState; throw error; }
            if (waitForStop) {
                await this.waitFor(() => this.stopSerial > before || this.state === 'terminated');
                await this.eventWork;
            }
            this.prompt(); return;
        }
        this.requireStopped();
        await this.eventWork;
        const trace = await this.request('stackTrace', {threadId: this.threadId});
        const frame = trace.stackFrames?.[0]; this.frame = frame;
        if (!frame) throw new Error('No source frame is available');
        if (command === 'bt') {
            for (const [index, item] of (trace.stackFrames || []).entries())
                this.write(`#${index} ${item.name} at ${item.source?.path || '?'}:${item.line}\n`);
        } else if (command === 'locals') {
            const scopes = await this.request('scopes', {frameId: frame.id});
            const locals = (scopes.scopes || []).filter(scope => /local/i.test(scope.name) || scope.presentationHint === 'locals');
            for (const scope of locals) {
                const variables = await this.request('variables', {variablesReference: scope.variablesReference, start: 0, count: 100});
                for (const variable of variables.variables || []) this.write(`${variable.name} = ${variable.value}${variable.type ? ` (${variable.type})` : ''}\n`);
            }
            if (!locals.length) this.write('No local-variable scope available.\n');
        } else if (command === 'print' || command === 'p') {
            validateExpression(arg);
            const value = await this.request('evaluate', {expression: arg, frameId: frame.id, context: 'watch'});
            this.write(`${arg} = ${value.result}${value.type ? ` (${value.type})` : ''}\n`);
        } else throw new Error(`Unknown command: ${command}. Type help.`);
        this.prompt();
    }
    fail(error) { this.write(`Debugger error: ${error.message}\n`); this.close(1); }
    close(code = 0) {
        if (this.closed) return;
        this.closed = true;
        for (const waiter of [...this.waiters]) waiter.reject(new Error('Debug session closed'));
        this.adapter.cleanup(); this.dapInput.end(); this.dapOutput.end();
        this.rl?.close(); this.finish(code); this.emit('closed', code);
    }
}

async function main(args = process.argv.slice(2), io = {}) {
    const options = parseArgs(args);
    if (options.help) {
        (io.output || process.stdout).write('Usage: node cli.cjs --project project.sb3 --map project.debug.json [--no-open]\n' + HELP);
        return 0;
    }
    const session = new TerminalDebugger(options, io);
    const interrupt = () => session.close();
    const signals = process.platform === 'win32' ? ['SIGINT', 'SIGTERM', 'SIGBREAK'] : ['SIGINT', 'SIGTERM'];
    for (const signal of signals) process.on(signal, interrupt);
    session.start().catch(error => session.fail(error));
    try { return await session.done; }
    finally { for (const signal of signals) process.removeListener(signal, interrupt); }
}

module.exports = {TerminalDebugger, parseArgs, parseLocation, validateExpression, main, HELP};
if (require.main === module) main().then(code => { process.exitCode = code; }, error => {
    console.error(`Debugger error: ${error.message}`); process.exitCode = 1;
});
