"""Stream saved replay expert inputs and exact pre-input observations in one pass.

  arena's Python -B bots/main/zero/tools/exact_export.py --replay REPLAY.zip --max-round 64

No bot, action, branch or match-runner export is called. Native ReplayNext walks
the saved history once. Every increment of the native input index is retained,
including draft, end-turn, cancellation and unknown input kinds. Observations
are queried before the input under the acting champion's personal perspective.
JSONL is streamed to gzip; only one pending record and one native frame are held.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import sys
import time
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[4]
SCRATCH = ROOT / '.cache/main/zero/exact'
sys.path.insert(0, str(ROOT / 'tools/arena'))
import compiled_runner as shared
import core
import exp

JS = r"""
import fs from 'node:fs';
import crypto from 'node:crypto';
import {createGzip} from 'node:zlib';
import {once} from 'node:events';
import {WASI} from 'node:wasi';
import {pathToFileURL} from 'node:url';
const spec=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
const {createPersonalAltarLockObserver}=await import(pathToFileURL(spec.nativeModule));
const {profileById}=await import(pathToFileURL(spec.profileModule));
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const engine=fs.readFileSync(spec.engine),payload=fs.readFileSync(spec.payload);
if(hash(engine)!==spec.engineSha256||hash(payload)!==spec.payloadSha256)throw Error('input changed');
for(const source of spec.sources)if(hash(fs.readFileSync(source.path))!==source.sha256)throw Error('source changed before export');
const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{}});
const instance=await WebAssembly.instantiate(await WebAssembly.compile(engine),{wasi_snapshot_preview1:wasi.wasiImport});
wasi.initialize(instance);
const e=instance.exports,view=()=>new DataView(e.memory.buffer);
const string=p=>{const b=new Uint8Array(e.memory.buffer);let q=p;while(q<b.length&&b[q])q++;return new TextDecoder().decode(b.subarray(p,q));};
const counts={},errors=[];let draftUnavailableQueries=0,draftTeamSize=null;
const call=(name,...args)=>{counts[name]=(counts[name]??0)+1;const code=e[name](...args);if(code){errors.push({name,code,message:string(e.GetLastErrorMsg())});throw Error(`${name}: ${code}: ${errors.at(-1).message}`);}};
if(e.Game_Visualiser_TimelineSnapshotSize()!==40152)throw Error('snapshot ABI changed');
if(typeof e.Game_API_GetTeleportOccupancy!=='function'||e.Game_API_GetTeleportOccupancy.length!==3)throw Error('teleport occupancy ABI changed');
if(typeof e.Game_API_GetAllyWardOccupancy!=='function'||e.Game_API_GetAllyWardOccupancy.length!==3)throw Error('allied ward occupancy ABI changed');
for(const [name,arity] of Object.entries({Game_API_Draft_GetNumRounds:2,Game_API_Draft_Get:4}))if(typeof e[name]!=='function'||e[name].length!==arity)throw Error(`draft ABI changed: ${name}`);
const pointers=[],alloc=n=>{const p=e.Engine_Malloc(n);if(!p)throw Error('allocation failed');pointers.push(p);return p;};
const input=alloc(payload.length),out=alloc(4096),snapshot=alloc(40152),perspective=alloc(8),listOut=alloc(4096),metaOut=alloc(20);
new Uint8Array(e.memory.buffer,input,payload.length).set(payload);
const kindNames=['phase','respawn','capture','tick','move','attack','cast','input','end-turn','set-priority','unknown-10','recall','tribute','shop','ward','consumable','cancel-channel','sandbox','gems-submitted','draft','altar-lock','teleport-channel','teleport-arrival','movement-blocked','resign','sell-item','turn-rollover','bot-error'];
const nameFor=n=>kindNames[n]??`unknown-${n}`;
const gzip=createGzip({level:6}),file=fs.createWriteStream(spec.output);gzip.pipe(file);
const write=async value=>{if(!gzip.write(JSON.stringify(value)+'\n'))await once(gzip,'drain');};
const started=performance.now();let game,lockObserver,rawBoard,boardBytes,width,height,stride,staticMap,rosterWritten=false;
const moment=b=>{const d=new DataView(b.buffer,b.byteOffset,b.byteLength),u=n=>d.getUint32(n,true),i=n=>d.getInt32(n,true);return {
 round:u(8),turn:u(12),team:u(16),awaitingInput:u(20)===1,phase:u(24),input:u(56),actor:u(60),stepPhase:u(64),kind:u(68),
 stepActor:{kind:u(72),id:u(76)},from:[i(80),i(84)],to:[i(88),i(92)],stepRound:u(96),stepInput:u(100),substep:u(104),slot:u(108),abilityType:u(112),priority:i(116)};};
