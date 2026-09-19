'use strict';
const test=require('node:test');
const assert=require('node:assert/strict');
const {waitForCostumeImages}=require('../llvmdbg.cjs');
function setup() {
    const image=new EventTarget(), skin={_svgImage:image,_svgImageLoaded:false};
    const vm={runtime:{renderer:{_allSkins:[skin]},targets:[{getCostumes:()=>[{skinId:0},{skinId:0}]}]}};
    return {image,skin,vm};
}
test('automatic start waits for SVG image load, even with repeated asset references', async()=>{
    const {image,skin,vm}=setup();let ready=false;
    const pending=waitForCostumeImages(vm).then(()=>{ready=true;});
    await new Promise(resolve=>setImmediate(resolve));assert.equal(ready,false);
    skin._svgImageLoaded=true;image.dispatchEvent(new Event('load'));await pending;
    assert.equal(ready,true);await waitForCostumeImages(vm);
    await waitForCostumeImages({runtime:{targets:[]}});
});
test('failed image and stalled image reject startup instead of silently stamping blank',async()=>{
    const {image,vm}=setup();const pending=waitForCostumeImages(vm);
    image.dispatchEvent(new Event('error'));await assert.rejects(pending,/decoded/);
    await assert.rejects(waitForCostumeImages(setup().vm,5),/Timed out/);
});
