import assert from 'node:assert/strict';
import fs from 'node:fs';
import {decodeCamp,decodeCampState,decodeCampDesign,checkCanaries,wasmSignatures,validateScene} from './personal-camp-oracle.mjs';

// Same32-byte positive vector as the retained fixed Conveyor adapter's fixture.
// This new generic decoder makes no claim about that adapter's case or its outputs.
const b=Buffer.alloc(32);b.writeInt32LE(3,0);b.writeInt32LE(4,4);
[120,300,30,0].forEach((x,i)=>b.writeFloatLE(x,8+i*4));b.writeInt32LE(2,24);b.writeInt32LE(8,28);
assert.deepEqual(decodeCamp(b),{x:3,y:4,health:120,maxHealth:300,strength:30,focus:0,cooldowns:[2,8],cooldownRawBits:[2,8]});
const signed=Buffer.from(b);signed.writeInt32LE(-1,28);
assert.equal(decodeCamp(signed).cooldowns[1],-1);assert.equal(decodeCamp(signed).cooldownRawBits[1],0xffffffff);
assert.throws(()=>decodeCamp(b.subarray(1)));
for(const [at,value] of [[8,0],[8,301],[8,NaN],[12,0],[16,NaN]]){
  const bad=Buffer.from(b);bad.writeFloatLE(value,at);assert.throws(()=>decodeCamp(bad));
}
for(const state of [0,1,2]){const raw=Buffer.alloc(4);raw.writeUInt32LE(state);const s=decodeCampState(raw);assert.equal(s.awake,state===1);assert.equal(s.asleep,state===2);}
const badState=Buffer.alloc(4);badState.writeUInt32LE(3);assert.throws(()=>decodeCampState(badState));
checkCanaries(Buffer.alloc(64,0xa5),32);
for(const offset of [0,15,48,63]){const bad=Buffer.alloc(64,0xa5);bad[offset]=0;assert.throws(()=>checkCanaries(bad,32));}
// A tiny static WASM function(i32)->i32 exercises function-index/type/export binding.
const wasm=Buffer.from([0,97,115,109,1,0,0,0,1,6,1,0x60,1,0x7f,1,0x7f,3,2,1,0,7,5,1,1,102,0,0]);
assert.deepEqual(wasmSignatures(wasm).f,{params:[0x7f],results:[0x7f]});
assert.throws(()=>wasmSignatures(wasm.subarray(0,wasm.length-1)));
const scene={schema:'arena-personal-camp-oracle/v1',replay:{path:'x.zip',sha256:'a'.repeat(64)},
  map:{id:'00lanes',path:'maps/00lanes.map26a',sha256:'b'.repeat(64)},actor:5,team:1,round:159,sample:true};
assert.equal(validateScene(scene),scene);
for(const changed of [{...scene,team:2},{...scene,actor:16},{...scene,round:2001},{...scene,sample:1},{...scene,action:'move'}])
  assert.throws(()=>validateScene(changed));
// Independent retained public-catalog ABI fixture; no live decoder is instantiated.
const catalogue=JSON.parse(fs.readFileSync(new URL('../../docs/reference/runtime-catalog.json',import.meta.url)));
for(const fixture of catalogue.result.camps){
  const design=decodeCampDesign(Buffer.from(fixture.rawHex,'hex'));
  for(const [field,value] of Object.entries(fixture.stats))assert.equal(design[field],value);
  assert.equal(design.definition.attackType,fixture.attack_type);
  assert.deepEqual(design.definition.abilities.map(a=>a.cooldown),fixture.abilities.map(a=>a.cooldown));
  assert.equal(design.basicAttackIntervalRounds,null);
}
console.log(JSON.stringify({pass:true,nativeCalls:0,groups:['Camp_Get32 fixed-vector parity','Camp_GetState4','public124Bdesign parity',
  'canaries','WASM signatures','scene refusals'],fixture:'same Camp_Get32 vector as fixed Conveyor; separate generic scope'}));
