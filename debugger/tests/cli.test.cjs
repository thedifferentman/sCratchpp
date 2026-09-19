'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {test} = require('node:test');
const {PassThrough} = require('node:stream');
const {once} = require('node:events');
const {spawnSync} = require('node:child_process');
const {TerminalDebugger, parseArgs, parseLocation, validateExpression} = require('../cli.cjs');

test('argument and Windows-drive source-location parsing', () => {
    const args = parseArgs(['--project', 'p.sb3', '--map', 'p.json', '--no-open', '--connect-timeout', '250',
        '--lldb-dap', 'lldb-dap', '--command', 'help', '--command', 'quit']);
    assert.equal(args.noOpen, true); assert.equal(args.connectTimeout, 250);
    assert.deepEqual(args.commands, ['help', 'quit']);
    assert.deepEqual(parseLocation('C:\\my sources\\main.cpp:123'), {path: 'C:\\my sources\\main.cpp', line: 123});
    assert.throws(() => parseLocation('a.cpp:0'));
    assert.throws(() => parseArgs(['--project']));
    assert.throws(() => parseArgs(['--project', 'p', '--connect-timeout', '-1']));
});

test('print accepts read-only expressions and rejects commands or side effects', () => {
    for (const expr of ['x', 'sum + 2', '(x + 1) * 4', '*ptr', 'p->value', 'a[i + 1]',
        'ns::value', 'a > 2 ? a : b', 'x == 3', '0x10 | 2', '!flag'])
        assert.equal(validateExpression(expr), expr);
    for (const expr of ['', 'x=2', '++x', 'x--', 'f()', 'a.method()', '(int)x',
        'platform shell echo unsafe', '`echo unsafe`', ';quit', 'x; system("x")', 'command script import x'])
        assert.throws(() => validateExpression(expr), expr);
});

function fakeSession({connecting = false, commands = []} = {}) {
    const input = new PassThrough(), output = new PassThrough();
    let text = '', adapter;
    output.on('data', data => { text += data; });
    const factory = () => {
        adapter = {
            requests: [], cleanupCount: 0, frame: {id: 10, name: 'main', source: {path: 'main.cpp'}, line: 1},
            cleanup() { ++this.cleanupCount; this.launchResolve?.({}); },
            async handle(req) {
                this.requests.push(req);
                switch (req.command) {
                case 'initialize': return {};
                case 'launch':
                    if (!connecting) setImmediate(() => this.event('initialized'));
                    return new Promise(resolve => { this.launchResolve = resolve; });
                case 'configurationDone': this.launchResolve({}); this.event('stopped', {threadId: 1, reason: 'entry'}); return {};
                case 'setBreakpoints': return {breakpoints: req.arguments.breakpoints.map(bp => ({...bp, verified: true}))};
                case 'stackTrace': return {stackFrames: [this.frame]};
                case 'scopes': return {scopes: [{name: 'Locals', variablesReference: 1}]};
                case 'variables': return {variables: [{name: 'x', value: '3', type: 'int'}]};
                case 'evaluate': return {result: '4', type: 'int'};
                case 'continue': case 'next': case 'stepIn': case 'stepOut': case 'pause':
                    this.frame = {...this.frame, line: this.frame.line + 1};
                    setImmediate(() => this.event('stopped', {threadId: 1, reason: 'step'})); return {};
                default: throw new Error('Unexpected request ' + req.command);
                }
            }
        };
        return adapter;
    };
    const session = new TerminalDebugger({project: 'p.sb3', connectTimeout: 1000, commands}, {input, output, adapterFactory: factory});
    return {session, input, output, get adapter() { return adapter; }, text: () => text};
}

test('terminal commands use the adapter without emitting DAP frames', async () => {
    const item = fakeSession({commands: ['break C:\\src\\main.cpp:3', 'continue', 'bt', 'locals',
        'print x + 1', 'next', 'step', 'finish', 'delete C:\\src\\main.cpp:3', 'clear', 'quit']});
    await item.session.start(); assert.equal(await item.session.done, 0);
    const requests = item.adapter.requests;
    assert(requests.findIndex(r => r.command === 'launch') < requests.findIndex(r => r.command === 'configurationDone'));
    assert.equal(requests.find(r => r.command === 'evaluate').arguments.context, 'watch');
    assert.match(item.text(), /Stopped/); assert.match(item.text(), /x = 3/);
    assert.match(item.text(), /x \+ 1 = 4/); assert.match(item.text(), /Breakpoint removed/);
    assert.doesNotMatch(item.text(), /Content-Length:/); assert.equal(item.adapter.cleanupCount, 1);
});

