'use strict';
// Run inside an actual VS Code extension test host. Do not manually activate
// our extension: startDebugging must discover it via its activation event.
const vscode = require('vscode');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {once} = require('node:events');
const root = path.resolve(__dirname, '../..');
const VM = require('../../tests/node_modules/turbowarp-vm');
const {create} = require('../engine.js');
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

exports.run = async () => {
    const results = [], disposables = [], active = [];
    let current, failure;
    const connect = async url => {
        const extension = new URL(url).searchParams.get('extension');
        const base = extension.slice(0, -'/bridge.js'.length);
        const [project, map] = await Promise.all([
            fetch(base + '/project.sb3').then(r => r.arrayBuffer()),
            fetch(base + '/debug.json').then(r => r.json())
        ]);
        const vm = new VM(); active.push({vm});
        await vm.loadProject(Buffer.from(project));
        const socket = new WebSocket(base.replace(/^http/, 'ws') + '/socket');
        active.at(-1).socket = socket;
        await once(socket, 'open');
        if (map) {
            const engine = create(vm, map, e => socket.send(JSON.stringify(e)));
            active.at(-1).engine = engine;
            socket.addEventListener('message', async e => {
                const m = JSON.parse(e.data);
                try { socket.send(JSON.stringify({id:m.id, result:await engine[m.method](...(m.args || []))})); }
                catch (err) { socket.send(JSON.stringify({id:m.id, error:err.message})); }
            });
            active.at(-1).pump = setInterval(() => { if (engine.running && !engine.paused) vm.runtime._step(); }, 1);
        }
        socket.send(JSON.stringify({event:'ready'}));
    };
    disposables.push(vscode.debug.registerDebugAdapterTrackerFactory('scratch-llvm', {
        createDebugAdapterTracker(session) {
            current.session = session;
            const state = current;
            return {onDidSendMessage(message) {
                state.messages.push(message);
                fs.appendFileSync(path.join(root,'build/validation/vscode-player-dap.jsonl'),JSON.stringify(message)+'\n');
                const url = message.event === 'output' && /Opening TurboWarp: (\S+)/.exec(message.body?.output || '');
                if (url) connect(url[1]).catch(e => { state.error=e; });
            }, onError(error) { state.error=error; }};
        }
    }));
    try {
        const folder = vscode.workspace.workspaceFolders[0];
        assert(folder, 'Test workspace missing');
        for (const mode of ['debug', 'run', 'noDebug']) {
            current = {messages:[], mode};
            const configuration = {
                name:'Activation regression ' + mode, type:'scratch-llvm', request:'launch',
                mode: mode === 'noDebug' ? 'debug' : mode,
                project:path.join(root,'build/validation/debugger/program.sb3'),
                debugMap:path.join(root,'build/validation/debugger/program.debug.json'),
                clang:path.join(root,'build/toolchains/windows/root/clang64/bin/clang.exe'),
                lldbDap:process.env.SCRATCH_LLDB_DAP || 'C:/Program Files/LLVM/bin/lldb-dap.exe',
                lldbPython:path.join(root,'build/debugger/python311'),
                scrate:process.env.SCRATE_EXECUTABLE || 'scrate',
                noOpen:true, port:18857, stopOnEntry:true
            };
            assert(await vscode.debug.startDebugging(folder, configuration, {noDebug: mode === 'noDebug'}), 'VS Code refused to start ' + mode);
            const deadline = Date.now()+30000;
            while (!current.messages.some(m => mode === 'debug' ? m.event === 'stopped' :
                m.type === 'response' && m.command === 'launch' && m.success)) {
                if (current.error) throw current.error;
                if (Date.now()>deadline) throw new Error('Actual VS Code adapter did not reach ' + mode);
                await delay(20);
            }
            const initialize = current.messages.find(m=>m.type==='response' && m.command==='initialize');
            assert(initialize?.success, 'Adapter initialize failed');
            assert(current.messages.some(m=>m.event==='scrppPlayer'), 'Embedded player was not requested');
            if (mode==='debug') {
                const frames = await current.session.customRequest('stackTrace',{threadId:1});
                assert.equal(frames.stackFrames[0].line,10);
                assert(initialize.body.$__lldb_version, 'Debug must use real LLDB');
            } else assert(!initialize.body.$__lldb_version, 'Run must not start LLDB');
            if(mode!=='debug') {
                const deadline=Date.now()+15000;
                while(!current.messages.some(m=>m.event==='scrppRunStopped')) {
                    if(Date.now()>deadline)throw new Error('Run did not finish in embedded stage');
                    await delay(20);
                }
                await delay(500);
                assert(!current.messages.some(m=>m.event==='terminated'), 'Run completion closed the stage');
                assert(vscode.window.tabGroups.all.some(g=>g.tabs.some(t=>t.label==='sCr++ · '+current.session.name)), 'Completed stage tab missing');
                results.push({mode,completedStageRetained:true});
            }
            results.push({mode,passed:true,launcher:'installed-scrate'});
            if(mode==='noDebug') {
                const ended=new Promise(resolve=>{
                    const disposable=vscode.debug.onDidTerminateDebugSession(session=>{
                        if(session.id===current.session.id){disposable.dispose();resolve();}
                    });
                    disposables.push(disposable);
                });
                await vscode.commands.executeCommand('workbench.action.closeActiveEditor');
                let timer;
                try {await Promise.race([ended,new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error('Closing player did not stop session')),10000);})]);}
                finally {clearTimeout(timer);}
                results.push({closePlayerStopsSession:true});
            } else await vscode.debug.stopDebugging(current.session);
            await delay(150);
        }
        assert(vscode.extensions.getExtension('scratch-llvm.debugger').isActive);
        const uri = vscode.Uri.joinPath(folder.uri,'.vscode','launch.json');
        await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(uri));
        await delay(2500);
        const diagnostics = vscode.languages.getDiagnostics(uri).filter(d=>d.severity===vscode.DiagnosticSeverity.Error || /noDebug|mode/.test(d.message));
        assert.deepEqual(diagnostics.map(d=>d.message), [], 'launch.json schema errors');
        results.push({launchSchema:true});
    } catch (error) { failure=error; }
    finally {
        for (const item of active) { clearInterval(item.pump);item.engine?.dispose();item.vm.stopAll();item.socket?.close(); }
        for (const disposable of disposables) disposable.dispose();
        await vscode.debug.stopDebugging();
        fs.writeFileSync(path.join(root,'build/validation/vscode-extension-report.json'), JSON.stringify({passed:!failure,error:failure?.stack,results},null,2));
    }
    if (failure) throw failure;
};
