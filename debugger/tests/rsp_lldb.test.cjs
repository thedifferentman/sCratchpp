'use strict';
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const {spawn}=require('node:child_process');
const {RspServer}=require('../rsp.cjs');
const {generateSymbols}=require('../symbols.cjs');

async function main() {
    const root=path.resolve(__dirname,'../..');
    const output=path.join(root,'build/validation/debugger-rsp');fs.mkdirSync(output,{recursive:true});
    const source=path.join(output,'sample.cpp').replace(/\\/g,'/');
    fs.writeFileSync(source,'int helper(int x) {\n  return x+1;\n}\nint main() {\n  int n=7;\n  n=helper(n);\n  return n;\n}\n');
    const point=(fn,instruction,line)=>({function:fn,instruction,block:'entry',ordinal:line,file:source,line,column:3});
    const map={schemaVersion:1,pointerBytes:8,points:{a:point('helper','h1',2),b:point('main','m1',5),c:point('main','m2',6),d:point('main','m3',7)},functions:{
        helper:{source:{file:source,line:1,name:'helper'},instructions:{h1:{ordinal:2,block:'entry'}},slots:{x:{offset:0}},variables:[{name:'x',parameter:true,type:{kind:'int',name:'int',bits:32,bytes:4,signed:true},locations:[{before:'h1',block:'entry',kind:'value',operand:{kind:'ref',id:'x'}}]}]},
        main:{source:{file:source,line:4,name:'main'}}}};
    const clang=process.env.SCRATCH_CLANG||path.join(root,'build/toolchains/windows/root/clang64/bin/clang.exe');
    const {elf,symbols}=generateSymbols(map,{clang,output:path.join(output,'sample.elf')});
    let location={function:'main',instruction:'m1'},breaks=[];
    let frames=[{...location,frameAddress:100,instanceId:1}];
    const requests=[];
    const request=async(method,...args)=>{
        requests.push([method,...args]);
        if(method==='getIRState')return {status:'paused',location,frames};
        if(method==='readMemory')return Array.from({length:args[2]},(_,i)=>(args[0]+i)===200?7:0);
        if(method==='setIRBreakpoints'){breaks=args[0];return breaks.map(b=>({...b,verified:true}));}
        if(method==='continue'||method==='stepInstruction') {
            if(method==='continue'&&breaks.length)location={...(breaks.find(b=>b.function!==location.function||b.instruction!==location.instruction)||breaks[0])};
            else location=location.instruction==='m2'?{function:'helper',instruction:'h1'}:{function:'main',instruction:location.instruction==='m1'?'m2':'m3'};
            frames=location.function==='helper'?[{...location,frameAddress:200,instanceId:2},{function:'main',instruction:'m2',frameAddress:100,instanceId:1}]:[{...location,frameAddress:100,instanceId:1}];
            setTimeout(()=>server.notifyStopped({reason:method==='continue'?'breakpoint':'step',location,irFrames:frames}),10);
            return {};
        }
        return {};
    };
    const server=new RspServer({request,symbols});const trace=[];server.on('packet',p=>trace.push(p));server.on('fault',e=>trace.push('ERROR: '+e.stack));
    const port=await server.listen();
    const lldb=process.env.SCRATCH_LLDB||'C:/Program Files/LLVM/bin/lldb.exe';
    const commands=[`target create "${elf.replace(/\\/g,'/')}"`,`gdb-remote 127.0.0.1:${port}`,'register read rip r12','thread step-over','thread step-in','thread backtrace',
        'breakpoint set --name helper','continue','thread backtrace','frame variable x','thread step-out','register read rip','process detach','quit'];
    const args=['--batch',...commands.flatMap(command=>['-o',command])];
    const child=spawn(lldb,args,{env:process.env,windowsHide:true});let stdout='',stderr='';
    child.stdout.on('data',d=>stdout+=d);child.stderr.on('data',d=>stderr+=d);
    const timer=setTimeout(()=>child.kill(),30000);
    const code=await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve);});clearTimeout(timer);server.close();
    fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({code,stdout,stderr,trace,requests},null,2));
    process.stdout.write(stdout);process.stderr.write(stderr);
    assert.equal(code,0);assert.match(stdout,/stop reason = breakpoint/);assert.match(stdout,/frame #1:.*main/);
    assert.match(stdout,/\(int\) x = 7/);assert.match(stdout,/stop reason = step over/);assert.match(stdout,/stop reason = step out/);assert(requests.some(r=>r[0]==='stepInstruction'));
    assert.match(stdout.split('(lldb) thread step-in')[1].split('(lldb)')[0],/frame #0:.*helper/);
    process.stdout.write('Actual LLDB remote-target breakpoint, stepping, stack and variable checks passed.\n');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
