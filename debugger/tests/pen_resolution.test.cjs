'use strict';
const {test}=require('node:test');
const assert=require('node:assert/strict');
const {preservePenResolution}=require('../player/pen-resolution.js');
test('viewport shrinking, hiding and enlarging cannot resample a persistent HQ pen layer',()=>{
 let stage=[480,360],changes=0;
 const skin={renderQuality:1,setRenderQuality(q){if(q!==this.renderQuality){changes++;this.renderQuality=q;}}};
 const r={_penSkinId:null,_allSkins:[],useHighQualityRender:true,maxTextureDimension:2048,
  gl:{MAX_TEXTURE_SIZE:1,getParameter:()=>4096},canvas:{width:960},getNativeSize:()=>stage,
  _updateRenderQuality(){this._allSkins[this._penSkinId]?.setRenderQuality(this.useHighQualityRender?this.canvas.width/stage[0]:1);}};
 preservePenResolution(r,{width:1920,height:1080},1);
 r._penSkinId=0;r._allSkins[0]=skin;r._updateRenderQuality();
 const count=changes,quality=skin.renderQuality;
 for(const width of [300,1,1400,420,960]){r.canvas.width=width;r._updateRenderQuality();}
 assert.equal(changes,count);assert.equal(skin.renderQuality,quality);
 r.useHighQualityRender=false;r._updateRenderQuality();assert.equal(skin.renderQuality,1);
 r.useHighQualityRender=true;stage=[8192,4096];r._updateRenderQuality();
 assert(skin.renderQuality*8192<=2048);
});
