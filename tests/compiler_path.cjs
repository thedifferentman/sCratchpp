'use strict';

const fs = require('node:fs');
const path = require('node:path');

// Return an actual file path, so reports hash the compiler that was executed.
// Never fall back to the old build/native MSVC build when Clang is unavailable.
function resolveCompiler(requested, options = {}) {
    const root = options.root || path.resolve(__dirname, '..');
    const cwd = options.cwd || process.cwd();
    const env = options.env || process.env;
    const platform = options.platform || process.platform;
    const executableName = platform === 'win32' ? 'scratch-llvm.exe' : 'scratch-llvm';
    const isExecutable = filename => {
        try {
            if (!fs.statSync(filename).isFile()) return false;
            if (platform !== 'win32') fs.accessSync(filename, fs.constants.X_OK);
            return true;
        } catch {
            return false;
        }
    };
    const pathKey = platform === 'win32' ? Object.keys(env).find(key => key.toUpperCase() === 'PATH') : 'PATH';
    const pathEntries = (env[pathKey] || '').split(platform === 'win32' ? ';' : ':').filter(Boolean);
    const fromPath = command => {
        const names = platform === 'win32' && !path.extname(command) ? [command + '.exe', command] : [command];
        for (const directory of pathEntries) {
            const unquoted = directory.replace(/^"(.*)"$/, '$1');
            for (const name of names) {
                const candidate = path.resolve(cwd, unquoted, name);
                if (isExecutable(candidate)) return candidate;
            }
        }
        return undefined;
    };
    const selected = requested === undefined ? env.SCRATCH_COMPILER : requested;
    if (selected !== undefined && selected !== '') {
        const hasPath = path.isAbsolute(selected) || /[/\\]/.test(selected);
        const resolved = hasPath ? path.resolve(cwd, selected) : fromPath(selected);
        if (resolved && isExecutable(resolved)) return resolved;
        throw new Error(`Compiler not found or not executable: ${selected}`);
    }
    if (requested === '') throw new Error('--compiler requires a nonempty path or command');
    const local = path.join(root, 'build', 'clang', executableName);
    if (isExecutable(local)) return local;
    const installed = fromPath(executableName);
    if (installed) return installed;
    throw new Error(`Compiler not found. Pass --compiler, set SCRATCH_COMPILER, build ${local}, or add scratch-llvm to PATH.`);
}

module.exports = {resolveCompiler};
