// Actual production Bot + unchanged cambc.h wrappers. This is an explicitly
// synthetic owning-inventory/shop/world host, not an official engine or VM.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {WASI} from 'node:wasi';

const [offPath,onPath,reportPath]=process.argv.slice(2);
const records=[];
const LIMITS=['Synthetic metadata, buy/combine effects and tiny empty map are declared fixture inputs.',
  'Identical fuel_remaining replies are conditional inputs, not measured fuel or resumable suspension.',
  'The real champion_items and metadata wrappers perform checked status handling; no removed-call-only error promise.',
  'Turn callers cover actual initialization and dead/live-injured-spawn shop paths, not hold-and-work execution.'];

async function guest(path,options={}) {
  let instance;
  const state={items:Array(42).fill(0),gold:0,round:1,alive:false,canShop:true,
    itemError:false,metadataError:false,buyError:false,itemsCalls:0,commands:[],trace:[],fuel:[],offeredFuel:100000,logs:[],...options};
  const memory=()=>instance.exports.memory;
  const view=()=>new DataView(memory().buffer);
  const checked=(p,n)=>{assert(Number.isSafeInteger(p)&&p>=0&&p+n<=memory().buffer.byteLength);return new Uint8Array(memory().buffer,p,n);};
  const u32=(p,v)=>{checked(p,4);view().setUint32(p,v,true);};
  const bytes=(p,n=8)=>Buffer.from(checked(p,n));
  const fill=(kind,id,p)=>{checked(p,instance.exports.fixture_size(kind));instance.exports.fixture_fill(kind,id,p);return 0;};
  const board=(p,v=0n)=>{checked(p,8);view().setBigUint64(p,v,true);return 0;};
  const env={
    game_settings:p=>fill(1,0,p),bc_metadata:p=>fill(0,0,p),
    bc_map_symmetry:p=>{u32(p,0);return 0;},
    consumable_info:(id,p)=>{u32(p,5);u32(p+4,1);return 0;},
    shared_memory_size:p=>{u32(p,0);return 0;},draft_memory_size:p=>{u32(p,0);return 0;},
    my_champion:()=>0,my_team:()=>0,
    champion_design:(id,p)=>fill(2,id,p),
    champion_ability_props:(id,slot,p)=>{assert.equal(id,0);assert(slot===0||slot===1);checked(p,36).fill(0);return 0;},
    champion_identity:(id,p)=>{assert.equal(id,0);u32(p,0);u32(p+4,11);return 0;},
    item_info:(id,p)=>{state.trace.push(['item_info',id]);return state.metadataError?37:fill(3,id,p);},
    champion_items:(id,p)=>{
      assert.equal(id,0);checked(p,42*4);++state.itemsCalls;state.trace.push(['champion_items',state.round]);
      if(state.itemError)return 37;
      state.items.forEach((value,i)=>u32(p+i*4,value));return 0;
    },
    champion_gold:(id,p)=>{assert.equal(id,0);u32(p,state.gold);return 0;},
    champion_gems:(id,p)=>{assert.equal(id,0);u32(p,0);return 0;},
    champion_is_alive:(id,p)=>{assert.equal(id,0);u32(p,+state.alive);return 0;},
    champion_can_shop:(id,consumable,p)=>{assert.equal(id,0);assert.equal(consumable,0);u32(p,+state.canShop);return 0;},
    champion:(id,p)=>{assert.equal(id,0);fill(4,0,p);if(!state.alive){view().setFloat32(p+16,0,true);u32(p+40,0);}return 0;},
    buy_item:(id,count)=>{
      const ids=Array.from({length:6},(_,i)=>instance.exports.fixture_id(i));
      const cost=id===ids[1]?20:id===ids[2]?30:10;
      state.commands.push(['buy_item',id,count]);assert(count>0&&state.gold>=cost*count);
      if(state.buyError)return 52;
      state.gold-=cost*count;state.items[id]+=count;
      if(id===ids[0]||id===ids[1]){
        const result=id===ids[0]?ids[3]:ids[4];
        while(state.items[id]>=4){state.items[id]-=4;++state.items[result];}
      }
      return 0;
    },
    bc_round_number:p=>{u32(p,state.round);return 0;},
    bc_submitted_gems:(team,p)=>{u32(p,0);return 0;},
    fuel_remaining:()=>{state.fuel.push(state.offeredFuel);return BigInt(state.offeredFuel);},
    bc_get_last_error:(p,n)=>{const b=Buffer.from('declared fixture SDK status');if(p&&n)checked(p,Math.min(n,b.length)).set(b.subarray(0,n));return b.length;},
    bc_map_mask:(kind,p)=>board(p),spawn_region:(team,p)=>{(team===0?[0,0,1,1]:[7,7,8,8]).forEach((v,i)=>u32(p+i*4,v));return 0;},
    map_camp_layout:(p,capacity,count)=>{u32(count,0);return 0;},
    bc_Bitboard_Zero:p=>board(p),
    bc_Bitboard_Union:(a,b,out)=>board(out,view().getBigUint64(a,true)|view().getBigUint64(b,true)),
    bc_Bitboard_Intersect:(a,b,out)=>board(out,view().getBigUint64(a,true)&view().getBigUint64(b,true)),
    bc_Bitboard_GetSetBits:(p,out,capacity,count)=>{
      const value=view().getBigUint64(p,true);const set=[];
      for(let i=0;i<64;++i)if(value&(1n<<BigInt(i)))set.push([i%8,Math.floor(i/8)]);
      u32(count,set.length);for(let i=0;i<Math.min(capacity,set.length);++i){u32(out+i*8,set[i][0]);u32(out+i*8+4,set[i][1]);}return 0;
    },
    bc_Bitboard_Set:(p,x,y,value)=>{const bit=1n<<BigInt(y*8+x),old=view().getBigUint64(p,true);return board(p,value?old|bit:old&~bit);},
    bc_Bitboard_Test:(p,x,y,out)=>{u32(out,Number((view().getBigUint64(p,true)>>BigInt(y*8+x))&1n));return 0;},
    bc_Bitboard_IsZero:(p,out)=>{u32(out,Number(view().getBigUint64(p,true)===0n));return 0;},
    bc_Bitboard_NumSetBits:(p,out)=>{let v=view().getBigUint64(p,true),n=0;while(v){n+=Number(v&1n);v>>=1n;}u32(out,n);return 0;},
    bc_Bitboard_Copy:(a,b)=>{checked(b,8).set(bytes(a));return 0;},
  };
  const module=await WebAssembly.compile(fs.readFileSync(path));
  const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{},returnOnExit:true});
  const imports={env:{},wasi_snapshot_preview1:{...wasi.wasiImport,fd_write:(fd,iovecs,count,written)=>{
    let text='',total=0;for(let i=0;i<count;++i){const p=view().getUint32(iovecs+i*8,true),n=view().getUint32(iovecs+i*8+4,true);text+=bytes(p,n).toString('utf8');total+=n;}
    state.logs.push(text);u32(written,total);return 0;
  }}};
  for(const item of WebAssembly.Module.imports(module)) {
    if(item.module==='env')imports.env[item.name]=env[item.name]??((...args)=>{throw Error('Unimplemented called import '+item.name+' '+args);});
    else assert.equal(item.module,'wasi_snapshot_preview1');
  }
  instance=await WebAssembly.instantiate(module,imports);wasi.initialize(instance);
  const ids=Array.from({length:7},(_,i)=>instance.exports.fixture_id(i));assert.equal(ids[6],42);
  const values=()=>Array.from({length:9},(_,i)=>instance.exports.fixture_value(i));
  const error=()=>{const p=instance.exports.fixture_error_text();let n=0;while(checked(p+n,1)[0])++n;return bytes(p,n).toString('utf8');};
  const call=mode=>{const before=state.itemsCalls;const code=instance.exports.fixture_call(mode);return {code,error:error(),values:values(),itemReads:state.itemsCalls-before,commands:structuredClone(state.commands),items:[...state.items],gold:state.gold,fuel:[...state.fuel]};};
  return {state,ids,call};
}

