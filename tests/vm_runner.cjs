#!/usr/bin/env node
'use strict';

// Actual VM execution lives in a child process: a compiled, non-yielding warp
// script must not be able to disable the timeout of the test controller.
const fs = require('node:fs');
const path = require('node:path');
const {fork} = require('node:child_process');
const {performance} = require('node:perf_hooks');

const USAGE = 'node tests/vm_runner.cjs project.sb3 [--vm scratch|turbowarp] [--timeout ms] [--list-limit count] [--warp-timer]';

function parseArgs(args) {
    const options = {vm: 'scratch', timeout: 30000, listLimit: null, warpTimer: false};
    let filename;
    for (let i = 0; i < args.length; ++i) {
        const arg = args[i];
        if (arg === '--help' || arg === '-h') return {help: true};
        if (arg === '--vm') options.vm = args[++i];
        else if (arg === '--timeout') options.timeout = Number(args[++i]);
        else if (arg === '--list-limit') options.listLimit = Number(args[++i]);
        else if (arg === '--warp-timer') options.warpTimer = true;
        else if (arg.startsWith('-') || filename) throw new Error(`Unexpected argument: ${arg}`);
        else filename = arg;
    }
    if (!filename) throw new Error(USAGE);
    if (!['scratch', 'turbowarp'].includes(options.vm)) throw new Error('--vm must be scratch or turbowarp');
    if (!Number.isSafeInteger(options.timeout) || options.timeout < 1) throw new Error('--timeout must be a positive integer');
    if (options.listLimit !== null && (!Number.isSafeInteger(options.listLimit) || options.listLimit < 0)) {
        throw new Error('--list-limit must be a nonnegative integer');
    }
    return {filename: path.resolve(filename), options};
}

function emptyResult(filename, options, status, error) {
    return {
        schemaVersion: 1, project: filename, vm: options.vm,
        mode: options.vm === 'turbowarp' ? 'compiled' : 'interpreted',
        status, error, variables: {}, lists: {}, listLengths: {}, targets: []
    };
}

function runProject(filename, options = {}) {
    options = {vm: 'scratch', timeout: 30000, listLimit: null, warpTimer: false, ...options};
    filename = path.resolve(filename);
    return new Promise(resolve => {
        const began = performance.now();
        const child = fork(__filename, ['--worker', filename, JSON.stringify(options)], {
            stdio: ['ignore', 'pipe', 'pipe', 'ipc'],
            windowsHide: true
        });
        let finished = false;
        let timer;
        let stderr = '';
        let phase = 'loading';
        const finish = result => {
            if (finished) return;
            finished = true;
            clearTimeout(timer);
            result.totalMs = performance.now() - began;
            if (stderr) result.diagnostics = stderr;
            if (child.connected) child.disconnect();
            child.kill();
            resolve(result);
        };
        const deadline = ms => {
            clearTimeout(timer);
            timer = setTimeout(() => finish(emptyResult(filename, options, 'timeout',
                `Exceeded ${ms} ms during ${phase}`)), ms);
        };
        // Module loading is not counted as program execution. It still has a
        // separate hard deadline, including loadProject and extension loading.
        deadline(30000);
        const capture = chunk => { stderr = (stderr + chunk.toString()).slice(-16000); };
        child.stdout.on('data', capture);
        child.stderr.on('data', capture);
        child.on('message', message => {
            if (message.kind === 'running') {
                phase = 'execution';
                deadline(options.timeout);
            } else if (message.kind === 'result') finish(message.result);
        });
        child.on('error', error => finish(emptyResult(filename, options, 'error', error.message)));
        child.on('exit', (code, signal) => {
            if (!finished) finish(emptyResult(filename, options, 'error',
                `VM worker exited before returning a result (code=${code}, signal=${signal})`));
        });
    });
}

function snapshot(vm, listLimit) {
    const targets = vm.runtime.targets.filter(t => t.isOriginal !== false).map(target => {
        const variables = {};
        const lists = {};
        const listLengths = {};
        const variableIds = {};
        const listIds = {};
        for (const [id, variable] of Object.entries(target.variables)) {
            if (variable.type === 'list') {
                lists[variable.name] = listLimit === null ? variable.value : variable.value.slice(0, listLimit);
                listLengths[variable.name] = variable.value.length;
                listIds[variable.name] = id;
            } else if (variable.type === '') {
                variables[variable.name] = variable.value;
                variableIds[variable.name] = id;
            }
        }
        return {id: target.id, name: target.getName(), isStage: target.isStage,
            variables, lists, listLengths, variableIds, listIds};
    });
    // Unqualified names are available only when unique. Full target maps retain
    // every value even when multiple targets define the same display name.
    const unique = key => {
        const result = {};
        const duplicates = new Set();
        for (const target of targets) {
            for (const [name, value] of Object.entries(target[key])) {
                if (Object.hasOwn(result, name)) duplicates.add(name);
                else result[name] = value;
            }
        }
        for (const name of duplicates) delete result[name];
        return result;
    };
    return {targets, variables: unique('variables'), lists: unique('lists'), listLengths: unique('listLengths')};
}

