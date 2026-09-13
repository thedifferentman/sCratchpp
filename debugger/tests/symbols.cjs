'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {buildAssembly,generateSymbols} = require('../symbols.cjs');
const root=path.resolve(__dirname,'../..');
const output=path.join(root,'build/validation/debugger-symbols');
fs.mkdirSync(output,{recursive:true});
const source=path.join(output,'source.cpp').replace(/\\/g,'/');
fs.writeFileSync(source,'int main() {\n  int value = 42;\n  return value;\n}\n');
const points={
    b0:{function:'main',instruction:'i0',ordinal:0,block:'entry',file:source,line:1,column:1,op:'alloca'},
    b1:{function:'main',instruction:'i1',ordinal:1,block:'entry',file:source,line:2,column:3,op:'store'},
    b2:{function:'main',instruction:'i2',ordinal:2,block:'entry',file:source,line:3,column:3,op:'ret'},
    b2alias:{function:'main',instruction:'i2',ordinal:2,block:'entry',file:source,line:3,column:3,op:'ret'}
};
const map={schemaVersion:1,pointerBytes:8,projectCrc32:'12345678',points,functions:{main:{
    source:{name:'main',file:source,line:1},slots:{p:{offset:16},v:{offset:24}},
    instructions:Object.fromEntries(Object.values(points).map(p=>[p.instruction,p])),
    variables:[
        {id:'x',name:'value',file:source,line:2,type:{kind:'int',name:'int',bits:32,bytes:4,signed:true},
            locations:[{before:'i2',block:'entry',kind:'address',operand:{kind:'ref',id:'p'}}]},
        {id:'constant',name:'constant',file:source,line:1,type:{kind:'int',name:'unsigned int',bits:32,bytes:4,signed:false},
            locations:[{before:'i0',block:'entry',kind:'value',operand:{kind:'bytes',bytes:[42,0,0,0]}}]},
        {id:'pointer',name:'pointer',file:source,line:1,type:{kind:'pointer',bytes:8,element:{kind:'int',name:'int',bits:32,bytes:4,signed:true}},
            locations:[{before:'i0',block:'entry',kind:'value',operand:{kind:'ref',id:'v'}}]}
    ]}}};
const built=buildAssembly(map);
assert.equal(built.symbols.points.length,3);
assert.deepEqual(built.symbols.points[2].blockIds,['b2','b2alias']);
assert.equal(built.symbols.points[0].pc,0x100000);
assert.equal(built.symbols.functions.main.highPC,0x100030);
assert.match(built.assembly,/\.cfi_offset %r12, -24/);
assert.match(built.assembly,/\.byte 0x94, 8/);
assert.throws(()=>buildAssembly({...map,pointerBytes:4}),/64-bit/);
assert.throws(()=>buildAssembly({...map,points:{}}),/no executable/);
const clang=process.argv[2] || path.join(root,'build/toolchains/windows/root/clang64/bin/clang.exe');
const result=generateSymbols(map,{clang,output:path.join(output,'program.elf')});
const tool=path.join(path.dirname(clang),'llvm-dwarfdump'+(process.platform==='win32'?'.exe':''));
const verify=spawnSync(tool,['--verify',result.elf],{encoding:'utf8',windowsHide:true});
fs.writeFileSync(path.join(output,'verify.txt'),verify.stdout+verify.stderr);
assert.equal(verify.status,0,verify.stdout+verify.stderr);
const dump=spawnSync(tool,['--debug-info','--debug-line','--debug-loc','--debug-frame',result.elf],{encoding:'utf8',windowsHide:true});
fs.writeFileSync(path.join(output,'dwarf.txt'),dump.stdout+dump.stderr);
assert.equal(dump.status,0,dump.stderr);
assert.match(dump.stdout,/DW_TAG_subprogram/);
assert.match(dump.stdout,/DW_AT_name\s+\("value"\)/);
assert.match(dump.stdout,/DW_OP_breg12 R12\+16, DW_OP_deref_size 0x8/);
assert.match(dump.stdout,/DW_OP_implicit_value/);
process.stdout.write(JSON.stringify({passed:true,elf:result.elf,points:3,variables:3})+'\n');
