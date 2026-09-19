'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');

const extension = path.resolve(__dirname, '../vscode/extension.cjs');
function fixture(t, withOldScript = false) {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'scrate adapter '));
    t.after(() => {
        assert(path.resolve(root).startsWith(path.resolve(os.tmpdir()) + path.sep));
        fs.rmSync(root, {recursive: true, force: true});
    });
    const extensionPath = path.join(root, 'extension');
    const workspace = path.join(root, 'workspace');
    fs.mkdirSync(path.join(extensionPath, 'runtime'), {recursive: true});
    fs.mkdirSync(workspace);
    fs.writeFileSync(path.join(extensionPath, 'runtime/scratch-debug.cjs'), '');
    if (withOldScript) fs.writeFileSync(path.join(workspace, 'scrate.py'), 'must not execute');
    let factory;
    class DebugAdapterExecutable {
        constructor(command, args, options) { Object.assign(this, {command, args, options}); }
    }
    const vscode = {DebugAdapterExecutable, debug: {
        registerDebugConfigurationProvider() { return {dispose(){}}; },
        registerDebugAdapterTrackerFactory() { return {dispose(){}}; },
        onDidTerminateDebugSession() { return {dispose(){}}; },
        registerDebugAdapterDescriptorFactory(type, item) {
            assert.equal(type, 'scratch-llvm'); factory = item; return {dispose() {}};
        }
    }};
    const sandbox = {module: {exports: {}}, process, require: name => name === 'vscode' ? vscode : require(name)};
    vm.runInNewContext(fs.readFileSync(extension, 'utf8'), sandbox, {filename: extension});
    const context = {extensionPath, subscriptions: []};
    sandbox.module.exports.activate(context);
    return {extensionPath, workspace, create(configuration = {}) {
        return factory.createDebugAdapterDescriptor({workspaceFolder: {uri: {fsPath: workspace}}, configuration});
    }};
}

test('Debug directly uses installed scrate and forwards debugger tools', t => {
    const f = fixture(t);
    const result = f.create({node: '/node custom', lldbDap: '/lldb dap', lldbPython: '/lldb python'});
    assert.equal(result.command, 'scrate');
    assert.deepEqual(Array.from(result.args), ['debug', '--dap', '--no-build', '--node', '/node custom',
        '--debugger', path.join(f.extensionPath, 'runtime/scratch-debug.cjs'),
        '--manifest', path.join(f.workspace, 'sCrpp.toml'), '--lldb-dap', '/lldb dap', '--lldb-python', '/lldb python']);
    assert.equal(result.options.env.ELECTRON_RUN_AS_NODE, '1');
    assert.equal(result.options.cwd, f.workspace);
});

test('installed executable path can be overridden without shell quoting', t => {
    const result = fixture(t).create({scrate: '/installed tools/scrate'});
    assert.equal(result.command, '/installed tools/scrate');
    assert.equal(result.args[0], 'debug');
});

test('Run uses scrate run without LLDB arguments', t => {
    const result = fixture(t).create({mode: 'run', lldbDap: '/unused', lldbPython: '/unused'});
    assert.equal(result.command, 'scrate');
    assert.equal(result.args[0], 'run');
    assert(!result.args.includes('--lldb-dap'));
    assert(!result.args.includes('--lldb-python'));
    assert(!result.args.includes('--no-debug'));
    assert.equal(result.args[result.args.indexOf('--node') + 1], process.execPath);
});

test('Run Without Debugging selects scrate run', t => {
    assert.equal(fixture(t).create({noDebug: true}).args[0], 'run');
});

test('old project-local launch scripts are not executed', t => {
    const f = fixture(t, true);
    const result = f.create();
    assert.equal(result.command, 'scrate');
    assert(!result.args.includes(path.join(f.workspace, 'scrate.py')));
});

test('schema exposes installed scrate override instead of Python launcher', () => {
    const manifest = JSON.parse(fs.readFileSync(path.resolve(__dirname, '../vscode/package.json'), 'utf8'));
    const properties = manifest.contributes.debuggers[0].configurationAttributes.launch.properties;
    assert.equal(manifest.version, '0.1.8');
    assert.equal(properties.scrate.type, 'string');
    assert.equal(properties.python, undefined);
});

test('template contains no forwarding scripts and tasks invoke scrate directly', () => {
    const root = path.resolve(__dirname, '../../template');
    assert(!fs.existsSync(path.join(root, 'build.py')));
    assert(!fs.existsSync(path.join(root, 'scrate.py')));
    const tasks = JSON.parse(fs.readFileSync(path.join(root, '.vscode/tasks.json'), 'utf8')).tasks;
    assert(tasks.every(task => task.command === 'scrate' && !task.windows));
    assert.deepEqual(tasks[0].args, ['build']);
    assert.deepEqual(tasks[1].args, ['build', '--debug']);
    const launch = JSON.parse(fs.readFileSync(path.join(root, '.vscode/launch.json'), 'utf8')).configurations;
    assert(launch.every(config => config.type === 'scratch-llvm'));
});
