#!/usr/bin/env node
'use strict';

// A symbol-only x86-64 ELF image. Its addresses identify interpreted Scratch
// execution points; none of its instruction sentinels execute on the host CPU.
// Guest memory remains byte-addressed at its actual Scratch list indices.
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const BASE = 0x100000;
const STRIDE = 16;
const quote = value => JSON.stringify(String(value).replace(/\\/g, '/'));

function buildAssembly(map) {
    if (map.schemaVersion !== 1) throw new Error('Unsupported Scratch debug-map schema');
    if (map.pointerBytes !== 8) throw new Error('LLDB debugging currently requires 64-bit LLVM pointers');
    const points = [], functions = {}, byInstruction = new Map();
    for (const [blockId, point] of Object.entries(map.points || {})) {
        const key = `${point.function}\0${point.instruction}`;
        let item = byInstruction.get(key);
        if (item) item.blockIds.push(blockId);
        else {
            item = {...point, blockId, blockIds: [blockId]};
            byInstruction.set(key, item); points.push(item);
        }
    }
    points.sort((a, b) => a.function < b.function ? -1 : a.function > b.function ? 1 :
        (a.ordinal || 0) - (b.ordinal || 0) || a.instruction.localeCompare(b.instruction));
    if (!points.length) throw new Error('Debug map has no executable points');
    const files = [], fileIds = new Map();
    const fileId = file => {
        if (!file) return 0;
        file = file.replace(/\\/g, '/');
        if (!fileIds.has(file)) { files.push(file); fileIds.set(file, files.length); }
        return fileIds.get(file);
    };
    for (const point of points) fileId(point.file);
    for (const fn of Object.values(map.functions || {})) { fileId(fn.source?.file); for (const v of fn.variables || []) fileId(v.file); }
    if (!files.length) fileId('scratch-program.ll');
    points.forEach((point, i) => {
        point.pc = BASE + i * STRIDE;
        if (!functions[point.function]) functions[point.function] = {lowPC: point.pc, highPC: point.pc + STRIDE};
        else functions[point.function].highPC = point.pc + STRIDE;
    });
    const symbols = {schemaVersion: 1, base: BASE, stride: STRIDE, pointerBytes: 8,
        projectCrc32: map.projectCrc32, points, functions};
    const out = ['.text', '.cfi_sections .debug_frame'];
    files.forEach((file, i) => out.push(`.file ${i + 1} ${quote(file)}`));
    let lastFunction = null, fnIndex = -1;
    for (let i = 0; i < points.length; ++i) {
        const p = points[i];
        if (p.function !== lastFunction) {
            if (lastFunction !== null) out.push('.cfi_endproc', `.Lfn_end${fnIndex}:`, `.size __scl_dbg_fn${fnIndex}, .-__scl_dbg_fn${fnIndex}`);
            ++fnIndex; lastFunction = p.function; functions[p.function].index = fnIndex;
            out.push(`.globl __scl_dbg_fn${fnIndex}`, `.type __scl_dbg_fn${fnIndex}, @function`, `__scl_dbg_fn${fnIndex}:`,
                '.cfi_startproc', '.cfi_def_cfa %rbp, 16', '.cfi_offset %rip, -8', '.cfi_offset %rbp, -16', '.cfi_offset %r12, -24');
        }
        out.push(`.Lpoint${i}:`);
        if (p.file && p.line > 0) out.push(`.loc ${fileId(p.file)} ${p.line} ${p.column || 0} is_stmt 1`);
        else out.push('.loc 1 0 0 is_stmt 0');
        out.push('.byte 0xff, 0xe0', `.fill ${STRIDE - 2}, 1, 0x90`);
    }
    out.push('.cfi_endproc', `.Lfn_end${fnIndex}:`, `.size __scl_dbg_fn${fnIndex}, .-__scl_dbg_fn${fnIndex}`);
    // All integer byte addresses are exact JS values (the guest limit is 200000).
    // DWARF v4, address-size 8, no register or machine ABI assumptions beyond
    // the synthetic RBP unwind chain and r12 guest-frame base agreed with RSP.
    out.push('.section .debug_abbrev,"",@progbits', '.Labbrev:');
    function abbrev(code, tag, children, attrs) {
        out.push(`.uleb128 ${code}`, `.uleb128 ${tag}`, `.byte ${children ? 1 : 0}`);
        for (const [attr, form] of attrs) out.push(`.uleb128 ${attr}`, `.uleb128 ${form}`);
        out.push('.byte 0, 0');
    }
    // DW_FORM: addr=1,data2=5,data4=6,string=8,data1=11,ref4=19,sec_offset=23,exprloc=24.
    abbrev(1, 0x11, true, [[0x25,8],[0x13,5],[0x03,8],[0x1b,8],[0x10,23],[0x11,1],[0x12,6]]);
    abbrev(2, 0x2e, true, [[0x03,8],[0x6e,8],[0x3a,6],[0x3b,6],[0x11,1],[0x12,6],[0x40,24]]);
    abbrev(3, 0x34, false, [[0x03,8],[0x49,19],[0x3a,6],[0x3b,6],[0x02,23]]);
    abbrev(4, 0x24, false, [[0x03,8],[0x0b,6],[0x3e,11]]);
    abbrev(5, 0x0f, false, [[0x0b,6],[0x49,19]]);
    abbrev(6, 0x13, false, [[0x03,8],[0x0b,6]]);
    abbrev(7, 0x05, false, [[0x03,8],[0x49,19],[0x3a,6],[0x3b,6],[0x02,23]]);
    out.push('.byte 0');
    const types = [], typeIds = new Map();
    function typeId(raw) {
        const t = raw || {kind:'unknown', name:'unknown', bytes:0};
        const key = JSON.stringify(t);
        if (typeIds.has(key)) return typeIds.get(key);
        const id = types.length; typeIds.set(key, id); types.push({...t});
        if (t.kind === 'pointer') types[id].elementId = typeId(t.element || {kind:'unknown',name:'void',bytes:0});
        return id;
    }
    const localLists = [];
    out.push('.section .debug_info,"",@progbits', '.Lcu:', '.long .Lcu_end-.Lcu_version', '.Lcu_version:',
        '.short 4', '.long .Labbrev', '.byte 8', '.uleb128 1', '.asciz "scratch-llvm interpreted target"',
        '.short 0x21', `.asciz ${quote(files[0])}`, '.asciz ""', '.long 0', `.quad ${BASE}`, `.long ${points.length * STRIDE}`);
    for (const [name, range] of Object.entries(functions)) {
        const fn = map.functions?.[name] || {}, source = fn.source || {};
        out.push('.uleb128 2', `.asciz ${quote(source.name || name)}`, `.asciz ${quote(name)}`,
            `.long ${fileId(source.file) || 1}`, `.long ${source.line || 1}`, `.quad ${range.lowPC}`, `.long ${range.highPC-range.lowPC}`,
            '.uleb128 1', '.byte 0x5c'); // DW_OP_reg12 frame base.
        for (const variable of fn.variables || []) {
            const id = localLists.length;
            localLists.push({id, fn, name, variable, range});
            out.push(`.uleb128 ${variable.parameter ? 7 : 3}`, `.asciz ${quote(variable.name || variable.id)}`,
                `.long .Ltype${typeId(variable.type)}-.Lcu`, `.long ${fileId(variable.file) || fileId(source.file) || 1}`,
                `.long ${variable.line || source.line || 1}`, `.long .Lloc${id}`);
        }
        out.push('.byte 0');
    }
    types.forEach((t, id) => {
        out.push(`.Ltype${id}:`);
        if (t.kind === 'pointer') out.push('.uleb128 5', `.long ${t.bytes || 8}`, `.long .Ltype${t.elementId}-.Lcu`);
        else if (['int','bool','float'].includes(t.kind)) out.push('.uleb128 4', `.asciz ${quote(t.name || t.kind)}`, `.long ${t.bytes || Math.ceil((t.bits || 0)/8)}`,
            `.byte ${t.kind === 'float' ? 4 : t.kind === 'bool' ? 2 : t.signed ? 5 : 7}`);
        else out.push('.uleb128 6', `.asciz ${quote(t.name || t.kind || 'unknown')}`, `.long ${t.bytes || 0}`);
    });
    out.push('.byte 0', '.Lcu_end:', '.section .debug_loc,"",@progbits');
    const expression = (location, fn) => {
        if (!location || location.kind === 'unavailable') return null;
        const operand = location.operand;
        if (operand?.kind === 'ref') {
            const slot = fn.slots?.[operand.id];
            if (!slot || !Number.isInteger(slot.offset)) return null;
            const expr = ['.byte 0x7c', `.sleb128 ${slot.offset}`]; // DW_OP_breg12.
            if (location.kind === 'address') expr.push('.byte 0x94, 8'); // deref_size LLVM pointer.
            return expr;
        }
        const bytes = operand?.bytes || location.bytes;
        if (Array.isArray(bytes) && bytes.every(n => Number.isInteger(n) && n >= 0 && n <= 255)) {
            if (location.kind === 'address') {
                let address = 0n; for (let i = bytes.length-1; i >= 0; --i) address = (address << 8n) | BigInt(bytes[i]);
                return ['.byte 0x03', `.quad ${address}`];
            }
            return ['.byte 0x9e', `.uleb128 ${bytes.length}`, ...(bytes.length ? [`.byte ${bytes.join(',')}`] : [])];
        }
        return null;
    };
    for (const {id, fn, name, variable, range} of localLists) {
        out.push(`.Lloc${id}:`, '.quad -1', '.quad 0'); // Explicit absolute base selection.
        const fnPoints = points.filter(p => p.function === name);
        const firstBlock = Object.values(fn.instructions || {}).sort((a,b)=>(a.ordinal||0)-(b.ordinal||0))[0]?.block;
        const locations = (variable.locations || []).map((location, order) => ({...location, order,
            ordinal: fn.instructions?.[location.before]?.ordinal})).filter(l => Number.isInteger(l.ordinal))
            .sort((a,b) => a.ordinal-b.ordinal || a.order-b.order);
        // Entry-block declarations have stable addresses across the function.
        // Other records are limited to their basic block: without CFG dominance
        // information it would be incorrect to expose stale SSA values elsewhere.
        let pending = null, runStart = null, runEnd = null;
        const emit = () => {
            if (pending === null) return;
            out.push(`.quad ${runStart}`, `.quad ${runEnd}`, `.short .Lexpr_end${id}_${runStart}-.Lexpr${id}_${runStart}`,
                `.Lexpr${id}_${runStart}:`, ...pending, `.Lexpr_end${id}_${runStart}:`);
        };
        for (const p of fnPoints) {
            const active = locations.filter(l => l.ordinal <= p.ordinal && (l.block === p.block ||
                (l.kind === 'address' && l.block === firstBlock))).at(-1);
            const expr = expression(active, fn);
            if (JSON.stringify(expr) === JSON.stringify(pending) && runEnd === p.pc) runEnd += STRIDE;
            else { emit(); pending = expr; runStart = p.pc; runEnd = p.pc + STRIDE; }
        }
        emit(); out.push('.quad 0, 0');
    }
    out.push('.section .note.GNU-stack,"",@progbits');
    return {assembly: out.join('\n')+'\n', symbols};
}

