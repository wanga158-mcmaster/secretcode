// Actual retained camp-prefix replay. No sandbox or raw-memory restoration.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import {WASI} from 'node:wasi';
import {fileURLToPath,pathToFileURL} from 'node:url';
const HERE=path.dirname(fileURLToPath(import.meta.url)), args=new Map();
for(let i=2;i<process.argv.length;i+=2) args.set(process.argv[i].slice(2),process.argv[i+1]);
const ROOT=path.resolve(args.get('repo')||path.join(HERE,'../../../../..'));
const requestBytes=fs.readFileSync(args.get('request')), req=JSON.parse(requestBytes.toString().replace(/^\uFEFF/,''));
const sha=x=>crypto.createHash('sha256').update(x).digest('hex');
const canonical=x=>JSON.stringify(x===null||typeof x!=='object'?x:Array.isArray(x)?x.map(v=>JSON.parse(canonical(v))):Object.fromEntries(Object.keys(x).sort().map(k=>[k,JSON.parse(canonical(x[k]))])));
// Preserve the caller's JSON number tokens (including C++ float32 '.0') when
// checking its compact expected-state digest, while removing presentation space.
function compactRawValue(key){
  const text=requestBytes.toString().replace(/^\uFEFF/,''),space=()=>{while(/\s/.test(text[i]||'')&&i<text.length)++i;};let i=0;
  const stringEnd=start=>{let p=start+1;for(;p<text.length;++p){if(text[p]==='\\'){++p;continue;}if(text[p]==='"')return p+1;}throw Error('Unterminated JSON string');};
  const valueEnd=start=>{if(text[start]==='"')return stringEnd(start);if(text[start]!=='['&&text[start]!=='{'){let p=start;while(p<text.length&&!/[\s,}\]]/.test(text[p]))++p;return p;}const stack=[];let p=start;for(;p<text.length;++p){if(text[p]==='"'){p=stringEnd(p)-1;continue;}if(text[p]==='['||text[p]==='{')stack.push(text[p]);else if(text[p]===']'||text[p]==='}'){stack.pop();if(!stack.length)return p+1;}}throw Error('Unterminated JSON value');};
  space();if(text[i++]!=='{')throw Error('Request object required');
  while(i<text.length){space();if(text[i]==='}')break;const end=stringEnd(i),name=JSON.parse(text.slice(i,end));i=end;space();if(text[i++]!==':')throw Error('JSON field separator');space();const start=i;i=valueEnd(start);if(name===key){const raw=text.slice(start,i);let result='',quoted=false;for(let p=0;p<raw.length;++p){const c=raw[p];if(c==='"')quoted=!quoted;if(quoted&&c==='\\'){result+=c+raw[++p];continue;}if(quoted||!/[\s]/.test(c))result+=c;}return result;}space();if(text[i]===','){++i;continue;}break;
  }return null;
}
const save=x=>{x.request_sha256=sha(requestBytes);const out=args.get('response');fs.mkdirSync(path.dirname(out),{recursive:true});fs.writeFileSync(out+'.tmp',JSON.stringify(x,null,2)+'\n');fs.renameSync(out+'.tmp',out);};
const asset=path.join(ROOT,'results/contenders/optimization/c11/camp-domain');
const domainPath=args.get('domain')||path.join(asset,'domain.json'), domain=JSON.parse(fs.readFileSync(domainPath));
const profilePath=args.get('profiles')||path.join(ROOT,'tools/replay-profiles.json');
const profile=JSON.parse(fs.readFileSync(profilePath)).profiles.find(p=>p.id==='cli-3.0.5');
const embeddedEngine=path.join(HERE,'../assets/oracle/gameplay.wasm');
const enginePath=args.get('engine')||process.env.OPT_GAME_ENGINE||(fs.existsSync(embeddedEngine)?embeddedEngine:path.join(process.env.LOCALAPPDATA||path.join(os.homedir(),'AppData/Local'),'cambc/assets',profile.cacheRelease,'visualiser/assets',profile.engineFile));
const replayPath=args.get('seed-replay')||path.join(asset,'seed.repl26a');
const samplerPath=args.get('sampler')||path.join(ROOT,'tools/replay-interaction-native.mjs');
const bundle=fs.readFileSync(replayPath), wasm=fs.readFileSync(enginePath);
function payloadOf(b){
  let end=-1;for(let p=b.length-22;p>=Math.max(0,b.length-65557);--p)if(b.readUInt32LE(p)===0x06054b50){end=p;break;}
  if(end<0)throw Error('Replay ZIP directory absent');let p=b.readUInt32LE(end+16);
  for(let n=0;n<b.readUInt16LE(end+10);++n){const method=b.readUInt16LE(p+10),size=b.readUInt32LE(p+24),compressed=b.readUInt32LE(p+20),nl=b.readUInt16LE(p+28),el=b.readUInt16LE(p+30),cl=b.readUInt16LE(p+32);
    if(b.subarray(p+46,p+46+nl).toString()==='replay.repl26a'){const q=b.readUInt32LE(p+42),start=q+30+b.readUInt16LE(q+26)+b.readUInt16LE(q+28),v=b.subarray(start,start+compressed);if(![0,8].includes(method)||size>256*1024*1024)throw Error('Replay method/size');const out=method===8?zlib.inflateRawSync(v,{maxOutputLength:size}):Buffer.from(v);if(out.length!==size)throw Error('Replay length');return out;}p+=46+nl+el+cl;
  }throw Error('Replay payload absent');
}
const payload=payloadOf(bundle);
if(sha(bundle)!==domain.replay_sha256||sha(payload)!==domain.payload_sha256||sha(wasm)!==domain.engine_sha256||sha(wasm)!==profile.decoderSha256)throw Error('Native camp immutable asset mismatch');
const identity={domain:'retained-varg-camp18-v1',engine_version:'1.0.3',engine_sha256:sha(wasm),replay_sha256:sha(bundle),payload_sha256:sha(payload),domain_sha256:sha(fs.readFileSync(domainPath)),profile_sha256:sha(fs.readFileSync(profilePath)),sampler_sha256:sha(fs.readFileSync(samplerPath)),oracle_sha256:sha(fs.readFileSync(fileURLToPath(import.meta.url)))};
if(req.kind==='identity'){save({schema:'contenders-native-oracle/v1',native:true,supported:true,identity});process.exit(0);}
const {createInteractionSampler}=await import(pathToFileURL(samplerPath).href);
const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{}});let instance;
const cstr=p=>{const b=new Uint8Array(instance.exports.memory.buffer);let q=p;while(q<b.length&&b[q])++q;return new TextDecoder().decode(b.subarray(p,q));};
instance=await WebAssembly.instantiate(await WebAssembly.compile(wasm),{wasi_snapshot_preview1:wasi.wasiImport,deep_wasm:{on_assert_fail_impl(a,b,c,d){throw Error(`Native assert ${cstr(b)}:${c} ${cstr(a)} ${cstr(d)}`);}}});wasi.initialize(instance);
const e=instance.exports,dv=()=>new DataView(e.memory.buffer),pointers=[];
const alloc=n=>{const p=e.Engine_Malloc(n);if(!p)throw Error('Native allocation');pointers.push(p);return p;};
const input=alloc(payload.length),out=alloc(1024),snap=alloc(profile.timelineSnapshotBytes),entity=alloc(16),perspective=alloc(8),position=alloc(8);
new Uint8Array(e.memory.buffer,input,payload.length).set(payload);
const checked=(name,params)=>{if(name.startsWith('Game_Sandbox_'))throw Error('Sandbox forbidden');const code=e[name](...params);if(code)throw Error(`${name}/${code}: ${cstr(e.GetLastErrorMsg())}`);};
const ACTOR=4,TEAM=0,GROUP=18,ORIGIN=[70,21],WIDTH=8,HEIGHT=7;
let game,sampler;
try{
  checked('Game_FromReplayFileBytes',[input,payload.length,out]);game=dv().getBigUint64(out,true);
  checked('Game_GetBitboardMetadata',[game,out]);const width=dv().getInt32(out,true),height=dv().getInt32(out+4,true),stride=dv().getUint32(out+16,true);
  sampler=createInteractionSampler({e,game,view:dv,profile,width,height,stride,cstring:cstr});
  checked('Game_Visualiser_Timeline_GetMain',[game,out]);const timeline=dv().getBigUint64(out,true);
  const header=()=>{checked('Game_Visualiser_ReadTimelineState',[game,snap]);const d=dv(),u=o=>d.getUint32(snap+o,true);return{round:u(8),phase:u(24),awaiting:!!u(20),owner:u(60),input:u(56),substep:u(104),kind:u(68),actor:u(76)};};
  const full=()=>{header();return sampler.sample(snap);};
  const query=(name,params,bytes,required=true)=>{new Uint8Array(e.memory.buffer,out,1024).fill(0xa5);const code=e[name](...params,out);if(new Uint8Array(e.memory.buffer,out+bytes,1024-bytes).some(x=>x!==0xa5))throw Error('Query canary '+name);if(code&&required)throw Error(`${name}/${code}: ${cstr(e.GetLastErrorMsg())}`);return code?null:dv();};
  dv().setUint32(perspective,TEAM,true);dv().setUint32(perspective+4,ACTOR,true);
  function projection(){
    const state=full(),h=header(),a=state.champions.find(c=>c.id===ACTOR);
    if(a.items.length||a.channel.kind!=='none'||a.restrictions!==0)throw Error('Outside no-items/no-channel/unrestricted actor domain');
    const camps=domain.group.members.map(member=>{const raw=state.camps.find(c=>c.id===member.nativeId),design=domain.catalog.camps.find(c=>c.type===member.typeId),cd=query('Game_API_Camp_GetState',[game,perspective,member.nativeId],4,false),status=cd?cd.getUint32(out,true):null;
      let combat=null;if(raw.health>0){const d=query('Game_API_Camp_Get',[game,perspective,member.nativeId],32);combat={max_hp:d.getFloat32(out+12,true),strength:d.getFloat32(out+16,true),focus:d.getFloat32(out+20,true),cooldowns:[d.getInt32(out+24,true),d.getInt32(out+28,true)]};}
      const buffCount=query('Game_API_Camp_GetNumBuffs',[game,perspective,member.nativeId],4,false);const buffs=buffCount?buffCount.getUint32(out,true):null;
      if(raw.health>0&&(buffs!==0||combat.cooldowns.some(c=>c!==0)))throw Error('Outside wolf/gold zero-cooldown/no-buff camp domain');
      return{id:member.nativeId,type:member.typeId,x:member.x-ORIGIN[0],y:member.y-ORIGIN[1],hp:raw.health,max_hp:combat?.max_hp??design.stats.base_health,strength:combat?.strength??design.stats.base_strength,range:design.stats.base_attack_range,state:status,awake:status===1,buffs,cooldowns:combat?.cooldowns??[0,0]};});
    const gd=query('Game_API_CampGroup_GetState',[game,perspective,GROUP],4,false),groupState=gd?gd.getUint32(out,true):null;
    let respawn=null;if(groupState===3){const d=query('Game_API_CampGroup_GetCooldown',[game,perspective,GROUP],4);respawn=d.getInt32(out,true);}
    return{round:h.round,owner:h.owner,phase:h.phase,input:h.input,substep:h.substep,actor:{id:a.id,team:a.team,type:a.type,x:a.x-ORIGIN[0],y:a.y-ORIGIN[1],hp:a.health,max_hp:a.maxHealth,strength:a.strength,focus:a.focus,range:a.range,alive:a.alive,move:a.availability.move,attack:a.availability.attack,cast:a.availability.cast,completed:a.availability.completedTurn,cooldowns:a.cooldowns,shield:a.shield,shield_ticks:a.buffs.find(b=>b.type===0)?.timer??0,stun:a.buffs.find(b=>b.type===1)?.timer??0,gold:a.gold,gems:a.gems,items:a.items,channel:a.channel,restrictions:a.restrictions},camps,group:{id:GROUP,state:groupState,capture_team:state.groups[GROUP].captureTeam,reward_available:state.groups[GROUP].rewardAvailable,respawn},others:state.champions.filter(c=>c.id!==ACTOR&&c.alive).map(c=>({id:c.id,team:c.team,x:c.x-ORIGIN[0],y:c.y-ORIGIN[1]}))};
  }
  function scenario(p,inputIndex){
    const actor=p.actor,blocked=[],terrain=domain.map.terrain;
    const hard=new Set([...(terrain.wall||[]),...(terrain.hole||[]),...(terrain.altar||[])]);
    for(let y=0;y<HEIGHT;++y)for(let x=0;x<WIDTH;++x)if(hard.has((y+ORIGIN[1])*100+x+ORIGIN[0]))blocked.push([x,y]);
    const spells=[{kind:'reset',cooldown:actor.cooldowns[0],max_cooldown:8,exclusive:false},
      {kind:'shield',cooldown:actor.cooldowns[1],max_cooldown:8,exclusive:false,shield:actor.focus*2.5,duration:5}];
    return{id:'native-varg4-camp18-input'+inputIndex,width:WIDTH,height:HEIGHT,round:p.round,active_actor:0,blocked,settings:{spawn_healing:0},actors:[{id:'4',team:TEAM,x:actor.x,y:actor.y,spawn:[0,0],hp:actor.hp,max_hp:actor.max_hp,strength:actor.strength,focus:actor.focus,range:actor.range,ranged:false,basic_radius:1,can_move:actor.move,can_attack:actor.attack,can_cast:actor.cast,completed:actor.completed,shield:actor.shield,shield_ticks:actor.shield_ticks,stun:actor.stun,gold:actor.gold,gems:actor.gems,spells,mitigation:1,passive:'already_applied'}],camps:p.camps.map((c,i)=>({...c,group:GROUP,gold:i===0?60:0,gems:i===0?4:0,respawn:50,quiet:0,stun:0})),group_states:[{id:GROUP,status:p.group.state===1?'alive':p.group.state===2?'reward':'paid',capture_team:p.group.capture_team===2?-1:p.group.capture_team,capture_left:-1,respawn_left:p.group.respawn??-1}],goal:{kind:'capture_depart',team:TEAM,group:GROUP},action_subset:['basic','cast','move','end'],provenance:{kind:'actual_native_retained_prefix',identity,source_input:inputIndex,native_origin:ORIGIN,actor:ACTOR,group:GROUP,guest_policies:'all other owners yield',action_subset:'Varg basic/splash, reset0, shield1, move and owner-end; no other actions',spawn_projection:'Native spawn is outside crop. Dummy anchor cannot heal (spawn_healing=0); recall excluded.',scope:'Full declared actor/camp/group trace, not complete-world equivalence'}};
  }
  function seek(index,branch=true){if(!Number.isInteger(index)||index<0||index>20000)throw Error('Invalid source_input');checked('Game_Visualiser_Timeline_Seek',[game,timeline,index,0]);const original=projection();if(branch){checked('Game_Visualiser_Timeline_Branch',[game,timeline,index,out]);const copy=projection();if(JSON.stringify(copy)!==JSON.stringify(original))throw Error('Branch represented prefix mismatch');}return original;}
  function eligible(p){return p.phase===10&&p.owner===ACTOR&&p.actor.alive&&p.actor.type===10&&!p.actor.items.length&&p.actor.channel.kind==='none'&&p.actor.restrictions===0&&p.actor.x>=0&&p.actor.x<WIDTH&&p.actor.y>=0&&p.actor.y<HEIGHT&&p.group.state===1&&p.camps.every(c=>c.buffs===0||c.hp===0)&&p.others.every(a=>Math.max(Math.abs(a.x-3),Math.abs(a.y-3))>8);}
  if(req.kind==='camp_inventory'){
    const first=req.first_input??1000,last=req.last_input??1542;if(!Number.isInteger(first)||!Number.isInteger(last)||last<first||last-first>3000)throw Error('Inventory bound max3001 input indices');const sources=[];
    for(let index=first;index<=last;++index){checked('Game_Visualiser_Timeline_Seek',[game,timeline,index,0]);const h=header();if(h.owner!==ACTOR||h.phase!==10||!h.awaiting)continue;const a=full().champions.find(c=>c.id===ACTOR);if(!a.alive||a.x<ORIGIN[0]||a.x>=ORIGIN[0]+WIDTH||a.y<ORIGIN[1]||a.y>=ORIGIN[1]+HEIGHT)continue;const p=projection();if(eligible(p))sources.push({source_input:index,projection:p,scenario:scenario(p,index)});}
    save({schema:'contenders-native-oracle/v1',native:true,supported:true,identity,sources,original_seed_unchanged:sha(fs.readFileSync(replayPath))===identity.replay_sha256});
  }else if(['camp_scenario','camp_trace','scenario_validation'].includes(req.kind)){
    const index=req.source_input??req.scenario?.provenance?.source_input??domain.default_input,before=seek(index),constructed=scenario(before,index);
    if(!eligible(before))throw Error('Source outside isolated no-items Varg camp domain');
    const supplied=req.scenario||constructed;
    if(canonical(supplied)!==canonical(constructed))throw Error('Requested scenario differs from actual native prefix');
    if(req.scenario_sha256&&req.scenario_sha256!==sha(canonical(supplied)))throw Error('Scenario hash differs from canonical actual scenario');
    if(req.trace_sha256&&req.trace_sha256!==sha(canonical(req.actions||[])))throw Error('Trace hash differs from canonical actions');
    const actions=req.actions||[],transitions=[];if(!Array.isArray(actions)||actions.length>4096)throw Error('Trace actions must be bounded');
    for(const action of actions){const fields={end:['type'],basic:['type','target_kind','target'],cast:['type','slot'],move:['type','x','y']}[action.type];if(!fields||Object.keys(action).some(key=>!fields.includes(key)))throw Error('Unsupported action field or kind');const prior=projection();if(prior.owner!==ACTOR||prior.phase!==10)throw Error('Not current actor input');let code=0,applied=true;const events=[];
      if(action.type==='end'){
        checked('Game_Runner_EndTurn',[game,0]);let found=false;
        for(let step=0;step<3000;++step){checked('Game_Runner_Step',[game]);const h=header();if(h.phase===7||h.phase===9||h.actor===ACTOR)events.push({round:h.round,phase:h.phase,kind:h.kind,actor:h.actor});if(h.phase===10&&h.awaiting){if(h.owner===ACTOR){found=true;break;}checked('Game_Runner_EndTurn',[game,0]);}if(h.phase===11)throw Error('Native game ended inside trace');}if(!found)throw Error('Owner input bound exceeded');
      }else if(action.type==='basic'&&action.target_kind==='camp'&&Number.isInteger(action.target)&&action.target>=0&&action.target<domain.group.members.length){dv().setUint32(entity,2,true);dv().setUint32(entity+4,domain.group.members[action.target].nativeId,true);code=e.Game_Action_Attack(game,entity);applied=code===0;
      }else if(action.type==='move'&&Number.isInteger(action.x)&&Number.isInteger(action.y)&&action.x>=0&&action.x<WIDTH&&action.y>=0&&action.y<HEIGHT){new Uint8Array(e.memory.buffer,out,8).fill(0xa5);code=e.Game_Action_Move(game,action.x+ORIGIN[0],action.y+ORIGIN[1],out);applied=code===0&&dv().getInt32(out,true)!==0;if(new Uint8Array(e.memory.buffer,out+4,4).some(x=>x!==0xa5))throw Error('Move canary');
      }else if(action.type==='cast'&&[0,1].includes(action.slot)&&Object.keys(action).every(k=>['type','slot'].includes(k))){dv().setUint32(entity,0,true);dv().setUint32(entity+4,0,true);dv().setInt32(position,-1,true);dv().setInt32(position+4,-1,true);new Uint8Array(e.memory.buffer,out,8).fill(0xa5);code=e.Game_Action_Cast(game,action.slot,entity,position,out);applied=code===0&&dv().getInt32(out,true)!==0;if(new Uint8Array(e.memory.buffer,out+4,4).some(x=>x!==0xa5))throw Error('Cast canary');
      }else throw Error('Unsupported camp-domain action');
      transitions.push({action,code,applied,error:code?cstr(e.GetLastErrorMsg()):null,before:prior,after:projection(),events});
      if(!applied)break;
    }
    const after=projection(),qualified=req.kind==='scenario_validation'&&Array.isArray(req.expected_states),differences=[];
    const predictionRaw=qualified?compactRawValue('expected_states'):null,predictionHash=predictionRaw?sha(predictionRaw):null;
    if(req.prediction_sha256&&req.prediction_sha256!==predictionHash)throw Error('Prediction hash differs from compact expected-state JSON');
    function compareModel(expected,actual,step){
      if(!expected||!Array.isArray(expected.actors)||expected.actors.length!==1||!Array.isArray(expected.camps)||!Array.isArray(expected.groups))throw Error('Expected states must be complete opt::sim::State records');
      const compare=(field,predicted,native)=>{if(predicted===undefined)throw Error('Expected model state missing '+field);if(typeof predicted==='number'&&typeof native==='number'?Math.abs(predicted-native)>1e-5:canonical(predicted)!==canonical(native))differences.push({step,field,predicted,native});};
      compare('round',expected.round,actual.round);const a=expected.actors[0];
      for(const field of ['x','y','hp','max_hp','strength','focus','range','shield','shield_ticks','stun','gold','gems','completed'])compare('actor.'+field,a[field],actual.actor[field]);
      compare('actor.channel',a.channel,actual.actor.channel.kind==='none'?'':actual.actor.channel.kind);
      for(const [m,n]of[['can_move','move'],['can_attack','attack'],['can_cast','cast']])compare('actor.'+m,a[m],actual.actor[n]);
      compare('actor.cooldowns',a.spells.map(s=>s.cooldown),actual.actor.cooldowns);
      if(expected.camps.length!==actual.camps.length)throw Error('Expected camp list differs');
      for(let i=0;i<actual.camps.length;++i){compare('camp'+i+'.hp',expected.camps[i].hp,actual.camps[i].hp);if(actual.camps[i].hp>0)compare('camp'+i+'.awake',expected.camps[i].awake,actual.camps[i].awake);}
      const g=expected.groups.find(g=>g.id===GROUP);if(!g)throw Error('Expected group missing');
      compare('group.state',({alive:1,reward:2,paid:3})[g.status],actual.group.state);
      compare('group.capture_team',g.capture_team<0?2:g.capture_team,actual.group.capture_team);
      compare('group.reward_available',g.status==='reward',actual.group.reward_available);
      if(actual.group.state===3)compare('group.respawn',g.respawn_left,actual.group.respawn);
    }
    if(qualified){if(req.expected_states.length!==transitions.length+1||transitions.length!==actions.length)differences.push({field:'trace_length'});compareModel(req.expected_states[0],before,-1);for(let i=0;i<Math.min(req.expected_states.length-1,transitions.length);++i){if(!transitions[i].applied)differences.push({step:i,field:'native_action_refused'});compareModel(req.expected_states[i+1],transitions[i].after,i);}}
    const queryStatus=sampler.metadata();if(queryStatus.errors!==0)throw Error('Native sampler reported query errors');
    if(transitions.some(t=>t.after.phase!==10||t.after.owner!==ACTOR))throw Error('Trace left the declared actor-input boundary domain');
    save({schema:'contenders-native-oracle/v1',native:true,supported:true,passed:qualified&&differences.length===0,scope:qualified?'entire-trace':'native-observed-trace',comparison_projection:qualified?'restricted-model-after-every-action':null,prediction_sha256:predictionHash,
      projection_scope:'Every command: round; actor x/y/HP/maxHP/Strength/Focus/range/shield/ticks/stun/gold/gems/completed/channel/3allowances/2cooldowns; camp HP and living awake; group lifecycle/capture owner/reward/paid respawn. Hidden capture countdown, camp quiet counter, and complete world excluded.',
      invariant_preconditions:['Exactly the declared Varg owner input after every command','No actor items/channels/restrictions','Living wolf/gold camp has no buffs and zero cooldowns','No other guest policy acts; all other owners yield'],identity,source_input:index,scenario:constructed,before,transitions,after,differences,scenario_sha256:req.scenario_sha256??null,trace_sha256:req.trace_sha256??null,original_seed_unchanged:sha(fs.readFileSync(replayPath))===identity.replay_sha256,query_status:queryStatus});
  }else throw Error('Unknown native camp request kind');
}catch(error){save({schema:'contenders-native-oracle/v1',native:false,supported:false,passed:false,scope:'unsupported',identity,reason:String(error),scenario_sha256:req.scenario_sha256??null,trace_sha256:req.trace_sha256??null});process.exitCode=1;}
finally{if(sampler)sampler.destroy();if(game)checked('Game_Destroy',[game]);for(const p of pointers)e.Engine_Free(p);}
