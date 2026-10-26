import fs from 'node:fs';
import {WASI} from 'node:wasi';
const module=await WebAssembly.compile(fs.readFileSync(process.argv[2]));
const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{},returnOnExit:true});
let instance;
const counts=new Map();
const view=()=>new DataView(instance.exports.memory.buffer);
const set=(p,n)=>view().setUint32(p,n,true);
const zero=(p,n)=>new Uint8Array(instance.exports.memory.buffer,p,n).fill(0);
const allowed={
  game_settings:()=>0,
  shared_memory_size:p=>{set(p,0);return 0;},
  bc_metadata:p=>{[45,30,6,4,8,192,3072].forEach((n,k)=>set(p+4*k,n));return 0;},
  consumable_info:(id,p)=>{zero(p,8);return 0;},
  champion_design:(id,p)=>{zero(p,128);return 0;},
  draft_memory_size:p=>{set(p,0);return 0;},
  read_draft_memory:()=>0,
  my_champion:()=>-1,
  bc_map_symmetry:p=>{set(p,0);return 0;},
  bc_map_mask:(id,p)=>{zero(p,192);return 0;},
  bc_Bitboard_Zero:p=>{zero(p,192);return 0;},
  my_team:()=>0,
  spawn_region:(team,p)=>{zero(p,16);return 0;},
};
const env={};
for(const imp of WebAssembly.Module.imports(module)) {
  if(imp.module==='wasi_snapshot_preview1')continue;
  if(imp.module!=='env'||imp.kind!=='function')throw new Error('unexpected import '+imp.module+'.'+imp.name);
  env[imp.name]=(...args)=>{
    if(!allowed[imp.name])throw new Error('unexpected constructor SDK query/action '+imp.name);
    counts.set(imp.name,(counts.get(imp.name)||0)+1);
    return allowed[imp.name](...args);
  };
}
instance=await WebAssembly.instantiate(module,{...wasi.getImportObject(),env});
process.exitCode=wasi.start(instance);
console.log('Offline constructor host calls: '+JSON.stringify(Object.fromEntries(counts)));