test('quit and stdin EOF close a pending connection immediately', async () => {
    for (const quit of [true, false]) {
        const item = fakeSession({connecting: true});
        const start = item.session.start();
        await new Promise(resolve => setImmediate(resolve));
        if (quit) item.input.write('quit\n'); else item.input.end();
        assert.equal(await item.session.done, 0); await start;
        assert.equal(item.adapter.cleanupCount, 1);
    }
});

test('pause is available while running and errors do not destroy the session', async () => {
    const item = fakeSession(); await item.session.start();
    item.session.state = 'running';
    await item.session.execute('pause', {waitForStop: true});
    assert.equal(item.session.state, 'stopped');
    item.input.write('print x = 4\n'); await item.session.commandQueue;
    assert.match(item.text(), /Error:/); assert.equal(item.session.closed, false);
    assert.equal(item.adapter.requests.filter(r => r.command === 'evaluate').length, 0);
    item.session.close();
});

test('real LLDB/TurboWarp terminal: breakpoint, locals, print, step in/out/over, continue to exit',
    {skip: process.env.SCRATCH_CLI_E2E !== '1', timeout: 120000}, async () => {
    const root = path.resolve(__dirname, '../..');
    const directory = path.join(root, 'build/validation/debugger-cli');
    fs.mkdirSync(directory, {recursive: true});
    const source = path.join(root, 'tests/fixtures/debugger_program.cpp');
    const clang = process.env.SCRATCH_CLANG || path.join(root, 'build/toolchains/windows/root/clang64/bin/clang.exe');
    const compiler = process.env.SCRATCH_COMPILER || path.join(root, 'build/clang/scratch-llvm' + (process.platform === 'win32' ? '.exe' : ''));
    const lldbDap = process.env.SCRATCH_LLDB_DAP || (process.platform === 'win32' ? 'C:/Program Files/LLVM/bin/lldb-dap.exe' : 'lldb-dap');
    const lldbPython = process.env.SCRATCH_LLDB_PYTHON || (process.platform === 'win32' ? path.join(root, 'build/debugger/python311') : undefined);
    const bitcode = path.join(directory, 'program.bc'), project = path.join(directory, 'program.sb3'), debugMap = path.join(directory, 'program.debug.json');
    for (const [executable, args] of [[clang, ['--target=x86_64-unknown-linux-gnu', '-O0', '-g', '-fstandalone-debug',
        '-emit-llvm', '-c', source, '-o', bitcode]], [compiler, [bitcode, '--whole-program', '--debug-map', debugMap, '-o', project]]]) {
        const result = spawnSync(executable, args, {encoding: 'utf8', windowsHide: true, timeout: 60000});
        assert.equal(result.status, 0, result.error?.message || result.stderr || result.stdout);
    }
    const VM = require('../../tests/node_modules/turbowarp-vm');
    const {create} = require('../engine.js');
    const input = new PassThrough(), output = new PassThrough();
    let transcript = '', vm, engine, socket, pump, connection;
    const session = new TerminalDebugger({project, debugMap, clang, lldbDap, lldbPython,
        noOpen: true, port: 18858, connectTimeout: 30000}, {input, output});
    async function connect(url) {
        const extension = new URL(url).searchParams.get('extension');
        const base = extension.slice(0, -'/bridge.js'.length);
        const [bytes, map] = await Promise.all([fetch(base + '/project.sb3').then(r => r.arrayBuffer()),
            fetch(base + '/debug.json').then(r => r.json())]);
        vm = new VM(); await vm.loadProject(Buffer.from(bytes)); vm.runtime.currentStepTime = 1000 / 30;
        socket = new WebSocket(base.replace(/^http/, 'ws') + '/socket'); await once(socket, 'open');
        engine = create(vm, map, event => socket.send(JSON.stringify(event)));
        socket.addEventListener('message', async event => {
            const message = JSON.parse(event.data);
            try { socket.send(JSON.stringify({id: message.id, result: await engine[message.method](...(message.args || []))})); }
            catch (error) { socket.send(JSON.stringify({id: message.id, error: error.stack || error.message})); }
        });
        socket.send(JSON.stringify({event: 'ready'}));
        pump = setInterval(() => { if (engine.running && !engine.paused) vm.runtime._step(); }, 1);
    }
    output.on('data', data => {
        transcript += data;
        const match = /Opening TurboWarp: (\S+)/.exec(transcript);
        if (match && !connection) { connection = connect(match[1]); connection.catch(error => session.fail(error)); }
    });
    const stops = [];
    session.on('debugEvent', ({event}) => { if (event === 'stopped') stops.push(session.stopSerial); });
    async function command(text, stop = false) {
        const before = session.stopSerial;
        input.write(text + '\n'); await session.commandQueue;
        if (stop) { await session.waitFor(() => session.stopSerial > before || session.state === 'terminated', 30000); await session.eventWork; }
    }
    try {
        await session.start(); await connection;
        assert.equal(session.ready, true, transcript);
        await command(`break ${source}:14`);
        await command('continue', true);
        assert.equal(session.frame.line, 14);
        await command('locals'); await command('print sum'); await command('bt');
        assert.match(transcript, /sum = 3/);
        await command(`delete ${source}:14`);
        await command('step', true);
        assert.match(session.frame.name, /recurse/);
        await command('finish', true); assert.match(session.frame.name, /main/);
        await command('next', true);
        await command('print sum'); assert.match(transcript, /sum = 9/);
        await command('clear'); await command('continue', true);
        assert.equal(await session.done, 0); assert(stops.length >= 5);
        assert.doesNotMatch(transcript, /Content-Length:/);
        assert.match(transcript, /Program exited with code 0/);
        fs.writeFileSync(path.join(directory, 'cli-report.json'), JSON.stringify({passed: true, stopCount: stops.length, transcript}, null, 2));
    } finally {
        session.close(); clearInterval(pump); engine?.dispose(); socket?.close(); vm?.stopAll(); vm?.quit();
        fs.writeFileSync(path.join(directory, 'cli-transcript.txt'), transcript);
    }
});

