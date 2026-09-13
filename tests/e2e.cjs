#!/usr/bin/env node
'use strict';

const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const {performance} = require('node:perf_hooks');
const {createHash} = require('node:crypto');
const {runProject} = require('./vm_runner.cjs');
const {resolveCompiler} = require('./compiler_path.cjs');

const root = path.resolve(__dirname, '..');
const usage = 'node tests/e2e.cjs [--compiler path|command] [--vm both|scratch|turbowarp] [--case glob] [--exclude glob] [--timeout ms] [--output-dir path] [--report path] [--list]';

function parse(args) {
    const options = {compiler: undefined,
        vm: 'both', cases: [], exclude: [], timeout: 30000,
        outputDir: path.join(__dirname, '.tmp/e2e', `${process.platform}-${process.arch}`), report: undefined, list: false};
    for (let i = 0; i < args.length; ++i) {
        if (['--compiler', '--vm', '--case', '--exclude', '--timeout', '--report', '--output-dir'].includes(args[i]) &&
            (!args[i + 1] || args[i + 1].startsWith('--'))) throw new Error(`Missing value for ${args[i]}`);
        if (args[i] === '--compiler') options.compiler = args[++i];
        else if (args[i] === '--vm') options.vm = args[++i];
        else if (args[i] === '--case') options.cases.push(args[++i]);
        else if (args[i] === '--exclude') options.exclude.push(args[++i]);
        else if (args[i] === '--timeout') options.timeout = Number(args[++i]);
        else if (args[i] === '--report') options.report = path.resolve(args[++i]);
        else if (args[i] === '--output-dir') options.outputDir = path.resolve(args[++i]);
        else if (args[i] === '--list') options.list = true;
        else if (args[i] === '--help') return {help: true};
        else throw new Error(`Unknown argument: ${args[i]}\n${usage}`);
    }
    if (!['scratch', 'turbowarp', 'both'].includes(options.vm)) throw new Error('Invalid --vm');
    if (!Number.isSafeInteger(options.timeout) || options.timeout < 1) throw new Error('Invalid --timeout');
    options.report ??= path.join(options.outputDir, 'report.json');
    return options;
}

function matches(name, pattern) {
    const escaped = pattern.replace(/[.+^${}()|[\]\\]/g, '\\$&').replaceAll('*', '.*').replaceAll('?', '.');
    return new RegExp(`^${escaped}$`).test(name);
}

function asInteger(value, label) {
    if (!['number', 'string'].includes(typeof value) || (typeof value === 'string' && value.trim() === '')) {
        throw new Error(`${label} is not a numeric value: ${JSON.stringify(value)}`);
    }
    const n = Number(value);
    if (!Number.isSafeInteger(n)) throw new Error(`${label} is not a safe integer: ${JSON.stringify(value)}`);
    return n;
}

function checkExecution(result, fixture) {
    if (result.status !== 'completed') {
        const exhausted = /JavaScript heap out of memory|Allocation failed.*heap/i.test(result.diagnostics || '');
        return {passed: false, failure: 'runtime', reason: exhausted ? 'memory_exhaustion' : result.status,
            detail: exhausted ? 'VM process exhausted its JavaScript heap' : result.error || result.status};
    }
    if (result.variables.__scl_status !== 'done') {
        return {passed: false, failure: 'runtime', reason: 'program_error',
            detail: `__scl_status=${JSON.stringify(result.variables.__scl_status)}`};
    }
    try {
        const expected = fixture.expectedExitCode ?? fixture.expectedReturn;
        const actual = asInteger(result.variables.exit_code, 'exit_code');
        if (actual !== expected) throw new Error(`exit_code: expected ${expected}, received ${actual}`);
        const bytes = result.lists.return_bytes;
        if (!Array.isArray(bytes)) throw new Error('Missing return_bytes list');
        const actualBytes = bytes.map((b, i) => asInteger(b, `return_bytes[${i}]`));
        if (JSON.stringify(actualBytes) !== JSON.stringify(fixture.expectedReturnBytes)) {
            throw new Error(`return_bytes: expected ${JSON.stringify(fixture.expectedReturnBytes)}, received ${JSON.stringify(actualBytes)}`);
        }
        if (result.vm === 'turbowarp' && !result.compiledThreads) throw new Error('No compiled TurboWarp thread was observed');
        return {passed: true};
    } catch (error) {
        return {passed: false, failure: 'assertion', detail: error.message};
    }
}

