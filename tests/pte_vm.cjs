'use strict';

// Real Scratch interpretation and real TW compilation, with observation at the
// pen/motion primitive boundary. No arithmetic, control or drawing opcode is
// replaced: wrappers record arguments then invoke the original implementation.
// This verifies vector commands; rendered pixel coverage is a separate test.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const JSZip = require('jszip');

function expect(condition, message) {
    if (!condition) throw new Error(message);
}

function validateFont(font, index, metadata) {
    expect(Array.isArray(font) && font.length > 0, 'Font manifest data missing');
    expect(index.length === 65536, 'BMP index must have 65536 entries');
    let previous = -1;
    for (let i = 0; i < font.length; ++i) {
        const row = font[i];
        expect(typeof row === 'string' && row.length >= 3, `Invalid font record ${i + 1}`);
        const cp = row.codePointAt(0);
        expect(cp > previous && cp <= 65535 && !(cp >= 0xd800 && cp <= 0xdfff),
            `Font codepoints must be unique sorted BMP scalars: row ${i + 1}`);
        expect(/^[0-9a-fA-F]{2}(?:[0-9a-fA-F]{3}|M)*$/.test(row.slice(1)),
            `Invalid width/stroke encoding at U+${cp.toString(16)}`);
        const strokes = row.slice(3);
        expect(!strokes.startsWith('M') && !strokes.endsWith('M') && !strokes.includes('MM'),
            `Empty stroke at U+${cp.toString(16)}`);
        expect(index[cp] === i + 1, `Codepoint index does not point to row ${i + 1}`);
        previous = cp;
    }
    for (let cp = 0; cp < index.length; ++cp) {
        const value = index[cp];
        expect(Number.isInteger(value) && value >= 0 && value <= font.length,
            `Invalid index entry U+${cp.toString(16)}`);
        expect(value === 0 || font[value - 1].codePointAt(0) === cp,
            `Index points to another codepoint at U+${cp.toString(16)}`);
    }
    expect(index[0x25a1] > 0, 'Missing square fallback U+25A1');
    for (const cp of [65, 0x4e2d, 32, 87, 105]) {
        expect(index[cp] > 0, `Missing trace fixture character U+${cp.toString(16)}`);
    }
    expect(metadata.glyph_count === font.length, 'SOURCE glyph_count mismatch');
    expect(metadata.index_entries === index.length, 'SOURCE index_entries mismatch');
    expect(metadata.fallback_codepoint === 'U+25A1' && metadata.fallback_glyph_index === index[0x25a1],
        'SOURCE fallback metadata mismatch');
    const hash = data => crypto.createHash('sha256').update(data, 'utf8').digest('hex');
    expect(metadata.font_sha256 === hash(font.join('\n') + '\n'), 'SOURCE font hash mismatch');
    expect(metadata.index_sha256 === hash(index.join('\n') + '\n'), 'SOURCE index hash mismatch');
    const result = {family: metadata.family || null, codepointAndIndexChecks: font.length + index.length,
        fallbackGlyph: index[0x25a1], asciiMonospacedChecked: false};
    if (String(metadata.family).toLowerCase() === 'consolas') {
        const widths = [];
        for (let cp = 0x20; cp <= 0x7e; ++cp) {
            expect(index[cp] > 0, `Consolas ASCII coverage missing U+${cp.toString(16)}`);
            widths.push(parseInt(font[index[cp] - 1].slice(1, 3), 16));
        }
        expect(widths[0] > 0 && widths.every(width => width === widths[0]),
            'Consolas printable ASCII must share one positive advance, including space');
        Object.assign(result, {asciiMonospacedChecked: true, asciiCharacters: widths.length,
            asciiAdvanceUnits: widths[0]});
    }
    return result;
}

function expectedTrace(font, index) {
    const trace = [];
    let exit = 0;
    for (const cp of [65, 0x4e2d, 0x10ffff, 32, 87, 105]) {
        const row = font[(index[cp] || index[0x25a1]) - 1];
        const advance = parseInt(row.slice(1, 3), 16);
        exit += advance;
        for (const cellWidth of [0, 10]) {
            const xScale = cellWidth ? Math.min(20, cellWidth * 64 / advance) : 20;
            const origin = -10 + (cellWidth ? (cellWidth - advance * xScale / 64) / 2 : 0);
            // User-selected PTE pen thickness is height / 16.
            trace.push(['up'], ['color', 0x123456], ['size', 20 / 16]);
            let penDown = false;
            for (let i = 3; i < row.length;) {
                if (row[i] === 'M') {
                    trace.push(['up']);
                    ++i;
                    penDown = false;
                } else {
                    const point = parseInt(row.slice(i, i + 3), 16);
                    trace.push(['move', origin + (point % 64) * xScale / 64,
                        10 - Math.floor(point / 64) * 20 / 64]);
                    if (!penDown) trace.push(['down']);
                    i += 3;
                    penDown = true;
                }
            }
            trace.push(['up']);
        }
    }
    return {trace, exit};
}

