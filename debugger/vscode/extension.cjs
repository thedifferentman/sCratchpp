'use strict';
const vscode = require('vscode');
const path = require('node:path');
const fs = require('node:fs');

function activate(context) {
    const panels=new Map();
    context.subscriptions.push(vscode.debug.registerDebugConfigurationProvider('scratch-llvm', {
        resolveDebugConfiguration(_folder, config) {
            return {...config, embeddedPlayer:true, noOpen:true, port:0, turbowarp:undefined};
        }
    }));
    context.subscriptions.push(vscode.debug.registerDebugAdapterTrackerFactory('scratch-llvm', {
        createDebugAdapterTracker(session) {
            return {onDidSendMessage(message) {
                if(message.event!=='scrppPlayer')return;
                const url=new URL(message.body.url);
                if(url.protocol!=='http:'||url.hostname!=='127.0.0.1'||!/^\/\w{48}\/player.html$/.test(url.pathname))return;
                if(panels.has(session.id))return;
                const panel=vscode.window.createWebviewPanel('scrppPlayer','sCr++ · '+session.name,
                    vscode.ViewColumn.Beside,{enableScripts:true,retainContextWhenHidden:true,localResourceRoots:[]});
                panels.set(session.id,panel);
                panel.webview.html=`<!doctype html><html><head><meta charset="utf-8"><meta http-equiv="Content-Security-Policy" content="default-src 'none'; frame-src ${url.origin}; style-src 'unsafe-inline';"><style>html,body,iframe{width:100%;height:100%;padding:0;margin:0;border:0;overflow:hidden}</style></head><body><iframe title="sCr++ 舞台" src="${url.href}" allow="autoplay" sandbox="allow-scripts allow-same-origin allow-pointer-lock allow-downloads"></iframe></body></html>`;
                panel.onDidDispose(()=>{if(panels.delete(session.id))void vscode.debug.stopDebugging(session);});
            }};
        }
    }));
    context.subscriptions.push(vscode.debug.onDidTerminateDebugSession(session=>{
        const panel=panels.get(session.id);panels.delete(session.id);panel?.dispose();
    }));
    context.subscriptions.push({dispose(){for(const panel of panels.values())panel.dispose();panels.clear();}});
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('scratch-llvm', {
        createDebugAdapterDescriptor(session) {
            // Packaged VSIX has runtime/. Development checkout uses ../.
            let entry = path.join(context.extensionPath, 'runtime', 'scratch-debug.cjs');
            if (!fs.existsSync(entry)) entry = path.join(context.extensionPath, '..', 'scratch-debug.cjs');
            if (!fs.existsSync(entry)) throw new Error('Scratch LLVM debugger runtime missing. Rebuild and reinstall the VSIX.');
            const workspace = session.workspaceFolder?.uri.fsPath;
            const run = session.configuration.mode === 'run' || session.configuration.noDebug;
            const node = session.configuration.node || process.execPath;
            const args = [run ? 'run' : 'debug', '--dap', '--no-build', '--node', node, '--debugger', entry];
            if (workspace) args.push('--manifest', path.join(workspace, 'sCrpp.toml'));
            if (!run && session.configuration.lldbDap) args.push('--lldb-dap', session.configuration.lldbDap);
            if (!run && session.configuration.lldbPython) args.push('--lldb-python', session.configuration.lldbPython);
            const executable = session.configuration.scrate || 'scrate';
            return new vscode.DebugAdapterExecutable(executable, args, {
                env: {ELECTRON_RUN_AS_NODE: '1'}, cwd: workspace
            });
        }
    }));
}
module.exports = {activate, deactivate() {}};
