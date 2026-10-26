import fs from 'node:fs';
import {WASI} from 'node:wasi';

// Executes the compiled, unchanged C++ adapter. This file translates SDK ABI
// records/pointers only; it contains no claim policy or invented game state.
export async function createCompiledAdapter({e,game,owner=9,team=1,wasmPath,onAction}){
  let guest,mirror=0,mirrorSize=0,lastError='';
  const owned=[],counts={},denials=[],missing=[],logs=[],diagnostics={rosters:[],champions:[]};
  const dv=()=>new DataView(e.memory.buffer),gv=()=>new DataView(guest.exports.memory.buffer);
  const allocate=n=>{const p=e.Engine_Malloc(n+15);if(!p)throw Error('bridge native allocation');owned.push(p);return(p+15)&~15;};
  const meta=allocate(32),scratch=allocate(8192),perspective=allocate(8),entity=allocate(8),position=allocate(8);
  dv().setUint32(perspective,team,true);dv().setUint32(perspective+4,owner,true);
  function native(name,args){if(name.includes('Sandbox'))throw Error('Sandbox forbidden');if(typeof e[name]!=='function'){missing.push(name);throw Error('Missing engine export '+name);}const code=e[name](...args);if(code){const b=new Uint8Array(e.memory.buffer);let q=e.GetLastErrorMsg();let end=q;while(b[end])end++;lastError=new TextDecoder().decode(b.subarray(q,end));denials.push({name,code,message:lastError});}return code;}
  if(native('Game_GetBitboardMetadata',[game,meta]))throw Error('metadata query failed');
  if(native('Bitboard_GetRawBufferSize',[meta,scratch]))throw Error('board bytes query failed');
  const boardBytes=dv().getUint32(scratch,true);
  if(native('DistanceField_GetRawBufferSize',[meta,scratch]))throw Error('field bytes query failed');
  const fieldBytes=dv().getUint32(scratch,true);
  function ensureMirror(){const size=guest.exports.memory.buffer.byteLength;if(size>mirrorSize){mirror=allocate(Math.max(size,16*1024*1024));mirrorSize=Math.max(size,16*1024*1024);}}
  function invoke(name,args,buffers=[]){
    ensureMirror();const translated=[...args];
    for(const[index,size]of buffers){const p=args[index];if(p){if(p+size>guest.exports.memory.buffer.byteLength)throw Error('guest buffer bounds');new Uint8Array(e.memory.buffer,mirror+p,size).set(new Uint8Array(guest.exports.memory.buffer,p,size));translated[index]=mirror+p;}}
    const code=native(name,translated);
    for(const[index,size]of buffers){const p=args[index];if(p)new Uint8Array(guest.exports.memory.buffer,p,size).set(new Uint8Array(e.memory.buffer,mirror+p,size));}
    return code;
  }

  function checkedOutput(name,args,index,size,inputs=[]){
    ensureMirror();const translated=[...args],guestOutput=args[index];
    if(!guestOutput||size<0||guestOutput+size>guest.exports.memory.buffer.byteLength)throw Error('checked output bounds '+name);
    for(const[i,n]of inputs){const p=args[i];if(!p||p+n>guest.exports.memory.buffer.byteLength)throw Error('checked input bounds '+name);
      if(p<guestOutput+size&&guestOutput<p+n)throw Error('checked output aliases input '+name);
      new Uint8Array(e.memory.buffer,mirror+p,n).set(new Uint8Array(guest.exports.memory.buffer,p,n));translated[i]=mirror+p;
    }
    const guard=allocate(size+32),output=guard+16;
    new Uint8Array(e.memory.buffer,guard,size+32).fill(0xa5);translated[index]=output;
    const code=native(name,translated);
    if(new Uint8Array(e.memory.buffer,guard,16).some(x=>x!==0xa5)||new Uint8Array(e.memory.buffer,output+size,16).some(x=>x!==0xa5))throw Error('native output canary '+name);
    diagnostics.outputCanaries??={};diagnostics.outputCanaries[name]=(diagnostics.outputCanaries[name]??0)+1;
    if(code===0)new Uint8Array(guest.exports.memory.buffer,guestOutput,size).set(new Uint8Array(e.memory.buffer,output,size));
    return code;
  }
  function publicRead(name,args,size){const p=allocate(Math.max(size,4));if(native(name,[...args,p]))throw Error('public query '+name);return p;}
  function publicU32(name,args){return dv().getUint32(publicRead(name,args,4),true);}
  let campLayout;
  function mapCampLayout(buffer,capacity,count){
    if(!campLayout){campLayout=[];const n=publicU32('Game_API_Map_GetNumCampGroups',[game]),ids=allocate(n*4),np=allocate(4);if(native('Game_API_Map_GetCampGroups',[game,ids,n,np]))throw Error('group list');
      for(let i=0;i<n;i++){const id=dv().getUint32(ids+i*4,true),reward=publicRead('Game_API_Map_CampGroup_Get',[game,id],8),gems=dv().getUint32(reward,true),gold=dv().getUint32(reward+4,true),respawn=publicU32('Game_API_Map_CampGroup_GetRespawnTime',[game,id]),nc=publicU32('Game_API_Map_CampGroup_GetNumCamps',[game,id]),camps=allocate(nc*4);if(native('Game_API_Map_CampGroup_GetCampInfo',[game,id,camps,nc,np]))throw Error('member list');campLayout.push(id,gold,gems,respawn,nc);
        for(let j=0;j<nc;j++){const c=dv().getUint32(camps+j*4,true),info=publicRead('Game_API_Map_CampInfo_Get',[game,c],12);campLayout.push(c,dv().getInt32(info+8,true),dv().getInt32(info,true),dv().getInt32(info+4,true));}}
    }
    gv().setUint32(count,campLayout.length,true);for(let i=0;i<Math.min(capacity,campLayout.length);i++)gv().setUint32(buffer+i*4,campLayout[i],true);return 0;
  }
  const env={
    game_settings:p=>invoke('Game_API_GetSettings',[p],[[0,120]]),
    shared_memory_size:p=>invoke('Game_API_GetSharedMemorySize',[game,p],[[1,4]]),
    draft_memory_size:p=>invoke('Game_API_GetDraftMemorySize',[game,p],[[1,4]]),
    read_draft_memory:(offset,p,n)=>invoke('Game_API_ReadDraftMemory',[game,team,offset,p,n],[[3,n]]),
    bc_metadata:p=>{new Uint8Array(guest.exports.memory.buffer,p,20).set(new Uint8Array(e.memory.buffer,meta,20));gv().setUint32(p+20,boardBytes,true);gv().setUint32(p+24,fieldBytes,true);return 0;},
    consumable_info:(id,p)=>invoke('Game_API_GetConsumableInfo',[id,p],[[1,8]]),
    champion_design:(type,p)=>invoke('Game_API_Champion_Design',[game,type,p],[[2,128]]),
    my_champion:()=>owner,my_team:()=>team,
    bc_map_symmetry:p=>invoke('Game_API_Map_GetSymmetry',[game,p],[[1,4]]),
    bc_map_mask:(kind,p)=>{const name=['Game_GetWallBuffer','Game_GetBushBuffer','Game_GetHoleBuffer','Game_GetAltarBuffer'][kind];if(!name)throw Error('map mask kind');const code=native(name,[game,scratch]);if(!code)new Uint8Array(guest.exports.memory.buffer,p,boardBytes).set(new Uint8Array(e.memory.buffer,dv().getUint32(scratch,true),boardBytes));return code;},
    spawn_region:(side,p)=>invoke('Game_API_GetSpawnRegion',[game,side,p],[[2,16]]),
    fuel_remaining:()=>1000000000000n,
    bc_round_number:p=>invoke('Game_API_GetRoundNumber',[game,p],[[1,4]]),
    champion_identity:(id,p)=>invoke('Game_API_Champion_GetIdentity',[game,perspective,id,p],[[3,8]]),
    champion:(id,p)=>invoke('Game_API_Champion_Get',[game,perspective,id,p],[[3,44]]),
    champion_ability_props:(id,slot,p)=>invoke('Game_API_Champion_GetAbilityProps',[game,perspective,id,slot,p],[[4,36]]),
    champion_ability_damage:(id,slot,p)=>checkedOutput('Game_API_Champion_GetAbilityDamage',[game,perspective,id,slot,p],4,4),
    champion_ability_shield:(id,slot,p)=>checkedOutput('Game_API_Champion_GetAbilityShield',[game,perspective,id,slot,p],4,4),
    bc_champion_priority:(id,next,p)=>checkedOutput('Game_API_Champion_GetPriority',[game,perspective,id,next,p],4,4),
    champion_restrictions:(id,p)=>invoke('Game_API_Champion_GetRestrictions',[game,perspective,id,p],[[3,1]]),
    champion_is_alive:(id,p)=>invoke('Game_API_Champion_IsAlive',[game,id,p],[[2,4]]),
    // Added for the actual Brain::retreat/Ctx/nav dependencies; exact SDK ABI.
    has_champion:(x,y,p)=>invoke('Game_API_HasChampion',[game,perspective,x,y,p],[[4,4]]),
    has_camp:(x,y,p)=>invoke('Game_API_HasCamp',[game,perspective,x,y,p],[[4,4]]),
    is_visible:(x,y,p)=>invoke('Game_API_InVision',[game,perspective,x,y,p],[[4,4]]),
    is_occupied_by_teleport:(x,y,p)=>invoke('Game_API_IsOccupiedByTeleport',[game,perspective,x,y,p],[[4,4]]),
    board_asleep_camp_occupancy:p=>invoke('Game_API_GetAsleepCampOccupancy',[game,perspective,p],[[2,boardBytes]]),
    champion_items:(id,p)=>checkedOutput('Game_API_Champion_GetItems',[game,perspective,id,p,42],3,168),
    item_info:(id,p)=>checkedOutput('Game_API_GetItemInfo',[id,p],1,40),
    ward_at:(x,y,p)=>{let c=invoke('Game_API_GetAllyWard',[game,perspective,x,y,p],[[4,4]]);
      if(c===97||c===98)c=invoke('Game_API_GetEnemyWard',[game,perspective,x,y,p],[[4,4]]);return c;},
    champion_gems:(id,p)=>invoke('Game_API_Champion_GetGems',[game,perspective,id,p],[[3,4]]),
    champion_gold:(id,p)=>invoke('Game_API_Champion_GetGold',[game,perspective,id,p],[[3,4]]),
    champion_cooldown:(id,slot,p)=>invoke('Game_API_Champion_GetCooldown',[game,perspective,id,slot,p],[[4,4]]),
    bc_champion_channel_time:(id,p)=>invoke('Game_API_Champion_GetChannelTime',[game,perspective,id,p],[[3,4]]),
    bc_champion_vision:(id,p)=>{if(id!==owner)throw Error('Unhandled non-own vision request');return invoke('Game_API_GetVision',[game,perspective,p],[[2,boardBytes]]);},
    bc_submitted_gems:(side,p)=>invoke('Game_API_GetSubmittedGemCount',[game,side,p],[[2,4]]),
    board_vision:p=>invoke('Game_API_GetVision',[game,perspective,p],[[2,boardBytes]]),
    board_camp_occupancy:p=>invoke('Game_API_GetCampOccupancy',[game,perspective,p],[[2,boardBytes]]),
    board_awake_camp_occupancy:p=>invoke('Game_API_GetAwakeCampOccupancy',[game,perspective,p],[[2,boardBytes]]),
    board_teleport_occupancy:p=>invoke('Game_API_GetTeleportOccupancy',[game,perspective,p],[[2,boardBytes]]),
    board_ally_occupancy:p=>invoke('Game_API_GetAllyOccupancy',[game,perspective,p],[[2,boardBytes]]),
    board_enemy_occupancy:p=>invoke('Game_API_GetEnemyOccupancy',[game,perspective,p],[[2,boardBytes]]),
    camp_group_state:(id,p)=>invoke('Game_API_CampGroup_GetState',[game,perspective,id,p],[[3,4]]),
    camp_group_cooldown:(id,p)=>invoke('Game_API_CampGroup_GetCooldown',[game,perspective,id,p],[[3,4]]),
    camp:(id,p)=>invoke('Game_API_Camp_Get',[game,perspective,id,p],[[3,32]]),
    camp_at:(x,y,p)=>invoke('Game_API_GetCamp',[game,perspective,x,y,p],[[4,4]]),
    map_camp_info:(id,p)=>invoke('Game_API_Map_CampInfo_Get',[game,id,p],[[2,12]]),
    map_camp_group_of:(id,p)=>invoke('Game_API_Map_CampInfo_GetGroup',[game,id,p],[[2,4]]),
    camp_design:(type,p)=>invoke('Game_API_Camp_Design',[game,type,p],[[2,124]]),
    map_camp_layout:mapCampLayout,
    camp_buffs:(id,p,n,count)=>invoke('Game_API_Camp_GetBuffs',[game,perspective,id,p,n,count],[[3,n*12],[5,4]]),
    champion_buff_data:(id,p,n,count)=>invoke('Game_API_Champion_GetBuffData',[game,perspective,id,p,n,count],[[3,n*12],[5,4]]),
    altar_locks:(p,n,count)=>invoke('Game_API_GetAltarLocks',[game,perspective,p,n,count],[[2,n*24],[4,4]]),
    num_allies:p=>invoke('Game_API_Champion_GetNumAllies',[game,perspective,p],[[2,4]]),
    num_enemies:p=>invoke('Game_API_Champion_GetNumEnemies',[game,perspective,p],[[2,4]]),
    allies:(p,n,count)=>invoke('Game_API_Champion_GetAllies',[game,perspective,p,n,count],[[2,n*4],[4,4]]),
    enemies:(p,n,count)=>invoke('Game_API_Champion_GetEnemies',[game,perspective,p,n,count],[[2,n*4],[4,4]]),
    bc_get_last_error:(p,n)=>{const bytes=new TextEncoder().encode(lastError);if(p&&n)new Uint8Array(guest.exports.memory.buffer,p,Math.min(n,bytes.length)).set(bytes.subarray(0,n));return bytes.length;},
  };
  function action(command,name,args,buffers=[]){const result=onAction(command,()=>{const code=invoke(name,args,buffers);return{code,applied:code===0&&(!buffers.length||gv().getInt32(args[buffers[0][0]],true)!==0)};});return result.code;}
  function target(kind,id){dv().setUint32(entity,kind,true);dv().setUint32(entity+4,id,true);return entity;}
  function pos(x,y){dv().setInt32(position,x,true);dv().setInt32(position+4,y,true);return position;}
  env.attack=(kind,id)=>action({type:'attack',target:{kind:({1:'champion',2:'camp',6:'lock'})[kind]??kind,id}},'Game_Action_Attack',[game,target(kind,id)]);
  env.cast_with_result=(slot,kind,id,x,y,p)=>action({type:'cast',slot,target:{kind:({0:'none',1:'champion',2:'camp'})[kind]??kind,id},to:[x,y]},'Game_Action_Cast',[game,slot,target(kind,id),pos(x,y),p],[[4,4]]);
  env.move_to_with_result=(x,y,p)=>action({type:'move',to:[x,y]},'Game_Action_Move',[game,x,y,p],[[3,4]]);
  env.submit_gems=(x,y)=>action({type:'tribute',altar:[x,y]},'Game_Action_SubmitGems',[game,pos(x,y)]);
  env.cancel_channel=()=>action({type:'cancel'},'Game_Action_CancelChannel',[game]);
  env.recall=()=>action({type:'recall'},'Game_Action_Recall',[game]);
  for(const[name,spec]of Object.entries({
    bc_Bitboard_Zero:a=>[[0,boardBytes]],bc_Bitboard_Set:a=>[[0,boardBytes]],
    bc_Bitboard_Test:a=>[[0,boardBytes],[3,4]],bc_Bitboard_NumSetBits:a=>[[0,boardBytes],[1,4]],
    bc_Bitboard_GetSetBits:a=>[[0,boardBytes],[1,a[2]*8],[3,4]],
    bc_Bitboard_Union:a=>[[0,boardBytes],[1,boardBytes],[2,boardBytes]],
    bc_Bitboard_Intersect:a=>[[0,boardBytes],[1,boardBytes],[2,boardBytes]],
    bc_Bitboard_Not:a=>[[0,boardBytes],[1,boardBytes]],
    bc_Bitboard_DrawChebyshev:a=>[[0,boardBytes]],
    bc_Bitboard_ReachableMask:a=>[[0,boardBytes],[3,boardBytes]],
    bc_Bitboard_ImportRawBuffer:a=>[[0,boardBytes],[1,a[2]]],
    bc_Bitboard_ImportLinearBuffer:a=>[[0,boardBytes],[1,a[2]]],
    bc_DistanceField_Get:a=>[[0,fieldBytes],[3,2]],
    bc_DistanceField_ComputeWithExpansionCount:a=>[[0,boardBytes],[1,boardBytes],[2,boardBytes],[3,fieldBytes],[5,2]],
  }))env[name]=(...args)=>invoke(name.slice(3),[meta,...args],spec(args).map(([i,n])=>[i+1,n]));
  env.bc_Bitboard_ExportLinearBuffer=(buffer,p,size)=>checkedOutput('Bitboard_ExportLinearBuffer',[meta,buffer,p,size],2,size,[[1,boardBytes]]);
  env.bc_DistanceField_ExportLinearBuffer=(buffer,p,size)=>checkedOutput('DistanceField_ExportLinearBuffer',[meta,buffer,p,size],2,size,[[1,fieldBytes]]);
  const bytes=fs.readFileSync(wasmPath),module=await WebAssembly.compile(bytes);
  for(const imp of WebAssembly.Module.imports(module))if(imp.module==='env'){
    const fn=env[imp.name];env[imp.name]=(...args)=>{counts[imp.name]=(counts[imp.name]??0)+1;if(!fn){missing.push(imp.name);throw Error('Unhandled SDK import '+imp.name);}const code=fn(...args);if(['allies','enemies'].includes(imp.name)&&code===0){const n=gv().getUint32(args[2],true);diagnostics.rosters.push({kind:imp.name,count:n,ids:Array.from({length:Math.min(args[1],n)},(_,i)=>gv().getUint32(args[0]+i*4,true))});}if(imp.name==='champion'&&diagnostics.champions.length<20){const p=args[1];diagnostics.champions.push({id:args[0],code,...(code===0?{x:gv().getInt32(p+8,true),y:gv().getInt32(p+12,true),state:gv().getUint32(p+40,true)}:{})});}return code;};
  }
  const wasi=new WASI({version:'preview1',args:[],env:{},preopens:{},returnOnExit:true});
  const wasiImports={...wasi.wasiImport,fd_write:(fd,iovs,n,out)=>{let size=0;const d=gv();for(let i=0;i<n;i++){const p=d.getUint32(iovs+i*8,true),len=d.getUint32(iovs+i*8+4,true);logs.push(new TextDecoder().decode(new Uint8Array(guest.exports.memory.buffer,p,len)));size+=len;}d.setUint32(out,size,true);return 0;}};
  try{guest=await WebAssembly.instantiate(module,{env,wasi_snapshot_preview1:wasiImports});wasi.initialize(guest);}
  catch(error){for(const p of owned)e.Engine_Free(p);throw error;}
  return{warm(){return guest.exports.fixture_warm();},turn(plan,applied){return guest.exports.fixture_turn(plan,applied?1:0);},flush(){if(guest.exports.fixture_flush)guest.exports.fixture_flush();},
    state(){return structuredClone({counts,denials,missing,diagnostics,logs:logs.join(''),fuel:'unmetered sentinel1000000000000; no100K claim'});},
    destroy(){for(const p of owned)e.Engine_Free(p);}};
}
