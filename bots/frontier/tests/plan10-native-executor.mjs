import fs from 'node:fs';import {WASI} from 'node:wasi';
const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{},returnOnExit:true});
let instance;const view=()=>new DataView(instance.exports.memory.buffer);
const zero=(p,n)=>new Uint8Array(instance.exports.memory.buffer,p,n).fill(0);
const i32=(p,v)=>view().setInt32(p,v,true);const f32=(p,v)=>view().setFloat32(p,v,true);
let s={};const calls={};const records=[];
function reset(scenario){s={scenario,round:scenario===11?8:7,fuel:scenario===12?30000:scenario===13?29999:scenario===14?52000:scenario===15?66000:scenario===17?52000:100000,attack:scenario!==1,cast:true,cd:scenario===2?3:0,hp:scenario===3?40:1000,ownHp:1000,enemyX:scenario===5?5:2,enemyY:scenario===5?5:1,basics:0,casts:0,attempts:0,sequence:0,camps:new Map([[100,{hp:40,x:2,y:1}],[101,{hp:80,x:2,y:2}]])};records.push({scenario,events:[]});}
reset(0);
function event(kind,detail){records.at(-1).events.push({kind,...detail});}
function props(id,slot,p){zero(p,36);if(id!==0)return 0;i32(p,slot===0?1:2);i32(p+4,8);i32(p+12,slot===0?8:2);if(slot===1){f32(p+20,4);f32(p+24,1);}return 0;}
function champion(id,p){zero(p,44);i32(p,id===0?0:1);i32(p+4,0);i32(p+8,id===0?1:s.enemyX);i32(p+12,id===0?1:s.enemyY);f32(p+16,id===0?s.ownHp:s.hp);f32(p+20,1000);f32(p+24,40);f32(p+28,100);i32(p+32,1);i32(p+36,1);let state=(id===0||s.hp>0)?1:0;if(id===0){state|=8;if(s.attack)state|=16;if(s.cast)state|=2048;if(s.scenario===6)state|=4;}i32(p+40,state);return 0;}
const implementations={
 game_settings(p){zero(p,120);i32(p,5);i32(p+8,60);i32(p+12,5);view().setFloat64(p+16,.1,true);i32(p+28,1);i32(p+32,1);i32(p+36,2000);i32(p+40,1000);i32(p+44,100);return 0;},
 champion_design(type,p){zero(p,128);i32(p,type);return 0;},
 consumable_info(type,p){i32(p,10);i32(p+4,1);return 0;},
 champion_ability_props:props,champion,my_champion(){return 0;},my_team(){return 0;},
 champion_identity(id,p){i32(p,id===0?0:1);i32(p+4,0);return 0;},
 champion_is_alive(id,p){i32(p,id===0||s.hp>0?1:0);return 0;},
 champion_cooldown(id,slot,p){i32(p,s.cd);return 0;},
 shared_memory_size(p){i32(p,0);return 0;},draft_memory_size(p){i32(p,0);return 0;},fuel_remaining(){if(s.scenario===10)s.fuel-=10000;if(s.scenario===15)s.fuel=Math.max(0,s.fuel-2000);if(s.scenario===17)s.fuel=Math.max(0,s.fuel-400);return BigInt(s.fuel);},bc_round_number(p){i32(p,s.round);return 0;},
 bc_metadata(p){[8,8,1,1,1,8,128].forEach((v,n)=>i32(p+4*n,v));return 0;},
 bc_map_symmetry(p){i32(p,0);return 0;},
 bc_map_mask(kind,p){zero(p,8);return 0;},spawn_region(team,p){zero(p,16);return 0;},
 bc_Bitboard_Zero(p){zero(p,8);return 0;},
 bc_Bitboard_DrawChebyshev(p,x,y,r){let bits=view().getBigUint64(p,true);for(let row=0;row<8;++row)for(let col=0;col<8;++col)if(Math.max(Math.abs(col-x),Math.abs(row-y))<=r)bits|=1n<<BigInt(row*8+col);view().setBigUint64(p,bits,true);return 0;},
 bc_Bitboard_Test(p,x,y,out){if(x<0||y<0||x>=8||y>=8)throw Error('test tile out of range');i32(out,Number((view().getBigUint64(p,true)>>BigInt(y*8+x))&1n));return 0;},
 attack(kind,id){if(!s.attack||!s.cast&&s.scenario===6)throw Error('mock refused unavailable basic');if(kind===1){if(id!==1||s.hp<=0||Math.max(Math.abs(s.enemyX-1),Math.abs(s.enemyY-1))>1)throw Error('mock refused illegal actor attack');s.hp=Math.max(0,s.hp-40);}else if(kind===2){const m=s.camps.get(id);if(!m||m.hp<=0)throw Error('mock refused dead camp');m.hp=Math.max(0,m.hp-40);}else throw Error('unexpected target kind');s.attack=false;++s.basics;s.sequence=s.sequence*10+1;event('basic',{kind,id});if(s.scenario===7)++s.round;return 0;},
 cast_with_result(slot,kind,id,x,y,out){++s.attempts;if(!s.cast||s.cd!==0||s.scenario===6)throw Error('mock received illegal cast');if(slot===0){if(s.attack)throw Error('mock received unspent reset');if(s.scenario===4){i32(out,0);event('failed-reset',{});return 0;}s.attack=true;}else if(slot===1){if(kind!==1||id!==1)throw Error('unexpected spell target');s.hp=Math.max(0,s.hp-100);}else throw Error('unexpected slot');s.cast=false;s.cd=8;++s.casts;s.sequence=s.sequence*10+2;i32(out,1);event('cast',{slot,kind,id});if(s.scenario===8)++s.round;return 0;},
 camp_state(id,p){const m=s.camps.get(id);if(!m)throw Error('unknown camp');i32(p,m.hp>0?2:0);return 0;},
 camp(id,p){const m=s.camps.get(id);if(!m)throw Error('unknown camp');if(m.hp<=0)return 97;zero(p,32);i32(p,m.x);i32(p+4,m.y);f32(p+8,m.hp);f32(p+12,80);return 0;},
};
const module=await WebAssembly.compile(fs.readFileSync(process.argv[2]));const env={};
for(const item of WebAssembly.Module.imports(module)){if(item.module!=='env')continue;env[item.name]=(...args)=>{calls[item.name]=(calls[item.name]??0)+1;const f=implementations[item.name];if(!f)throw Error('Unimplemented reached SDK import: '+item.name);return f(...args);};}
instance=await WebAssembly.instantiate(module,{wasi_snapshot_preview1:wasi.wasiImport,env,fixture:{reset,metric(n){return [s.basics,s.casts,s.attempts,s.sequence][n];}}});
try{const code=wasi.start(instance);if(code)process.exitCode=code;}finally{fs.writeFileSync(process.argv[3],JSON.stringify({calls,records,engineLaunches:0,nativeGames:0},null,2)+'\n');}
