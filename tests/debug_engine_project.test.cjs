'use strict';
// Run after debug_map_test.py has generated debugger_program.cpp's project.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const VM = require('turbowarp-vm');
const {create} = require('../debugger/engine.js');

async function main() {
    const directory = path.resolve(process.argv[2] || 'build/validation/debugger');
    const map = JSON.parse(fs.readFileSync(path.join(directory, 'program.debug.json'), 'utf8'));
    const vm = new VM();
    await vm.loadProject(fs.readFileSync(path.join(directory, 'program.sb3')));
    vm.runtime.currentStepTime = 1000 / 30;
    const events = [];
    for (const name of ['SCRIPT_GLOW_ON', 'SCRIPT_GLOW_OFF']) {
        vm.runtime.on(name, ({id}) => {
            assert(vm.runtime.targets.some(target => target.blocks.getBlock(id)),
                `Interpreter debugger emitted a glow for nonexistent VM block ${id}`);
        });
    }
    const engine = create(vm, map, event => events.push(event));
    const pump = async () => {
        const deadline = Date.now() + 15000;
        while (!engine.paused && engine.running) {
            vm.runtime._step();
            if (Date.now() > deadline) throw new Error('Project did not pause/terminate within 15 seconds');
            await new Promise(resolve => setImmediate(resolve));
        }
    };
    const source = map.functions.main.source.file;
    engine.start({stopOnEntry: true}); await pump();
    assert.equal(engine.stackTrace()[0].name, 'main');
    assert(engine.stackTrace()[0].frameAddress > 0);
    // LLVM's first alloca has no source row; entry nevertheless exposes its
    // real IR location, which the LLDB layer can advance using its line table.
    assert(engine.getIRState().location.instruction);
    engine.setBreakpoints(source, [12]);
    for (let i = 0; i < 3; ++i) {
        engine.continue(); await pump();
        assert.equal(engine.stackTrace()[0].line, 12, 'loop breakpoint must stop on each dynamic iteration');
    }
    engine.setBreakpoints(source, [14]);
    engine.continue(); await pump();
    assert.equal(engine.stackTrace()[0].line, 14);
    engine.setBreakpoints(source, []);
    engine.stepIn(); await pump();
    assert.equal(engine.stackTrace()[0].name, 'recurse');
    assert.equal(engine.stackTrace().length, 2);
    assert(engine.stackTrace()[0].frameAddress < engine.stackTrace()[1].frameAddress);
    assert.equal(engine.getIRState().frames[0].function, '_Z7recursei');
    engine.setBreakpoints(source, [4]);
    engine.continue(); await pump();
    assert.equal(engine.stackTrace().length, 3, 'recursion exposes distinct guest frames');
    engine.setBreakpoints(source, []);
    engine.stepOut(); await pump();
    assert.equal(engine.stackTrace().length, 2, 'stepOut returns to the caller invocation');
    engine.stepOut(); await pump();
    assert.equal(engine.stackTrace().length, 1);
    engine.continue(); await pump();
    assert(events.some(event => event.event === 'terminated'));
    const variables = vm.runtime.targets.flatMap(t => Object.values(t.variables));
    assert.equal(variables.find(v => v.name === '__scl_status').value, 'done');
    assert.equal(Number(variables.find(v => v.name === 'exit_code').value), 0);
    engine.start({stopOnEntry: true, sourceEntry: true}); await pump();
    assert.equal(engine.stackTrace()[0].name, 'main');
    assert.equal(engine.stackTrace()[0].line, 10, 'GUI entry should stop on the first real source line');
    engine.dispose(); vm.stopAll();
    console.log('compiled C++ debug project: entry, dynamic loop breakpoints, nested LLVM frames, recursion, stepOut, and correct final result passed');
}
main().catch(error => { console.error(error); process.exitCode = 1; });
