'use strict';
const crypto = require('node:crypto');
const {EventEmitter} = require('node:events');

// A deliberately small RFC 6455 server for the authenticated, local browser
// bridge. No compression/extensions; text messages and control frames only.
class Peer extends EventEmitter {
    constructor(socket, head = Buffer.alloc(0)) {
        super(); this.socket = socket; this.buffer = Buffer.alloc(0); this.fragments = []; this.fragmentBytes = 0;
        socket.on('data', data => this.feed(data));
        socket.on('close', () => this.emit('close'));
        socket.on('error', error => this.emit('fault', error));
        if (head.length) queueMicrotask(() => this.feed(head));
    }
    send(value) { this.frame(1, Buffer.from(JSON.stringify(value))); }
    frame(opcode, payload) {
        if (this.socket.destroyed) return;
        let header;
        if (payload.length < 126) header = Buffer.from([0x80 | opcode, payload.length]);
        else if (payload.length <= 65535) { header = Buffer.alloc(4); header[0] = 0x80 | opcode; header[1] = 126; header.writeUInt16BE(payload.length, 2); }
        else { header = Buffer.alloc(10); header[0] = 0x80 | opcode; header[1] = 127; header.writeBigUInt64BE(BigInt(payload.length), 2); }
        this.socket.write(Buffer.concat([header, payload]));
    }
    close() { this.frame(8, Buffer.alloc(0)); this.socket.end(); this.socket.destroySoon?.(); }
    feed(data) {
        this.buffer = Buffer.concat([this.buffer, data]);
        try {
            while (this.buffer.length >= 2) {
                const b = this.buffer, fin = !!(b[0] & 128), op = b[0] & 15;
                if ((b[0] & 0x70) || !(b[1] & 128)) throw new Error('Invalid WebSocket frame');
                let n = b[1] & 127, p = 2;
                if (n === 126) { if (b.length < 4) return; n = b.readUInt16BE(2); p = 4; }
                else if (n === 127) { if (b.length < 10) return; const big = b.readBigUInt64BE(2); if (big > 4194304n) throw new Error('Message too large'); n = Number(big); p = 10; }
                if (n > 4194304 || this.fragmentBytes + n > 4194304) throw new Error('Message too large');
                if (op >= 8 && (!fin || n > 125)) throw new Error('Invalid control frame');
                if (b.length < p + 4 + n) return;
                const mask = b.subarray(p, p + 4), payload = Buffer.from(b.subarray(p + 4, p + 4 + n));
                for (let i = 0; i < n; i++) payload[i] ^= mask[i % 4];
                this.buffer = b.subarray(p + 4 + n);
                if (op === 8) { this.close(); return; }
                if (op === 9) { this.frame(10, payload); continue; }
                if (op === 10) continue;
                if (![0, 1].includes(op) || (op === 0) !== (this.fragments.length > 0)) throw new Error('Unsupported data frame');
                this.fragments.push(payload); this.fragmentBytes += n;
                if (fin) { const value = JSON.parse(Buffer.concat(this.fragments).toString('utf8')); this.fragments = []; this.fragmentBytes = 0; this.emit('message', value); }
            }
        } catch (error) { this.emit('fault', error); this.socket.destroy(); }
    }
}
function upgrade(request, socket, head) {
    const key = request.headers['sec-websocket-key'];
    if (request.headers.upgrade?.toLowerCase() !== 'websocket' || request.headers['sec-websocket-version'] !== '13' || !key || Buffer.from(key, 'base64').length !== 16) { socket.end('HTTP/1.1 400 Bad Request\r\n\r\n'); return null; }
    const accept = crypto.createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
    socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ' + accept + '\r\n\r\n');
    return new Peer(socket, head);
}
module.exports = {upgrade};
