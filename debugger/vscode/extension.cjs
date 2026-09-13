'use strict';
const vscode = require('vscode');
const path = require('node:path');
const fs = require('node:fs');

function activate(context) {
    context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory('scratch-llvm', {
        createDebugAdapterDescriptor(session) {
            // Packaged VSIX has runtime/. Development checkout uses ../.
            let entry = path.join(context.extensionPath, 'runtime', 'scratch-debug.cjs');
            if (!fs.existsSync(entry)) entry = path.join(context.extensionPath, '..', 'scratch-debug.cjs');
            if (!fs.existsSync(entry)) throw new Error('Scratch LLVM debugger runtime missing. Rebuild and reinstall the VSIX.');
            const args = [entry, '--dap'];
            if (session.configuration.lldbDap) args.push('--lldb-dap', session.configuration.lldbDap);
            if (session.configuration.lldbPython) args.push('--lldb-python', session.configuration.lldbPython);
            if (session.configuration.mode === 'run' || session.configuration.noDebug) args.push('--no-debug');
            return new vscode.DebugAdapterExecutable(session.configuration.node || process.execPath, args, {
                env: {ELECTRON_RUN_AS_NODE: '1'}, cwd: session.workspaceFolder?.uri.fsPath
            });
        }
    }));
}
module.exports = {activate, deactivate() {}};