test('real LLDB pending connections clean up on quit, EOF and timeout',
    {skip: process.env.SCRATCH_CLI_E2E !== '1', timeout: 30000}, async () => {
    const root = path.resolve(__dirname, '../..'), directory = path.join(root, 'build/validation/debugger-cli');
    const options = {project: path.join(directory, 'program.sb3'), debugMap: path.join(directory, 'program.debug.json'),
        clang: process.env.SCRATCH_CLANG || path.join(root, 'build/toolchains/windows/root/clang64/bin/clang.exe'),
        lldbDap: process.env.SCRATCH_LLDB_DAP || (process.platform === 'win32' ? 'C:/Program Files/LLVM/bin/lldb-dap.exe' : 'lldb-dap'),
        lldbPython: process.env.SCRATCH_LLDB_PYTHON || (process.platform === 'win32' ? path.join(root, 'build/debugger/python311') : undefined),
        noOpen: true, port: 18859};
    const report = [];
    for (const mode of ['quit', 'eof', 'timeout']) {
        const input = new PassThrough(), output = new PassThrough();
        let text = '';
        output.on('data', chunk => { text += chunk; });
        const session = new TerminalDebugger({...options, connectTimeout: mode === 'timeout' ? 40 : 10000}, {input, output});
        const start = session.start();
        await session.waitFor(() => Boolean(session.adapter.bridge?.url), 10000);
        const lldb = session.adapter.connection.child;
        const exited = once(lldb, 'exit');
        const began = Date.now();
        if (mode === 'quit') input.write('quit\n');
        if (mode === 'eof') input.end();
        const code = await session.done;
        await start;
        let timer;
        try { await Promise.race([exited, new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('LLDB process leaked')), 2000); })]); }
        finally { clearTimeout(timer); session.close(); }
        assert.equal(code, mode === 'timeout' ? 1 : 0, text);
        assert.equal(session.adapter.bridge.server.listening, false);
        assert.equal(session.adapter.rsp.server.listening, false);
        assert(Date.now() - began < 2000, 'cancel should not leave the launch timeout running');
        report.push({mode, code, elapsedMs: Date.now() - began, lldbExited: true});
    }
    fs.writeFileSync(path.join(directory, 'cli-cleanup-report.json'), JSON.stringify({passed: true, cases: report}, null, 2));
});