const capture=()=>{call('Game_Visualiser_ReadTimelineState',game,snapshot);const b=Buffer.from(new Uint8Array(e.memory.buffer,snapshot,40152));if(b.readUInt32LE(0)!==1||b.readUInt32LE(4)!==40152)throw Error('timeline layout changed');return {bytes:b,moment:moment(b)};};
const digest=frame=>{const b=Buffer.from(frame.bytes);b.fill(0,48,56);return hash(b);};
const champions=frame=>{const b=frame.bytes,a=[];for(let i=0;i<b.readUInt32LE(172);i++){const p=196+i*252;a.push({id:b.readUInt32LE(p+4),team:b.readUInt32LE(p+8),type:b.readUInt32LE(p+12),x:b.readInt32LE(p+16),y:b.readInt32LE(p+20),hp:b.readFloatLE(p+24),gold:b.readUInt32LE(p+36),gems:b.readUInt32LE(p+40),items:Array.from({length:42},(_,j)=>b.readUInt32LE(p+48+j*4)),consumables:Array.from({length:4},(_,j)=>b.readUInt32LE(p+216+j*4))});}return a;};
const board=ptr=>{const d=view(),chunks=[],tiles=[];for(let i=0;i<boardBytes/8;i++){const v=d.getBigUint64(ptr+i*8,true);if(v)chunks.push([i,v.toString(16).padStart(16,'0')]);for(let k=0;k<64;k++)if(v&(1n<<BigInt(k))){const x=(i%stride)*8+(k%8),y=Math.floor(i/stride)*8+Math.floor(k/8);if(x<width&&y<height)tiles.push(y*width+x);}}return {chunks,tiles};};
const staticMetadata=first=>{
 const c=champions(first).find(c=>c.id===first.moment.actor);
 view().setUint32(perspective,c?.team??0,true);view().setUint32(perspective+4,c?.id??0xffffffff,true);
 call('Game_API_Map_GetBitboardMetadata',game,metaOut);width=view().getInt32(metaOut,true);height=view().getInt32(metaOut+4,true);stride=view().getUint32(metaOut+16,true);
 call('Bitboard_GetRawBufferSize',metaOut,out);boardBytes=view().getUint32(out,true);rawBoard=alloc(boardBytes);
 const terrain={};for(const [label,name] of Object.entries({wall:'Wall',bush:'Bush',hole:'Hole',altar:'Altar'})){call(`Game_API_Map_Get${name}Bitboard`,game,perspective,rawBoard);terrain[label]=board(rawBoard).tiles;}
 const spawns=[0,1].map(team=>{call('Game_API_GetSpawnRegion',game,team,out);return [0,4,8,12].map(n=>view().getInt32(out+n,true));});
 call('Game_API_Map_GetSymmetry',game,out);const symmetry=view().getUint32(out,true);
 call('Game_API_Map_GetNumCampGroups',game,out);const n=view().getUint32(out,true);if(n>500)throw Error('too many groups');
 call('Game_API_Map_GetCampGroups',game,listOut,n,out);if(view().getUint32(out,true)!==n)throw Error('group count changed');const ids=Array.from({length:n},(_,i)=>view().getUint32(listOut+i*4,true));
 const groups=ids.map(id=>{call('Game_API_Map_CampGroup_Get',game,id,out);const gems=view().getUint32(out,true),gold=view().getUint32(out+4,true);
  call('Game_API_Map_CampGroup_GetRespawnTime',game,id,out);const respawn=view().getInt32(out,true);
  call('Game_API_Map_CampGroup_GetNumCamps',game,id,out);const size=view().getUint32(out,true);if(size>500)throw Error('too many members');
  call('Game_API_Map_CampGroup_GetCampInfo',game,id,listOut,size,out);if(view().getUint32(out,true)!==size)throw Error('member count changed');const members=Array.from({length:size},(_,j)=>view().getUint32(listOut+j*4,true));
  return {id,gold,gems,respawn,members:members.map(infoId=>{call('Game_API_Map_CampInfo_Get',game,infoId,out);return {infoId,at:[view().getInt32(out,true),view().getInt32(out+4,true)],type:view().getInt32(out+8,true)};})};});
 call('Game_API_GetSettings',out);const settingsRawHex=Buffer.from(new Uint8Array(e.memory.buffer,out,120)).toString('hex');
 const consumables=[0,2].map(rawId=>{call('Game_API_GetConsumableInfo',rawId,out);return {rawId,cost:view().getUint32(out,true),carryLimit:view().getUint32(out+4,true)};});
 return {type:'static-map',width,height,stride,boardBytes,terrain,spawns,symmetry,groups,settingsRawHex,consumables,
  tileEncoding:'y*width+x',visionEncoding:'sparse [chunkIndex,u64 numeric hex]; chunk=(y>>3)*stride+(x>>3),bit=((y&7)<<3)+(x&7)'};
};
const draftFacts=m=>{
 // Match SDK draft::team_size/current_draft. GetNumRounds writes one byte;
 // Get receives absolute team before zero-based pick round and writes I32.
 new Uint8Array(e.memory.buffer,out,16).fill(0xa5);call('Game_API_Draft_GetNumRounds',game,out);
 if(new Uint8Array(e.memory.buffer,out+1,15).some(x=>x!==0xa5))throw Error('draft team-size output width changed');
 const teamSize=view().getUint8(out);if(teamSize<1||teamSize>16)throw Error('draft team size outside storage bound');
 draftTeamSize=teamSize;
 const picksByTeam=[0,1].map(team=>Array.from({length:teamSize},(_,round)=>{
  new Uint8Array(e.memory.buffer,out,16).fill(0xa5);const name='Game_API_Draft_Get';counts[name]=(counts[name]??0)+1;
  const code=e[name](game,team,round,out);
  if(new Uint8Array(e.memory.buffer,out+4,12).some(x=>x!==0xa5))throw Error('draft pick output width changed');
  if(code===16){draftUnavailableQueries++;return {round,status:'unavailable',rawPick:null,errorCode:16};}
  if(code){const message=string(e.GetLastErrorMsg());errors.push({name,code,message});throw Error(`${name}: ${code}: ${message}`);}
  const rawPick=view().getInt32(out,true);return {round,status:rawPick<0?'unselected-sentinel':'picked',rawPick};
 }));
 const complete=team=>picksByTeam[team].filter(p=>p.status==='picked').map(p=>p.rawPick);
 return {team:m.team,teamSize,picksByTeam,oursRawPicks:complete(m.team),theirsRawPicks:complete(1-m.team),availableRawPickIds:null,
  availabilitySource:'No native available-pick-set API; legacy zero draft loops raw IDs 0..11 and excludes its own completed picks'};
};
const completedDraftPicks=()=>{
 if(draftTeamSize===null)throw Error('completed draft labels require the traversed native team size');
 // A post-draft label table, never an observation feature. Preserve native
 // failures/sentinels instead of substituting identities from the final roster.
 return [0,1].map(team=>Array.from({length:draftTeamSize},(_,round)=>{
  new Uint8Array(e.memory.buffer,out,16).fill(0xa5);const name='Game_API_Draft_Get';counts[name]=(counts[name]??0)+1;
  const queryCode=e[name](game,team,round,out);
  if(new Uint8Array(e.memory.buffer,out+4,12).some(x=>x!==0xa5))throw Error('completed draft pick output width changed');
  if(queryCode)return {round,status:queryCode===13||queryCode===16?'unavailable':'query-error',rawPick:null,queryCode,message:string(e.GetLastErrorMsg())};
  const rawPick=view().getInt32(out,true);return {round,status:rawPick<0?'unselected-sentinel':'picked',rawPick,queryCode};
 }));
};
const observe=frame=>{
 const m=frame.moment,all=champions(frame),c=all.find(c=>c.id===m.actor);
 if(m.phase===12)return {moment:m,draft:true,draftFacts:draftFacts(m),team:m.team,self:null,visibleChampions:[],visibleCamps:[],groups:[],altarLocks:[],reservedTiles:null,allyWardTiles:null,banked:null};
 if(!c)throw Error('gameplay input has no current champion');
 view().setUint32(perspective,c.team,true);view().setUint32(perspective+4,c.id,true);
 const unit=n=>{call('Game_API_Champion_Get',game,perspective,n.id,out);const d=view(),state=d.getUint32(out+40,true);
  if(d.getInt32(out+8,true)!==n.x||d.getInt32(out+12,true)!==n.y||d.getFloat32(out+16,true)!==n.hp)throw Error('native champion differs from same frame');
  const info={id:n.id,team:n.team,type:n.type,at:[n.x,n.y],hp:n.hp,maxHp:d.getFloat32(out+20,true),strength:d.getFloat32(out+24,true),focus:d.getFloat32(out+28,true),moveSpeed:d.getInt32(out+32,true),attackRange:d.getInt32(out+36,true),gems:n.gems,stateBits:state,
   recalling:!!(state&2),tributing:!!(state&4),channel:state&4096?'teleport':state&4?'tribute':state&2?'recall':'none',channelRemaining:null,allowances:{move:!!(state&8),attack:!!(state&16),cast:!!(state&2048)}};
  if(n.id===c.id){
   call('Game_API_Champion_GetGold',game,perspective,n.id,out);if(view().getUint32(out,true)!==n.gold)throw Error('own gold differs from timeline');
   call('Game_API_Champion_GetGems',game,perspective,n.id,out);if(view().getUint32(out,true)!==n.gems)throw Error('own gems differ from timeline');
   call('Game_API_Champion_GetItems',game,perspective,n.id,out,42);if(n.items.some((x,i)=>x!==view().getUint32(out+i*4,true)))throw Error('own items differ from timeline');
   call('Game_API_Champion_GetConsumables',game,perspective,n.id,out,4);if(n.consumables.some((x,i)=>x!==view().getUint32(out+i*4,true)))throw Error('own consumables differ from timeline');
   call('Game_API_Champion_GetWards',game,perspective,n.id,out);const wardCharges=view().getUint32(out,true),wardCooldown=view().getInt32(out+4,true);
   call('Game_API_Champion_CanShop',game,perspective,n.id,0,out);const shopAllowed=view().getInt32(out,true)!==0;
   call('Game_API_Champion_CanShop',game,perspective,n.id,1,out);const consumableShopAllowed=view().getInt32(out,true)!==0;
   call('Game_API_Champion_CanSell',game,perspective,n.id,out);const canSell=view().getInt32(out,true)!==0;
   call('Game_API_Champion_GetPriority',game,perspective,n.id,0,out);const priority=view().getInt32(out,true);
   call('Game_API_Champion_GetPriority',game,perspective,n.id,1,out);const nextPriority=view().getInt32(out,true);
   call('Game_API_Champion_GetWardPositions',game,perspective,n.id,0,0,out);const wardCount=view().getUint32(out,true);if(wardCount>512)throw Error('ward position count exceeds buffer');
   call('Game_API_Champion_GetWardPositions',game,perspective,n.id,listOut,wardCount,out);if(view().getUint32(out,true)!==wardCount)throw Error('ward position count changed');const wards=Array.from({length:wardCount},(_,j)=>[view().getInt32(listOut+j*8,true),view().getInt32(listOut+j*8+4,true)]);
   Object.assign(info,{gold:n.gold,items:n.items,consumables:n.consumables,wardCharges,wardCooldown,wardReady:wardCharges>0,wards,priority,nextPriority,shopAllowed,consumableShopAllowed,canSell});
  }
  info.cooldown=[0,1].map(slot=>{call('Game_API_Champion_GetCooldown',game,perspective,n.id,slot,out);return view().getInt32(out,true);});
  if(info.channel!=='none'){call('Game_API_Champion_GetChannelTime',game,perspective,n.id,out);info.channelRemaining=view().getInt32(out,true);}
  call('Game_API_Champion_GetRestrictions',game,perspective,n.id,out);info.restrictions=view().getUint8(out);
  call('Game_API_Champion_GetNumBuffs',game,perspective,n.id,out);const buffCount=view().getUint32(out,true);if(buffCount>3)throw Error('buff count exceeds ABI');
  call('Game_API_Champion_GetBuffData',game,perspective,n.id,listOut,3,out);if(view().getUint32(out,true)!==buffCount)throw Error('buff count changed');info.buffs=Array.from({length:buffCount},(_,j)=>{const type=view().getUint32(listOut+j*12,true);return {type,timer:view().getInt32(listOut+j*12+4,true),payloadRaw:view().getUint32(listOut+j*12+8,true),...(type===0?{value:view().getFloat32(listOut+j*12+8,true)}:{})};});
  info.shield=info.buffs.filter(b=>b.type===0).reduce((s,b)=>s+b.value,0);info.stunned=info.buffs.some(b=>b.type===1);
  return info;};
 const self=unit(c);
 call('Game_Visualiser_GetVisionBuffer',game,perspective,out);const visionPtr=view().getUint32(out,true),vision=board(visionPtr),visibleTiles=new Set(vision.tiles),seen=(x,y)=>x>=0&&y>=0&&visibleTiles.has(y*width+x);
 // Match SDK vision::teleport_tiles(): the native query applies this actor's
 // personal visibility. A successful empty board is []; draft/no actor is null.
 // Query failures throw and prevent publication of a successful export.
 call('Game_API_GetTeleportOccupancy',game,perspective,rawBoard);
 const reservedTiles=board(rawBoard).tiles.map(tile=>[tile%width,Math.floor(tile/width)]);
 // Match SDK vision::ward_tiles(ally), including other allies' revealed wards;
 // self.wards is a different ownership list and cannot substitute for this view.
 call('Game_API_GetAllyWardOccupancy',game,perspective,rawBoard);
 const allyWardTiles=board(rawBoard).tiles.map(tile=>[tile%width,Math.floor(tile/width)]);
 const visible=all.filter(n=>n.id!==c.id&&n.hp>0&&seen(n.x,n.y));
 const camps=[],b=frame.bytes;for(let i=0;i<b.readUInt32LE(176);i++){const p=4228+i*28,x=b.readInt32LE(p+16),y=b.readInt32LE(p+20),hp=b.readFloatLE(p+24);if(hp>0&&seen(x,y)){const camp={id:b.readUInt32LE(p+4),type:b.readInt32LE(p+12),at:[x,y],hp};call('Game_API_Camp_GetState',game,perspective,camp.id,out);camp.stateBits=view().getUint32(out,true);camps.push(camp);}}
 const visibleChampions=visible.map(unit);
 const groups=[];for(const g of staticMap.groups)if(g.members.some(n=>seen(...n.at))){call('Game_API_CampGroup_GetState',game,perspective,g.id,out);const state=view().getUint32(out,true);let cooldown=null;if(state===3){call('Game_API_CampGroup_GetCooldown',game,perspective,g.id,out);cooldown=view().getInt32(out,true);}groups.push({id:g.id,state,cooldown});}
 const altarLocks=lockObserver.observe(c);if(altarLocks===null)throw Error('personal altar lock query failed');
 const banked=[0,1].map(team=>{call('Game_API_GetSubmittedGemCount',game,team,out);return view().getUint32(out,true);});
 const aliveCounts=[0,0];for(const n of all){call('Game_API_Champion_IsAlive',game,n.id,out);if(view().getInt32(out,true)!==0)aliveCounts[n.team]++;}
 return {moment:m,self,visibleChampions,visibleCamps:camps,groups,altarLocks,reservedTiles,allyWardTiles,banked,aliveCounts,vision:{chunks:vision.chunks,tiles:vision.tiles}};
};
let records=0,steps=0,lastInput=0,observedInputs=0,nonReadyInputs=0,stopped='step-cap',pending,previous;
const kinds={},actors=new Map(),inputHash=crypto.createHash('sha256');let firstCampContact=null;
try{
 call('Game_FromReplayFileBytes',input,payload.length,out);game=view().getBigUint64(out,true);
 call('Game_Visualiser_Timeline_GetMain',game,out);const timeline=view().getBigUint64(out,true);
 call('Game_Visualiser_Timeline_Info',game,timeline,out,out+8,out+16);const timelineInputs=view().getUint32(out,true);
 call('Game_Visualiser_Timeline_Seek',game,timeline,0,0);
 await write({type:'metadata',schema:'zero-exact-inputs/v1.6',engineSha256:spec.engineSha256,replaySha256:spec.replaySha256,payloadSha256:spec.payloadSha256,
  match:spec.match,winner:spec.winner,winnerSource:spec.winnerSource,profile:spec.profile,sources:spec.sources,nativeInputKinds:kindNames,timelineInputs,
  scope:'Exact native pre-input champion perspective; raw replay successor step. No actions, branches, bot execution or fuel validation.',
  reservedTiles:{source:'Game_API_GetTeleportOccupancy(game,personalPerspective,outRawBoard)',scope:'Current personally visible teleport reservations, matching SDK vision::teleport_tiles()',empty:[],unavailable:null,queryFailure:'Export fails; no successful artifact is published'},
  allyWardTiles:{source:'Game_API_GetAllyWardOccupancy(game,personalPerspective,outRawBoard)',scope:'Current personally visible allied ordinary-ward occupancy, matching SDK vision::ward_tiles(ally); independent of self.wards',known:'Non-null means the native occupancy query succeeded, including []',empty:[],unavailable:null,queryFailure:'Export fails; no successful artifact is published'},
  draftFacts:{source:'Game_API_Draft_GetNumRounds(game,outU8); Game_API_Draft_Get(game,absoluteTeam,zeroBasedRound,outI32)',
   unavailableStatus:'API error16 DraftUnavailable: unknown or future pick; rawPick null',unselectedSentinel:'API success with a negative rawPick is preserved separately',
   availableRawPickIds:null,availabilityApi:null,legacyCandidateLoop:'SDK champion_draft raw IDs 0..11; existing zero draft excludes only its own completed picks. This is a static legacy loop, not a native availability claim.'},
  completedDraftPicks:{record:'actors.completedDraftPicksByTeam',labelOnly:true,source:'Game_API_Draft_Get(game,absoluteTeam,zeroBasedRound,outI32), queried once after draft',lookup:'For native kind19, index by before.draftFacts.team and action.stepRound (the same draft round as before.moment.round). action.slot is not the draft round, and action.team can name the successor team. Native kind7 is not a pick label.',failure:'Native query codes and negative sentinels remain explicit; no final-roster substitution or future before-feature fill'},
  limits:{maxRound:spec.maxRound,maxSteps:spec.maxSteps,contactTeam:spec.contactTeam,contactRounds:spec.contactRounds},snapshotHashMask:[48,56]});
 let frame=capture();
 for(;steps<spec.maxSteps;steps++){
  const m=frame.moment;
  if(m.phase!==12&&m.round>spec.maxRound){stopped='round-window-end';break;}
  if(firstCampContact&&m.round>firstCampContact.round+spec.contactRounds){stopped='first-camp-contact-window-end';break;}
  if(!staticMap&&m.phase!==12&&champions(frame).some(c=>c.id===m.actor)){
   staticMap=staticMetadata(frame);await write(staticMap);
   lockObserver=createPersonalAltarLockObserver({e,game,view,profile:profileById(spec.profile),width,height});
   const roster=champions(frame).map(c=>({id:c.id,team:c.team,type:c.type}));for(const c of roster)actors.set(c.id,c);
   const types=spec.championTypes,designs=types.map(type=>{call('Game_API_Champion_Design',game,type,out);return {type,rawHex:Buffer.from(new Uint8Array(e.memory.buffer,out,128)).toString('hex')};});
   const completedDraftPicksByTeam=completedDraftPicks();
   await write({type:'actors',roster,designs,designsScope:'All physical champion types in the pinned runtime catalog; label-independent static facts',completedDraftPicksByTeam,completedDraftPicksLabelOnly:true,completedDraftPickTeamSize:draftTeamSize,completedDraftPickSourceMoment:m});rosterWritten=true;
  }
  if(m.awaitingInput){pending={before:observe(frame),beforeTimelineSha256:digest(frame)};}
  const code=e.Game_Visualiser_Timeline_ReplayNext(game,1);counts.Game_Visualiser_Timeline_ReplayNext=(counts.Game_Visualiser_Timeline_ReplayNext??0)+1;
  if(code){if(m.input<timelineInputs)throw Error(`ReplayNext before timeline end: ${code}: ${string(e.GetLastErrorMsg())}`);stopped='replay-end';break;}
  const next=capture(),n=next.moment;
  if(n.input!==m.input){
   observedInputs++;
   if(n.input!==m.input+1)throw Error(`input gap: ${m.input}->${n.input}`);
   if(!m.awaitingInput||!pending){nonReadyInputs++;throw Error(`input advanced without ready predecessor: ${JSON.stringify({m,n})}`);}
   if(n.substep!==0)throw Error(`input successor substep is ${n.substep}`);
   const owner=pending.before.self,afterOwner=owner?champions(next).find(c=>c.id===owner.id):null;
   const action={...n,name:nameFor(n.kind)};
   const record={type:'input',sequence:records,input:n.input,before:pending.before,action,
    afterSelf:afterOwner?{id:afterOwner.id,at:[afterOwner.x,afterOwner.y],hp:afterOwner.hp,gold:afterOwner.gold,gems:afterOwner.gems,items:afterOwner.items,consumables:afterOwner.consumables}:null,
    beforeTimelineSha256:pending.beforeTimelineSha256,afterTimelineSha256:digest(next)};
   if(firstCampContact===null&&spec.contactTeam!==null&&owner?.team===spec.contactTeam&&[5,6].includes(n.kind)){
    const targets=pending.before.visibleCamps.filter(c=>c.at[0]===n.to[0]&&c.at[1]===n.to[1]);
    if(targets.length)firstCampContact={input:n.input,round:m.round,actor:owner.id,kind:n.kind,targets};
   }
   await write(record);inputHash.update(`${n.input}:${n.kind}\n`);records++;kinds[action.name]=(kinds[action.name]??0)+1;lastInput=n.input;pending=null;
  }
  previous=frame;frame=next;
 }
 if(stopped==='step-cap')throw Error('step cap before requested window end');
 const locks=lockObserver?.metadata();if(locks?.errors)throw Error('altar lock query failures');
 for(const source of spec.sources)if(hash(fs.readFileSync(source.path))!==source.sha256)throw Error('source changed during export');
 const summary={type:'trailer',pass:true,records,observedInputs,nonReadyInputs,firstInput:records?1:null,lastInput,kinds,steps,stopped,rosterWritten,queryErrors:errors,firstCampContact,
  nativeCalls:counts,draftUnavailableQueries,altarLockQueries:locks,inputSequenceSha256:inputHash.digest('hex'),elapsedSeconds:(performance.now()-started)/1000};
 await write(summary);gzip.end();await once(file,'close');console.log(JSON.stringify(summary));
}finally{lockObserver?.destroy();if(game!==undefined)call('Game_Destroy',game);for(const p of pointers)e.Engine_Free(p);if(!gzip.writableEnded)gzip.destroy();}
"""


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--replay', type=Path, required=True)
    ap.add_argument('--max-round', type=int, default=64)
    ap.add_argument('--max-steps', type=int, default=100000)
    ap.add_argument('--timeout', type=int, default=55)
    ap.add_argument('--contact-team', choices=['A', 'B'], help='Stop after the first observed camp attack/cast by this team plus the declared round window')
    ap.add_argument('--contact-rounds', type=int, default=12)
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    if not 1 <= args.max_round <= 2000 or not 1 <= args.max_steps <= 1000000 or not 1 <= args.timeout <= 3600 or not 0 <= args.contact_rounds <= 64:
        ap.error('invalid traversal bounds')
    replay = args.replay.resolve(strict=True)
    data = replay.read_bytes()
    if zipfile.is_zipfile(replay):
        with zipfile.ZipFile(replay) as z:
            if z.getinfo('replay.repl26a').file_size > 256 * 1024 * 1024:
                raise ValueError('oversize replay payload')
            payload = z.read('replay.repl26a')
    else:
        payload = data
    if len(payload) < 14 or int.from_bytes(payload[:8], 'little') != 125779999602273:
        raise ValueError('unsupported replay header')
    engine = exp.engine_wasm()
    engine_sha = digest(engine)
    profiles = json.loads((ROOT / 'tools/replay-profiles.json').read_text())['profiles']
    profile = next(p for p in profiles if p['decoderSha256'] == engine_sha)
    if '.'.join(map(str, payload[8:11])) != profile['engineVersion']:
        raise ValueError('saved replay requires another engine')
    directory = SCRATCH / uuid.uuid4().hex
    directory.mkdir(parents=True)
    output = (args.output or directory / 'inputs.jsonl.gz').resolve()
    if not output.is_relative_to(SCRATCH.resolve()):
        raise ValueError('output must stay under .cache/main/zero/exact')
    if output.exists():
        raise ValueError('refusing to overwrite existing output')
    output.parent.mkdir(parents=True, exist_ok=True)
    payload_file, script, spec_file = (directory / n for n in ['input.repl26a','exact.mjs','spec.json'])
    staged_output = directory / 'inputs.partial.jsonl.gz'
    payload_file.write_bytes(payload)
    script.write_text(JS, encoding='utf-8', newline='\n')
    source_paths = [Path(__file__).resolve(), ROOT / 'tools/replay-interaction-native.mjs',
        ROOT / 'tools/replay-profiles.mjs', ROOT / 'tools/replay-profiles.json',
        ROOT / 'tools/arena/compiled_runner.py', ROOT / 'tools/arena/core.py',
        ROOT / 'docs/reference/runtime-catalog.json', ROOT / 'sdk/cpp/cambc.h']
    winner, winner_source, match = None, None, {'matchId': replay.stem}
    index = replay.parent / 'index.json'
    if index.is_file():
        index_bytes = index.read_bytes()
        matches = [x for x in json.loads(index_bytes) if x.get('matchId') == replay.stem]
        if len(matches) == 1:
            match = matches[0]
            winning = [t for t in match.get('teams', []) if t.get('result') == 'win']
            winner = (0 if winning[0]['side'] == 'a' else 1) if len(winning) == 1 else -1
            winner_source = {'path':str(index),'sha256':hashlib.sha256(index_bytes).hexdigest(),'entry':replay.stem}
    spec = {'engine':str(engine),'engineSha256':engine_sha,'payload':str(payload_file),
        'payloadSha256':hashlib.sha256(payload).hexdigest(),'replaySha256':hashlib.sha256(data).hexdigest(),
        'output':str(staged_output),'profile':profile['id'],'nativeModule':str(ROOT / 'tools/replay-interaction-native.mjs'),
        'profileModule':str(ROOT / 'tools/replay-profiles.mjs'),'sources':[{'path':str(p),'sha256':digest(p)} for p in source_paths],
        'maxRound':args.max_round,'maxSteps':args.max_steps,'contactTeam':None if args.contact_team is None else int(args.contact_team=='B'),'contactRounds':args.contact_rounds,'winner':winner,'winnerSource':winner_source,'match':match,
        'championTypes':[c['type'] for c in json.loads((ROOT / 'docs/reference/runtime-catalog.json').read_text())['result']['champions']]}
    spec_file.write_text(json.dumps(spec), encoding='utf-8')
    wall_start = time.monotonic()
    try:
        record = shared._slotted_command(lambda _: [str(core.NODE),'--disable-warning=ExperimentalWarning',str(script),str(spec_file)], ROOT, directory, args.timeout, jobs=1)
    finally:
        while shared._PENDING_CLEANUP:
            if shared.confirm_pending_cleanup():
                break
    stdout, stderr = (directory / 'stdout.bin').read_text(), (directory / 'stderr.bin').read_text()
    for path in (payload_file, script, directory / 'stdout.bin', directory / 'stderr.bin'):
        path.unlink(missing_ok=True)
    if record['returncode'] != 0 or not record['tree_exit_confirmed'] or record['timed_out']:
        (directory / 'failure.json').write_text(json.dumps({'stderr':stderr[-8000:],'process':record}), encoding='utf-8')
        print(stderr, file=sys.stderr)
        staged_output.unlink(missing_ok=True)
        return record['returncode'] or 1
    result = json.loads(stdout)
    if result.get('pass') is not True:
        raise ValueError('export did not confirm completion')
    os.replace(staged_output, output)
    result.update({'output':str(output),'outputSha256':digest(output),'compressedBytes':output.stat().st_size,'wallSeconds':time.monotonic()-wall_start})
    (directory / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({k:result[k] for k in ['pass','records','kinds','steps','stopped','elapsedSeconds','compressedBytes','output']}))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
