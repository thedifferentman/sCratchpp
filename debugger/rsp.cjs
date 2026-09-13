'use strict';

// A single-thread GDB remote target. The guest has an x86-64 shaped debugging
// ABI only: instructions execute in Scratch, never on the host CPU.
const net = require('node:net');
const {EventEmitter} = require('node:events');
const {IRTarget} = require('./ir-target.cjs');

const REGISTERS = [
    ['rax',64,0],['rbx',64,3],['rcx',64,2],['rdx',64,1],
    ['rsi',64,4],['rdi',64,5],['rbp',64,6],['rsp',64,7],
    ['r8',64,8],['r9',64,9],['r10',64,10],['r11',64,11],
    ['r12',64,12],['r13',64,13],['r14',64,14],['r15',64,15],
    ['rip',64,16],['eflags',32,49],['cs',32,51],['ss',32,52],
    ['ds',32,53],['es',32,50],['fs',32,54],['gs',32,55]
];
const TARGET_XML = '<?xml version="1.0"?><!DOCTYPE target SYSTEM "gdb-target.dtd">' +
    '<target><architecture>i386:x86-64</architecture><osabi>GNU/Linux</osabi>' +
    '<feature name="org.gnu.gdb.i386.core">' + REGISTERS.map(([name,bits],i) =>
        `<reg name="${name}" bitsize="${bits}" regnum="${i}" type="${name==='rip'?'code_ptr':name==='rsp'||name==='rbp'?'data_ptr':bits===64?'int64':'int32'}"/>`).join('') + '</feature></target>';
const STACK_BASE = 0x80000000;
const STACK_STRIDE = 64;
function hexText(value) { return Buffer.from(String(value),'utf8').toString('hex'); }
function le(value, bytes=8) {
    const buffer = Buffer.alloc(bytes); let remaining = BigInt(value || 0);
    for (let i=0;i<bytes;i++) { buffer[i]=Number(remaining & 255n); remaining >>= 8n; }
    return buffer;
}
function packet(data) {
    const bytes = Buffer.from(data,'latin1'); let checksum=0;
    for (const byte of bytes) checksum=(checksum+byte)&255;
    return Buffer.concat([Buffer.from('$'),bytes,Buffer.from('#'+checksum.toString(16).padStart(2,'0'))]);
}

