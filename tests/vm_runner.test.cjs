'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const JSZip = require('jszip');
const {runProject} = require('./vm_runner.cjs');

async function makeFixture(filename, forever = false) {
    const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"><path fill="none" d="M0 0h1v1H0z"/></svg>';
    const assetId = crypto.createHash('md5').update(svg).digest('hex');
    const costume = {name: 'blank', assetId, md5ext: `${assetId}.svg`, dataFormat: 'svg',
        bitmapResolution: 1, rotationCenterX: 0, rotationCenterY: 0};
    const block = (opcode, next, parent, inputs = {}, fields = {}, extra = {}) =>
        ({opcode, next, parent, inputs, fields, shadow: false, topLevel: false, ...extra});
    const numeric = value => [1, [4, String(value)]];
    const blocks = {
        flag: block('event_whenflagclicked', 'call', null, {}, {}, {topLevel: true, x: 0, y: 0}),
        call: block('procedures_call', null, 'flag', {}, {}, {mutation: {
            tagName: 'mutation', children: [], proccode: 'run', argumentids: '[]', warp: 'true'
        }}),
        definition: block('procedures_definition', 'set', null, {custom_block: [1, 'prototype']}, {},
            {topLevel: true, x: 0, y: 100}),
        prototype: block('procedures_prototype', null, 'definition', {}, {}, {
            shadow: true, mutation: {tagName: 'mutation', children: [], proccode: 'run',
                argumentids: '[]', argumentnames: '[]', argumentdefaults: '[]', warp: 'true'}
        }),
        set: block('data_setvariableto', forever ? 'forever' : 'append', 'definition',
            {VALUE: [3, 'add', [4, '0']]}, {VARIABLE: ['result', 'result-id']}),
        add: block('operator_add', null, 'set', {NUM1: numeric(17), NUM2: numeric(25)}),
        append: block('data_addtolist', 'if', 'set', {ITEM: [3, 'read-result', [10, '']]}, {LIST: ['results', 'results-id']}),
        'read-result': block('data_variable', null, 'append', {}, {VARIABLE: ['result', 'result-id']}),
        if: block('control_if', null, 'append', {CONDITION: [2, 'list-item'], SUBSTACK: [2, 'done']}),
        'list-item': block('data_itemoflist', null, 'if', {INDEX: numeric(1)}, {LIST: ['results', 'results-id']}),
        done: block('data_setvariableto', null, 'if', {VALUE: numeric(1)}, {VARIABLE: ['done', 'done-id']})
    };
    if (forever) {
        blocks.forever = block('control_forever', null, 'set', {SUBSTACK: [2, 'increment']});
        blocks.increment = block('data_changevariableby', null, 'forever', {VALUE: numeric(1)}, {VARIABLE: ['result', 'result-id']});
    }
    const base = {variables: {}, lists: {}, broadcasts: {}, blocks: {}, comments: {},
        currentCostume: 0, costumes: [costume], sounds: [], volume: 100, layerOrder: 0};
    const project = {targets: [
        {...base, isStage: true, name: 'Stage', tempo: 60, videoTransparency: 50, videoState: 'off', textToSpeechLanguage: null},
        {...base, isStage: false, name: 'Program', variables: {
            'result-id': ['result', 0], 'done-id': ['done', 0]
        }, lists: {'results-id': ['results', []]}, blocks,
        visible: false, x: 0, y: 0, size: 100, direction: 90, draggable: false, rotationStyle: 'all around', layerOrder: 1}
    ], monitors: [], extensions: [], meta: {semver: '3.0.0', vm: '0.2.0', agent: 'scratchpp-vm-test'}};
    const zip = new JSZip();
    zip.file('project.json', JSON.stringify(project));
    zip.file(`${assetId}.svg`, svg);
    await fs.mkdir(path.dirname(filename), {recursive: true});
    await fs.writeFile(filename, await zip.generateAsync({type: 'nodebuffer'}));
}

for (const engine of ['scratch', 'turbowarp']) {
    test(`real ${engine} executes warp procedure and list reporter in a Boolean input`, async () => {
        const filename = path.join(__dirname, '.tmp', `runner-${engine}.sb3`);
        await makeFixture(filename);
        const result = await runProject(filename, {vm: engine, timeout: 5000});
        assert.equal(result.status, 'completed', JSON.stringify(result));
        assert.equal(result.variables.result, 42);
        // Scratch keeps literal numeric inputs as strings; TurboWarp can fold
        // the same literal into a Number. Preserve that distinction in reports.
        assert.equal(Number(result.variables.done), 1);
        assert.deepEqual(result.lists.results, [42]);
        assert.equal(result.listLengths.results, 1);
        assert.equal(result.threadCount, 1);
        assert.equal(result.compiledThreads, engine === 'turbowarp' ? 1 : 0);
        assert.ok(result.assets.length === 2 && result.assets.every(asset => asset.loaded));
    });

    test(`real ${engine} infinite warp execution is stopped by a process deadline`, async () => {
        const filename = path.join(__dirname, '.tmp', `runner-infinite-${engine}.sb3`);
        await makeFixture(filename, true);
        const result = await runProject(filename, {vm: engine, timeout: 150});
        assert.equal(result.status, 'timeout', JSON.stringify(result));
        assert.ok(result.totalMs < 15000);
    });
}

test('invalid project produces an error rather than a completed result', async () => {
    const filename = path.join(__dirname, '.tmp', 'invalid.sb3');
    await fs.mkdir(path.dirname(filename), {recursive: true});
    await fs.writeFile(filename, 'not an sb3');
    const result = await runProject(filename, {vm: 'scratch', timeout: 1000});
    assert.equal(result.status, 'error');
});
