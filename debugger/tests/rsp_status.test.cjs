'use strict';
const assert=require('node:assert/strict');
const {RspServer,STACK_BASE}=require('../rsp.cjs');

async function main() {
    const symbols={stride:16,points:[{pc:0x100000,function:'main',instruction:'entry'}]};
    const state={status:'terminated',location:{function:'main',instruction:'entry'},frames:[]};
    const server=new RspServer({symbols,request:async method=>{
        assert.equal(method,'getIRState');return state;
    }});
    const packets=[],faults=[];server.send=p=>packets.push(p);server.on('fault',error=>faults.push(error.message));
    server.notifyExited({exitCode:0,runtimeStatus:'done'});assert.equal(packets.at(-1),'W00');assert.equal(faults.length,0);
    server.notifyExited({exitCode:42,runtimeStatus:'done'});assert.equal(packets.at(-1),'W2a');assert.equal(faults.length,0);
    server.notifyExited({exitCode:256,runtimeStatus:'done'});assert.equal(packets.at(-1),'W100');
    server.notifyExited({exitCode:-1,runtimeStatus:'done'});assert.equal(packets.at(-1),'Wffffffff');
    server.notifyExited({exitCode:0,runtimeStatus:'invalid memory access'});
    assert.equal(packets.at(-1),'W01');assert.match(faults.at(-1),/invalid memory access/);
    assert.equal(await server.handle('?'),'W01');
    server.notifyExited({exitCode:17,runtimeStatus:'stack exhausted'});assert.equal(packets.at(-1),'W11');assert.match(faults.at(-1),/stack exhausted/);
    server.notifyExited({exitCode:3});assert.equal(packets.at(-1),'W03');
    server.running=true;
    await server.notifyStopped({reason:'pause',location:state.location,irFrames:[{...state.location,frameAddress:1024,instanceId:1}]});
    assert.match(packets.at(-1),/^T02thread:1;reason:signal;/);
    assert.equal((await server.readMemory(STACK_BASE,8)).readBigUInt64LE(),0n);
    assert.equal((await server.readMemory(0x100000,2)).toString('hex'),'ffe0');
    await assert.rejects(server.readMemory(-1,1),/Invalid memory request/);
    await assert.rejects(server.readMemory(Number.MAX_SAFE_INTEGER,1),/Invalid memory request/);
    process.stdout.write('RSP normal/trap exit status, pause signal and unsigned synthetic memory checks passed.\n');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
