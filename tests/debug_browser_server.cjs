'use strict';
// Browser integration fixture: open url.txt in a browser, approve the local
// extension, then inspect report.json. The bridge never modifies SB3 blocks.
const fs = require('node:fs');
const path = require('node:path');
const {Bridge} = require('../debugger/llvmdbg.cjs');
const out = path.resolve(process.argv[2] || 'build/validation/debugger');
const events = [];
const bridge = new Bridge({project: path.join(out, 'program.sb3'), debugMap: path.join(out, 'program.debug.json'), noOpen: true, connectTimeout: 300000}, event => {
    events.push(event);
    console.log(JSON.stringify(event));
});
async function waitFor(predicate) {
    const deadline = Date.now() + 30000;
    while (!events.some(predicate)) {
        if (Date.now() > deadline) throw new Error('Timed out waiting for browser stop');
        await new Promise(resolve => setTimeout(resolve, 30));
    }
}
(async () => {
    await bridge.listen();
    fs.writeFileSync(path.join(out, 'browser-url.txt'), bridge.url);
    await bridge.ready;
    await bridge.request('setBreakpoints', bridge.map.functions.main.source.file, [12]);
    await bridge.request('start', {stopOnEntry: false});
    await waitFor(e => e.event === 'stopped');
    const stack = await bridge.request('stackTrace');
    const state = await bridge.request('getIRState');
    if (stack[0].line !== 12) throw new Error('Browser did not stop on source loop');
    fs.writeFileSync(path.join(out, 'browser-report.json'), JSON.stringify({passed:true, stack,state,events}, null,2));
    console.log('BROWSER DEBUG CHECK PASSED; left paused for visual inspection');
})().catch(error => {console.error(error);bridge.close();process.exitCode=1;});
process.on('SIGINT', () => {bridge.close();process.exit(0);});
