// WASI production DraftAllocator supplied ONLY the frozen actual public input record.
// No native engine is loaded here. Fuel and fresh draft memory are explicit model interventions.
import fs from 'node:fs';
import crypto from 'node:crypto';
import {WASI} from 'node:wasi';
const [wasmPath,publicPath,publicSha]=process.argv.slice(2);
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
const input=fs.readFileSync(publicPath);if(sha(input)!==publicSha)throw Error('Public input pin mismatch');
const p=JSON.parse(input),need=(x,m)=>{if(!x)throw Error(m);};
need(p.schema==='matchmaker-public-inputs/v1'&&p.readOnly&&p.team===1,'Wrong input schema/team');
const raw=h=>Buffer.from(h,'hex'),calls={};let instance;
const mem=()=>new Uint8Array(instance.exports.memory.buffer),dv=()=>new DataView(instance.exports.memory.buffer);
const write=(ptr,b)=>{need(ptr>0&&ptr+b.length<=mem().length,'Host pointer');mem().set(b,ptr);};
const u32=(ptr,v)=>dv().setUint32(ptr,v,true),i32=(ptr,v)=>dv().setInt32(ptr,v,true);
const bool=(ptr,v)=>i32(ptr,v?1:0);
const read=(ptr,n)=>Buffer.from(mem().subarray(ptr,ptr+n));
const boardBytes=p.boardBytes,fieldBytes=p.fieldBytes;
function index(x,y){need(Number.isInteger(x)&&Number.isInteger(y)&&x>=0&&y>=0&&x<p.width&&y<p.height,'Out of map');return [((y>>3)*p.stride+(x>>3))*8,BigInt(((y&7)<<3)+(x&7))];}
function get(ptr,x,y){const [at,bit]=index(x,y);return !!(dv().getBigUint64(ptr+at,true)&(1n<<bit));}
function set(ptr,x,y,value){const [at,bit]=index(x,y),old=dv().getBigUint64(ptr+at,true),mask=1n<<bit;dv().setBigUint64(ptr+at,value?old|mask:old&~mask,true);}
function points(ptr){const result=[];for(let y=0;y<p.height;++y)for(let x=0;x<p.width;++x)if(get(ptr,x,y))result.push([x,y]);return result;}
const freshDraft=Buffer.alloc(raw(p.settingsRawHex).readUInt32LE(72));let draft=freshDraft;
const records={};for(const g of p.groups)for(const m of g.members){need(!records[m.handle],'Repeated handle');records[m.handle]=m;}
const layout=[];for(const g of p.groups){layout.push(g.id,g.gold,g.gems,g.cooldown,g.members.length);for(const m of g.members)layout.push(m.handle,m.type,m.x,m.y);}
const settings=raw(p.settingsRawHex),metadata=Buffer.alloc(28);raw(p.metadataRawHex).copy(metadata);metadata.writeUInt32LE(boardBytes,20);metadata.writeUInt32LE(fieldBytes,24);
const env={
 game_settings:out=>{write(out,settings);return 0;},shared_memory_size:out=>{u32(out,settings.readUInt32LE(68));return 0;},
 bc_metadata:out=>{write(out,metadata);return 0;},my_champion:()=>-1,my_team:()=>p.team,
 champion_design:(id,out)=>{need(p.champions[id]?.id===id,'Champion ID');write(out,raw(p.champions[id].rawHex));return 0;},
 camp_design:(id,out)=>{need(p.camps[id]?.id===id,'Camp ID');write(out,raw(p.camps[id].rawHex));return 0;},
 consumable_info:(id,out)=>{const v=p.consumables.find(x=>x.id===id);need(v,'Consumable ID');write(out,raw(v.rawHex));return 0;},
 draft_memory_size:out=>{u32(out,draft.length);return 0;},
 read_draft_memory:(offset,out,size)=>{need(offset>=0&&offset+size<=draft.length,'Draft read');write(out,draft.subarray(offset,offset+size));return 0;},
 bc_submit_draft_memory:(ptr,size)=>{need(size===draft.length,'Draft publication width');draft=read(ptr,size);return 0;},
 draft_num_rounds:out=>{mem()[out]=p.teamSize;return 0;},
 bc_map_symmetry:out=>{i32(out,1);return 0;},
 bc_map_mask:(kind,out)=>{const name=['wall','bush','hole','altar'][kind];need(name,'Mask tag');write(out,raw(p.terrain[name].rawHex));return 0;},
 spawn_region:(team,out)=>{need(team===0||team===1,'Team');write(out,raw(p.spawns[team].rawHex));return 0;},
 map_camp_layout:(out,capacity,count)=>{u32(count,layout.length);for(let i=0;i<Math.min(capacity,layout.length);++i)u32(out+4*i,layout[i]);return 0;},
 fuel_remaining:()=>1000000n,
 bc_get_last_error:()=>{throw Error('Unexpected SDK error request');},
 bc_Bitboard_Zero:ptr=>{mem().fill(0,ptr,ptr+boardBytes);return 0;},
 bc_Bitboard_Set:(ptr,x,y,value)=>{set(ptr,x,y,value);return 0;},
 bc_Bitboard_ImportRawBuffer:(ptr,bytes,size)=>{need(size===boardBytes,'Board import size');write(ptr,read(bytes,size));return 0;},
 bc_Bitboard_Union:(a,b,out)=>{const aa=read(a,boardBytes),bb=read(b,boardBytes);for(let i=0;i<boardBytes;++i)aa[i]|=bb[i];write(out,aa);return 0;},
 bc_Bitboard_NumSetBits:(ptr,out)=>{u32(out,points(ptr).length);return 0;},
 bc_Bitboard_GetSetBits:(ptr,out,capacity,count)=>{const ps=points(ptr);u32(count,ps.length);for(let i=0;i<Math.min(capacity,ps.length);++i){i32(out+8*i,ps[i][0]);i32(out+8*i+4,ps[i][1]);}return 0;},
 bc_Bitboard_DrawChebyshev:(ptr,x,y,radius)=>{need(Number.isFinite(radius)&&radius>=0,'Radius');for(let yy=0;yy<p.height;++yy)for(let xx=0;xx<p.width;++xx)if(Math.max(Math.abs(xx-x),Math.abs(yy-y))<=radius)set(ptr,xx,yy,true);return 0;},
 bc_Bitboard_Test:(ptr,x,y,out)=>{bool(out,get(ptr,x,y));return 0;},
 bc_DistanceField_ComputeWithExpansionCount:(walls,roots,visited,field,maxDistance,count)=>{
  need(read(walls,boardBytes).equals(raw(p.distance.obstacleRawHex)),'Native field obstacle mismatch');
  need(read(roots,boardBytes).equals(raw(p.distance.rootRawHex))&&maxDistance===p.distance.maxDistance,'Native field root/radius mismatch');
  write(visited,raw(p.distance.visitedRawHex));write(field,raw(p.distance.fieldRawHex));write(count,raw(p.distance.expansionRawHex));return 0;},
 bc_DistanceField_Get:(ptr,x,y,out)=>{index(x,y);need(read(ptr,fieldBytes).equals(raw(p.distance.fieldRawHex)),'Field changed');dv().setUint16(out,raw(p.distance.fieldLinearHex).readUInt16LE(2*(y*p.width+x)),true);return 0;}
};
for(const [n,fn]of Object.entries(env))env[n]=(...args)=>{calls[n]=(calls[n]??0)+1;return fn(...args);};
const module=await WebAssembly.compile(fs.readFileSync(wasmPath));
const unusedImports=[];
for(const item of WebAssembly.Module.imports(module)) {
 need(item.module==='wasi_snapshot_preview1'||item.module==='env'&&item.kind==='function',`Unsupported import ${item.module}.${item.name}`);
 if(item.module==='env'&&!env[item.name]) {
  unusedImports.push(item.name);
  env[item.name]=()=>{throw Error(`Unexpected SDK call ${item.name}`);};
 }
}
const wasi=new WASI({version:'preview1',args:[wasmPath],env:{},preopens:{},returnOnExit:true});
instance=await WebAssembly.instantiate(module,{wasi_snapshot_preview1:wasi.wasiImport,env});
const code=wasi.start(instance);need(code===0,`Fixture exit ${code}`);
console.error(JSON.stringify({schema:'matchmaker-host-audit/v1',publicSha256:publicSha,calls,unusedImports,fuelModel:1000000,draftMemoryModel:'fresh zero bytes; production publication only',mapSymmetryModel:'VERTICAL from pinned immutable map export; not used by matrix producer',positionEnumeration:'row-major; producer uses order-independent extrema/union, native group/member ordering preserved exactly',fieldInputMatched:calls.bc_DistanceField_ComputeWithExpansionCount===1,privateReads:0,nativeCalls:0}));
