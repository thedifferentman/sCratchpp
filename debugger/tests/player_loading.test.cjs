'use strict';
const {test}=require('node:test');const assert=require('node:assert/strict');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const code=fs.readFileSync(path.join(__dirname,'../player/player.js'),'utf8');
const until=async f=>{for(let i=0;i<100;++i){if(f())return;await new Promise(r=>setImmediate(r));}throw Error('Player phase did not arrive');};
function setup(failed=false){
 const status={hidden:false,dataset:{},textContent:'',setAttribute(){}};
 let socket,release,atStart;
 const load=new Promise(r=>{release=r;});
 const fakeVM={runtime:{renderer:{_allSkins:{}}},setCompilerOptions(){},on(){},start(){},stopAll(){},quit(){},postIOData(){},
  greenFlag(){atStart={text:status.textContent,hidden:status.hidden};}};
 class Socket extends EventTarget {static OPEN=1;constructor(){super();socket=this;this.readyState=1;}send(){}close(){}}
 class Player {constructor(){this.vm=fakeVM;this.renderer={};}setup(){}appendTo(){}loadProject(){return load;}}
 const sandbox={Scaffolding:{Scaffolding:Player},preservePenResolution(){},WebSocket:Socket,URL,
  location:{href:'http://127.0.0.1:1234/token/player.html'},requestAnimationFrame:f=>setImmediate(f),
  setTimeout,clearTimeout,document:{getElementById:id=>id==='status'?status:{addEventListener(){}}},window:{addEventListener(){}},
  fetch:async url=>({ok:!failed,status:failed?404:200,json:async()=>({noDebug:true}),arrayBuffer:async()=>new ArrayBuffer(0)})};
 return {status,release,start:()=>atStart,open:()=>socket.dispatchEvent(new Event('open')),ready:vm.runInNewContext(code,sandbox)};
}
test('loading stays visible through project loading and first compilation, then hides',async()=>{
 const t=setup();await t.ready;t.open();await until(()=>t.status.textContent==='正在加载作品与造型…');
 assert.equal(t.status.hidden,false);t.release();await until(()=>t.status.hidden);
 assert.deepEqual(t.start(),{text:'正在准备运行…',hidden:false});assert.equal(t.status.textContent,'运行中');
});
test('HTTP failure produces visible error instead of a blank stage',async()=>{
 const t=setup(true);await t.ready;assert.equal(t.status.hidden,false);assert.equal(t.status.dataset.state,'error');assert.match(t.status.textContent,/HTTP 404/);
});
