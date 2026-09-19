'use strict';
(async () => {
    const status=document.getElementById('status');
    const loading=text=>{status.hidden=false;status.dataset.state='loading';status.textContent=text;};
    const paint=()=>new Promise(resolve=>{
        // A hidden retained webview may suspend animation frames; still finish startup.
        const timer=setTimeout(resolve,100);
        requestAnimationFrame(()=>requestAnimationFrame(()=>{clearTimeout(timer);resolve();}));
    });
    const fetchChecked=async url=>{const r=await fetch(url);if(!r.ok)throw Error('加载失败：'+url+' (HTTP '+r.status+')');return r;};
    loading('正在初始化播放器…');
    await paint();
    const settings=await fetchChecked('settings.json').then(r=>r.json());
    const player=new Scaffolding.Scaffolding();
    player.setup();preservePenResolution(player.renderer);player.appendTo(document.getElementById('stage'));
    const vm=player.vm;
    // Deliberately expose only inside the isolated local player for diagnostics.
    globalThis.scrppPlayer={vm,player};
    vm.setCompilerOptions({enabled:settings.noDebug,warpTimer:false});
    // Generated projects need only built-in extensions. Never fetch project-supplied code.
    if(vm.extensionManager?.securityManager) {
        vm.extensionManager.securityManager.canLoadExtensionFromProject=()=>Promise.resolve(false);
    }
    const socket=new WebSocket(new URL('socket',location.href).href.replace('http:','ws:'));
    let engine,closed=false;
    const send=m=>{if(socket.readyState===WebSocket.OPEN)socket.send(JSON.stringify(m));};
    const fail=e=>{status.hidden=false;status.dataset.state='error';status.setAttribute('role','alert');status.textContent=String(e.message||e);send({event:'error',body:{message:status.textContent}});};
    const dispose=()=>{if(closed)return;closed=true;engine?.dispose();vm.stopAll();vm.quit();socket.close();};
    socket.addEventListener('open',async()=>{
        try {
            loading('正在读取作品…');
            const [project,map]=await Promise.all([fetchChecked('project.sb3').then(r=>r.arrayBuffer()),settings.noDebug?null:fetchChecked('debug.json').then(r=>r.json())]);
            loading('正在加载作品与造型…');
            await paint();
            await player.loadProject(project);
            vm.setCompilerOptions({enabled:settings.noDebug,warpTimer:false});
            // Wait for SVG skins before the first stamp or pen draw.
            const skins=vm.runtime.renderer?._allSkins||{};
            await Promise.all(Object.values(skins).filter(s=>s?._svgImage&&s._svgImageLoaded===false).map(s=>new Promise((resolve,reject)=>{
                const image=s._svgImage;
                const finish=e=>{clearTimeout(timer);image.removeEventListener('load',ok);image.removeEventListener('error',bad);e?reject(e):resolve();};
                const ok=()=>finish(),bad=()=>finish(new Error('造型加载失败'));
                const timer=setTimeout(()=>finish(new Error('造型加载超时')),30000);
                image.addEventListener('load',ok);image.addEventListener('error',bad);if(s._svgImageLoaded)ok();
            })));
            if(!settings.noDebug)engine=globalThis.ScratchLLVMEngine.create(vm,map,event=>{
                if(event.event==='stopped'){status.textContent='已暂停';status.hidden=true;}
                send(event);
            });
            loading(settings.noDebug?'正在准备运行…':'正在连接调试器…');
            await paint();
            vm.start();
            send({event:'ready'});
            if(settings.noDebug){vm.greenFlag();status.textContent='运行中';status.hidden=true;}
        } catch(e){fail(e);}
    });
    const allowed=new Set(['getIRState','setIRBreakpoints','stepInstruction','terminate','setBreakpoints','start','continue','pause','next','stepIn','stepOut','stackTrace','scopes','variables','readMemory','evaluate','dispose']);
    socket.addEventListener('message',async e=>{
        let m;
        try {m=JSON.parse(e.data);if(!engine||!allowed.has(m.method)||typeof engine[m.method]!=='function')throw Error('Unsupported debugger operation');
            send({id:m.id,result:await engine[m.method](...(m.args||[]))});
        }catch(error){send({id:m?.id,error:error.message});}
    });
    vm.on('PROJECT_RUN_STOP',()=>{if(settings.noDebug){status.textContent='已结束';send({event:'scrppRunStopped'});}});
    document.getElementById('stage').addEventListener('pointerdown',()=>{document.activeElement?.blur();window.focus();player.audioEngine?.audioContext?.resume();});
    const heldKeys=new Set();
    window.addEventListener('keydown',e=>heldKeys.add(e.key));
    window.addEventListener('keyup',e=>heldKeys.delete(e.key));
    window.addEventListener('blur',()=>{for(const key of heldKeys)vm.postIOData('keyboard',{key,isDown:false});heldKeys.clear();});
    window.addEventListener('pagehide',dispose);
    socket.addEventListener('close',()=>{dispose();if(status.dataset.state!=='error'){status.textContent='会话已结束';status.hidden=true;}});
    socket.addEventListener('error',()=>fail(new Error('播放器连接失败')));
})().catch(e=>{const status=document.getElementById('status');status.hidden=false;status.dataset.state='error';status.setAttribute('role','alert');status.textContent=String(e.message||e);});
