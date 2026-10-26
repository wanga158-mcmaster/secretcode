// One frozen retained replay's public draft inputs. No actions, runner, guest or private memory.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {WASI} from 'node:wasi';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const need=(v,m)=>{if(!v)throw Error(m);};
const specPath=path.resolve(process.argv[2]??'');
const specBytes=fs.readFileSync(specPath), spec=JSON.parse(specBytes);
need(sha(specBytes)===process.argv[3],'Spec pin mismatch');
for(const [p,h] of Object.entries({...spec.source,...spec.inputs}))need(sha(fs.readFileSync(path.resolve(root,p)))===h,`Changed input ${p}`);
const asset=Object.values(spec.officialAssets)[0], wasm=fs.readFileSync(asset.path);
need(sha(wasm)===asset.sha256,'Decoder pin mismatch');
const payload=fs.readFileSync(path.resolve(root,spec.payload.path));need(sha(payload)===spec.payload.sha256,'Payload pin mismatch');
const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{}});
let instance;
const text=p=>{const b=new Uint8Array(instance.exports.memory.buffer);let end=p;while(b[end])++end;return new TextDecoder().decode(b.subarray(p,end));};
instance=await WebAssembly.instantiate(await WebAssembly.compile(wasm),{wasi_snapshot_preview1:wasi.wasiImport,deep_wasm:{on_assert_fail_impl(a,b,c,d){throw Error(`Engine assertion ${text(b)}:${c} ${text(a)} ${text(d)}`);}}});
wasi.initialize(instance);const e=instance.exports,view=()=>new DataView(e.memory.buffer);
const allowed={Engine_Malloc:1,Engine_Free:1,GetLastErrorMsg:0,Game_FromReplayFileBytes:3,Game_Destroy:1,
 Game_Visualiser_TimelineSnapshotSize:0,Game_Visualiser_ReadTimelineState:2,Game_Visualiser_Timeline_ReplayNext:2,
 Game_GetBitboardMetadata:2,Game_GetNumChampsPerTeam:2,Game_GetWallBuffer:2,Game_GetHoleBuffer:2,Game_GetAltarBuffer:2,Game_GetBushBuffer:2,
 Game_API_GetSettings:1,Game_API_Champion_Design:3,Game_API_Camp_Design:3,Game_API_GetConsumableInfo:2,Game_API_GetSpawnRegion:3,
 Game_API_Map_GetNumCampGroups:2,Game_API_Map_GetCampGroups:4,Game_API_Map_CampGroup_Get:3,
 Game_API_Map_CampGroup_GetRespawnTime:3,Game_API_Map_CampGroup_GetNumCamps:3,Game_API_Map_CampGroup_GetCampInfo:5,Game_API_Map_CampInfo_Get:3,
 Managed_Bitboard_Create:3,Managed_Bitboard_Destroy:1,Managed_Bitboard_GetRawBufferSize:2,Managed_Bitboard_ImportRawBuffer:3,
 Managed_Bitboard_ExportRawBuffer:3,Managed_Bitboard_Set:4,Managed_Bitboard_GetSetBits:4,Managed_Bitboard_DrawChebyshev:4,
 Managed_Bitboard_DistanceFieldWithExpansionCount:6,Managed_Bitboard_Test:4,
 Managed_DistanceField_Create:3,Managed_DistanceField_Destroy:1,Managed_DistanceField_GetRawBufferSize:2,
 Managed_DistanceField_ExportRawBuffer:3,Managed_DistanceField_ExportLinearBuffer:3,Managed_DistanceField_Get:4};