class RspServer extends EventEmitter {
    constructor({request,symbols}) {
        super(); this.target=new IRTarget(request);this.request=this.target.request.bind(this.target); this.symbols=symbols;
        this.breaks=new Set(); this.state={status:'paused',frames:[]}; this.running=false;
        this.frameAddresses=new Map();this.nextFrameAddress=STACK_BASE;
        this.points=new Map(); this.locations=new Map();
        for (const point of symbols.points) {
            this.points.set(point.pc,point);
            this.locations.set(JSON.stringify([point.function,point.instruction]),point);
        }
        if(!symbols.points.length)throw new Error('IR debug symbols have no execution points');
        this.lowPC=symbols.points.reduce((value,p)=>Math.min(value,p.pc),Infinity);
        this.highPC=symbols.points.reduce((value,p)=>Math.max(value,p.pc),0)+symbols.stride;
    }
    async listen(port=0,host='127.0.0.1') {
        this.server=net.createServer(socket=>this.attach(socket));
        await new Promise((resolve,reject)=>{this.server.once('error',reject);this.server.listen(port,host,resolve);});
        return this.server.address().port;
    }
    close() { this.socket?.destroy(); this.server?.close(); }
    attach(socket) {
        if(this.socket&&!this.socket.destroyed) {socket.destroy();return;}
        this.socket=socket; this.noAck=false; this.input=Buffer.alloc(0);this.queue=Promise.resolve();
        socket.setNoDelay(true);
        socket.on('data',data=>this.feed(data));
        socket.on('error',error=>this.emit('fault',error));
        socket.on('close',()=>this.emit('disconnect'));
    }
    send(value) { if(!this.socket?.destroyed) {this.lastPacket=packet(value);this.socket?.write(this.lastPacket);} }
    feed(data) {
        this.input=Buffer.concat([this.input,data]);
        while(this.input.length) {
            const first=this.input[0];
            if(first===3) {this.input=this.input.subarray(1);this.request('pause').catch(e=>this.emit('fault',e));continue;}
            if(first===43||first===45) {if(first===45&&this.lastPacket)this.socket.write(this.lastPacket);this.input=this.input.subarray(1);continue;}
            if(first!==36) {this.input=this.input.subarray(1);continue;}
            const end=this.input.indexOf(35,1); if(end<0||this.input.length<end+3)return;
            const raw=this.input.subarray(1,end);let sum=0;for(const byte of raw)sum=(sum+byte)&255;
            const expected=parseInt(this.input.subarray(end+1,end+3).toString(),16);
            this.input=this.input.subarray(end+3);
            if(sum!==expected) {if(!this.noAck)this.socket.write('-');continue;}
            if(!this.noAck)this.socket.write('+');
            const decoded=[];for(let i=0;i<raw.length;i++)decoded.push(raw[i]===125?raw[++i]^32:raw[i]);
            const command=Buffer.from(decoded).toString('latin1');
            this.emit('packet',command);
            this.queue=this.queue.then(async()=>{
                try {const result=await this.handle(command);if(result!==null)this.send(result);}
                catch(error) {this.emit('fault',error);this.send('E01');}
            });
        }
    }
    pointFor(frame) {
        return this.locations.get(JSON.stringify([frame?.function,frame?.instruction]));
    }
    pointAt(pc) {return this.points.get(pc)||this.symbols.points.find(p=>pc>=p.pc&&pc<p.pc+this.symbols.stride);}
    framePointer(frame,index=0) {
        if(!frame)return 0;
        const key=frame.instanceId ?? JSON.stringify([frame.function,frame.frameAddress,index]);
        if(!this.frameAddresses.has(key)) {this.frameAddresses.set(key,this.nextFrameAddress);this.nextFrameAddress-=STACK_STRIDE;}
        return this.frameAddresses.get(key);
    }
    async refresh() { this.state=await this.target.getState(); return this.state; }
    currentPC() {
        const pc=this.pointFor(this.state.location)?.pc || this.pointFor(this.state.frames?.[0])?.pc || this.lowPC;
        return this.pointAt(this.stoppedPC)?.pc===pc?this.stoppedPC:pc;
    }
    registers() {
        const top=this.state.frames?.[0];
        const rbp=this.framePointer(top);
        return REGISTERS.map(([name,bits])=>le(name==='rip'?this.currentPC():name==='rbp'?rbp:
            name==='rsp'?rbp-32:name==='r12'?top?.frameAddress||0:name==='eflags'?0x202:0,bits/8));
    }
    stopReply(reason='breakpoint',signal=5) {
        const regs=this.registers();return 'T'+signal.toString(16).padStart(2,'0')+'thread:1;reason:'+reason+';'+
            [6,7,12,16].map(i=>i.toString(16).padStart(2,'0')+':'+regs[i].toString('hex')+';').join('');
    }
    async notifyStopped(body={}) {
        try {
            if(body.irFrames) this.state={location:body.location,frames:body.irFrames,status:'paused'}; else await this.refresh();
            this.stoppedPC=undefined;
            const pc=this.currentPC();this.stoppedPC=[...this.breaks].find(at=>this.pointAt(at)?.pc===pc);
            if(this.running) {
                this.running=false;
                this.send(body.reason==='pause'?this.stopReply('signal',2):this.stopReply(this.stoppedPC!==undefined?'breakpoint':'trace'));
            }
        } catch(error) {this.emit('fault',error);}
    }
    notifyExited(body={}) {
        let exitCode=Number.isSafeInteger(Number(body.exitCode))?Number(body.exitCode)>>>0:0;
        const runtimeFailed=body.runtimeStatus!==undefined&&body.runtimeStatus!=='done';
        if(runtimeFailed) {
            if(exitCode===0)exitCode=1;
            this.emit('fault',new Error(`Program terminated before normal completion: ${String(body.runtimeStatus)}`));
        }
        this.state={...this.state,status:'terminated',exitCode,runtimeStatus:body.runtimeStatus};this.running=false;
        this.exitReply='W'+exitCode.toString(16).padStart(2,'0');this.send(this.exitReply);
    }
    async readMemory(address,count) {
        if(!Number.isSafeInteger(address)||address<0||!Number.isSafeInteger(count)||count<0||count>0x10000||!Number.isSafeInteger(address+count))throw Error('Invalid memory request');
        const result=Buffer.alloc(count);const frames=this.state.frames||[];
        const stack=new Map();
        const put=(at,value)=>{const bytes=le(value);for(let i=0;i<8;i++)stack.set(at+i,bytes[i]);};
        for(let i=0;i<frames.length;i++) {
            const rbp=this.framePointer(frames[i],i);const caller=frames[i+1];
            put(rbp,this.framePointer(caller,i+1));
            const callerPoint=this.pointFor(caller);const following=this.points.get((callerPoint?.pc||0)+this.symbols.stride);
            put(rbp+8,caller?(following?.function===caller.function?following.pc:(callerPoint?.pc||0)+1):0);
            put(rbp-8,caller?.frameAddress||0);
        }
        if(address>=Math.floor(this.lowPC/4096)*4096&&address+count<=Math.ceil(this.highPC/4096)*4096) {
            const code=Buffer.alloc(count,0x90);
            for(let i=0;i<count;i++) {const pc=address+i;const offset=(pc-this.lowPC)%this.symbols.stride;
                if(pc>=this.lowPC&&pc<this.highPC&&offset===0)code[i]=0xff;
                else if(pc>=this.lowPC&&pc<this.highPC&&offset===1)code[i]=0xe0;
            }return code;
        }
        if(address>=this.nextFrameAddress-0x10000&&address+count<=STACK_BASE+0x10000) {
            for(let i=0;i<count;i++)result[i]=stack.get(address+i)||0;return result;
        }
        const value=await this.request('readMemory',address,0,count);
        const bytes=Buffer.isBuffer(value)?value:Array.isArray(value)?Buffer.from(value):
            value?.data?Buffer.from(value.data,'base64'):Buffer.from(value?.bytes||[]);
        if(bytes.length!==count)throw Error('Unavailable guest memory');return bytes;
    }
    async syncBreakpoints() {
        const locations=[];for(const pc of this.breaks) {const p=this.pointAt(pc);if(p)locations.push({function:p.function,instruction:p.instruction});}
        await this.target.setBreakpoints(locations);
    }
    async handle(command) {
        command=command.replace(/;thread:[0-9a-f]+;$/i,'');
        if(command==='QStartNoAckMode'){this.noAck=true;return 'OK';}
        if(command.startsWith('qSupported'))return 'PacketSize=10000;QStartNoAckMode+;qXfer:features:read+;swbreak+;vContSupported+';
        if(command==='qHostInfo'||command==='qProcessInfo')return 'pid:1;ptrsize:8;endian:little;ostype:linux;vendor:pc;triple:'+hexText('x86_64-pc-linux-gnu')+';';
        if(command.startsWith('qXfer:features:read:target.xml:')){
            const [offset,count]=command.slice(command.lastIndexOf(':')+1).split(',').map(x=>parseInt(x,16));
            return (offset+count>=TARGET_XML.length?'l':'m')+TARGET_XML.slice(offset,offset+count);
        }
        if(command.startsWith('qRegisterInfo')) {
            const index=parseInt(command.slice(13),16);const reg=REGISTERS[index];if(!reg)return 'E45';
            const [name,bits,dwarf]=reg;const generic={rip:'pc',rsp:'sp',rbp:'fp',eflags:'flags'}[name];
            return `name:${name};bitsize:${bits};offset:${REGISTERS.slice(0,index).reduce((n,r)=>n+r[1]/8,0)};encoding:uint;format:hex;set:General Purpose Registers;gcc:${dwarf};dwarf:${dwarf};${generic?'generic:'+generic+';':''}`;
        }
        if(command==='qC')return 'QC1';
        if(command==='qfThreadInfo')return 'm1';if(command==='qsThreadInfo')return 'l';
        if(command==='qAttached'||command==='qAttached:1')return '1';
        if(command==='qOffsets')return 'Text=0;Data=0;Bss=0';
        if(command==='?'||command.startsWith('qThreadStopInfo')) {await this.refresh();return this.state.status==='terminated'?(this.exitReply||'W00'):this.stopReply();}
        if(command==='g') {await this.refresh();return Buffer.concat(this.registers()).toString('hex');}
        if(/^p[0-9a-f]+$/i.test(command)) {await this.refresh();return this.registers()[parseInt(command.slice(1),16)]?.toString('hex')||'E45';}
        if(command.startsWith('m')) {
            const parts=command.slice(1).split(',');if(parts.length!==2)return 'E01';
            return (await this.readMemory(parseInt(parts[0],16),parseInt(parts[1],16))).toString('hex');
        }
        if(/^z0,|^Z0,/.test(command)) {
            const pc=parseInt(command.split(',')[1],16);if(!this.pointAt(pc))return 'E01';
            if(command[0]==='Z')this.breaks.add(pc);else this.breaks.delete(pc);
            await this.syncBreakpoints();return 'OK';
        }
        if(command==='vCont?')return 'vCont;c;s;t';
        if(command==='c'||command==='s'||/^vCont;[cst](?::1)?$/.test(command)) {
            const action=command.startsWith('vCont;')?command[6]:command;
            this.running=true;this.stoppedPC=undefined;
            try {await this.request(action==='s'?'stepInstruction':action==='t'?'pause':'continue');}
            catch(error){this.running=false;throw error;}return null;
        }
        if(command==='D'||command.startsWith('D;')) {await this.request('continue');return 'OK';}
        if(command==='k'||command.startsWith('vKill;')) {await this.request('terminate');return 'OK';}
        if(command==='!'||/^H[gc]/.test(command)||command==='T1'||command==='QThreadSuffixSupported'||command==='QListThreadsInStopReply')return 'OK';
        // Unsupported probes must receive an empty response, not an error.
        return '';
    }
}
module.exports={RspServer,REGISTERS,TARGET_XML,STACK_BASE,STACK_STRIDE,packet};