function generateSymbols(map, options = {}) {
    if (!options.clang) throw new Error('A Clang path is required to generate LLDB debug symbols');
    if (!options.output) throw new Error('A symbols output ELF path is required');
    const elf = path.resolve(options.output), asm = elf + '.s', object = elf + '.o';
    fs.mkdirSync(path.dirname(elf), {recursive:true});
    const {assembly, symbols} = buildAssembly(map);
    fs.writeFileSync(asm, assembly, 'utf8');
    const run = (exe, args) => {
        const result = spawnSync(exe, args, {encoding:'utf8', windowsHide:true});
        if (result.error || result.status !== 0) throw new Error(`Debug symbols command failed: ${exe}\n${result.error?.message || result.stderr || result.stdout}`);
    };
    run(options.clang, ['--target=x86_64-unknown-linux-gnu', '-c', '-x', 'assembler', '-gdwarf-4', asm, '-o', object]);
    const suffix = process.platform === 'win32' ? '.exe' : '';
    const sibling = path.join(path.dirname(options.clang), 'ld.lld'+suffix);
    const linker = options.linker || (fs.existsSync(sibling) ? sibling : 'ld.lld');
    run(linker, ['-m','elf_x86_64','--build-id=none','--image-base=0xf0000','-e','__scl_dbg_fn0','-Ttext=0x100000',object,'-o',elf]);
    const symbolsPath = elf + '.json';
    fs.writeFileSync(symbolsPath, JSON.stringify(symbols,null,2)+'\n','utf8');
    return {elf, symbolsPath, symbols};
}

module.exports = {buildAssembly, generateSymbols, BASE, STRIDE};
if (require.main === module) {
    const args = process.argv.slice(2), options = {};
    let mapFile;
    for (let i=0; i<args.length; ++i) {
        if (args[i] === '--clang') options.clang=args[++i];
        else if (args[i] === '--linker') options.linker=args[++i];
        else if (args[i] === '-o' || args[i] === '--output') options.output=args[++i];
        else if (!mapFile) mapFile=args[i]; else throw new Error(`Unknown argument: ${args[i]}`);
    }
    try {
        if (!mapFile) throw new Error('Usage: node symbols.cjs project.debug.json --clang clang -o project.debug.elf');
        const result=generateSymbols(JSON.parse(fs.readFileSync(mapFile,'utf8')), options);
        process.stdout.write(result.elf+'\n');
    } catch (error) { process.stderr.write(error.message+'\n'); process.exitCode=1; }
}