for(const [n,a]of Object.entries(allowed))need(typeof e[n]==='function'&&e[n].length===a&&spec.abi[n]?.parameters.length===a,`Unknown ABI ${n}`);
const counts={},allocations=[],handles=[];let game=0n,canaryChecks=0;
function call(n,...args){need(allowed[n]===args.length,`Forbidden call ${n}`);counts[n]=(counts[n]??0)+1;return e[n](...args);}
function checked(n,...args){const code=call(n,...args);need(code===0,`${n}: ${code} ${text(e.GetLastErrorMsg())}`);}
function alloc(size){const base=call('Engine_Malloc',size+32);need(base>0&&base%8===0,'Bad allocation');const row={base,ptr:base+16,size,freed:false};allocations.push(row);new Uint8Array(e.memory.buffer,base,size+32).fill(0xa5);new Uint8Array(e.memory.buffer,row.ptr,size).fill(0);return row;}
function bytes(r){const b=new Uint8Array(e.memory.buffer,r.base,r.size+32);need(b.subarray(0,16).every(x=>x===0xa5)&&b.subarray(r.size+16).every(x=>x===0xa5),'Output canary changed');++canaryChecks;return Buffer.from(b.subarray(16,r.size+16));}
function query(n,args,size){const r=alloc(size);checked(n,...args,r.ptr);return bytes(r);}
const u32=b=>b.readUInt32LE(0),i32=b=>b.readInt32LE(0),u64=b=>b.readBigUInt64LE(0),hex=b=>b.toString('hex');
function list(n,args,count){need(count>0&&count<=10000,'Invalid list size');const r=alloc(count*4),num=alloc(4);checked(n,...args,r.ptr,count,num.ptr);need(u32(bytes(num))===count,'List length mismatch');const b=bytes(r);return Array.from({length:count},(_,i)=>b.readUInt32LE(i*4));}
function board(){const h=u64(query('Managed_Bitboard_Create',[width,height],8));need(h!==0n,'Missing board');handles.push(['Managed_Bitboard_Destroy',h]);return h;}
function exportBoard(h){const r=alloc(boardBytes);checked('Managed_Bitboard_ExportRawBuffer',h,r.ptr,r.size);return bytes(r);}
function importBoard(h,b){need(b.length===boardBytes,'Board size');const r=alloc(b.length);new Uint8Array(e.memory.buffer,r.ptr,r.size).set(b);checked('Managed_Bitboard_ImportRawBuffer',h,r.ptr,r.size);bytes(r);}
function positions(h){const r=alloc(width*height*8),n=alloc(4);checked('Managed_Bitboard_GetSetBits',h,r.ptr,width*height,n.ptr);const count=u32(bytes(n));need(count<=width*height,'Bad position count');const b=bytes(r);return Array.from({length:count},(_,i)=>[b.readInt32LE(8*i),b.readInt32LE(8*i+4)]);}
let width,height,boardBytes;
let result;
try{
 need(call('Game_Visualiser_TimelineSnapshotSize')===40152,'Timeline width');
 const input=alloc(payload.length);new Uint8Array(e.memory.buffer,input.ptr,input.size).set(payload);
 game=u64(query('Game_FromReplayFileBytes',[input.ptr,input.size],8));bytes(input);need(game!==0n,'Replay handle');
 const snap=alloc(40152);let steps=0;
 for(;steps<256;++steps){checked('Game_Visualiser_ReadTimelineState',game,snap.ptr);const b=bytes(snap);need(b.readUInt32LE(0)===1&&b.readUInt32LE(4)===40152,'Timeline header');
  if(b.readUInt32LE(24)!==12&&b.readUInt32LE(8)===1&&b.readUInt32LE(20)===1&&b.readUInt32LE(64)===14&&b.readUInt32LE(68)===3)break;
  checked('Game_Visualiser_Timeline_ReplayNext',game,1);
 }
 need(steps<256,'No first gameplay owner-start');const initial=bytes(snap),snapshotHash=sha(initial);
 const metadata=query('Game_GetBitboardMetadata',[game],20);width=metadata.readInt32LE(0);height=metadata.readInt32LE(4);
 need(width===50&&height===40,'Wrong retained map dimensions');const chunkWidth=metadata.readUInt32LE(8),chunkHeight=metadata.readUInt32LE(12),stride=metadata.readUInt32LE(16);
 need(chunkWidth===Math.ceil(width/8)&&chunkHeight===Math.ceil(height/8)&&stride>=chunkWidth,'Metadata chunks');
 const oracle=board();boardBytes=u32(query('Managed_Bitboard_GetRawBufferSize',[oracle],4));need(boardBytes>=((chunkHeight-1)*stride+chunkWidth)*8&&boardBytes<=stride*chunkHeight*8,'Raw board width');
 function bit(b,x,y){return !!(b.readBigUInt64LE(((y>>3)*stride+(x>>3))*8)&(1n<<BigInt(((y&7)<<3)+(x&7))));}
 function set(b,x,y){const at=((y>>3)*stride+(x>>3))*8;b.writeBigUInt64LE(b.readBigUInt64LE(at)|(1n<<BigInt(((y&7)<<3)+(x&7))),at);}
 const terrain={};for(const kind of ['Wall','Hole','Altar','Bush']){const p=u32(query(`Game_Get${kind}Buffer`,[game],4));need(p>0&&p%8===0&&p+boardBytes<=e.memory.buffer.byteLength,'Public map pointer');const raw=Buffer.from(new Uint8Array(e.memory.buffer,p,boardBytes));importBoard(oracle,raw);terrain[kind.toLowerCase()]={rawHex:hex(raw),positions:positions(oracle)};}
 const settings=query('Game_API_GetSettings',[],120),teamSize=u32(query('Game_GetNumChampsPerTeam',[game],4));need(teamSize===5,'Team size');
 const champions=Array.from({length:13},(_,id)=>{const raw=query('Game_API_Champion_Design',[game,id],128);need(i32(raw)===id,'Champion catalogue ID');return {id,rawHex:hex(raw)};});
 const camps=Array.from({length:6},(_,id)=>{const raw=query('Game_API_Camp_Design',[game,id],124);need(i32(raw)===id,'Camp catalogue ID');return {id,rawHex:hex(raw)};});
 const consumables=[0,2].map(id=>({id,rawHex:hex(query('Game_API_GetConsumableInfo',[id],8))}));
 const spawns=[0,1].map(team=>{const raw=query('Game_API_GetSpawnRegion',[game,team],16);const bounds=Array.from({length:4},(_,i)=>raw.readInt32LE(i*4));need(bounds[0]>=0&&bounds[1]>=0&&bounds[2]<=width&&bounds[3]<=height&&bounds[0]<bounds[2]&&bounds[1]<bounds[3],'Spawn rectangle');return {team,bounds,rawHex:hex(raw)};});
 const count=u32(query('Game_API_Map_GetNumCampGroups',[game],4));const nativeOrder=list('Game_API_Map_GetCampGroups',[game],count);
 const groups=nativeOrder.map(id=>{const reward=query('Game_API_Map_CampGroup_Get',[game,id],8),cooldown=i32(query('Game_API_Map_CampGroup_GetRespawnTime',[game,id],4)),n=u32(query('Game_API_Map_CampGroup_GetNumCamps',[game,id],4));
  const members=list('Game_API_Map_CampGroup_GetCampInfo',[game,id],n).map(handle=>{const raw=query('Game_API_Map_CampInfo_Get',[game,handle],12);return {handle,x:raw.readInt32LE(0),y:raw.readInt32LE(4),type:raw.readInt32LE(8),rawHex:hex(raw)};});
  need(members.every(m=>m.x>=0&&m.x<width&&m.y>=0&&m.y<height&&m.type>=0&&m.type<6),'Camp info');
  return {id,gems:reward.readUInt32LE(0),gold:reward.readUInt32LE(4),rewardRawHex:hex(reward),cooldown,members};});
 const obstacles=Buffer.alloc(boardBytes);for(const kind of ['wall','hole','altar']){const b=Buffer.from(terrain[kind].rawHex,'hex');for(let i=0;i<boardBytes;++i)obstacles[i]|=b[i];}
 const enemy=spawns[0].bounds,ally=spawns[1].bounds;for(let y=enemy[1];y<enemy[3];++y)for(let x=enemy[0];x<enemy[2];++x)set(obstacles,x,y);
 const wall=board(),roots=board(),visited=board(),approaches=board();importBoard(wall,obstacles);checked('Managed_Bitboard_Set',roots,ally[0],ally[1],1);
 for(const [x,y]of terrain.altar.positions)checked('Managed_Bitboard_DrawChebyshev',approaches,x,y,settings.readInt32LE(32));
 const field=u64(query('Managed_DistanceField_Create',[width,height],8));handles.push(['Managed_DistanceField_Destroy',field]);const fieldBytes=u32(query('Managed_DistanceField_GetRawBufferSize',[field],4));need(fieldBytes>0&&fieldBytes<=width*height*4,'Field storage bound');
 const expansion=query('Managed_Bitboard_DistanceFieldWithExpansionCount',[wall,roots,visited,field,65535],2);
 const rawField=alloc(fieldBytes),linearField=alloc(width*height*2);checked('Managed_DistanceField_ExportRawBuffer',field,rawField.ptr,rawField.size);checked('Managed_DistanceField_ExportLinearBuffer',field,linearField.ptr,linearField.size);
 const raw=bytes(rawField),linear=bytes(linearField),seen=exportBoard(visited);let oracleChecks=0;
 for(let y=0;y<height;++y)for(let x=0;x<width;++x){const d=query('Managed_DistanceField_Get',[field,x,y],2);need(d.readUInt16LE(0)===linear.readUInt16LE(2*(y*width+x)),'Distance export mismatch');const v=i32(query('Managed_Bitboard_Test',[visited,x,y],4));need(!!v===bit(seen,x,y),'Visited decoding mismatch');oracleChecks+=2;}
 need(linear.readUInt16LE(2*(ally[1]*width+ally[0]))===0&&bit(seen,ally[0],ally[1]),'Root distance canary');
 const roster=[];const number=initial.readUInt32LE(172);need(number===10,'Roster count');for(let i=0;i<number;++i){const at=196+i*252;roster.push({actor:initial.readUInt32LE(at+4),team:initial.readUInt32LE(at+8),type:initial.readUInt32LE(at+12)});}
 checked('Game_Visualiser_ReadTimelineState',game,snap.ptr);need(sha(bytes(snap))===snapshotHash,'Public queries changed timeline state');
 need(JSON.stringify(list('Game_API_Map_GetCampGroups',[game],count))===JSON.stringify(nativeOrder),'Native group order unstable');
 result={schema:'matchmaker-public-inputs/v1',readOnly:true,specSha256:sha(specBytes),moment:{round:1,input:initial.readUInt32LE(56),actor:initial.readUInt32LE(60),phase:14,kind:3,replaySteps:steps,snapshotSha256:snapshotHash},
  metadataRawHex:hex(metadata),width,height,chunkWidth,chunkHeight,stride,boardBytes,fieldBytes,team:1,teamSize,roster,settingsRawHex:hex(settings),champions,camps,consumables,spawns,terrain,groups,
  distance:{hub:[ally[0],ally[1]],obstacleRawHex:hex(obstacles),rootRawHex:hex(exportBoard(roots)),visitedRawHex:hex(seen),fieldRawHex:hex(raw),fieldLinearHex:hex(linear),maxDistance:65535,expansionRawHex:hex(expansion),approachRawHex:hex(exportBoard(approaches)),approachPositions:positions(approaches),oracleChecks},
  boundary:{privateMemoryRead:false,actions:0,runnerCalls:0,guestExecutions:0,newGames:0,modelDraftMemory:'Fresh host-only zero bytes, then production publication; retained private bytes never read.'}};
}finally{
 for(const [name,h]of handles.reverse())checked(name,h);
 if(game!==0n)checked('Game_Destroy',game);
 for(const r of allocations){bytes(r);call('Engine_Free',r.base);r.freed=true;}
}
result.audit={calls:counts,canaryChecks,allResourcesFreed:allocations.every(r=>r.freed),managedHandlesDestroyed:handles.length};
console.log(JSON.stringify(result,null,2));
