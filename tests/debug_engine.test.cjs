'use strict';
const assert = require('node:assert/strict');
const VM = require('turbowarp-vm');
const {create} = require('../debugger/engine.js');

function projectFixture(forever = false, asynchronous = false) {
    const blocks = {}, points = {};
    let serial = 0;
    const literal = value => [1, [4, String(value)]];
    const make = (opcode, inputs = {}, fields = {}, extra = {}) => {
        const id = `b${++serial}`;
        blocks[id] = {opcode, next: null, parent: null, inputs, fields, shadow: false, topLevel: false, ...extra};
        for (const input of Object.values(inputs)) if (typeof input[1] === 'string') blocks[input[1]].parent = id;
        return id;
    };
    const chain = ids => { ids.forEach((id, i) => { if (i) blocks[ids[i - 1]].next = id; if (i) blocks[id].parent = ids[i - 1]; }); return ids[0]; };
    const change = amount => make('data_changevariableby', {VALUE: literal(amount)}, {VARIABLE: ['x', 'x']});
    const frame = () => make('argument_reporter_string_number', {}, {VALUE: ['frame', null]});
    const call = (name, argument) => make('procedures_call', {argframe: typeof argument === 'string' ? [2, argument] : literal(argument)}, {},
        {mutation: {tagName: 'mutation', children: [], proccode: name + ' %s', argumentids: '["argframe"]', warp: 'true'}});
    const procedure = (name, body) => {
        const prototype = make('procedures_prototype', {}, {}, {shadow: true,
            mutation: {tagName: 'mutation', children: [], proccode: name + ' %s', argumentids: '["argframe"]',
                argumentnames: '["frame"]', argumentdefaults: '[0]', warp: 'true'}});
        const definition = make('procedures_definition', {custom_block: [1, prototype]}, {}, {topLevel: true, x: 0, y: 0});
        chain([definition, ...body]);
    };
    const point = (id, fn, line, ordinal) => { points[id] = {function: fn, instruction: `i${ordinal}`, file: '/example.cpp', line, column: 1, ordinal}; return id; };
    const first = point(change(1), 'main', 10, 0);
    const increment = point(change(10), 'main', 11, 1);
    const loop = forever ? make('control_forever', {SUBSTACK: [2, increment]}) : make('control_repeat', {TIMES: literal(3), SUBSTACK: [2, increment]});
    const fooCall = point(call('foo', 3), 'main', 12, 2);
    const last = point(change(1000), 'main', 13, 3);
    const ask = asynchronous ? point(make('sensing_askandwait', {QUESTION: [1, [10, 'test']]}), 'main', 9, -1) : null;
    procedure('main', [...(ask ? [ask] : []), first, loop, fooCall, last]);
    const fooFirst = point(change(1), 'foo', 20, 0);
    const subtract = make('operator_subtract', {NUM1: [2, frame()], NUM2: literal(1)});
    const recurse = call('foo', subtract);
    const condition = make('operator_gt', {OPERAND1: [2, frame()], OPERAND2: literal(1)});
    const fooIf = point(make('control_if', {CONDITION: [2, condition], SUBSTACK: [2, recurse]}), 'foo', 21, 1);
    const fooLast = point(change(100), 'foo', 22, 2);
    procedure('foo', [fooFirst, fooIf, fooLast]);
    const hat = make('event_whenflagclicked', {}, {}, {topLevel: true, x: 0, y: 300});
    chain([hat, call('main', 100)]);
    return {
        project: {targets: [{isStage: true, name: 'Stage', variables: {x: ['x', 0]}, lists: {memory: ['__scl_memory', [1, 2, 3, 4]]},
            broadcasts: {}, blocks, comments: {}, currentCostume: 0, costumes: [], sounds: [], volume: 100,
            layerOrder: 0, tempo: 60, videoTransparency: 50, videoState: 'off', textToSpeechLanguage: null}], monitors: [], extensions: [], meta: {semver: '3.0.0'}},
        map: {points, functions: {
            main: {body: 'main %s', source: {file: '/example.cpp', line: 10, name: 'main'}, slots: {}},
            foo: {body: 'foo %s', source: {file: '/example.cpp', line: 20, name: 'foo'}, slots: {}}
        }}, ids: {first, increment, fooCall, last, fooFirst, fooIf, fooLast}
    };
}

async function setup(forever = false, asynchronous = false) {
    const vm = new VM();
    const fixture = projectFixture(forever, asynchronous);
    await vm.loadProject(JSON.stringify(fixture.project));
    vm.runtime.currentStepTime = 1000 / 30;
    const events = [];
    const originalStep = vm.runtime.sequencer.stepThreads;
    const beforeBlocks = JSON.stringify(vm.runtime.targets[0].blocks._blocks);
    const engine = create(vm, fixture.map, event => events.push(event), {sliceMs: 2});
    const pump = async () => {
        const deadline = Date.now() + 4000;
        while (!engine.paused && engine.running) {
            vm.runtime._step();
            if (Date.now() > deadline) throw new Error('Debugger failed to stop or terminate');
            await new Promise(resolve => setImmediate(resolve));
        }
    };
    return {vm, fixture, events, engine, pump, originalStep, beforeBlocks,
        x: () => vm.runtime.targets[0].variables.x.value};
}