async function worker(filename, options) {
    // VM logging must never contaminate the controller's JSON stdout.
    console.log = console.info = console.debug = (...args) => console.error(...args);
    let vm;
    let poll;
    let result;
    let started;
    let ticks = 0;
    let threadCount = 0;
    let compiledThreads = 0;
    let threadCompilationMs = 0;
    const compileErrors = [];
    const began = performance.now();
    try {
        const moduleName = options.vm === 'turbowarp' ? 'turbowarp-vm' : 'scratch-vm';
        const VirtualMachine = require(moduleName);
        const storageModule = require('scratch-storage');
        const ScratchStorage = storageModule.ScratchStorage || storageModule;
        vm = new VirtualMachine();
        vm.attachStorage(new ScratchStorage());
        vm.setCompatibilityMode(true);
        vm.setTurboMode(true);
        if (options.vm === 'turbowarp') {
            if (typeof vm.setCompilerOptions !== 'function') throw new Error('TurboWarp compiler API is unavailable');
            vm.setCompilerOptions({enabled: true, warpTimer: options.warpTimer});
            vm.on('COMPILE_ERROR', (target, error) => {
                compileErrors.push({target: target.getName(), message: String(error)});
            });
        }
        const pushThread = vm.runtime._pushThread;
        vm.runtime._pushThread = function (...args) {
            const creationStart = performance.now();
            const thread = pushThread.apply(this, args);
            if (!thread.updateMonitor) {
                ++threadCount;
                if (thread.isCompiled) {
                    ++compiledThreads;
                    threadCompilationMs += performance.now() - creationStart;
                }
            }
            return thread;
        };
        const step = vm.runtime._step;
        vm.runtime._step = function (...args) {
            ++ticks;
            return step.apply(this, args);
        };
        vm.start();
        await vm.loadProject(fs.readFileSync(filename));
        // Loading assets uses the real storage implementation. Rendering is
        // intentionally absent; no VM opcode or arithmetic is replaced.
        const loadMs = performance.now() - began;
        const assets = vm.runtime.targets.flatMap(target => target.sprite.costumes.map(costume => ({
            target: target.getName(), name: costume.name, assetId: costume.assetId,
            loaded: Boolean(costume.asset && costume.asset.data)
        })));
        process.send({kind: 'running'});
        started = performance.now();
        vm.greenFlag();
        await new Promise((resolve, reject) => {
            const inspect = () => {
                if (compileErrors.length) return reject(new Error('TurboWarp failed to compile a script'));
                if (!vm.runtime.threads.some(thread => !thread.updateMonitor)) return resolve();
                if (performance.now() - started > options.timeout) return reject(new Error('Execution timeout'));
            };
            poll = setInterval(inspect, 5);
            inspect();
        });
        const executionMs = performance.now() - started;
        result = {
            ...emptyResult(filename, options, 'completed', undefined),
            ...snapshot(vm, options.listLimit),
            loadMs, executionMs, threadCompilationMs,
            executionMinusThreadCompilationMs: executionMs - threadCompilationMs,
            ticks, threadCount, compiledThreads,
            compilerEnabled: options.vm === 'turbowarp', compilerOptions: vm.runtime.compilerOptions,
            rendering: false, compatibilityMode: true, turboMode: true, assets, listsTruncated: options.listLimit !== null,
            runtimeIdentity: options.vm === 'turbowarp'
                ? 'TurboWarp/scratch-vm@c4823421cb7c17d8d8a89878851ce1668c26a21f'
                : 'scratch-vm@5.0.300'
        };
    } catch (error) {
        result = {
            ...emptyResult(filename, options, error.message === 'Execution timeout' ? 'timeout' : 'error', error.stack),
            ...(vm ? snapshot(vm, options.listLimit) : {}),
            executionMs: started === undefined ? 0 : performance.now() - started,
            ticks, threadCount, compiledThreads, compileErrors
        };
    } finally {
        clearInterval(poll);
        if (vm) {
            vm.stopAll();
            vm.quit();
        }
    }
    const memory = process.memoryUsage();
    result.memory = {
        rssBytes: memory.rss, heapUsedBytes: memory.heapUsed, heapTotalBytes: memory.heapTotal,
        maxRssBytes: process.resourceUsage().maxRSS * 1024,
        heapLimitBytes: require('node:v8').getHeapStatistics().heap_size_limit
    };
    process.send({kind: 'result', result}, () => process.exit(result.status === 'completed' ? 0 : 1));
}

module.exports = {runProject, parseArgs};

if (require.main === module) {
    if (process.argv[2] === '--worker') {
        worker(process.argv[3], JSON.parse(process.argv[4])).catch(error => {
            console.error(error);
            process.exit(1);
        });
    } else {
        (async () => {
            try {
                const parsed = parseArgs(process.argv.slice(2));
                if (parsed.help) return console.log(USAGE);
                const result = await runProject(parsed.filename, parsed.options);
                console.log(JSON.stringify(result));
                process.exitCode = result.status === 'completed' ? 0 : 1;
            } catch (error) {
                console.error(error.message);
                process.exitCode = 2;
            }
        })();
    }
}