async function paired(name,configure,mode,verify) {
  const a=await guest(offPath),b=await guest(onPath);
  configure(a);configure(b);
  const off=a.call(mode),on=b.call(mode);
  assert.deepEqual({...off,itemReads:undefined},{...on,itemReads:undefined},name+' actual fields/commands/results');
  if(off.code===0)assert.equal(off.itemReads,on.itemReads*3,name+'3to1 reads per refresh');
  verify?.(off,on,a,b);
  records.push({name,pass:true,off,on});return {a,b,off,on};
}

let failure;
try {
  await paired('empty-refresh',()=>{},0,(off,on)=>{assert.equal(off.itemReads,3);assert.equal(on.itemReads,1);assert.deepEqual(on.values.slice(0,4),[0,0,2,1]);});
  await paired('partial-shards',g=>{g.state.items[g.ids[0]]=2;g.state.items[g.ids[1]]=3;},0,(off,on)=>assert.deepEqual(on.values.slice(0,4),[2,3,2,1]));
  await paired('emblems-and-passive-precedence',g=>{g.state.items[g.ids[3]]=1;g.state.items[g.ids[4]]=1;g.state.items[g.ids[5]]=1;},0,(off,on)=>{assert.deepEqual(on.values.slice(0,3),[4,4,3]);assert(Math.abs(on.values[3]-.675)<1e-6);});
  await paired('shop-no-gold-refusal-refresh',()=>{},1,(off,on)=>{assert.equal(on.itemReads,2);assert.equal(on.commands.length,0);});
  await paired('shop-canbuy-refusal-refresh',g=>{g.state.gold=300;g.state.canShop=false;},1,(off,on)=>{assert.equal(on.itemReads,2);assert.equal(on.commands.length,0);});
  await paired('shop-three-stages-normal-refresh',g=>{g.state.gold=240;},1,(off,on,a,b)=>{assert.equal(on.itemReads,2);assert.deepEqual(on.commands.map(x=>x[2]),[4,4,4]);assert.equal(on.values[4],3);assert.deepEqual(on.values.slice(0,3),[4,4,3]);assert.equal(on.items[b.ids[3]],1);});
  await paired('shop-purchase-then-refusal-refresh',g=>{g.state.gold=60;},1,(off,on)=>{assert.equal(on.itemReads,2);assert.equal(on.commands.length,2);assert.deepEqual(on.values.slice(0,2),[4,1]);});
  await paired('checked-first-inventory-error',g=>{g.state.itemError=true;},1,(off,on)=>{assert.equal(off.code,37);assert.equal(off.itemReads,1);assert.equal(on.itemReads,1);assert.match(on.error,/declared fixture SDK status/);assert.equal(on.commands.length,0);});
  await paired('checked-reachable-item-metadata-error',g=>{g.state.metadataError=true;},0,(off,on)=>{assert.equal(off.code,37);assert.equal(off.itemReads,2);assert.equal(on.itemReads,1);assert.equal(on.commands.length,0);});
  await paired('checked-buy-error',g=>{g.state.gold=100;g.state.buyError=true;},1,(off,on)=>{assert.equal(on.code,52);assert.equal(on.commands.length,1);assert.equal(on.values[4],0);});
  await paired('actual-dead-turn-caller',g=>{g.state.gold=60;g.state.alive=false;},2,(off,on)=>{assert.equal(on.values[5],1);assert.equal(on.commands.length,2);assert(on.fuel.length>0);assert(on.fuel.every(value=>value===100000));});
  await paired('actual-live-injured-spawn-turn-caller',g=>{g.state.gold=60;g.state.alive=true;},2,(off,on)=>{assert.equal(on.values[5],1);assert.equal(on.commands.length,2);assert(on.fuel.length>0);assert(on.fuel.every(value=>value===100000));});
  await paired('actual-dead-turn-declared-lowfuel',g=>{g.state.gold=60;g.state.alive=false;g.state.offeredFuel=6000;},2,(off,on)=>{assert.equal(on.values[5],1);assert.equal(on.commands.length,2);assert(on.fuel.length>0);assert(on.fuel.every(value=>value===6000));});
  const recache=await paired('next-invocation-first-refresh',()=>{},0);
  for(const g of [recache.a,recache.b]){g.state.round=2;g.state.items[g.ids[3]]=1;}
  const off2=recache.a.call(0),on2=recache.b.call(0);
  assert.deepEqual({...off2,itemReads:undefined},{...on2,itemReads:undefined});assert.equal(on2.values[0],4);assert.equal(off2.itemReads,3);assert.equal(on2.itemReads,1);
  records.push({name:'next-invocation-recache',pass:true,off:off2,on:on2});
} catch(error) {failure={message:error.message,stack:error.stack};process.exitCode=1;}
const report={schemaVersion:1,kind:'actual whole-Bot real-SDK import-spy fixture',pass:!failure,
  cases:records,failure,limits:LIMITS,newGames:0,nativeEngineCalls:0,resumableSuspensionTested:false};
fs.writeFileSync(reportPath,JSON.stringify(report,null,2)+'\n',{flag:'wx'});
process.stdout.write(JSON.stringify({pass:report.pass,cases:records.length,failure,newGames:0,nativeEngineCalls:0})+'\n');