async function main() {
    const {vm, fixture, events, engine, pump, originalStep, beforeBlocks, x} = await setup();
    engine.setBreakpoints('/example.cpp', [11]);
    engine.start();
    await pump();
    assert.equal(x(), 1, 'breakpoint stops before side effect');
    assert.equal(engine.getState().pointId, fixture.ids.increment);
    assert.equal(engine.stackTrace()[0].name, 'main');
    assert.equal(engine.stackTrace()[0].frameAddress, 100);
    assert.deepEqual(engine.readMemory(1, 0, 4), [1, 2, 3, 4]);
    engine.continue(); await pump();
    assert.equal(x(), 11, 'same block breaks again on the next loop iteration');
    engine.setBreakpoints('/example.cpp', [20]);
    engine.continue(); await pump();
    assert.equal(x(), 31, 'breakpoint changes without project mutations');
    assert.deepEqual(engine.stackTrace().map(f => f.name), ['foo', 'main']);
    assert.equal(engine.stackTrace()[0].frameAddress, 3);
    engine.setBreakpoints('/example.cpp', []);
    engine.stepIn(); await pump();
    assert.equal(engine.getState().pointId, fixture.ids.fooIf);
    engine.next(); await pump();
    assert.equal(engine.getState().pointId, fixture.ids.fooLast);
    assert.equal(engine.stackTrace()[0].frameAddress, 3, 'next skips recursive calls');
    assert.equal(x(), 234);
    engine.stepOut(); await pump();
    assert.equal(engine.getState().pointId, fixture.ids.last);
    assert.equal(x(), 334);
    engine.continue(); await pump();
    assert.equal(x(), 1334);
    assert(events.some(event => event.event === 'terminated'));
    assert.equal(JSON.stringify(vm.runtime.targets[0].blocks._blocks), beforeBlocks, 'debugger never changes project blocks');

    vm.runtime.targets[0].variables.x.value = 0;
    engine.start({stopOnEntry: true}); await pump();
    assert.equal(x(), 0);
    engine.stepInstruction(); await pump();
    assert.equal(engine.getState().pointId, fixture.ids.increment);
    engine.setInstructionBreakpoints([fixture.ids.fooCall]);
    engine.continue(); await pump();
    assert.equal(engine.getState().pointId, fixture.ids.fooCall);
    assert.deepEqual(engine.getIRState().location, {function: 'main', instruction: 'i2'});
    assert.deepEqual(engine.setIRBreakpoints([{function: 'foo', instruction: 'i1'}]),
        [{function: 'foo', instruction: 'i1', verified: true}]);
    engine.stepIn(); await pump();
    assert.equal(engine.stackTrace()[0].name, 'foo');
    vm.runtime.targets[0].blocks.resetCache();
    engine.stepInstruction(); await pump();
    assert.equal(engine.getState().pointId, fixture.ids.fooIf, 'cache reset preserves execution hook');
    engine.dispose();
    assert.equal(vm.runtime.sequencer.stepThreads, originalStep, 'dispose restores interpreter');
    vm.stopAll();

    const asynchronous = await setup(false, true);
    let calls = 0;
    asynchronous.vm.runtime._primitives.sensing_askandwait = () => {
        calls++;
        return new Promise(resolve => setTimeout(() => {
            asynchronous.vm.runtime.targets[0].variables.x.value = 7;
            resolve();
        }, 15));
    };
    asynchronous.engine.setBreakpoints('/example.cpp', [10]);
    asynchronous.engine.start(); await asynchronous.pump();
    assert.equal(calls, 1, 'async primitive invoked exactly once');
    assert.equal(asynchronous.x(), 7, 'promise finishes before following statement breakpoint');
    const clock = asynchronous.vm.runtime.ioDevices.clock;
    const clockBefore = clock.projectTimer();
    await new Promise(resolve => setTimeout(resolve, 35));
    asynchronous.vm.runtime._step();
    assert.equal(clock.projectTimer(), clockBefore, 'project timer freezes while paused');
    asynchronous.engine.setBreakpoints('/example.cpp', []);
    asynchronous.engine.continue();
    assert(Math.abs(clock.projectTimer() - clockBefore) < 0.02, 'project timer excludes paused wall time after resume');
    await asynchronous.pump();
    assert.equal(calls, 1, 'resume does not replay resolved promise');
    assert.equal(asynchronous.x(), 1341);
    asynchronous.engine.dispose(); asynchronous.vm.stopAll();

    const long = await setup(true);
    long.engine.start();
    const began = Date.now();
    const pauseTimer = setTimeout(() => long.engine.pause(), 15);
    await long.pump();
    clearTimeout(pauseTimer);
    assert(Date.now() - began < 500, 'warp loop yields to external pause request');
    assert(long.x() > 1);
    const pausedValue = long.x();
    long.vm.runtime._step();
    assert.equal(long.x(), pausedValue, 'paused VM executes no guest operations');
    long.engine.dispose(); long.vm.stopAll();
    console.log('debug engine: dynamic source/address breakpoints, recursion, source/instruction stepping, restart, cache reset, disposal, memory, promise continuation, timer freeze and bounded warp pause passed');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
