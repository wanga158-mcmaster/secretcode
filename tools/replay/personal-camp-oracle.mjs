// Read one saved owner-start through the pinned official decoder. Only public
// metadata and the selected champion's personal SDK queries are exposed.
// Launch native sampling through personal-camp-oracle.py, which owns its Slot/Job.
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import {fileURLToPath} from 'node:url';
import {WASI} from 'node:wasi';
import {PROFILE_MODULE_SHA256, PROFILE_SHA256, REPLAY_PROFILES, verifyProfileInputs} from '../replay-profiles.mjs';
import {layouts} from '../runtime-catalog/public-config-layout-v2.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const ensure = (condition, message) => { if (!condition) throw Error(message); };
export function decodeCamp(bytes) {
  ensure(bytes.length === 32, 'Camp_Get requires exactly32bytes');
  const d = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const value = {x:d.getInt32(0,true),y:d.getInt32(4,true),health:d.getFloat32(8,true),
    maxHealth:d.getFloat32(12,true),strength:d.getFloat32(16,true),focus:d.getFloat32(20,true),
    cooldowns:[d.getInt32(24,true),d.getInt32(28,true)],
    cooldownRawBits:[d.getUint32(24,true),d.getUint32(28,true)]};
  ensure([value.health,value.maxHealth,value.strength,value.focus].every(Number.isFinite) &&
    value.health>0 && value.maxHealth>0 && value.health<=value.maxHealth,'Invalid living Camp_Get fields');
  return value;
}
export function decodeCampState(bytes) {
  ensure(bytes.length===4,'Camp_GetState requires exactly4bytes');
  const state=bytes.readUInt32LE(0);
  ensure(state<=2,'Unknown camp state enum');
  return {state,alive:state!==0,awake:state===1,asleep:state===2};
}
export function decodeCampDesign(bytes) {
  ensure(bytes.length===124&&layouts.camp_stats.size===36,'Camp design requires verified124-byte C record');
  const d=new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength),stats={};
  for(const [name,type,offset] of layouts.camp_stats.fields)stats[name]=type==='i32'?d.getInt32(88+offset,true):d.getFloat32(88+offset,true);
  ensure(Object.values(stats).every(Number.isFinite)&&Number.isInteger(stats.base_attack_range)&&stats.base_attack_range>=0,'Camp design stats invalid');
  const abilities=[12,48].map(at=>({kind:d.getUint32(at,true),cooldown:d.getInt32(at+4,true),
    maxCharges:d.getUint32(at+8,true),targets:d.getUint32(at+12,true),attributes:d.getUint32(at+16,true),
    payloadRawHex:bytes.subarray(at+20,at+36).toString('hex')}));
  return {...stats,definition:{type:d.getInt32(0,true),visionType:d.getUint32(4,true),
      passiveType:d.getUint8(8),attackType:d.getUint8(84),abilities},
    rawHex:bytes.toString('hex'),basicAttackIntervalRounds:null,
    basicAttackIntervalStatus:'Not exposed by these bound SDK records. Ability cooldowns are not a basic-attack interval.'};
}
export function checkCanaries(bytes,width) {
  ensure(bytes.length===width+32,'Invalid canary buffer width');
  ensure(!bytes.subarray(0,16).some(x=>x!==0xa5) &&
    !bytes.subarray(16+width).some(x=>x!==0xa5),'Native output overwrote canary');
}
export function wasmSignatures(bytes) {
  ensure(bytes.subarray(0,8).equals(Buffer.from([0,97,115,109,1,0,0,0])),'Unknown WASM header');
  let p=8;const types=[],imports=[],functions=[],exports=[];
  const u=()=>{let v=0,n=0;for(;n<5;n++){ensure(p<bytes.length,'Truncated WASM integer');const b=bytes[p++];v+=(b&127)*2**(7*n);if(!(b&128))return v;}throw Error('Oversized WASM integer');};
  const name=()=>{const n=u();ensure(p+n<=bytes.length,'Truncated WASM name');const s=bytes.toString('utf8',p,p+n);p+=n;return s;};
  const limits=()=>{const f=u();u();if(f&1)u();ensure(!(f&~3),'Unsupported WASM limits');};
  while(p<bytes.length){const id=bytes[p++],size=u(),end=p+size;ensure(end<=bytes.length,'Truncated WASM section');
    if(id===1){const n=u();for(let j=0;j<n;j++){ensure(bytes[p++]===0x60,'Unsupported WASM type');const take=()=>Array.from({length:u()},()=>bytes[p++]);types.push({params:take(),results:take()});}}
    else if(id===2){const n=u();for(let j=0;j<n;j++){name();name();const kind=bytes[p++];if(kind===0)imports.push(u());else if(kind===1){p++;limits();}else if(kind===2)limits();else if(kind===3)p+=2;else if(kind===4){u();u();}else throw Error('Unknown WASM import');}}
    else if(id===3){const n=u();for(let j=0;j<n;j++)functions.push(u());}
    else if(id===7){const n=u();for(let j=0;j<n;j++){const key=name(),kind=bytes[p++],index=u();if(kind===0)exports.push([key,index]);}}
    ensure(p<=end,'WASM section parser overflow');p=end;
  }
  const indices=[...imports,...functions];
  return Object.fromEntries(exports.map(([name,index])=>{ensure(types[indices[index]],'Missing function type');return [name,types[indices[index]]];}));
}
function crc32(bytes) {let c=0xffffffff;for(const b of bytes){c^=b;for(let k=0;k<8;k++)c=(c>>>1)^((c&1)?0xedb88320:0);}return(c^0xffffffff)>>>0;}
export function replayPayload(bytes) {
  if(bytes.length>=8&&bytes.readBigUInt64LE(0)===125779999602273n)return bytes;
  let end=-1;for(let p=bytes.length-22;p>=Math.max(0,bytes.length-65557);p--)if(bytes.readUInt32LE(p)===0x06054b50){end=p;break;}
  ensure(end>=0&&!bytes.readUInt16LE(end+4)&&!bytes.readUInt16LE(end+6),'Unsupported replay ZIP');
  const count=bytes.readUInt16LE(end+10);let at=bytes.readUInt32LE(end+16),found;
  ensure(count!==65535&&at!==0xffffffff,'ZIP64 is unsupported');
  for(let j=0;j<count;j++){
    ensure(bytes.readUInt32LE(at)===0x02014b50,'Malformed ZIP directory');
    const flags=bytes.readUInt16LE(at+8),method=bytes.readUInt16LE(at+10),crc=bytes.readUInt32LE(at+16),
      packed=bytes.readUInt32LE(at+20),size=bytes.readUInt32LE(at+24),n=bytes.readUInt16LE(at+28),
      extra=bytes.readUInt16LE(at+30),comment=bytes.readUInt16LE(at+32),name=bytes.toString('utf8',at+46,at+46+n);
    if(name==='replay.repl26a'){
      ensure(!found&&!(flags&1)&&[0,8].includes(method)&&size<=256*1024*1024,'Unsupported replay member');
      const local=bytes.readUInt32LE(at+42);ensure(bytes.readUInt32LE(local)===0x04034b50,'Malformed ZIP local header');
      const start=local+30+bytes.readUInt16LE(local+26)+bytes.readUInt16LE(local+28);
      const input=bytes.subarray(start,start+packed);ensure(input.length===packed,'Truncated replay member');
      found=method===8?zlib.inflateRawSync(input,{maxOutputLength:Math.max(1,size)}):Buffer.from(input);
      ensure(found.length===size&&crc32(found)===crc,'Replay member length/CRC differs');
    }
    at+=46+n+extra+comment;
  }
  ensure(found,'Replay payload is absent');return found;
}
export function validateScene(spec) {
  ensure(spec?.schema==='arena-personal-camp-oracle/v1','Unsupported scene schema');
  ensure(Object.keys(spec).sort().join(',')==='actor,map,replay,round,sample,schema,team','Unexpected scene fields');
  for(const name of ['replay','map'])ensure(spec[name]&&typeof spec[name].path==='string'&&/^[a-f0-9]{64}$/.test(spec[name].sha256),'Invalid input pin');
  ensure(Object.keys(spec.replay).sort().join(',')==='path,sha256'&&Object.keys(spec.map).sort().join(',')==='id,path,sha256','Unexpected input pin fields');
  ensure(/^[A-Za-z0-9_-]+$/.test(spec.map.id),'Invalid map id');
  ensure(Number.isInteger(spec.actor)&&spec.actor>=0&&spec.actor<16&&[0,1].includes(spec.team),'Invalid personal perspective');
  ensure(Number.isInteger(spec.round)&&spec.round>=0&&spec.round<=2000&&typeof spec.sample==='boolean','Invalid bounded sample');
  return spec;
}
const I32=0x7f,I64=0x7e;
const ABI = Object.freeze({
  Engine_Malloc:[[I32],[I32]],Engine_Free:[[I32],[]],GetLastErrorMsg:[[],[I32]],
  Game_FromReplayFileBytes:[[I32,I32,I32],[I32]],Game_Destroy:[[I64],[I32]],
  Game_Visualiser_TimelineSnapshotSize:[[],[I32]],Game_Visualiser_ReadTimelineState:[[I64,I32],[I32]],
  Game_Visualiser_Timeline_GetMain:[[I64,I32],[I32]],Game_Visualiser_Timeline_Info:[[I64,I64,I32,I32,I32],[I32]],
  Game_Visualiser_Timeline_Seek:[[I64,I64,I32,I32],[I32]],Game_Visualiser_Timeline_ReplayNext:[[I64,I32],[I32]],
  Game_API_Map_GetBitboardMetadata:[[I64,I32],[I32]],
  Game_API_Map_GetWallBitboard:[[I64,I32,I32],[I32]],Game_API_Map_GetHoleBitboard:[[I64,I32,I32],[I32]],
  Game_API_Map_GetAltarBitboard:[[I64,I32,I32],[I32]],Game_API_Map_GetBushBitboard:[[I64,I32,I32],[I32]],
  Game_API_GetSpawnRegion:[[I64,I32,I32],[I32]],Game_API_GetVision:[[I64,I32,I32],[I32]],
  Game_API_GetCampOccupancy:[[I64,I32,I32],[I32]],Game_API_GetAwakeCampOccupancy:[[I64,I32,I32],[I32]],
  Game_API_GetAsleepCampOccupancy:[[I64,I32,I32],[I32]],Game_API_GetAllyOccupancy:[[I64,I32,I32],[I32]],
  Game_API_GetEnemyOccupancy:[[I64,I32,I32],[I32]],Game_API_GetTeleportOccupancy:[[I64,I32,I32],[I32]],
  Game_API_GetCamp:[[I64,I32,I32,I32,I32],[I32]],Game_API_GetChampion:[[I64,I32,I32,I32,I32],[I32]],
  Game_API_Camp_Get:[[I64,I32,I32,I32],[I32]],Game_API_Camp_GetState:[[I64,I32,I32,I32],[I32]],
  Game_API_Map_CampInfo_Get:[[I64,I32,I32],[I32]],Game_API_Camp_Design:[[I64,I32,I32],[I32]],
  Game_API_Champion_Get:[[I64,I32,I32,I32],[I32]],
  Bitboard_GetRawBufferSize:[[I32,I32],[I32]],
  Bitboard_ReachableMask:[[I32,I32,I32,I32,I32,I32],[I32]],Bitboard_GetSetBits:[[I32,I32,I32,I32,I32],[I32]]
});
export function verifyABI(signatures) {
  for(const [name,[params,results]] of Object.entries(ABI))
    ensure(JSON.stringify(signatures[name])===JSON.stringify({params,results}),
      'Unsupported official ABI: '+name+' actual='+JSON.stringify(signatures[name])+' expected='+JSON.stringify({params,results}));
}
export async function sampleScene(spec,scenePin) {
  validateScene(spec);ensure(process.env.ARENA_REPLAY_SLOT==='1','Native sample requires the contained Slot/Job launcher');
  const inputPath=name=>{const file=path.resolve(ROOT,spec[name].path);ensure(file.startsWith(ROOT+path.sep)&&fs.realpathSync(file)===file,'Input path redirected/outside workspace');return file;};
  const archivePath=inputPath('replay'),mapPath=inputPath('map'),archive=fs.readFileSync(archivePath),mapBytes=fs.readFileSync(mapPath);
  ensure(sha(archive)===spec.replay.sha256&&sha(mapBytes)===spec.map.sha256,'Pinned scene input differs');
  const payload=replayPayload(archive);ensure(payload.length>=14&&payload.readBigUInt64LE(0)===125779999602273n,'Unknown replay header');
  const version=offset=>[...payload.subarray(offset,offset+3)].join('.');
  const profiles=REPLAY_PROFILES.filter(p=>p.engineVersion===version(8)&&p.replayFormatVersions.includes(version(11)));
  ensure(profiles.length===1&&profiles[0].id==='cli-3.0.5','Camp oracle supports only the verified cli3.0.5 ABI');
  const profile=profiles[0],assets=path.join(process.env.LOCALAPPDATA??path.join(os.homedir(),'AppData/Local'),'cambc/assets');
  const enginePath=['visualiser','mapeditor'].map(area=>path.join(assets,profile.cacheRelease,area,'assets',profile.engineFile)).find(fs.existsSync);
  ensure(enginePath,'Pinned official decoder is not cached');
  const wasm=fs.readFileSync(enginePath);ensure(sha(wasm)===profile.decoderSha256,'Official decoder pin differs');
  const signatures=wasmSignatures(wasm);verifyABI(signatures);
  const geometryPath=path.join(ROOT,'maps/geometry.json'),geometryBytes=fs.readFileSync(geometryPath),geometry=JSON.parse(geometryBytes.toString('utf8').replace(/^\uFEFF/,''));
  ensure(geometry.engineSha256===profile.decoderSha256,'Geometry decoder differs');
  const maps=geometry.maps.filter(m=>m.id===spec.map.id&&m.mapSha256===spec.map.sha256);
  ensure(maps.length===1,'Public map/source pin unavailable');const map=maps[0];
  const dependencies=[fileURLToPath(import.meta.url),path.join(ROOT,'tools/replay-profiles.mjs'),path.join(ROOT,'tools/replay-profiles.json'),
    path.join(ROOT,'tools/runtime-catalog/public-config-layout-v2.mjs'),path.join(ROOT,'tools/runtime-catalog/abi.cpp'),
    path.join(ROOT,'sdk/cpp/imports.h'),path.join(ROOT,'sdk/cpp/cambc.h'),
    path.join(ROOT,'tools/export-game-geometry.mjs'),path.join(ROOT,'tools/fastrun/fastrun.cpp'),
    path.join(ROOT,'tools/replay/conveyor/native-camp.mjs'),geometryPath,mapPath,archivePath,enginePath];
  const pins=dependencies.map(file=>({path:file,sha256:sha(fs.readFileSync(file)),bytes:fs.statSync(file).size}));
  const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{}});let instance;
  const cstring=ptr=>{const b=new Uint8Array(instance.exports.memory.buffer);let q=ptr;while(q<b.length&&q<ptr+16384&&b[q])q++;return new TextDecoder().decode(b.subarray(ptr,q));};
  instance=await WebAssembly.instantiate(await WebAssembly.compile(wasm),{wasi_snapshot_preview1:wasi.wasiImport,
    deep_wasm:{on_assert_fail_impl(a,b,c,d){throw Error('Official decoder assertion '+cstring(b)+':'+c+' '+cstring(a)+' '+cstring(d));}}});
  wasi.initialize(instance);const e=instance.exports,view=()=>new DataView(e.memory.buffer),counts={},resources=[];
  const call=(name,...args)=>{ensure(Object.hasOwn(ABI,name),'Disallowed native export');counts[name]=(counts[name]??0)+1;return e[name](...args);};
  const checked=code=>{if(code!==0)throw Error('Official query failed: '+code+' '+cstring(call('GetLastErrorMsg')));};
  const alloc=size=>{const base=call('Engine_Malloc',size+32);ensure(base>0&&base%8===0,'Invalid native allocation');const row={base,ptr:base+16,size};resources.push(row);new Uint8Array(e.memory.buffer,base,size+32).fill(0xa5);return row;};
  const bytes=row=>{const all=Buffer.from(new Uint8Array(e.memory.buffer,row.base,row.size+32));checkCanaries(all,row.size);return all.subarray(16,16+row.size);};
  const query=(name,args,size)=>{const row=alloc(size);checked(call(name,...args,row.ptr));return Buffer.from(bytes(row));};
  let game=0n;
  try {
    ensure(call('Game_Visualiser_TimelineSnapshotSize')===profile.timelineSnapshotBytes,'Timeline ABI differs');
    const input=alloc(payload.length),out=alloc(32),snapshot=alloc(profile.timelineSnapshotBytes),perspective=alloc(8);
    new Uint8Array(e.memory.buffer,input.ptr,input.size).set(payload);
    checked(call('Game_FromReplayFileBytes',input.ptr,input.size,out.ptr));bytes(input);bytes(out);
    game=view().getBigUint64(out.ptr,true);ensure(game!==0n,'Replay game handle unavailable');
    // The public C engine metadata is20bytes; SDK's28bytes adds derived sizes.
    // Native Bitboard calls take this metadata pointer before the SDK arguments
    // (tools/fastrun/fastrun.cpp Marshal::call and play metadata setup).
    const metaRow=alloc(20);checked(call('Game_API_Map_GetBitboardMetadata',game,metaRow.ptr));
    const meta=Buffer.from(bytes(metaRow)),md=new DataView(meta.buffer,meta.byteOffset,meta.byteLength);
    const width=md.getInt32(0,true),height=md.getInt32(4,true),cw=md.getUint32(8,true),ch=md.getUint32(12,true),
      stride=md.getUint32(16,true),boardBytes=query('Bitboard_GetRawBufferSize',[metaRow.ptr],4).readUInt32LE(0);
    ensure(width===map.width&&height===map.height&&width>0&&height>0&&cw===Math.ceil(width/8)&&ch===Math.ceil(height/8)&&
      boardBytes>=((ch-1)*stride+cw)*8&&boardBytes<=stride*ch*8&&boardBytes%8===0,'Map bitboard metadata differs');
    view().setUint32(perspective.ptr,spec.team,true);view().setUint32(perspective.ptr+4,spec.actor,true);
    checked(call('Game_Visualiser_Timeline_GetMain',game,out.ptr));const timeline=view().getBigUint64(out.ptr,true);
    checked(call('Game_Visualiser_Timeline_Info',game,timeline,out.ptr,out.ptr+8,out.ptr+16));const inputCount=view().getUint32(out.ptr,true);
    const read=()=>{checked(call('Game_Visualiser_ReadTimelineState',game,snapshot.ptr));const b=Buffer.from(bytes(snapshot));ensure(b.readUInt32LE(0)===1&&b.readUInt32LE(4)===profile.timelineSnapshotBytes,'Timeline record differs');return b;};
    const roundAt=n=>{checked(call('Game_Visualiser_Timeline_Seek',game,timeline,n,0));const b=read();return b.readUInt32LE(24)===12?-1:b.readUInt32LE(8);};
    let lo=0,hi=inputCount;while(lo+1<hi){const middle=lo+Math.floor((hi-lo)/2);if(roundAt(middle)<spec.round)lo=middle;else hi=middle;}
    roundAt(lo);let before=null,moment=null,steps=0;const walk=crypto.createHash('sha256');
    for(;steps<50000;steps++){
      const b=read();walk.update(b);const round=b.readUInt32LE(8);
      ensure(b.readUInt32LE(24)===12||round<=spec.round,'Requested actor owner-start not found');
      if(b.readUInt32LE(24)!==12&&round===spec.round&&b.readUInt32LE(20)===1&&b.readUInt32LE(68)===3&&b.readUInt32LE(64)===14&&b.readUInt32LE(60)===spec.actor){
        before=b;moment={round,input:b.readUInt32LE(56),substep:b.readUInt32LE(104),phase:b.readUInt32LE(64),kind:b.readUInt32LE(68),actor:b.readUInt32LE(60),awaitingInput:true};break;
      }
      checked(call('Game_Visualiser_Timeline_ReplayNext',game,1));
    }
    ensure(before&&moment,'Owner-start lookup exceeded bound');
    const count=before.readUInt32LE(172);ensure(count<=16,'Timeline champion count invalid');
    const roster=Array.from({length:count},(_,j)=>{const at=196+j*252;return{id:before.readUInt32LE(at+4),team:before.readUInt32LE(at+8),type:before.readUInt32LE(at+12),x:before.readInt32LE(at+16),y:before.readInt32LE(at+20),health:before.readFloatLE(at+24),gold:before.readUInt32LE(at+36),gems:before.readUInt32LE(at+40)};});
    const actor=roster.find(c=>c.id===spec.actor);ensure(actor&&actor.team===spec.team&&actor.health>0,'Selected own actor identity differs/is dead');
    let card=null;
    if(spec.sample){
      const champion=(id)=>{const b=query('Game_API_Champion_Get',[game,perspective.ptr,id],44);return{id,team:b.readInt32LE(0),type:b.readInt32LE(4),x:b.readInt32LE(8),y:b.readInt32LE(12),health:b.readFloatLE(16),maxHealth:b.readFloatLE(20),strength:b.readFloatLE(24),focus:b.readFloatLE(28),moveSpeed:b.readInt32LE(32),attackRange:b.readInt32LE(36),stateBits:b.readUInt32LE(40)};};
      const me=champion(spec.actor);ensure(['team','type','x','y','health'].every(k=>me[k]===actor[k]),'Own Champion_Get differs from native timeline');
      const boards={},terrainExports={wall:'Game_API_Map_GetWallBitboard',hole:'Game_API_Map_GetHoleBitboard',altar:'Game_API_Map_GetAltarBitboard',bush:'Game_API_Map_GetBushBitboard'};
      for(const [key,name] of Object.entries(terrainExports))boards[key]=query(name,[game,perspective.ptr],boardBytes);
      const spawnRaw=query('Game_API_GetSpawnRegion',[game,1-spec.team],16),spawnBounds={
        minX:spawnRaw.readInt32LE(0),minY:spawnRaw.readInt32LE(4),maxX:spawnRaw.readInt32LE(8),maxY:spawnRaw.readInt32LE(12)};
      boards.enemySpawn=Buffer.alloc(boardBytes);
      ensure(spawnBounds.minX>=0&&spawnBounds.minY>=0&&spawnBounds.maxX<=width&&spawnBounds.maxY<=height,'Invalid native spawn bounds');
      for(let y=spawnBounds.minY;y<spawnBounds.maxY;y++)for(let x=spawnBounds.minX;x<spawnBounds.maxX;x++){
        const offset=((y>>3)*stride+(x>>3))*8,bit=1n<<BigInt(((y&7)<<3)+(x&7));
        boards.enemySpawn.writeBigUInt64LE(boards.enemySpawn.readBigUInt64LE(offset)|bit,offset);
      }
      for(const [key,name] of Object.entries({vision:'Game_API_GetVision',camp:'Game_API_GetCampOccupancy',awakeCamp:'Game_API_GetAwakeCampOccupancy',
        asleepCamp:'Game_API_GetAsleepCampOccupancy',allyChampion:'Game_API_GetAllyOccupancy',enemyChampion:'Game_API_GetEnemyOccupancy',teleport:'Game_API_GetTeleportOccupancy'}))
        boards[key]=query(name,[game,perspective.ptr],boardBytes);
      const sees=(board,x,y)=>x>=0&&y>=0&&x<width&&y<height&&Boolean(board.readBigUInt64LE(((y>>3)*stride+(x>>3))*8)&(1n<<BigInt(((y&7)<<3)+(x&7))));
      const positions=board=>{const list=[];for(let y=0;y<height;y++)for(let x=0;x<width;x++)if(sees(board,x,y))list.push({x,y});return list;};
      for(const key of Object.keys(terrainExports)){
        const actual=positions(boards[key]).map(p=>p.y*width+p.x),expected=[...map.terrain[key]].sort((a,b)=>a-b);
        ensure(JSON.stringify(actual)===JSON.stringify(expected),'Native public terrain differs from pinned map: '+key);
      }
      const spawn=map.spawns[spec.team===0?'teamB':'teamA'],spawnTiles=[];
      for(let y=spawn.minY;y<spawn.maxY;y++)for(let x=spawn.minX;x<spawn.maxX;x++)spawnTiles.push(y*width+x);
      ensure(JSON.stringify(positions(boards.enemySpawn).map(p=>p.y*width+p.x))===JSON.stringify(spawnTiles),'Native enemy spawn differs');
      const resolve=(name,p)=>query(name,[game,perspective.ptr,p.x,p.y],4).readUInt32LE(0);
      const designs=new Map(),members=new Map(map.groups.flatMap(g=>g.members.map(m=>[m.nativeId,{...m,groupId:g.nativeId,group:g.key}])));
      const camps=positions(boards.camp).map(p=>{
        ensure(sees(boards.vision,p.x,p.y),'Camp occupancy contains a hidden tile');
        const id=resolve('Game_API_GetCamp',p),state=decodeCampState(query('Game_API_Camp_GetState',[game,perspective.ptr,id],4)),
          live=decodeCamp(query('Game_API_Camp_Get',[game,perspective.ptr,id],32));
        ensure(state.alive&&live.x===p.x&&live.y===p.y,'Camp personal occupancy/state/live oracle differs');
        ensure(state.awake===sees(boards.awakeCamp,p.x,p.y)&&state.asleep===sees(boards.asleepCamp,p.x,p.y),'Camp awake/asleep occupancy differs');
        const raw=query('Game_API_Map_CampInfo_Get',[game,id],12),metadata={x:raw.readInt32LE(0),y:raw.readInt32LE(4),type:raw.readInt32LE(8)},member=members.get(id);
        ensure(member&&metadata.x===member.x&&metadata.y===member.y&&metadata.type===member.typeId,'Camp public metadata differs');
        if(!designs.has(metadata.type)){
          designs.set(metadata.type,decodeCampDesign(query('Game_API_Camp_Design',[game,metadata.type],124)));
        }
        return{id,group:member.group,groupId:member.groupId,type:metadata.type,spawn:{x:metadata.x,y:metadata.y},...state,...live,attackRange:designs.get(metadata.type).base_attack_range};
      });
      const enemies=positions(boards.enemyChampion).map(p=>{ensure(sees(boards.vision,p.x,p.y),'Enemy occupancy contains hidden tile');const c=champion(resolve('Game_API_GetChampion',p));ensure(c.team!==spec.team&&c.x===p.x&&c.y===p.y&&c.health>0,'Enemy personal occupancy differs');return c;});
      const knownBlocked=Buffer.alloc(boardBytes);
      for(let p=0;p<boardBytes;p++)for(const key of ['wall','hole','altar','enemySpawn','allyChampion','enemyChampion','camp','teleport'])knownBlocked[p]|=boards[key][p];
      const originByte=((actor.y>>3)*stride+(actor.x>>3))*8,originBit=1n<<BigInt(((actor.y&7)<<3)+(actor.x&7));
      knownBlocked.writeBigUInt64LE(knownBlocked.readBigUInt64LE(originByte)&~originBit,originByte);
      const walls=alloc(boardBytes),reachable=alloc(boardBytes),tiles=alloc(9*8),tileCount=alloc(4);
      new Uint8Array(e.memory.buffer,walls.ptr,boardBytes).set(knownBlocked);
      checked(call('Bitboard_ReachableMask',metaRow.ptr,walls.ptr,actor.x,actor.y,reachable.ptr,1));
      ensure(bytes(walls).equals(knownBlocked),'Pure reachable query changed input walls');bytes(reachable);
      checked(call('Bitboard_GetSetBits',metaRow.ptr,reachable.ptr,tiles.ptr,9,tileCount.ptr));const n=bytes(tileCount).readUInt32LE(0),tileBytes=bytes(tiles);
      ensure(n<=9,'One-step native reachable count invalid');
      const reachableOrder=Array.from({length:n},(_,j)=>({x:tileBytes.readInt32LE(j*8),y:tileBytes.readInt32LE(j*8+4)}));
      ensure(reachableOrder.every(p=>Math.max(Math.abs(p.x-actor.x),Math.abs(p.y-actor.y))<=1&&!sees(knownBlocked,p.x,p.y)),'Reachable mask outside one-step known walls');
      const localTiles=[];
      for(let y=Math.max(0,actor.y-1);y<=Math.min(height-1,actor.y+1);y++)for(let x=Math.max(0,actor.x-1);x<=Math.min(width-1,actor.x+1);x++)
        localTiles.push({x,y,...Object.fromEntries(Object.entries(boards).map(([name,board])=>[name,sees(board,x,y)])),blockedWithCurrentVisibleCamps:sees(knownBlocked,x,y)});
      const encoded=Object.fromEntries(Object.entries(boards).map(([name,b])=>[name,Array.from({length:boardBytes/8},(_,j)=>[j,b.readBigUInt64LE(j*8)]).filter(([,v])=>v!==0n).map(([j,v])=>[j,v.toString(16).padStart(16,'0')])]));
      card={actor:me,camps,enemies,campDesigns:Object.fromEntries(designs),localTiles,allLocalTilesPersonallyVisible:localTiles.every(p=>p.vision),
        reachableOrderWithCurrentVisibleCampBlockers:reachableOrder,boards:encoded,boardEncoding:{kind:'sparse-u64-hex',width,height,stride,bytes:boardBytes},
        privateInputsUnknown:['recovering_','remembered_camps_ outside current personal vision','fuel at recovery guard','whether recover() was called'],
        reachableScope:'Native one-step order using public terrain and CURRENT personal occupants. It equals the private local blocked mask only where all one-step tiles are currently visible. No fallback route/home or greedy decision is inferred.'};
    }
    const after=read();ensure(before.equals(after),'Personal queries changed the complete exposed timeline snapshot');
    for(const row of resources)bytes(row);
    ensure(bytes(input).equals(payload),'Read-only replay payload memory changed');
    ensure(bytes(metaRow).equals(meta),'Read-only native metadata changed');
    const expectedPerspective=Buffer.alloc(8);expectedPerspective.writeUInt32LE(spec.team,0);expectedPerspective.writeUInt32LE(spec.actor,4);
    ensure(bytes(perspective).equals(expectedPerspective),'Read-only personal perspective changed');
    verifyProfileInputs();for(const pin of pins)ensure(sha(fs.readFileSync(pin.path))===pin.sha256,'Oracle input changed during sample: '+pin.path);
    return{schema:'arena-personal-camp-oracle-result/v1',readOnly:true,scene:spec,scenePin,profile:profile.id,profileRegistrySha256:PROFILE_SHA256,
      profileModuleSha256:PROFILE_MODULE_SHA256,inputs:pins,abi:Object.fromEntries(Object.keys(ABI).map(name=>[name,signatures[name]])),
      replayPayloadSha256:sha(payload),mapIdentification:spec.sample?'Pinned source map plus full public terrain/spawn and visible camp metadata match.':'Pinned source map and dimensions only; personal-query control.',moment,actor,
      playback:{positionedInput:lo,walkedSubsteps:steps,prefixTimelineSha256:walk.digest('hex'),noPlaybackAdvanceAfterSample:true},
      memoryGuards:{allocationCanariesChecked:resources.length,replayInputEqual:true,metadataEqual:true,perspectiveEqual:true},
      invariance:{timelineBytes:profile.timelineSnapshotBytes,beforeSha256:sha(before),afterSha256:sha(after),equal:true,
        scope:'Complete exposed timeline snapshot only; not a claim about hidden/private state or fuel.'},nativeCalls:counts,card};
  } finally {
    if(game!==0n)checked(call('Game_Destroy',game));
    for(const row of resources)call('Engine_Free',row.base);
  }
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  ensure(process.argv.length===6&&process.argv[2]==='--spec'&&process.argv[4]==='--spec-sha256','Expected --spec PATH --spec-sha256 HASH');
  const file=path.resolve(process.argv[3]),bytes=fs.readFileSync(file);
  ensure(/^[a-f0-9]{64}$/.test(process.argv[5])&&sha(bytes)===process.argv[5],'Scene spec hash differs');
  console.log(JSON.stringify(await sampleScene(JSON.parse(bytes),{path:file,sha256:sha(bytes)}),null,2));
}
