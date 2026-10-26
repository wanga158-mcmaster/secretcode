import fs from 'node:fs';
import assert from 'node:assert/strict';
import {WASI} from 'node:wasi';
const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{},returnOnExit:true});
let instance;
const view=()=>new DataView(instance.exports.memory.buffer);
const zero=(p,n)=>new Uint8Array(instance.exports.memory.buffer,p,n).fill(0);
const i32=(p,v)=>view().setInt32(p,v,true);
const f32=(p,v)=>view().setFloat32(p,v,true);
const calls={},records=[],writes=[];
let s;
function reset(scenario){s={scenario,round:5,channel:false,fuel:100000,writeMode:0,receiptWrites:0,acceptedWrites:0,submits:0,lockAttacks:0};if(scenario===2)s.round=6;records.push({scenario,events:[]});}
function event(kind,detail={}){records.at(-1).events.push({kind,round:s.round,...detail});}
reset(0);
const implementations={
 game_settings(p){zero(p,120);i32(p,5);i32(p+8,60);i32(p+12,5);view().setFloat64(p+16,.1,true);i32(p+28,1);i32(p+32,1);i32(p+36,2000);i32(p+40,1000);i32(p+44,100);return 0;},
 champion_design(type,p){zero(p,128);i32(p,type);return 0;},
 consumable_info(type,p){i32(p,10);i32(p+4,1);return 0;},
 champion_ability_props(id,slot,p){assert.equal(id,0);zero(p,36);return 0;},
 champion(id,p){assert.equal(id,0);zero(p,44);i32(p,0);i32(p+4,2);i32(p+8,1);i32(p+12,1);f32(p+16,1000);f32(p+20,1000);f32(p+24,40);f32(p+28,100);i32(p+32,1);i32(p+36,1);i32(p+40,1|16|(s.channel?4:0));return 0;},
 my_champion(){return 0;},my_team(){return 0;},
 champion_identity(id,p){assert.equal(id,0);i32(p,0);i32(p+4,2);return 0;},
 champion_is_alive(id,p){assert.equal(id,0);i32(p,1);return 0;},
 shared_memory_size(p){i32(p,0);return 0;},draft_memory_size(p){i32(p,0);return 0;},
 fuel_remaining(){return BigInt(s.fuel);},bc_round_number(p){i32(p,s.round);return 0;},
 bc_metadata(p){[8,8,1,1,1,8,128].forEach((v,n)=>i32(p+4*n,v));return 0;},
 bc_map_symmetry(p){i32(p,0);return 0;},
 bc_map_mask(kind,p){zero(p,8);return 0;},spawn_region(team,p){zero(p,16);return 0;},
 bc_Bitboard_Zero(p){zero(p,8);return 0;},
 bc_Bitboard_ImportRawBuffer(p,source,size){assert.equal(size,8);new Uint8Array(instance.exports.memory.buffer,p,size).set(new Uint8Array(instance.exports.memory.buffer,source,size).slice());return 0;},
 bc_Bitboard_DrawChebyshev(p,x,y,r){let bits=view().getBigUint64(p,true);for(let row=0;row<8;++row)for(let col=0;col<8;++col)if(Math.max(Math.abs(col-x),Math.abs(row-y))<=r)bits|=1n<<BigInt(row*8+col);view().setBigUint64(p,bits,true);return 0;},
 bc_Bitboard_Test(p,x,y,out){assert(x>=0&&y>=0&&x<8&&y<8);i32(out,Number((view().getBigUint64(p,true)>>BigInt(y*8+x))&1n));return 0;},
 submit_gems(x,y){assert.equal(s.round,5);assert.equal(s.channel,false);assert.deepEqual([x,y],[2,1]);++s.submits;s.channel=s.scenario!==1;event('submit',{x,y,channel:s.channel});return 0;},
 altar_lock(x,y,team,p){assert.deepEqual([x,y],[2,1]);if(team===0)return 98;assert.equal(team,1);zero(p,24);i32(p,6);i32(p+4,55);i32(p+8,team);i32(p+12,x);i32(p+16,y);i32(p+20,5);return 0;},
 attack(kind,id){assert.equal(kind,6);assert.equal(id,55);++s.lockAttacks;event('lock-attack',{kind,id});return 0;},
};
const module=await WebAssembly.compile(fs.readFileSync(process.argv[2]));
const env={};
for(const item of WebAssembly.Module.imports(module)){if(item.module!=='env')continue;env[item.name]=(...args)=>{calls[item.name]=(calls[item.name]??0)+1;const f=implementations[item.name];if(!f)throw Error('Unimplemented reached SDK import: '+item.name);return f(...args);};}
const wasiImports={...wasi.wasiImport};
wasiImports.fd_write=(fd,iovs,count,written)=>{
 let text='';for(let n=0;n<count;++n){const p=view().getUint32(iovs+n*8,true),size=view().getUint32(iovs+n*8+4,true);text+=Buffer.from(instance.exports.memory.buffer,p,size).toString('utf8');}
 if(!text.startsWith('p10_runtime1'))return wasi.wasiImport.fd_write(fd,iovs,count,written);
 assert.equal(fd,1);assert(s.round>6,'receipt I/O entered the collection window');assert.equal(s.receiptWrites<24,true);
 const words=text.trimEnd().split(' ');assert.equal(words[0],'p10_runtime1');assert(words.slice(1).every(w=>/^[0-9a-f]{8}$/.test(w)));assert.equal(words[1],'00000001');
 ++s.receiptWrites;writes.push({round:s.round,mode:s.writeMode,words});event('receipt-write',{mode:s.writeMode});
 if(s.writeMode===1)return 29; // WASI EIO; production ::write must still consume this attempt.
 const length=Buffer.byteLength(text);i32(written,s.writeMode===2?length-1:length);if(s.writeMode===0)++s.acceptedWrites;return 0;
};
instance=await WebAssembly.instantiate(module,{wasi_snapshot_preview1:wasiImports,env,fixture:{reset,clock(round,channel){s.round=round;s.channel=!!channel;},write_mode(mode){s.writeMode=mode;},metric(n){return [s.receiptWrites,s.acceptedWrites,s.submits,s.lockAttacks][n];}}});
try{const code=wasi.start(instance);if(code)process.exitCode=code;}finally{fs.writeFileSync(process.argv[3],JSON.stringify({calls,records,writes,engineLaunches:0,nativeGames:0,scope:'Production Runtime receipt/bank ABI fixtures; delayed post-flush observation; no native ability evidence'},null,2)+'\n');}
