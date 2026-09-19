'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const JSZip=require('jszip'), Storage=require('scratch-storage');
const cases=[[-120,-90,0,110,120,-90,0x4c97ff],[120,-90,0,110,-120,-90,0x4c97ff],[-20,-30,100,-30,-20,100,0x20a060],[-80,0,80,1,30,3,0x9933cc],[0,0,1,0,0,1,0xff8800],[-120.5,-90.5,0.5,110.5,120.5,-90.5,0x123456]];
async function run(mode,bytes,compiled){
 const VM=require(mode),vm=new VM(),trace=[];let timer,compiledThreads=0;
 try{
  vm.attachStorage(new (Storage.ScratchStorage||Storage)());vm.setTurboMode(true);
  if(vm.setCompilerOptions)vm.setCompilerOptions({enabled:compiled});
  const push=vm.runtime._pushThread;
  vm.runtime._pushThread=function(...a){const thread=push.apply(this,a);if(thread.isCompiled)compiledThreads++;return thread;};
  await vm.loadProject(bytes);
  const add=(kind,...values)=>trace.push([kind,...values.map(Number)]);
  if(compiled){
   const pen=vm.runtime.ext_pen;
   for(const [name,record] of Object.entries({_penUp:()=>add('up'),_penDown:()=>add('down'),_setPenSizeTo:a=>add('size',a),_setPenColorToColor:a=>add('color',a)})){
    const original=pen[name];pen[name]=function(...a){record(...a);return original.apply(this,a);};
   }
   const target=vm.runtime.targets.find(t=>!t.isStage),move=target.setXY;
   target.setXY=function(x,y,...a){add('move',x,y);return move.call(this,x,y,...a);};
  }else{
   for(const [op,record] of Object.entries({pen_penUp:()=>add('up'),pen_penDown:()=>add('down'),pen_setPenSizeTo:a=>add('size',a.SIZE),pen_setPenColorToColor:a=>add('color',a.COLOR),motion_gotoxy:a=>add('move',a.X,a.Y)})){
    const original=vm.runtime._primitives[op];vm.runtime._primitives[op]=(a,u)=>{record(a);return original(a,u);};
   }
  }
  const errors=[];vm.on('COMPILE_ERROR',(_t,e)=>errors.push(String(e)));vm.start();vm.greenFlag();
  await new Promise((resolve,reject)=>{const end=Date.now()+30000;timer=setInterval(()=>{if(errors.length)reject(Error(errors.join('\n')));else if(Date.now()>end)reject(Error('VM timeout'));else if(!vm.runtime.threads.some(t=>!t.updateMonitor))resolve();},10);});
  if(compiled)assert(compiledThreads>0,'Expected real TurboWarp compilation');
  return trace;
 }finally{clearInterval(timer);vm.stopAll();vm.quit();}
}
(async()=>{
 const [file,report]=process.argv.slice(2),bytes=fs.readFileSync(file),zip=await JSZip.loadAsync(bytes);
 const project=JSON.parse(await zip.file('project.json').async('string'));
 const kernel=JSON.parse(fs.readFileSync(path.join(__dirname,'fixtures/triangle-kernel.json'),'utf8'));
 const target=project.targets[1];target.blocks=JSON.parse(JSON.stringify(kernel.target.blocks));target.variables=kernel.target.variables;target.lists={};
 const proto=Object.values(target.blocks).find(b=>b.opcode==='procedures_prototype'&&b.mutation.proccode===kernel.entry);
 const ids=JSON.parse(proto.mutation.argumentids);let last=null,serial=0;
 const block=(opcode,inputs={},mutation)=>{const id='triangle_test_'+serial++;target.blocks[id]={opcode,inputs,fields:{},parent:last,next:null,shadow:false,topLevel:!last,...(mutation?{mutation}:{}),...(!last?{x:0,y:0}:{})};if(last)target.blocks[last].next=id;last=id;};
 block('event_whenflagclicked');
 for(const values of cases){block('pen_penUp');block('pen_setPenColorToColor',{COLOR:[1,[4,String(values[6])]]});block('procedures_call',Object.fromEntries(ids.map((id,i)=>[id,[1,[4,String(values[i])]]])),{tagName:'mutation',children:[],proccode:kernel.entry,argumentids:JSON.stringify(ids),warp:'true'});block('pen_penUp');}
 zip.file('project.json',JSON.stringify(project));const reference=await zip.generateAsync({type:'nodebuffer'});
 const reports=[];
 for(const [mode,compiled] of [['scratch-vm',false],['turbowarp-vm',false],['turbowarp-vm',true]]){
  const expected=await run(mode,reference,compiled),actual=await run(mode,bytes,compiled);
  // Invalid triangles still set color and leave the pen up, but make no strokes.
  for(let i=0;i<2;i++)expected.push(['up'],['color',0],['up']);
  assert.equal(actual.length,expected.length);
  for(let i=0;i<actual.length;i++)for(let j=0;j<actual[i].length;j++){
   const a=actual[i][j],e=expected[i][j];assert(typeof a==='number'?Math.abs(a-e)<1e-9:a===e,`${mode}/${compiled} trace ${i}/${j}: ${a} != ${e}`);
  }
  reports.push({mode,compiled,operations:actual.length,sourceTraceMatched:true});
 }
 fs.writeFileSync(report,JSON.stringify({passed:true,reports},null,2));console.log(JSON.stringify(reports));
})().catch(e=>{console.error(e);process.exitCode=1});
