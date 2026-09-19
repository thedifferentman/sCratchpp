'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {PassThrough} = require('node:stream');
const {once} = require('node:events');
const {Adapter, Bridge, crc32, projectChecksum} = require('../llvmdbg.cjs');

function archive(text) {
    const name = Buffer.from('project.json'), data = Buffer.from(text), crc = parseInt(crc32(data), 16);
    const local = Buffer.alloc(30); local.writeUInt32LE(0x04034b50); local.writeUInt32LE(crc, 14); local.writeUInt32LE(data.length, 18); local.writeUInt32LE(data.length, 22); local.writeUInt16LE(name.length, 26);
    const central = Buffer.alloc(46); central.writeUInt32LE(0x02014b50); central.writeUInt32LE(crc, 16); central.writeUInt32LE(data.length, 20); central.writeUInt32LE(data.length, 24); central.writeUInt16LE(name.length, 28);
    const end = Buffer.alloc(22); end.writeUInt32LE(0x06054b50); end.writeUInt16LE(1, 8); end.writeUInt16LE(1, 10); end.writeUInt32LE(central.length + name.length, 12); end.writeUInt32LE(local.length + name.length + data.length, 16);
    return Buffer.concat([local, name, data, central, name, end]);
}
function readDap(stream) {
    let buffer = Buffer.alloc(0); const messages = [], waiters = [];
    stream.on('data', data => {
        buffer = Buffer.concat([buffer, data]);
        while (true) {
            const at = buffer.indexOf('\r\n\r\n'); if (at < 0) break;
            const length = Number(/Content-Length: (\d+)/i.exec(buffer.subarray(0, at).toString())[1]);
            if (buffer.length < at + 4 + length) break;
            const message = JSON.parse(buffer.subarray(at + 4, at + 4 + length)); buffer = buffer.subarray(at + 4 + length);
            waiters.length ? waiters.shift()(message) : messages.push(message);
        }
    });
    return () => messages.length ? Promise.resolve(messages.shift()) : new Promise(resolve => waiters.push(resolve));
}
test('DAP accepts fragmented headers and UTF-8 byte lengths', async () => {
    const input = new PassThrough(), output = new PassThrough(), next = readDap(output);
    new Adapter(input, output);
    const body = Buffer.from(JSON.stringify({seq: 7, type: 'request', command: 'initialize', arguments: {clientID: '中文'}}));
    const packet = Buffer.concat([Buffer.from(`Content-Length: ${body.length}\r\n\r\n`), body]);
    for (let at = 0; at < packet.length; at += 3) input.write(packet.subarray(at, at + 3));
    const response = await next(); assert.equal(response.request_seq, 7); assert.equal(response.success, true); assert.equal(response.body.supportsReadMemoryRequest, true);
    input.end();
});
test('bridge validates artifacts, serves only token paths, and exchanges WebSocket RPC', async t => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'scratch-debug-'));
    t.after(() => fs.rmSync(directory, {recursive: true, force: true}));
    const project = path.join(directory, 'project.sb3'), debugMap = path.join(directory, 'project.debug.json'), zip = archive('{"targets":[]}');
    fs.writeFileSync(project, zip);
    assert.equal(projectChecksum(zip), crc32(Buffer.from('{"targets":[]}')));
    fs.writeFileSync(debugMap, JSON.stringify({schemaVersion: 1, projectCrc32: '00000000'}));
    assert.throws(() => new Bridge({project, debugMap}, () => {}), /does not match/);
    fs.writeFileSync(debugMap, JSON.stringify({schemaVersion: 1, projectCrc32: projectChecksum(zip), points: {}}));
    const events = [], bridge = new Bridge({project, debugMap, noOpen: true, port: 18841}, e => events.push(e));
    t.after(() => bridge.close()); await bridge.listen();
    const base = `http://localhost:18841/${bridge.token}`;
    assert.equal((await fetch('http://localhost:18841/project.sb3')).status, 404);
    assert.equal((await fetch(base + '/debug.json', {headers: {Origin: 'https://malicious.example'}})).status, 403);
    const script = await (await fetch(base + '/bridge.js', {headers: {Origin: 'https://turbowarp.org'}})).text();
    assert.match(script, /ScratchLLVMEngine/); assert.match(script, /setIRBreakpoints/);
    assert.equal((await fetch(base + '/project.sb3')).headers.get('cache-control'), 'no-store');
    const socket = new WebSocket(`ws://localhost:18841/${bridge.token}/socket`);
    await once(socket, 'open'); socket.send(JSON.stringify({event: 'ready'})); await bridge.ready;
    socket.addEventListener('message', event => { const request = JSON.parse(event.data); socket.send(JSON.stringify({id: request.id, result: {method: request.method, args: request.args}})); });
    assert.deepEqual(await bridge.request('readMemory', '128', 0, 4), {method: 'readMemory', args: ['128', 0, 4]});
    socket.send(JSON.stringify({event: 'stopped', body: {reason: 'breakpoint'}}));
    await new Promise(resolve => setTimeout(resolve, 20));
    assert(events.some(e => e.event === 'stopped'));
    bridge.close();
});

test('embedded player uses an ephemeral port, token-scoped offline assets and origin checks', async t => {
    const directory=fs.mkdtempSync(path.join(os.tmpdir(),'scrpp-player-test-'));
    t.after(()=>{assert(directory.startsWith(os.tmpdir()));fs.rmSync(directory,{recursive:true,force:true});});
    const project=path.join(directory,'p.sb3');fs.writeFileSync(project,archive('{"targets":[]}'));
    const events=[], bridge=new Bridge({project,noDebug:true,embeddedPlayer:true},e=>events.push(e));
    t.after(()=>bridge.close());await bridge.listen();
    assert(events.some(e=>e.event==='scrppPlayer'));
    assert(!events.some(e=>e.event==='output'));
    const url=new URL(bridge.url), base=new URL('.',url);
    assert.notEqual(Number(url.port),0);
    const response=await fetch(url);
    assert.equal(response.status,200);
    assert.match(response.headers.get('content-security-policy'),/default-src 'none'/);
    const html=await response.text();
    assert.match(html,/scaffolding.js/);
    assert(!/<(?:button|header|footer)\b/.test(html), 'Stage must not have player controls or chrome');
    assert.equal((await fetch(new URL('/player.html',url))).status,404);
    assert.equal((await fetch(url,{headers:{Origin:'https://invalid.example'}})).status,403);
    assert.equal((await fetch(new URL('scaffolding.js',base))).status,200);
    assert.deepEqual(await (await fetch(new URL('settings.json',base))).json(),{noDebug:true});
    bridge.close();
});
