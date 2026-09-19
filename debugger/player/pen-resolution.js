'use strict';
// Window resizing must not resample persistent pen pixels. Keep an HQ backing
// resolution for the stage's lifetime; CSS/canvas presentation still resizes.
(function(root){
function preservePenResolution(renderer, display = globalThis.screen, pixelRatio = globalThis.devicePixelRatio || 1) {
    if(typeof renderer._updateRenderQuality!=='function')throw new Error('Unsupported TurboWarp pen renderer');
    const update=renderer._updateRenderQuality;
    const wrapped=new WeakSet();
    renderer._updateRenderQuality=function(){
        const skin=this._allSkins[this._penSkinId];
        if(skin && !wrapped.has(skin)) {
            wrapped.add(skin);
            const set=skin.setRenderQuality;
            let dimensions='',quality=1;
            skin.setRenderQuality=function(requested){
                if(renderer.useHighQualityRender) {
                    const size=renderer.getNativeSize(), key=size.join('x');
                    if(key!==dimensions) {
                        dimensions=key;
                        const budget=Math.min(renderer.maxTextureDimension || 2048,
                            renderer.gl.getParameter(renderer.gl.MAX_TEXTURE_SIZE));
                        const wanted=Math.max(2,Math.ceil((display?.width || size[0])*pixelRatio/size[0]),
                            Math.ceil((display?.height || size[1])*pixelRatio/size[1]));
                        quality=Math.min(wanted,budget/Math.max(...size));
                    }
                    requested=quality;
                }
                return set.call(this,requested);
            };
        }
        return update.call(this);
    };
    renderer._updateRenderQuality();
}
root.preservePenResolution=preservePenResolution;
if(typeof module!=='undefined')module.exports={preservePenResolution};
})(globalThis);
