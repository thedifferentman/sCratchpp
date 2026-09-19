'use strict';
// Verify the public SB3 format with both consumers, not a duplicate parser.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const assert = require('node:assert/strict');
const {execFileSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'scrpp-tw-settings-'));
(async () => {
    try {
        for (const [name, settings, expected] of [
            ['default', null, {fps:60,hq:true,fencing:false,clones:300,limits:true,width:480,height:360,interpolation:false}],
            ['custom', {framerate:72.5,high_quality_pen:false,offscreen_sprites:false,
                unlimited_clones:true,remove_limits:true,stage_width:640,stage_height:480,interpolation:true},
                {fps:72.5,hq:false,fencing:true,clones:Infinity,limits:false,width:640,height:480,interpolation:true}],
            ['display', {framerate:0}, {fps:0,hq:true,fencing:false,clones:300,limits:true,width:480,height:360,interpolation:false}]
        ]) {
            const output=path.join(directory, name+'.sb3');
            const args=[path.join(root,'tests/fixtures/integer.ll'),'-o',output];
            if (settings) {
                const config=path.join(directory,name+'.json');
                fs.writeFileSync(config,JSON.stringify(settings));
                args.push('--turbowarp-settings',config);
            }
            execFileSync(process.argv[2],args);
            for (const mode of ['turbowarp-vm','scratch-vm']) {
                const VM=require(mode), vm=new VM();
                try {
                    await vm.loadProject(fs.readFileSync(output));
                    const stage=vm.runtime.getTargetForStage();
                    assert(Object.values(stage.comments).some(c=>c.text.endsWith(' // _twconfig_')));
                    if(mode==='turbowarp-vm') {
                        const runtime=vm.runtime;
                        assert.equal(runtime.frameLoop.framerate,expected.fps);
                        assert.equal(runtime.runtimeOptions.fencing,expected.fencing);
                        assert.equal(runtime.runtimeOptions.maxClones,expected.clones);
                        assert.equal(runtime.runtimeOptions.miscLimits,expected.limits);
                        assert.equal(runtime.stageWidth,expected.width);
                        assert.equal(runtime.stageHeight,expected.height);
                        assert.equal(runtime.interpolationEnabled,expected.interpolation);
                        let hq=false;
                        // Headless load has no renderer. Exercise TW's own renderer-setting branch.
                        runtime.renderer={setUseHighQualityRender:value=>{hq=value;}};
                        runtime.parseProjectOptions();
                        runtime.renderer=null;
                        assert.equal(hq,expected.hq);
                    }
                } finally {vm.stopAll();vm.quit();}
            }
        }
        console.log('Stored settings: defaults, overrides and display FPS accepted by TW and vanilla Scratch.');
    } finally {
        assert(path.resolve(directory).startsWith(path.resolve(os.tmpdir())+path.sep));
        fs.rmSync(directory,{recursive:true,force:true});
    }
})().catch(error=>{console.error(error);process.exitCode=1;});
