'use strict';
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const {spawnSync} = require('node:child_process');
const {runProject} = require('./vm_runner.cjs');
const {resolveCompiler} = require('./compiler_path.cjs');
const usage = 'node tests/varargs_test.cjs [--compiler path|command] [--output-dir path]';

function parse(args) {
    const options = {compiler: undefined,
        outputDir: path.join(__dirname, '.tmp/varargs', `${process.platform}-${process.arch}`)};
    for (let i = 0; i < args.length; ++i) {
        const arg = args[i];
        if (arg === '--help') return {help: true};
        if (arg === '--compiler' || arg === '--output-dir') {
            if (!args[i + 1] || args[i + 1].startsWith('--')) throw new Error(`Missing value for ${arg}`);
            if (arg === '--compiler') options.compiler = args[++i];
            else options.outputDir = path.resolve(args[++i]);
        } else if (!arg.startsWith('-') && options.compiler === undefined) {
            // Keep the previous positional compiler argument working.
            options.compiler = arg;
        } else throw new Error(`Unknown argument: ${arg}\n${usage}`);
    }
    return options;
}

async function main() {
    const options = parse(process.argv.slice(2));
    if (options.help) return console.log(usage);
    const compiler = resolveCompiler(options.compiler);
    const out = options.outputDir;
    fs.mkdirSync(out, {recursive: true});
    for (const [name, expected] of [['varargs_ir',58], ['varargs_sysv',57]]) {
        const sb3 = path.join(out, name+'.sb3');
        if (fs.existsSync(sb3)) fs.unlinkSync(sb3);
        const built = spawnSync(compiler, [path.join(__dirname,'fixtures',name+'.ll'),'-o',sb3],
            {encoding:'utf8',windowsHide:true,timeout:60000});
        assert.equal(built.status, 0, built.error?.message || built.stderr);
        assert(fs.existsSync(sb3), `Compiler did not produce ${sb3}`);
        for (const vm of ['scratch','turbowarp']) {
            const result = await runProject(sb3,{vm,timeout:30000,listLimit:16});
            assert.equal(result.status,'completed',result.error);
            assert.equal(result.variables.__scl_status,'done');
            assert.equal(Number(result.variables.exit_code),expected,name+' '+vm);
            assert.deepEqual(result.lists.return_bytes.map(Number),[expected,0,0,0]);
            if(vm==='turbowarp')assert(result.compiledThreads>0);
            console.log(`${name} ${vm}: ${expected} (${Math.round(result.executionMs)} ms)`);
        }
    }
    for(const [name,pattern] of [
        ['varargs_negative_windows',/variadic ABI requires.*System V/],
        ['varargs_negative_aggregate',/aggregate\/vector\/wide/]]) {
        const sb3=path.join(out,name+'.sb3');
        if(fs.existsSync(sb3))fs.unlinkSync(sb3);
        const built=spawnSync(compiler,[path.join(__dirname,'fixtures',name+'.ll'),'-o',sb3],
            {encoding:'utf8',windowsHide:true,timeout:60000});
        assert.ifError(built.error);
        assert.notEqual(built.status,null,'Compiler was terminated before producing a diagnostic');
        assert.notEqual(built.status,0);
        assert.match(built.stderr,pattern);
        assert(!fs.existsSync(sb3));
        console.log(name+': diagnostic passed');
    }
}

module.exports = {parse};
if (require.main === module) main().catch(error=>{console.error(error);process.exitCode=1;});
