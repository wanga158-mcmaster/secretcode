// General real replay branches. Full champion physics stays inside the pinned engine.
// No sandbox restore, raw-memory clone, or recorded future is used after branching.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import {WASI} from 'node:wasi';
import {fileURLToPath,pathToFileURL} from 'node:url';

const HERE=path.dirname(fileURLToPath(import.meta.url)), args=new Map();
for(let i=2;i<process.argv.length;i+=2)args.set(process.argv[i].slice(2),process.argv[i+1]);
const ROOT=path.resolve(args.get('repo')||path.join(HERE,'../../../../..'));
const requestBytes=fs.readFileSync(args.get('request')), req=JSON.parse(requestBytes.toString().replace(/^\uFEFF/,''));
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const sorted=x=>x===null||typeof x!=='object'?x:Array.isArray(x)?x.map(sorted):Object.fromEntries(Object.keys(x).sort().map(k=>[k,sorted(x[k])]));
const canonical=x=>JSON.stringify(sorted(x));
const save=x=>{const output=args.get('response');fs.mkdirSync(path.dirname(output),{recursive:true});fs.writeFileSync(output+'.tmp',JSON.stringify({...x,schema:'contenders-micro/v1',request_id:req.request_id??null,request_sha256:sha(requestBytes)},null,req.pretty_json?2:0)+'\n');fs.renameSync(output+'.tmp',output);};
let identity=null;
try {
if(req.schema!=='contenders-micro/v1'||!['identity','evaluate','inventory','baseline'].includes(req.kind))throw Error('Expected contenders-micro/v1 identity/evaluate/inventory/baseline');
const scenario=req.scenario;if(!scenario||typeof scenario.replay!=='string')throw Error('scenario.replay is required');
const scenarioFields=new Set(['replay','replay_sha256','payload_sha256','profile','input','actor','participants','background_policy','max_world_steps','max_background_rounds','objective']);
if(Object.keys(scenario).some(k=>!scenarioFields.has(k)))throw Error('Unsupported scenario field; arbitrary state restoration is unavailable');
if(scenario.verifiedsetup!==undefined||scenario.setup!==undefined)throw Error('Arbitrary setup is unsupported; supply an actual retained replay/input');
const inputIndex=scenario.input??(req.kind==='inventory'?0:undefined),actorId=scenario.actor??(req.kind==='inventory'?0:undefined);
if(!Number.isInteger(inputIndex)||inputIndex<0||inputIndex>1000000||!Number.isInteger(actorId)||actorId<0||actorId>15)throw Error('Invalid input/actor');
const replayPath=path.resolve(ROOT,scenario.replay), replay=fs.readFileSync(replayPath);
function replayPayload(b){
  // Arena .repl26a files are ZIP containers with the native payload entry.
  let end=-1;for(let p=b.length-22;p>=Math.max(0,b.length-65557);--p)if(b.readUInt32LE(p)===0x06054b50){end=p;break;}
  if(end<0)throw Error('Replay ZIP directory absent');let p=b.readUInt32LE(end+16);
  for(let n=0;n<b.readUInt16LE(end+10);++n){if(b.readUInt32LE(p)!==0x02014b50)throw Error('Replay ZIP central directory invalid');const method=b.readUInt16LE(p+10),size=b.readUInt32LE(p+24),compressed=b.readUInt32LE(p+20),nl=b.readUInt16LE(p+28),el=b.readUInt16LE(p+30),cl=b.readUInt16LE(p+32);
    if(b.subarray(p+46,p+46+nl).toString()==='replay.repl26a'){const q=b.readUInt32LE(p+42),start=q+30+b.readUInt16LE(q+26)+b.readUInt16LE(q+28),v=b.subarray(start,start+compressed);if(![0,8].includes(method)||size>256*1024*1024)throw Error('Replay payload method/size unsupported');const result=method===8?zlib.inflateRawSync(v,{maxOutputLength:size}):Buffer.from(v);if(result.length!==size)throw Error('Replay payload length mismatch');return result;}p+=46+nl+el+cl;
  }throw Error('Replay payload absent');
}
const payload=replayPayload(replay);
if(scenario.replay_sha256&&scenario.replay_sha256!==sha(replay))throw Error('Replay SHA mismatch');
if(scenario.payload_sha256&&scenario.payload_sha256!==sha(payload))throw Error('Payload SHA mismatch');
const profilePath=args.get('profiles')||path.join(ROOT,'tools/replay-profiles.json');
const profileBytes=fs.readFileSync(profilePath),profile=JSON.parse(profileBytes).profiles.find(p=>p.id===(scenario.profile||'cli-3.0.5'));
if(!profile||profile.timelineSnapshotBytes!==40152)throw Error('Unsupported native profile');
const embedded=path.join(HERE,'../assets/oracle/gameplay.wasm');
const enginePath=args.get('engine')||process.env.OPT_GAME_ENGINE||(fs.existsSync(embedded)?embedded:path.join(process.env.LOCALAPPDATA||path.join(os.homedir(),'AppData/Local'),'cambc/assets',profile.cacheRelease,'visualiser/assets',profile.engineFile));
const wasm=fs.readFileSync(enginePath);if(sha(wasm)!==profile.decoderSha256)throw Error('Pinned engine SHA mismatch');
const samplerPath=args.get('sampler')||path.join(ROOT,'tools/replay-interaction-native.mjs');
const participants=scenario.participants??[actorId];
if(!Array.isArray(participants)||!participants.length||participants.length>16||new Set(participants).size!==participants.length||participants.some(x=>!Number.isInteger(x)||x<0||x>15)||!participants.includes(actorId))throw Error('Invalid selected participants');
const background=scenario.background_policy??'yield';
if(!['yield','reactive-v1'].includes(typeof background==='string'?background:background?.kind))throw Error('Unsupported background policy');
const policy=typeof background==='string'?{kind:background}:background;
const generation={ground_full:true,prefix_reuse:false,...req.generation};
if(Object.keys(generation).some(k=>!['ground_full','channels','prefix_reuse'].includes(k))||Object.values(generation).some(v=>typeof v!=='boolean'))throw Error('Unsupported generation setting; supported boolean fields ground_full/channels/prefix_reuse');
const objective=req.objective??scenario.objective??null;
const traceMode=req.trace_mode??'summary';if(!['summary','full'].includes(traceMode))throw Error('trace_mode must be summary or full');
const retainedEvent=e=>e.group_changes?.length||e.resource_changes?.length;
const traceEntry=x=>traceMode==='full'?x:{...x,events:x.events.filter(retainedEvent),effects:{...x.effects,champions:x.effects.champions.map(a=>({...a,changed:Object.fromEntries(Object.entries(a.changed).map(([k,v])=>[k,k==='personalVision'?{before_sha256:sha(canonical(v.before)),after_sha256:sha(canonical(v.after))}:v]))})),camps:x.effects.camps.filter(c=>c.before?.health!==c.after?.health),projection:'Summary changes and exact native group/resource step deltas; request trace_mode:full for all world substeps/vision arrays'}};
const effects=(before,after)=>{
 const fields=['x','y','health','maxHealth','alive','gold','gems','kills','items','consumables','strength','focus','moveSpeed','range','availability','channel','cooldowns','buffs','shield','stunned','restrictions','personalVision','personalVisibleChampions'];
 const champions=after.champions.flatMap(a=>{const prior=before.champions.find(b=>b.id===a.id),changed=Object.fromEntries(fields.filter(k=>canonical(prior?.[k])!==canonical(a[k])).map(k=>[k,{before:prior?.[k],after:a[k]}]));return Object.keys(changed).length?[{id:a.id,changed}]:[];});
 const camps=after.camps.flatMap(a=>{const b=before.camps.find(b=>b.id===a.id);return canonical(a)!==canonical(b)?[{id:a.id,before:b,after:a}]:[];});
 const groups=after.groups.flatMap(a=>{const b=before.groups.find(b=>b.id===a.id);return canonical(a)!==canonical(b)?[{id:a.id,before:b,after:a}]:[];});
 return{champions,camps,groups,scores:canonical(before.scores)===canonical(after.scores)?null:{before:before.scores,after:after.scores},scope:'Exposed native state differences, including personal vision even when applied=false'};
};
const {createInteractionSampler}=await import(pathToFileURL(samplerPath).href);
const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{}});let instance;
const cstr=p=>{const b=new Uint8Array(instance.exports.memory.buffer);let q=p;while(q<b.length&&b[q])++q;return new TextDecoder().decode(b.subarray(p,q));};
const started=performance.now(),module=await WebAssembly.compile(wasm);
instance=await WebAssembly.instantiate(module,{wasi_snapshot_preview1:wasi.wasiImport,deep_wasm:{on_assert_fail_impl(a,b,c,d){throw Error(`Native assert ${cstr(b)}:${c} ${cstr(a)} ${cstr(d)}`);}}});wasi.initialize(instance);
const e=instance.exports,dv=()=>new DataView(e.memory.buffer),pointers=[];
const alloc=n=>{const p=e.Engine_Malloc(n);if(!p)throw Error('Native allocation failed');pointers.push(p);return p;};
const inPtr=alloc(payload.length),out=alloc(8192),snapshot=alloc(profile.timelineSnapshotBytes),entity=alloc(8),position=alloc(8),perspective=alloc(8),count=alloc(8);
new Uint8Array(e.memory.buffer,inPtr,payload.length).set(payload);
const checked=(name,params)=>{if(typeof e[name]!=='function')throw Error('Missing native export '+name);const code=e[name](...params);if(code)throw Error(`${name}/${code}: ${cstr(e.GetLastErrorMsg())}`);};
checked('Game_API_GetSettings',[out]);const settingsRaw=Buffer.from(new Uint8Array(e.memory.buffer,out,120)).toString('hex');
const settings={capture_time:dv().getInt32(out,true),camp_sleep_time:dv().getInt32(out+4,true),respawn_time:dv().getInt32(out+8,true),recall_time:dv().getInt32(out+12,true),submit_gem_base_duration:dv().getInt32(out+24,true),submit_gem_minimum_duration:dv().getInt32(out+28,true),submit_gems_range:dv().getInt32(out+32,true),max_rounds:dv().getUint32(out+36,true),winning_gems:dv().getUint32(out+40,true)};
identity={backend:'actual-replay-micro-v1',engine_sha256:sha(wasm),engine_version:'1.0.3',replay_sha256:sha(replay),payload_sha256:sha(payload),profile_sha256:sha(profileBytes),sampler_sha256:sha(fs.readFileSync(samplerPath)),oracle_sha256:sha(fs.readFileSync(fileURLToPath(import.meta.url))),launcher_sha256:sha(fs.readFileSync(path.join(HERE,'native_micro.py'))),node_version:process.version,settings_sha256:sha(Buffer.from(settingsRaw,'hex')),settings_raw_hex:settingsRaw,settings,source_input:inputIndex,source_actor:actorId,participants,background_policy:policy,generation,objective,limits:{jobs:512,prefix_actions:512,inventory_span:3001,baseline_selected_turns:512,prefix_reuse_branches_per_game:16,prefix_reuse_records:128,prefix_reuse_checkpoints:"after accepted selected-owner end only; nonapplied prefixes excluded"},setup:'none; immutable retained native prefix'};
const compileMs=performance.now()-started;
let games=0,cacheHits=0;
const runtimeStats=()=>({games,cache_hits:cacheHits,wasm_memory_bytes:e.memory.buffer.byteLength,node_rss_bytes:process.memoryUsage().rss,compiled_module_once:true});
function fresh(){
 checked('Game_FromReplayFileBytes',[inPtr,payload.length,out]);const game=dv().getBigUint64(out,true);games++;
 checked('Game_GetBitboardMetadata',[game,out]);const width=dv().getInt32(out,true),height=dv().getInt32(out+4,true),stride=dv().getUint32(out+16,true),boardBytes=stride*Math.ceil(height/8)*8;
 const board=alloc(boardBytes+16),sampler=createInteractionSampler({e,game,view:dv,profile,width,height,stride,cstring:cstr});let queries=0,altars=null,lastBranch=null,cacheSafe=true;const campGroups=new Map(),campSpawns=new Map(),groupMetadata=new Map(),payouts=[];
 const query=(name,params,n,optional=false)=>{new Uint8Array(e.memory.buffer,out,8192).fill(0xa5);queries++;const code=e[name](...params,out);if(new Uint8Array(e.memory.buffer,out+n,8192-n).some(x=>x!==0xa5))throw Error('Native query canary '+name);if(code){if(optional)return null;throw Error(`${name}/${code}: ${cstr(e.GetLastErrorMsg())}`);}return dv();};
 const setPerspective=a=>{dv().setUint32(perspective,a.team,true);dv().setUint32(perspective+4,a.id,true);};
 const header=()=>{checked('Game_Visualiser_ReadTimelineState',[game,snapshot]);const d=dv(),u=o=>d.getUint32(snapshot+o,true);return{round:u(8),owner:u(60),phase:u(24),awaiting:!!u(20),input:u(56),substep:u(104),kind:u(68),actor_kind:u(72),actor:u(76),from:[d.getInt32(snapshot+80,true),d.getInt32(snapshot+84,true)],to:[d.getInt32(snapshot+88,true),d.getInt32(snapshot+92,true)]};};
 const bitAt=(hex,x,y)=>{const c=(y>>3)*stride+(x>>3),b=((y&7)<<3)+(x&7);return !!(BigInt('0x'+(hex.get(c)||'0'))&(1n<<BigInt(b)));};
 const boardTiles=(name,params)=>{new Uint8Array(e.memory.buffer,board,boardBytes+16).fill(0xa5);checked(name,[...params,board]);if(new Uint8Array(e.memory.buffer,board+boardBytes,16).some(x=>x!==0xa5))throw Error('Bitboard canary '+name);const tiles=[];for(let y=0;y<height;++y)for(let x=0;x<width;++x){const chunk=(y>>3)*stride+(x>>3),bit=((y&7)<<3)+(x&7);if(dv().getBigUint64(board+8*chunk,true)&(1n<<BigInt(bit)))tiles.push({x,y});}return tiles;};
 function sample(){
  header();const s=sampler.sample(snapshot),a=s.champions.find(c=>c.id===s.moment.currentChampion);s.current_owner=s.moment.currentChampion;s.round=s.moment.round;s.map={width,height,chunk_stride:stride};s.objective_history={payouts:payouts.map(p=>({...p})),scope:'Payouts observed on this counterfactual branch only; source-prefix historical owner is not inferred'};
  for(const camp of s.camps){if(!campGroups.has(camp.id)){let d=query('Game_API_Map_CampInfo_GetGroup',[game,camp.id],4);campGroups.set(camp.id,d.getUint32(out,true));d=query('Game_API_Map_CampInfo_Get',[game,camp.id],12);campSpawns.set(camp.id,{x:d.getInt32(out,true),y:d.getInt32(out+4,true),type:d.getInt32(out+8,true)});}camp.group_id=campGroups.get(camp.id);camp.spawn=campSpawns.get(camp.id);}
  for(const group of s.groups){if(!groupMetadata.has(group.id)){const d=query('Game_API_Map_CampGroup_Get',[game,group.id],8);groupMetadata.set(group.id,{gems:d.getUint32(out,true),gold:d.getUint32(out+4,true),members:s.camps.filter(c=>c.group_id===group.id).map(c=>c.id)});}Object.assign(group,groupMetadata.get(group.id));}
  if(a){setPerspective(a);const visible=new Map(a.personalVision);s.observation={owner:a.id,team:a.team,vision:a.personalVision,champion_ids:a.personalVisibleChampions,camp_ids:s.camps.filter(c=>bitAt(visible,c.x,c.y)).map(c=>c.id),scope:'Owner personal vision. Omniscient state retained separately for offline analysis.'};
   altars??=boardTiles('Game_API_Map_GetAltarBitboard',[game,perspective]);s.map.altars=altars;s.altar_locks=[];
   for(const [name,team]of[['Game_API_GetAllyAltarLockOccupancy',a.team],['Game_API_GetEnemyAltarLockOccupancy',1-a.team]])for(const p of boardTiles(name,[game,perspective])){const d=query('Game_API_GetAltarLock',[game,perspective,p.x,p.y,team],24);s.altar_locks.push({kind:d.getUint32(out,true),id:d.getUint32(out+4,true),team:d.getUint32(out+8,true),x:d.getInt32(out+12,true),y:d.getInt32(out+16,true),hits_remaining:d.getUint32(out+20,true)});}
   for(const camp of s.camps){camp.visible=bitAt(visible,camp.x,camp.y);if(!camp.visible)continue;let d=query('Game_API_Camp_GetState',[game,perspective,camp.id],4,true);camp.state=d?d.getUint32(out,true):null;if(camp.health>0){d=query('Game_API_Camp_Get',[game,perspective,camp.id],32,true);if(d)Object.assign(camp,{maxHealth:d.getFloat32(out+12,true),strength:d.getFloat32(out+16,true),focus:d.getFloat32(out+20,true),cooldowns:[d.getInt32(out+24,true),d.getInt32(out+28,true)]});}}
   for(const group of s.groups){const d=query('Game_API_CampGroup_GetState',[game,perspective,group.id],4,true);group.state=d?d.getUint32(out,true):null;if(group.state===3){const cd=query('Game_API_CampGroup_GetCooldown',[game,perspective,group.id],4,true);group.respawn=cd?cd.getInt32(out,true):null;}}
  }
  const status=sampler.metadata();if(status.errors)throw Error('Sampler errors: '+canonical(status));
  s.fingerprint_scope='All exposed sampler fields plus visible camp combat/group lifecycle and owner observation; no hidden-world serialization claim';
  s.fingerprint=sha(canonical({...s,moment:{...s.moment,input:undefined,substep:undefined,step:undefined}}));return s;
 }
 function proposals(s){
  const a=s.champions.find(c=>c.id===s.current_owner);if(!a||s.moment.phase!==10||!s.moment.awaitingInput)return [];
  if(!a.alive)return[{type:'end'}];
  const actions=[{type:'end'}],visible=new Set(s.observation.champion_ids),camps=new Set(s.observation.camp_ids);setPerspective(a);
  if(a.availability.move){new Uint8Array(e.memory.buffer,board,boardBytes+16).fill(0xa5);checked('Game_API_Champion_GetMovementMask',[game,perspective,a.id,board]);if(new Uint8Array(e.memory.buffer,board+boardBytes,16).some(x=>x!==0xa5))throw Error('Movement mask canary');for(let y=0;y<height;++y)for(let x=0;x<width;++x){const chunk=(y>>3)*stride+(x>>3),bit=((y&7)<<3)+(x&7);if((x!==a.x||y!==a.y)&&(dv().getBigUint64(board+8*chunk,true)&(1n<<BigInt(bit))))actions.push({type:'move',x,y});}}
  const targets=s.champions.filter(c=>c.alive&&visible.has(c.id)).map(c=>({kind:1,id:c.id,x:c.x,y:c.y,team:c.team})).concat(s.camps.filter(c=>c.health>0&&camps.has(c.id)).map(c=>({kind:2,id:c.id,x:c.x,y:c.y}))).concat((s.altar_locks??[]).filter(x=>x.team!==a.team).map(x=>({...x,kind:6})));
  const dist=t=>Math.max(Math.abs(t.x-a.x),Math.abs(t.y-a.y));
  if(a.availability.attack)for(const t of targets)if((t.kind===2||t.team!==a.team)&&dist(t)<=a.range)actions.push({type:'attack',target:{kind:t.kind,id:t.id}});
  if(a.availability.cast)for(const ability of a.abilities){const slot=ability.slot;if(ability.kind===0||a.cooldowns[slot]>0||ability.kind===1&&a.availability.attack)continue;const mask=a.abilityTargets[slot]??ability.targets,range=ability.payload.range??0;
   if((mask&8)||ability.kind===1||ability.kind===7)actions.push({type:'cast',slot,target:null,x:-1,y:-1});
   for(const t of targets){if(t.kind===6)continue;const flag=t.kind===2?1:t.id===a.id?8:t.team===a.team?4:2;if((mask&flag)&&dist(t)<=Math.ceil(range))actions.push({type:'cast',slot,target:{kind:t.kind,id:t.id},x:-1,y:-1});}
   if(mask&16){const r=Math.ceil(range);for(let y=Math.max(0,a.y-r);y<=Math.min(height-1,a.y+r);++y)for(let x=Math.max(0,a.x-r);x<=Math.min(width-1,a.x+r);++x)if(generation.ground_full||targets.some(t=>Math.max(Math.abs(t.x-x),Math.abs(t.y-y))<=Math.ceil(ability.payload.radius??0)))actions.push({type:'cast',slot,target:null,x,y});}
  }
  if(generation.channels){if(a.channel.kind!=='none')actions.push({type:'cancel_channel'});else{actions.push({type:'recall'});if(a.gems>0)for(const p of s.map.altars)if(dist(p)<=settings.submit_gems_range&&!s.altar_locks.some(l=>l.team!==a.team&&l.x===p.x&&l.y===p.y))actions.push({type:'submit_gems',x:p.x,y:p.y});}}
  return [...new Map(actions.map(x=>[canonical(x),x])).values()];
 }
 function primitive(action){
  const allowed={move:['type','x','y'],attack:['type','target'],cast:['type','slot','target','x','y'],end:['type'],recall:['type'],cancel_channel:['type'],submit_gems:['type','x','y']}[action?.type];
  if(!allowed||Object.keys(action).some(k=>!allowed.includes(k)))throw Error('Unsupported primitive/field');
  let code=0,applied=true;
  const target=t=>{if(t===null||t===undefined)t={kind:0,id:0};if(![0,1,2,3,4,5,6].includes(t.kind)||!Number.isInteger(t.id)||t.id<0)throw Error('Invalid native target');dv().setUint32(entity,t.kind,true);dv().setUint32(entity+4,t.id,true);};
  const pos=()=>{if(!Number.isInteger(action.x)||!Number.isInteger(action.y))throw Error('Integer action position required');dv().setInt32(position,action.x,true);dv().setInt32(position+4,action.y,true);};
  new Uint8Array(e.memory.buffer,out,8).fill(0xa5);
  if(action.type==='move'){pos();code=e.Game_Action_Move(game,action.x,action.y,out);applied=code===0&&dv().getInt32(out,true)!==0;}
  else if(action.type==='attack'){target(action.target);code=e.Game_Action_Attack(game,entity);applied=code===0;}
  else if(action.type==='cast'){if(![0,1].includes(action.slot))throw Error('Invalid slot');target(action.target);pos();code=e.Game_Action_Cast(game,action.slot,entity,position,out);applied=code===0&&dv().getInt32(out,true)!==0;}
  else if(action.type==='recall')code=e.Game_Action_Recall(game);
  else if(action.type==='cancel_channel')code=e.Game_Action_CancelChannel(game);
  else if(action.type==='submit_gems'){pos();code=e.Game_Action_SubmitGems(game,position);}
  else code=e.Game_Runner_EndTurn(game,0);
  if(['move','cast'].includes(action.type)&&new Uint8Array(e.memory.buffer,out+4,4).some(x=>x!==0xa5))throw Error('Native action output canary');
  if(code!==0||!applied)cacheSafe=false;return{code,applied:code===0&&applied,error:code?cstr(e.GetLastErrorMsg()):null};
 }
 function backgroundTurn(events){
  if(policy.kind==='reactive-v1'){
   const attempts=new Set();for(let turn=0;turn<6;++turn){const s=sample(),a=s.champions.find(c=>c.id===s.current_owner),all=proposals(s);const visible=new Set(a.personalVisibleEnemies),enemies=s.champions.filter(c=>c.alive&&visible.has(c.id));
    const choices=all.filter(x=>x.type==='attack'||x.type==='cast'&&(x.target?.kind===1&&enemies.some(c=>c.id===x.target.id)||x.target===null&&x.x===-1&&a.abilities[x.slot].kind===7));
    const next=choices.find(x=>!attempts.has(canonical(x)));if(!next)break;attempts.add(canonical(next));const result=primitive(next);events.push({background_owner:a.id,action:next,...result});}
  }
  checked('Game_Runner_EndTurn',[game,0]);
 }
 function eventProjection(){
  const h=header(),d=dv(),u=o=>d.getUint32(snapshot+o,true),champions=Array.from({length:u(172)},(_,i)=>{const p=196+i*252;return{id:u(p+4),team:u(p+8),gold:u(p+36),gems:u(p+40),kills:u(p+44)};}),groups=Array.from({length:u(180)},(_,id)=>({id,rewardAvailable:!!u(18228+id*8),captureTeam:u(18232+id*8)}));return{h,champions,groups};
 }
 function advance(events){
  const startRound=header().round,limit=scenario.max_world_steps??10000,maxRounds=scenario.max_background_rounds??50;
  if(!Number.isInteger(limit)||limit<1||limit>100000||!Number.isInteger(maxRounds)||maxRounds<1||maxRounds>2000)throw Error('Invalid world advance bound');
  for(let n=0;n<limit;++n){const prior=eventProjection();checked('Game_Runner_Step',[game]);const next=eventProjection(),h=next.h;
   const group_changes=next.groups.flatMap((g,i)=>g.rewardAvailable!==prior.groups[i].rewardAvailable||g.captureTeam!==prior.groups[i].captureTeam?[{id:g.id,before:prior.groups[i],after:g}]:[]),resource_changes=next.champions.flatMap((a,i)=>a.gold!==prior.champions[i].gold||a.gems!==prior.champions[i].gems||a.kills!==prior.champions[i].kills?[{id:a.id,before:prior.champions[i],after:a}]:[]);
   const paid=group_changes.filter(g=>g.before.rewardAvailable&&!g.after.rewardAvailable&&[0,1].includes(g.before.captureTeam));
   for(const g of paid){const recipients=resource_changes.filter(a=>a.after.gold>a.before.gold||a.after.gems>a.before.gems),reward=groupMetadata.get(g.id),gold=recipients.reduce((v,a)=>v+a.after.gold-a.before.gold,0),gems=recipients.reduce((v,a)=>v+a.after.gems-a.before.gems,0);payouts.push({group:g.id,team:g.before.captureTeam,round:h.round,phase:h.phase,event_kind:h.kind,recipients,credited_gold:gold,credited_gems:gems,attribution_unique:paid.length===1&&recipients.every(a=>a.after.team===g.before.captureTeam)&&gold===reward?.gold&&gems===reward?.gems,group_before:g.before,group_after:g.after});}
   events.push({...h,...(group_changes.length?{group_changes}:{}),...(resource_changes.length?{resource_changes}:{}),attribution_scope:'Native single-step delta; simultaneous actors/events remain explicitly listed'});
   if(h.phase===11)return{reason:'native-finished'};if(h.phase===10&&h.awaiting){if(participants.includes(h.owner))return null;backgroundTurn(events);}if(h.round-startRound>maxRounds)return{reason:'background-round-bound',truncated:true};}
  return{reason:'world-step-bound',truncated:true};
 }
 checked('Game_Visualiser_Timeline_GetMain',[game,out]);const main=dv().getBigUint64(out,true);
 checked('Game_Visualiser_Timeline_Info',[game,main,out,out+8,out+16]);const timelineSize=dv().getUint32(out,true);
 const seek=index=>checked('Game_Visualiser_Timeline_Seek',[game,main,index,0]);
 function branch(){seek(inputIndex);const original=sample();if(original.current_owner!==actorId||original.moment.phase!==10||!original.moment.awaitingInput)throw Error('Source must be the requested actor ready input');if(participants.some(id=>!original.champions.some(c=>c.id===id)))throw Error('Selected participant is absent from native roster');checked('Game_Visualiser_Timeline_Branch',[game,main,inputIndex,out]);lastBranch=dv().getBigUint64(out,true);const copy=sample();if(canonical(copy)!==canonical(original))throw Error('Branch differs from actual source projection');return copy;}
 
 const snapshotHash=()=>{header();const bytes=Buffer.from(new Uint8Array(e.memory.buffer,snapshot,profile.timelineSnapshotBytes));bytes.fill(0,48,56);return sha(bytes);};
 // This opaque native timeline bookmark is never a raw host-state clone.
 // Prefix history is copied separately because payout attribution is backend metadata.
 const capture=state=>({timeline:lastBranch,input:state.moment.input,fingerprint:state.fingerprint,snapshot_sha256:snapshotHash(),payouts:payouts.slice(),cache_safe:cacheSafe});
 const restore=record=>{cacheSafe=record.cache_safe;payouts.splice(0,payouts.length,...record.payouts);checked('Game_Visualiser_Timeline_Seek',[game,record.timeline,record.input,0]);checked('Game_Visualiser_Timeline_Branch',[game,record.timeline,record.input,out]);lastBranch=dv().getBigUint64(out,true);const state=sample();if(state.fingerprint!==record.fingerprint||snapshotHash()!==record.snapshot_sha256)throw Error('Native saved prefix restoration differs');return state;};
 return{game,sampler,sample,proposals,primitive,advance,branch,seek,header,timelineSize,snapshotHash,capture,restore,cacheSafe:()=>cacheSafe,queryCounts:()=>({extra_queries:queries,...sampler.metadata()}),close:()=>{sampler.destroy();checked('Game_Destroy',[game]);}};
}
const cache=new Map();let rootState=null,rootActions=null,rootProof=null;
try{
 if(req.kind!=='inventory'){const context=fresh();try{rootState=context.branch();rootActions=context.proposals(rootState);rootProof={native_branch_equal:true,root_fingerprint:rootState.fingerprint,query_status:context.queryCounts()};}finally{context.close();}}
 identity.root_fingerprint=rootState?.fingerprint??null;identity.root_state_scope=rootState?.fingerprint_scope??'read-only inventory; no branch claim';identity.scenario_sha256=sha(canonical(scenario));identity.identity_sha256=sha(canonical(identity));
 if(req.kind==='identity')save({native:true,supported:true,identity,state:rootState,actions:rootActions,branch_proof:rootProof,stats:runtimeStats(),timing_ms:{compile:compileMs,total:performance.now()-started}});
 else if(req.kind==='inventory'){
  const first=req.first_input??inputIndex,last=req.last_input??inputIndex;if(!Number.isInteger(first)||!Number.isInteger(last)||first<0||last<first||last-first>3000)throw Error('Inventory max3001 boundaries');const filter=req.inventory_filter??{};if(Object.keys(filter).some(k=>!['owner_dead','has_gems','near_altar','has_enemies','owner_types','hidden_blocker'].includes(k)))throw Error('Unsupported inventory filter');const c=fresh(),states=[],timelineSize=c.timelineSize;try{for(let i=first;i<=Math.min(last,timelineSize);++i){c.seek(i);const h=c.header();if(h.phase===10&&h.awaiting){const s=c.sample(),a=s.champions.find(x=>x.id===h.owner);if(filter.owner_dead!==undefined&&(!a.alive)!==filter.owner_dead||filter.has_gems!==undefined&&(a.gems>0)!==filter.has_gems||filter.has_enemies!==undefined&&(a.personalVisibleEnemies.length>0)!==filter.has_enemies||filter.owner_types&&!filter.owner_types.includes(a.type)||filter.near_altar&&(!a.alive||!s.map.altars.some(p=>Math.max(Math.abs(a.x-p.x),Math.abs(a.y-p.y))<=settings.submit_gems_range))||filter.hidden_blocker&&(!a.alive||!a.availability.move||!s.champions.some(b=>b.alive&&b.team!==a.team&&!a.personalVisibleEnemies.includes(b.id)&&Math.max(Math.abs(a.x-b.x),Math.abs(a.y-b.y))<=a.moveSpeed)))continue;states.push({input:i,owner:h.owner,round:h.round,champions:s.champions,camps:s.camps.filter(x=>x.visible),map:s.map,altar_locks:s.altar_locks,scores:s.scores});}}}finally{c.close();}save({native:true,supported:true,identity,states,timeline_size:timelineSize,requested_range:[first,last],stats:runtimeStats(),timing_ms:{compile:compileMs,total:performance.now()-started}});
 }else if(req.kind==='baseline'){
  const horizon=req.horizon_turns??4;if(!Number.isInteger(horizon)||horizon<1||horizon>512)throw Error('Baseline horizon_turns must be1..512');const c=fresh(),prefix=[],trace=[],refusals=[];let s,terminal=null,ends=0;const attempted=new Set();
  try{s=c.branch();for(let step=0;step<Math.min(8192,horizon*16)&&ends<horizon&&!terminal;++step){const a=s.champions.find(x=>x.id===s.current_owner),all=c.proposals(s),visible=new Set(s.observation.champion_ids),enemy=s.champions.filter(x=>x.alive&&x.team!==a.team&&visible.has(x.id)),selectedCamps=s.camps.filter(x=>objective?.camp_ids?objective.camp_ids.includes(x.id):objective?.group!==undefined?x.group_id===objective.group:x.visible),alive=selectedCamps.filter(x=>x.health>0),group=s.groups.find(x=>x.id===objective?.group),distance=t=>Math.max(Math.abs(t.x-a.x),Math.abs(t.y-a.y)),targets=(objective?.kind==='combat'?enemy:alive.length?alive:enemy).sort((x,y)=>distance(x)-distance(y)||x.health-y.health||x.id-y.id);
    const key=x=>s.current_owner+':'+s.round+':'+canonical(x),available=all.filter(x=>!attempted.has(key(x)));let next=available.find(x=>x.type==='attack'&&targets.some(t=>t.id===x.target.id&&x.target.kind===(s.camps.includes(t)?2:1)));
    if(!next)next=available.find(x=>x.type==='cast'&&x.target&&targets.some(t=>t.id===x.target.id&&x.target.kind===(s.camps.includes(t)?2:1)));
    if(!next&&!a.availability.attack)next=available.find(x=>x.type==='cast'&&a.abilities[x.slot].kind===1);
    if(!next&&a.shield===0&&targets.length)next=available.find(x=>x.type==='cast'&&a.abilities[x.slot].kind===7);
    let destinations=targets;
    if(!alive.length&&group?.rewardAvailable)destinations=selectedCamps.map(c=>c.spawn);
    const departing=!alive.length&&group?.state===3&&objective?.require_depart;
    if(!next){const moves=available.filter(x=>x.type==='move');if(departing){const spawns=new Set(selectedCamps.map(c=>c.spawn.x+','+c.spawn.y));if(spawns.has(a.x+','+a.y))next=moves.filter(x=>!spawns.has(x.x+','+x.y)).sort((x,y)=>Math.max(Math.abs(x.x-a.x),Math.abs(x.y-a.y))-Math.max(Math.abs(y.x-a.x),Math.abs(y.y-a.y))||x.y-y.y||x.x-y.x)[0];}else if(destinations.length){const d=x=>Math.min(...destinations.map(t=>Math.max(Math.abs(t.x-x.x),Math.abs(t.y-x.y))));next=moves.filter(x=>d(x)<d(a)).sort((x,y)=>d(x)-d(y)||x.y-y.y||x.x-y.x)[0];}}
    next??={type:'end'};attempted.add(key(next));const beforeState=s,before=s.fingerprint,owner=s.current_owner,r=c.primitive(next),events=[];if(r.code===0&&next.type==='end'){ends++;terminal=c.advance(events);}s=c.sample();if(r.code){refusals.push({owner,action:next,...r,before_fingerprint:before,after_fingerprint:s.fingerprint});if(s.fingerprint!==before)throw Error('Baseline refusal changed exposed state; no feasible control exported');continue;}prefix.push(next);trace.push(traceEntry({owner,action:next,...r,before_fingerprint:before,after_fingerprint:s.fingerprint,effects:effects(beforeState,s),events}));
   }
   const verify=fresh();let verified;try{verified=verify.branch();for(const action of prefix){const r=verify.primitive(action);if(r.code)throw Error('Exported baseline refused on independent replay');if(action.type==='end'){const events=[];verify.advance(events);}}verified=verify.sample();if(verified.fingerprint!==s.fingerprint)throw Error('Exported baseline differs on independent replay');}finally{verify.close();}
   save({native:true,supported:true,prefix_valid:true,identity,prefix,trace,refusals,state:s,actions:c.proposals(s),terminal:terminal||{reason:ends>=horizon?'baseline-horizon':'baseline-command-bound',truncated:true},policy:'greedy-native-feasible-v1',horizon_turns:ends,independent_replay:{passed:true,fingerprint:verified.fingerprint},query_counts:c.queryCounts(),stats:runtimeStats(),timing_ms:{compile:compileMs,total:performance.now()-started},scope:'Deterministic feasible native control, independently replayed accepted prefix; no optimum claim'});
  }finally{c.close();}
 }else{
  if(!Array.isArray(req.jobs)||req.jobs.length>512)throw Error('Expected bounded jobs array (max512)');const results=[];
  const reuse={enabled:generation.prefix_reuse,restores:0,reused_actions:0,epochs:0,recycles:0,fallbacks:0,records_peak:0,branches_limit:16,records_limit:128,epoch_wasm_bytes:[]};
  let shared=null,epochBranches=0,records=[];
  const closeEpoch=()=>{if(shared){reuse.epoch_wasm_bytes.push(e.memory.buffer.byteLength);shared.close();shared=null;}records=[];epochBranches=0;};
  const rootEpoch=()=>{shared=fresh();const state=shared.branch();if(state.fingerprint!==identity.root_fingerprint)throw Error('Independent reuse root differs');reuse.epochs++;epochBranches=1;records=[{...shared.capture(state),keys:[],trace:[]}];return state;};
  const remember=(state,keys,trace)=>{records.push({...shared.capture(state),keys:keys.slice(),trace:trace.slice()});if(records.length>reuse.records_limit)records.splice(1,1);reuse.records_peak=Math.max(reuse.records_peak,records.length);};
  const restoredJob=keys=>{if(!shared)rootEpoch();if(epochBranches>=reuse.branches_limit){closeEpoch();reuse.recycles++;rootEpoch();}let best=records[0];for(const record of records)if(record.keys.length>best.keys.length&&record.keys.length<=keys.length&&record.keys.every((k,i)=>k===keys[i]))best=record;
   try{const state=shared.restore(best);epochBranches++;reuse.restores++;reuse.reused_actions+=best.keys.length;return{c:shared,state,start:best.keys.length,trace:best.trace.slice(),cacheable:true};}
   catch(error){reuse.fallbacks++;closeEpoch();const state=rootEpoch();return{c:shared,state,start:0,trace:[],cacheable:true,restore_failure:String(error)};}};
  try{for(const job of req.jobs){if(!Array.isArray(job.prefix)||job.prefix.length>512)throw Error('Expected bounded primitive prefix (max512)');const key=sha(canonical(job.prefix));if(cache.has(key)){cacheHits++;results.push({...cache.get(key),id:job.id,cache_hit:true});continue;}const begin=performance.now(),keys=job.prefix.map(canonical);let c,result,prepared;
   try{prepared=generation.prefix_reuse?restoredJob(keys):null;c=prepared?.c??fresh();let s=prepared?.state??c.branch(),terminal=null,prefixValid=true,cacheable=true;const start=prepared?.start??0;if(!prepared&&s.fingerprint!==identity.root_fingerprint)throw Error('Independent branch differs from root identity');const trace=prepared?.trace??[];
    for(let index=start;index<job.prefix.length;++index){if(terminal){prefixValid=false;break;}const action=job.prefix[index],beforeState=s,before=s.fingerprint,owner=s.current_owner,r=c.primitive(action),events=[];if(r.code===0&&action.type==='end')terminal=c.advance(events);s=c.sample();trace.push(traceEntry({index,owner,action,...r,before_fingerprint:before,after_fingerprint:s.fingerprint,effects:effects(beforeState,s),events}));if(r.code){prefixValid=false;cacheable=false;break;}if(!r.applied)cacheable=false;
     if(generation.prefix_reuse&&cacheable&&c.cacheSafe()&&!terminal&&action.type==='end'&&s.moment.phase===10&&s.moment.awaitingInput)remember(s,keys.slice(0,index+1),trace);
    }
    result={supported:true,prefix_valid:prefixValid,state:s,actions:terminal?[]:c.proposals(s),terminal:terminal||{reason:null},trace,query_counts:c.queryCounts(),native_snapshot_sha256:c.snapshotHash(),native_snapshot_scope:'Official40152-byte timeline snapshot with documented timelineID bytes48..55 zeroed; no hidden-world serialization claim',prefix_sha256:key,timing_ms:performance.now()-begin,prefix_reuse:{enabled:generation.prefix_reuse,restored_actions:start,epoch:reuse.epochs,restoration_checked:!!prepared,restore_failure:prepared?.restore_failure??null}};
   }catch(error){result={supported:false,prefix_valid:false,reason:String(error),prefix_sha256:key,timing_ms:performance.now()-begin};if(generation.prefix_reuse)closeEpoch();}
   finally{if(!generation.prefix_reuse&&c)c.close();}
   cache.set(key,result);results.push({...result,id:job.id,cache_hit:false});
  }}finally{closeEpoch();}
  save({native:true,supported:true,identity,root_state:rootState,branch_proof:rootProof,results,stats:{...runtimeStats(),unique_prefixes:cache.size,prefix_reuse:reuse},timing_ms:{compile:compileMs,total:performance.now()-started},scope:'Exact ordered native branches from one retained input. Optional native Seek→Branch reuse is limited to checked selected-owner end boundaries, bounded game epochs and separate branch-observed history; fresh reconstruction remains default.'});
 }
}finally{for(const p of pointers)e.Engine_Free(p);}
}catch(error){save({native:false,supported:false,identity,reason:String(error)});process.exitCode=1;}