async function runVM(mode, data, expected) {
    const VM = require(mode);
    const Storage = require('scratch-storage');
    const vm = new VM();
    const actual = [];
    let compiled = 0;
    let timer;
    const compileErrors = [];
    try {
        vm.attachStorage(new (Storage.ScratchStorage || Storage)());
        vm.setTurboMode(true);
        if (vm.setCompilerOptions) {
            vm.setCompilerOptions({enabled: true});
            vm.on('COMPILE_ERROR', (_target, error) => compileErrors.push(String(error)));
        }
        vm.start();
        await vm.loadProject(data);
        if (mode === 'scratch-vm') {
            const opcodes = {
                pen_penUp: () => ['up'],
                pen_penDown: () => ['down'],
                pen_setPenColorToColor: a => ['color', Number(a.COLOR)],
                pen_setPenSizeTo: a => ['size', Number(a.SIZE)],
                motion_gotoxy: a => ['move', Number(a.X), Number(a.Y)]
            };
            for (const [opcode, record] of Object.entries(opcodes)) {
                const original = vm.runtime._primitives[opcode];
                vm.runtime._primitives[opcode] = (args, util) => {
                    actual.push(record(args));
                    return original(args, util);
                };
            }
        } else {
            // TW compiled primitives call these same underlying pen methods.
            const pen = vm.runtime.ext_pen;
            const methods = {
                _penUp: () => ['up'],
                _penDown: () => ['down'],
                _setPenColorToColor: a => ['color', Number(a)],
                _setPenSizeTo: a => ['size', Number(a)]
            };
            for (const [method, record] of Object.entries(methods)) {
                const original = pen[method];
                pen[method] = function (...args) {
                    actual.push(record(...args));
                    return original.apply(this, args);
                };
            }
            const target = vm.runtime.targets.find(item => !item.isStage);
            const setXY = target.setXY;
            target.setXY = function (x, y, ...args) {
                actual.push(['move', x, y]);
                return setXY.call(this, x, y, ...args);
            };
            const pushThread = vm.runtime._pushThread;
            vm.runtime._pushThread = function (...args) {
                const thread = pushThread.apply(this, args);
                if (thread.isCompiled) ++compiled;
                return thread;
            };
        }
        vm.greenFlag();
        const start = Date.now();
        await new Promise((resolve, reject) => {
            timer = setInterval(() => {
                if (compileErrors.length) reject(new Error(compileErrors.join('\n')));
                else if (Date.now() - start > 30000) reject(new Error('VM execution timeout'));
                else if (!vm.runtime.threads.some(thread => !thread.updateMonitor)) resolve();
            }, 10);
        });
        const vars = vm.runtime.targets.flatMap(target => Object.values(target.variables));
        const exit = vars.find(variable => variable.name === 'exit_code').value;
        const status = vars.find(variable => variable.name === '__scl_status').value;
        const equal = (a, b) => typeof a === 'number' && typeof b === 'number' ? Math.abs(a - b) < 1e-10 : a === b;
        const mismatch = actual.findIndex((row, i) => !expected.trace[i] || row.length !== expected.trace[i].length ||
            row.some((value, j) => !equal(value, expected.trace[i][j])));
        const ok = actual.length === expected.trace.length && mismatch === -1 && exit === expected.exit &&
            status === 'done' && (mode !== 'turbowarp-vm' || compiled > 0);
        return {mode, ok, compiledThreads: compiled, traceLength: actual.length,
            expectedLength: expected.trace.length, exit, expectedExit: expected.exit, status,
            ...(ok ? {} : {mismatch, expected: expected.trace, actual})};
    } finally {
        clearInterval(timer);
        vm.stopAll();
        vm.quit();
    }
}

async function main() {
    const [filename, reportFile, metadataFile] = process.argv.slice(2);
    expect(filename && reportFile && metadataFile,
        'Usage: node tests/pte_vm.cjs project.sb3 report.json SOURCE.json');
    const data = fs.readFileSync(filename);
    const archive = await JSZip.loadAsync(data);
    const project = JSON.parse(await archive.file('project.json').async('string'));
    const lists = Object.fromEntries(Object.values(project.targets.find(target => !target.isStage).lists));
    const font = lists['__scl_pte::font'];
    const index = lists['__scl_pte::index'].map(Number);
    const metadata = JSON.parse(fs.readFileSync(metadataFile, 'utf8'));
    const fontValidation = validateFont(font, index, metadata);
    const expected = expectedTrace(font, index);
    const reports = [];
    for (const mode of ['scratch-vm', 'turbowarp-vm']) {
        reports.push({...await runVM(mode, data, expected), fontRows: font.length, indexRows: index.length,
            fontValidation});
    }
    fs.mkdirSync(path.dirname(path.resolve(reportFile)), {recursive: true});
    fs.writeFileSync(reportFile, JSON.stringify(reports, null, 2) + '\n');
    expect(reports.every(report => report.ok), 'PTE trace mismatch; see report JSON');
    console.log('PTE pen traces passed in Scratch and compiled TurboWarp');
}

main().then(() => process.exit(0)).catch(error => {
    console.error(error);
    process.exit(1);
});