async function main() {
    const options = parse(process.argv.slice(2));
    if (options.help) return console.log(usage);
    const manifestPath = path.join(__dirname, 'fixtures/manifest.json');
    const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    const cases = manifest.fixtures.filter(fixture => (!options.cases.length ||
        options.cases.some(pattern => matches(fixture.file, pattern))) &&
        !options.exclude.some(pattern => matches(fixture.file, pattern)));
    if (!cases.length) throw new Error('No matching fixtures');
    if (options.list) return console.log(cases.map(fixture => fixture.file).join('\n'));
    options.compiler = resolveCompiler(options.compiler);
    const outDirectory = options.outputDir;
    fs.mkdirSync(outDirectory, {recursive: true});
    const began = performance.now();
    const hashFile = filename => createHash('sha256').update(fs.readFileSync(filename)).digest('hex');
    const report = {schemaVersion: 1, compiler: options.compiler, compilerMtime: fs.statSync(options.compiler).mtime.toISOString(),
        compilerSha256: hashFile(options.compiler), manifestSha256: hashFile(manifestPath),
        nodeVersion: process.version, platform: process.platform, arch: process.arch, timeoutMs: options.timeout,
        engines: options.vm,
        startedAt: new Date().toISOString(), cases: []};
    for (const fixture of cases) {
        const source = path.join(path.dirname(manifestPath), fixture.file);
        const output = path.join(outDirectory, fixture.file.replaceAll('/', '_').replace(/\.ll$/, '.sb3'));
        // A stale artifact must not make a failed compilation look successful.
        if (fs.existsSync(output)) fs.unlinkSync(output);
        const compileStart = performance.now();
        const compiler = spawnSync(options.compiler, [path.relative(root, source), '-o', path.relative(root, output), ...(fixture.compilerArgs || [])], {
            cwd: root, encoding: 'utf8', timeout: 60000, windowsHide: true, maxBuffer: 8 * 1024 * 1024
        });
        const entry = {fixture: fixture.file, expected: fixture.expectedExitCode ?? fixture.expectedReturn,
            fixtureSha256: hashFile(source),
            compilerMtime: fs.statSync(options.compiler).mtime.toISOString(), compilerArgs: fixture.compilerArgs || [],
            compileMs: performance.now() - compileStart, compilerExit: compiler.status,
            compilerStdout: compiler.stdout || '', compilerStderr: compiler.stderr || '', runs: []};
        report.cases.push(entry);
        if (fixture.expectCompileError) {
            const diagnostic = `${compiler.stdout || ''}\n${compiler.stderr || ''}`;
            entry.passed = compiler.status !== null && compiler.status !== 0 &&
                !/cannot read LLVM module|No mapping for the Unicode character|No such file/i.test(diagnostic) &&
                new RegExp(fixture.diagnosticPattern, 'i').test(diagnostic) && !fs.existsSync(output);
            if (!entry.passed) {
                entry.failure = 'diagnostic';
                entry.detail = compiler.error?.message || `Expected compile error matching /${fixture.diagnosticPattern}/ with no output artifact`;
            }
        } else if (compiler.error || compiler.status !== 0 || !fs.existsSync(output)) {
            entry.passed = false;
            entry.failure = 'compiler';
            entry.detail = compiler.error?.message || entry.compilerStderr || 'Compiler did not produce an SB3';
        } else {
            entry.sb3Bytes = fs.statSync(output).size;
            const engines = options.vm === 'both' ? ['scratch', 'turbowarp'] : [options.vm];
            for (const engine of engines) {
                const result = await runProject(output, {vm: engine, timeout: options.timeout, listLimit: 256});
                const check = checkExecution(result, fixture);
                const run = {vm: engine, ...result, ...check};
                entry.runs.push(run);
                console.log(`${check.passed ? 'PASS' : 'FAIL'} ${fixture.file} [${engine}]` +
                    (check.passed ? ` (${result.executionMs?.toFixed(1)} ms)` : ` ${check.failure}: ${check.detail}`));
            }
            entry.passed = entry.runs.every(run => run.passed);
        }
        if (!entry.runs.length) console.log(`${entry.passed ? 'PASS' : 'FAIL'} ${fixture.file} [compile]` +
            (entry.passed ? '' : ` ${entry.failure}: ${entry.detail?.trim()}`));
        fs.mkdirSync(path.dirname(options.report), {recursive: true});
        fs.writeFileSync(options.report, JSON.stringify(report, null, 2));
    }
    report.elapsedMs = performance.now() - began;
    report.passed = report.cases.filter(entry => entry.passed).length;
    report.failed = report.cases.length - report.passed;
    fs.writeFileSync(options.report, JSON.stringify(report, null, 2));
    console.log(`\n${report.passed}/${report.cases.length} fixtures passed; report: ${options.report}`);
    process.exitCode = report.failed ? 1 : 0;
}

module.exports = {checkExecution, matches, parse};
if (require.main === module) main().catch(error => { console.error(error.stack); process.exitCode = 2; });
