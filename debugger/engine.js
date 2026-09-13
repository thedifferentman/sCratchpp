/* Scratch LLVM interpreter debugger. No project blocks are added or changed.
 * The execute-cache read is immediately before execute.js evaluates reporters.
 * A private sentinel unwinds to our stepThreads wrapper at that safe boundary;
 * the original sequencer and primitives retain all continuation bookkeeping.
 */
(function (root, factory) {
    'use strict';
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.ScratchLLVMEngine = factory();
}(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    'use strict';
    const pathKey = value => {
        let path = String(value || '').replace(/\\/g, '/').replace(/\/+/g, '/');
        if (/^[a-z]:\//i.test(path)) path = path.toLowerCase();
        return path;
    };
    const basename = path => String(path).replace(/\\/g, '/').split('/').pop();
    const own = (object, key) => Object.prototype.hasOwnProperty.call(object, key);
    class Engine {
        constructor(vm, map, onEvent, options = {}) {
            this.vm = vm;
            this.runtime = vm.runtime;
            this.emit = typeof onEvent === 'function' ? onEvent : () => {};
            this.options = {sliceMs: 8, ...options};
            this.paused = false;
            this.running = false;
            this.disposed = false;
            this.mode = 'continue';
            this.breakpoints = new Map();
            this.instructionBreakpoints = new Set();
            this.breakpointSerial = 1;
            this.contextSerial = 1;
            this.contexts = new WeakMap();
            this.lastPoints = new WeakMap();
            this.references = new Map();
            this.referenceSerial = 1;
            this.cacheHooks = new Map();
            this.sentinel = Object.freeze({scratchLLVMDebugYield: true});
            const runtime = this.runtime;
            const sequencer = runtime && runtime.sequencer;
            if (!sequencer || typeof sequencer.stepThreads !== 'function' ||
                typeof sequencer.stepThread !== 'function' || typeof sequencer.stepToProcedure !== 'function' ||
                !Array.isArray(runtime.threads) || !Array.isArray(runtime.targets)) {
                throw new Error('This TurboWarp VM does not expose the required interpreter capabilities.');
            }
            if (runtime.threads.some(thread => thread.isCompiled)) {
                throw new Error('Stop the project before attaching: compiled threads cannot be debugged.');
            }
            this.originalCompilerOptions = runtime.compilerOptions && {...runtime.compilerOptions};
            if (typeof vm.setCompilerOptions === 'function') vm.setCompilerOptions({enabled: false});
            else if (runtime.compilerOptions) runtime.compilerOptions.enabled = false;
            else throw new Error('This TurboWarp VM cannot disable compilation.');
            this.sequencer = sequencer;
            this.original = {
                stepThreads: sequencer.stepThreads,
                stepThread: sequencer.stepThread,
                stepToProcedure: sequencer.stepToProcedure
            };
            this.loadMap(map);
            const engine = this;
            this.wrappers = {
                stepThreads: function (...args) {
                    if (engine.paused) return [];
                    engine.deadline = Date.now() + Math.max(1, engine.options.sliceMs);
                    try { return engine.original.stepThreads.apply(this, args); }
                    catch (error) { if (error !== engine.sentinel) throw error; return []; }
                    finally { engine.executingThread = null; this.activeThread = null; }
                },
                stepThread: function (thread, ...args) {
                    if (engine.paused) throw engine.sentinel;
                    if (thread.isCompiled) throw new Error('LLVM debugging requires TurboWarp compilation to remain disabled.');
                    engine.hookCache(thread.blockContainer);
                    engine.executingThread = thread;
                    try { return engine.original.stepThread.call(this, thread, ...args); }
                    finally { engine.executingThread = null; }
                },
                stepToProcedure: function (thread, code, ...args) {
                    const functionName = engine.bodyFunctions.get(code);
                    if (functionName) {
                        const frame = thread.peekStackFrame();
                        engine.contexts.set(frame, {
                            id: engine.contextSerial++, function: functionName,
                            frameAddress: Number(frame.params && frame.params.frame),
                            callsite: engine.lastPoints.get(thread) || null, point: null
                        });
                    }
                    return engine.original.stepToProcedure.call(this, thread, code, ...args);
                }
            };
            for (const name of Object.keys(this.wrappers)) sequencer[name] = this.wrappers[name];
            this.onStop = () => {
                if (!this.running) return;
                this.running = false;
                this.thaw();
                const variables = runtime.targets.flatMap(target => Object.values(target.variables || {}));
                const exit = variables.find(variable => variable.name === 'exit_code' && variable.type === '');
                const status = variables.find(variable => variable.name === '__scl_status' && variable.type === '');
                const exitCode = exit && Number.isSafeInteger(Number(exit.value)) ? Number(exit.value) : 0;
                this.emit({event: 'terminated', body: {exitCode, runtimeStatus: status && status.value}});
            };
            runtime.on('PROJECT_RUN_STOP', this.onStop);
            for (const target of runtime.targets) this.hookCache(target.blocks);
        }

        loadMap(map) {
            if (!map || typeof map.points !== 'object' || !map.functions) throw new Error('Missing LLVM debug map points/functions.');
            this.map = map;
            this.points = map.points;
            this.bodyFunctions = new Map(Object.entries(map.functions).map(([name, fn]) => [fn.body, name]));
            this.files = new Map();
            for (const [id, point] of Object.entries(this.points)) {
                if (!point.file || !Number.isInteger(point.line) || point.line < 1) continue;
                const key = pathKey(point.file);
                if (!this.files.has(key)) this.files.set(key, []);
                this.files.get(key).push({...point, id});
            }
        }

        hookCache(blocks) {
            if (!blocks) return;
            if (!blocks._cache || !blocks._cache._executeCached || typeof blocks.getBlock !== 'function') {
                throw new Error('Unsupported TurboWarp interpreter execute cache: cannot install safe execution hook.');
            }
            const current = blocks._cache._executeCached;
            const prior = this.cacheHooks.get(blocks);
            if (prior && current === prior.proxy) return;
            const engine = this;
            const proxy = new Proxy(current, {
                get(target, id, receiver) {
                    const thread = engine.executingThread;
                    if (thread && thread.blockContainer === blocks && typeof id === 'string' && id === thread.peekStack()) {
                        engine.boundary(thread, id);
                    }
                    return Reflect.get(target, id, receiver);
                }
            });
            blocks._cache._executeCached = proxy;
            this.cacheHooks.set(blocks, {proxy, original: current});
        }

        activeFrames(thread = this.stoppedThread) {
            if (!thread || !thread.target) return [];
            const result = [];
            // A procedure call is active only while its child stack exists.
            // StackFrame objects are pooled by the VM, so an old WeakMap entry
            // must never make a not-yet-executed/recycled call look active.
            for (let i = 0; i + 1 < thread.stack.length; ++i) {
                const context = this.contexts.get(thread.stackFrames[i]);
                if (!context) continue;
                const block = thread.target.blocks.getBlock(thread.stack[i]);
                if (!block || !block.mutation || this.bodyFunctions.get(block.mutation.proccode) !== context.function) continue;
                result.push(context);
            }
            return result;
        }

        boundary(thread, id) {
            this.boundaryCount = (this.boundaryCount || 0) + 1;
            if (this.paused) throw this.sentinel;
            if (Date.now() >= this.deadline) throw this.sentinel;
            const point = this.points[id];
            if (!point) return;
            const frames = this.activeFrames(thread);
            const frame = frames[frames.length - 1];
            const current = {...point, id, invocation: frame ? frame.id : 0};
            if (frame) frame.point = current;
            const previous = this.lastPoints.get(thread);
            const changed = !previous || previous.invocation !== current.invocation ||
                pathKey(previous.file) !== pathKey(current.file) || previous.line !== current.line ||
                previous.id === current.id ||
                (Number.isFinite(previous.ordinal) && Number.isFinite(current.ordinal) && current.ordinal <= previous.ordinal);
            const skip = this.skip && this.skip.thread === thread && this.skip.id === id && this.skip.invocation === current.invocation;
            if (skip) this.skip = null;
            let reason;
            if (!skip) {
                const breakpoints = this.breakpoints.get(pathKey(point.file));
                if (changed && breakpoints && breakpoints.some(bp => bp.verified && bp.line === point.line)) reason = 'breakpoint';
                if (this.instructionBreakpoints.has(id)) reason = 'breakpoint';
                const sourceEntry = this.mode === 'entry' && this.entrySource;
                if (this.pauseRequested && (!sourceEntry || (point.file && point.line > 0 &&
                    (!this.entryFunction || point.function === this.entryFunction))))
                    reason = this.mode === 'entry' ? 'entry' : 'pause';
                if (!reason && this.mode === 'stepInstruction') reason = 'step';
                if (!reason && changed && point.file && point.line > 0) {
                    if (this.mode === 'stepIn') reason = 'step';
                    else if (this.mode === 'next' && (!this.stepFrame ||
                        current.invocation === this.stepFrame || !frames.some(f => f.id === this.stepFrame))) reason = 'step';
                    else if (this.mode === 'stepOut' && !frames.some(f => f.id === this.stepFrame)) reason = 'step';
                }
            }
            if (reason) {
                this.stop(thread, current, reason);
                throw this.sentinel;
            }
            this.lastPoints.set(thread, current);
        }

        stop(thread, point, reason) {
            this.paused = true;
            this.pauseRequested = false;
            this.stoppedThread = thread;
            this.stoppedPoint = point;
            this.pauseTime = Date.now();
            this.references.clear();
            this.clockWasPaused = !!(this.runtime.ioDevices.clock && this.runtime.ioDevices.clock._paused);
            if (this.runtime.ioDevices.clock && !this.clockWasPaused && typeof this.runtime.ioDevices.clock.pause === 'function') {
                this.runtime.ioDevices.clock.pause();
            }
            this.emit({event: 'stopped', body: {reason, threadId: 1, allThreadsStopped: true,
                location: this.irLocation(point), irFrames: this.irFrames()}});
        }

        thaw() {
            if (!this.paused) return;
            const elapsed = Date.now() - this.pauseTime;
            const timers = new Set();
            for (const thread of this.runtime.threads) {
                if (thread.warpTimer) timers.add(thread.warpTimer);
                for (const frame of thread.stackFrames) {
                    if (frame.executionContext && frame.executionContext.timer) timers.add(frame.executionContext.timer);
                }
            }
            for (const timer of timers) if (typeof timer.startTime === 'number') timer.startTime += elapsed;
            const clock = this.runtime.ioDevices.clock;
            // stepThreads is suspended, so its usual clock refresh did not run
            // during the pause. Refresh before Clock.resume calculates the gap.
            if (typeof this.runtime.updateCurrentMSecs === 'function') this.runtime.updateCurrentMSecs();
            if (clock && !this.clockWasPaused && typeof clock.resume === 'function') clock.resume();
            this.paused = false;
        }

        setBreakpoints(file, lines) {
            if (file && typeof file === 'object') file = file.path;
            const available = this.files.get(pathKey(file)) || [];
            const result = lines.map(request => {
                const line = typeof request === 'object' ? request.line : request;
                const executable = available.filter(p => p.line >= line).sort((a, b) => a.line - b.line)[0];
                return {id: this.breakpointSerial++, verified: !!executable, line: executable ? executable.line : line,
                    ...(executable ? {} : {message: 'No executable source location at or after this line.'})};
            });
            this.breakpoints.set(pathKey(file), result);
            return result;
        }
        setInstructionBreakpoints(ids) {
            this.instructionBreakpoints = new Set(ids.filter(id => own(this.points, id)));
            return ids.map(id => ({id, verified: this.instructionBreakpoints.has(id)}));
        }
        setIRBreakpoints(locations) {
            const key = location => JSON.stringify([location.function, location.instruction]);
            const requested = new Set(locations.map(key));
            const installed = new Set();
            this.instructionBreakpoints.clear();
            for (const [id, point] of Object.entries(this.points)) {
                if (requested.has(key(point))) {
                    installed.add(key(point));
                    this.instructionBreakpoints.add(id);
                }
            }
            return locations.map(location => ({...this.irLocation(location), verified: installed.has(key(location))}));
        }

        start(options = {}) {
            if (this.disposed) throw new Error('Debugger has been disposed.');
            this.thaw();
            this.lastPoints = new WeakMap();
            this.contexts = new WeakMap();
            this.skip = null;
            this.stoppedPoint = null;
            this.stoppedThread = null;
            this.running = true;
            this.mode = options.stopOnEntry ? 'entry' : 'continue';
            this.entrySource = !!options.sourceEntry && Object.values(this.points).some(p => p.file && p.line > 0);
            this.entryFunction = Object.values(this.points).some(p => p.function === 'main' && p.file && p.line > 0) ? 'main' : null;
            this.pauseRequested = !!options.stopOnEntry;
            this.vm.greenFlag();
        }

        resume(mode) {
            if (!this.running) throw new Error('The debug program is not running.');
            if (mode !== 'continue' && !this.paused) throw new Error('Pause the program before stepping.');
            this.mode = mode;
            this.pauseRequested = false;
            if (this.paused) {
                this.skip = {thread: this.stoppedThread, id: this.stoppedPoint.id, invocation: this.stoppedPoint.invocation};
                this.stepFrame = this.stoppedPoint.invocation;
                this.thaw();
            }
            this.emit({event: 'continued', body: {threadId: 1, allThreadsContinued: true}});
        }
        continue() { this.resume('continue'); }
        next() { this.resume('next'); }
        stepIn() { this.resume('stepIn'); }
        stepOut() { this.resume('stepOut'); }
        stepInstruction() { this.resume('stepInstruction'); }
        pause() { if (this.running && !this.paused) { this.pauseRequested = true; this.mode = 'pause'; } }
        terminate() {
            this.thaw();
            this.vm.stopAll();
            this.onStop();
        }
        getState() {
            return {status: this.paused ? 'stopped' : this.running ? 'running' : 'terminated',
                pointId: this.stoppedPoint && this.stoppedPoint.id, frames: this.stackTrace()};
        }
        irLocation(point) {
            return point ? {function: point.function, instruction: point.instruction} : null;
        }
        irFrames() {
            return this.stackTrace().map(frame => ({function: frame.function,
                instruction: frame.point.instruction, frameAddress: frame.frameAddress, instanceId: frame.id}));
        }
        getIRState() {
            return {status: this.paused ? 'stopped' : this.running ? 'running' : 'terminated',
                location: this.paused ? this.irLocation(this.stoppedPoint) : null, frames: this.irFrames()};
        }

        stackTrace() {
            if (!this.paused) return [];
            return this.activeFrames().reverse().map(context => {
                const fn = this.map.functions[context.function];
                const point = {...(fn.source || {}), ...(context.point || {})};
                return {id: context.id, name: (fn.source && fn.source.name) || context.function,
                    source: point.file ? {name: basename(point.file), path: point.file} : undefined,
                    line: point.line || 1, column: point.column || 1,
                    instructionPointerReference: point.id,
                    function: context.function, frameAddress: context.frameAddress, point};
            });
        }

        memory() {
            const name = this.map.memoryList || '__scl_memory';
            for (const target of this.runtime.targets) {
                for (const variable of Object.values(target.variables || {})) {
                    if (variable.type === 'list' && variable.name === name) return variable.value;
                }
            }
            throw new Error('Program byte-memory list is not available.');
        }
        readMemory(address, offset = 0, count = 0) {
            if (!this.paused) throw new Error('Memory can only be read while paused.');
            const start = Number(address) + Number(offset);
            if (!Number.isSafeInteger(start) || start < 1 || !Number.isSafeInteger(count) || count < 0 || count > 65536) {
                throw new Error('Invalid memory range (addresses are one-based, maximum read is 65536 bytes).');
            }
            return this.memory().slice(start - 1, start - 1 + count).map(byte => Number(byte) & 255);
        }

        reference(value) { const id = this.referenceSerial++; this.references.set(id, value); return id; }
        scopes(frameId) {
            const frame = this.stackTrace().find(value => value.id === frameId);
            if (!frame) throw new Error('Unknown or expired stack frame.');
            return [{name: 'LLVM slots', presentationHint: 'locals', expensive: false,
                variablesReference: this.reference({kind: 'slots', frame})}];
        }
        variables(reference, start = 0, count = 100) {
            if (!this.paused) throw new Error('Variables can only be inspected while paused.');
            const entry = this.references.get(reference);
            if (!entry) return [];
            if (entry.kind === 'bytes') return entry.bytes.slice(start, start + count).map((byte, i) => ({
                name: String(start + i), value: String(byte), type: 'byte', variablesReference: 0
            }));
            const fn = this.map.functions[entry.frame.function];
            return Object.entries(fn.slots || {}).slice(start, start + count).map(([name, slot]) => {
                const address = entry.frame.frameAddress + slot.offset;
                return this.value(name, slot.type, address);
            });
        }
        value(name, type, address) {
            type = type || {};
            const size = Number(type.size || type.store_size || Math.ceil((type.bits || 8) / 8));
            const bytes = this.readMemory(address, 0, Math.min(65536, size));
            let value, children = 0;
            let typeName = type.kind || 'bytes';
            if (bytes.length < size) value = '<outside memory>';
            else if (type.kind === 'int' || type.kind === 'ptr' || type.kind === 'pointer') {
                let integer = 0n;
                for (let i = bytes.length - 1; i >= 0; --i) integer = (integer << 8n) | BigInt(bytes[i]);
                if (type.kind === 'int') {
                    typeName = `i${type.bits || size * 8}`;
                    value = BigInt.asIntN(type.bits || size * 8, integer).toString();
                } else value = '0x' + integer.toString(16);
            } else if ((type.kind === 'float' || type.kind === 'double') && (size === 4 || size === 8)) {
                const view = new DataView(Uint8Array.from(bytes).buffer);
                value = String(size === 4 ? view.getFloat32(0, true) : view.getFloat64(0, true));
            } else {
                value = bytes.slice(0, 32).map(b => b.toString(16).padStart(2, '0')).join(' ') + (bytes.length > 32 ? ' …' : '');
                children = this.reference({kind: 'bytes', bytes});
            }
            return {name, value, type: typeName, variablesReference: children,
                memoryReference: String(address), ...(children ? {indexedVariables: bytes.length} : {})};
        }

        dispose() {
            if (this.disposed) return;
            this.thaw();
            this.disposed = true;
            this.runtime.removeListener('PROJECT_RUN_STOP', this.onStop);
            for (const name of Object.keys(this.original)) {
                if (this.sequencer[name] === this.wrappers[name]) this.sequencer[name] = this.original[name];
            }
            for (const [blocks, hook] of this.cacheHooks) {
                if (blocks._cache._executeCached === hook.proxy) blocks._cache._executeCached = hook.original;
            }
            if (this.originalCompilerOptions && typeof this.vm.setCompilerOptions === 'function') {
                this.vm.setCompilerOptions(this.originalCompilerOptions);
            }
        }
    }
    return {Engine, create: (vm, map, onEvent, options) => new Engine(vm, map, onEvent, options), pathKey};
}));
