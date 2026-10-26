"""Whole production Bot + current SDK import spy; no engine, games or release build.

Run with arena Python, --out results/population/breakpoint-caller/ATTEMPT.
The host supplies explicit personal facts; it does not reconstruct a native journey.
"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
import argparse
import hashlib
import json
import math
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT/'tools/arena'))
import core
import wasi

WRAPPER = r'''
#include <optional>
#include <cstring>
#define main fixture_original_main
#include "repo/bots/specialists/breakpoint-brigade/src/main.cpp"
#undef main
#include "catalogue.h"
namespace { std::optional<Bot> fixture_bot; std::string fixture_error; int shop_result=-2; }
#define EX(name) extern "C" __attribute__((export_name(#name)))
EX(fixture_call) int fixture_call(int mode) {
 try {
  fixture_error.clear();shop_result=-2;
  if(!fixture_bot)fixture_bot.emplace();
  if(mode==3)fixture_bot->turn();
  else {
   if(!fixture_bot->initialized_)fixture_bot->initialize();
   if(mode==1){fixture_bot->refresh_equipment();shop_result=fixture_bot->breakpoint_shop();}
   else if(mode==2)fixture_bot->shop();
   else return -2;
  }
  return 0;
 }catch(const cambc::error& e){fixture_error=e.what();return static_cast<int>(e.code());}
 catch(const std::exception& e){fixture_error=e.what();return -1;}
}
EX(fixture_error_text) const char* fixture_error_text(){return fixture_error.c_str();}
EX(fixture_flush) void fixture_flush(){std::cout.flush();}
EX(fixture_value) double fixture_value(unsigned i){
 if(!fixture_bot)return -999;
 switch(i){
 case 0:return fixture_bot->initialized_;case 1:return fixture_bot->target_;
 case 2:return fixture_bot->purchases_;case 3:return fixture_bot->breakpoint_purchases_;
 case 4:return fixture_bot->reserved_gold_turns_;case 5:return fixture_bot->reserved_gold_total_;
 case 6:return fixture_bot->prediction_after_;case 7:return fixture_bot->prediction_confounded_;
 case 8:return fixture_bot->camp_multiplier_;case 9:return fixture_bot->incoming_multiplier_;
 case 10:return fixture_bot->monster_shards_;case 11:return fixture_bot->jungle_shards_;
 case 12:return fixture_bot->groups_.size();case 13:return shop_result;case 14:return fixture_bot->dropped_;
 default:return -998;
 }
}
EX(fixture_size) unsigned fixture_size(unsigned kind){
 switch(kind){case 0:return sizeof(cambc::detail::metadata);case 1:return sizeof(cambc::detail::game_settings);
 case 2:return sizeof(cambc::detail::champion_design);case 3:return sizeof(cambc::detail::item_info);
 case 4:return sizeof(cambc::detail::champion_info);case 5:return sizeof(cambc::detail::camp_design);
 case 6:return sizeof(cambc::detail::camp_state);default:return 0;}
}
EX(fixture_fill) void fixture_fill(unsigned kind,unsigned id,void* out){
 if(kind==0){*static_cast<cambc::detail::metadata*>(out)=fixture_metadata;return;}
 if(kind==1){fixture_settings(*static_cast<cambc::detail::game_settings*>(out));return;}
 if(kind==3){*static_cast<cambc::detail::item_info*>(out)=fixture_items[id];return;}
}
EX(fixture_hits) unsigned fixture_hits(float a,float b,float c,float damage){return breakpoint::hits({a,b,c},damage);}
'''

HOST = r'''
import fs from 'node:fs';
import assert from 'node:assert/strict';
import {WASI} from 'node:wasi';
const [inputPath,reportPath]=process.argv.slice(2),I=JSON.parse(fs.readFileSync(inputPath,'utf8'));
const M=I.map,C=I.catalogue,items=C.items,designs=new Map(C.champions.map(x=>[x.id,x])),enemyDesign=C.champions.find(x=>x.name==='varg'),camps=new Map();
for(const g of M.groups)for(const m of g.members)camps.set(m.nativeId,{...m,group:g.nativeId});
const group=M.groups.find(g=>g.key==='g_113'),W=M.width,H=M.height;
const stride=2**Math.ceil(Math.log2(Math.ceil(W/8))),BB=stride*Math.ceil(H/8)*8;
const bit=(x,y)=>BigInt((Math.floor(y/8)*stride+Math.floor(x/8))*64+(y%8)*8+x%8);
const toBoard=points=>points.reduce((v,p)=>{const q=typeof p==='number'?{x:p%W,y:Math.floor(p/W)}:p;return v|(1n<<bit(q.x,q.y));},0n);
const allMask=toBoard(Array.from({length:W*H},(_,i)=>({x:i%W,y:Math.floor(i/W)})));
const records=[];
async function guest(path,configuration){
 let instance;
 const s={gold:150,items:Array(42).fill(0),round:16,hp:899,alive:true,canShop:true,fuel:100000,
  position:{x:2,y:2},enemyPosition:{x:0,y:3},hps:[120,120,120],hiddenMember:-1,enemy:false,advanceDuringCamp:false,
  buyError:false,trace:[],buys:[],logs:[],phase:'initialization',...structuredClone(configuration)};
 const memory=()=>instance.exports.memory,V=()=>new DataView(memory().buffer);
 const checked=(p,n)=>{assert(Number.isSafeInteger(p)&&p>=0&&p+n<=memory().buffer.byteLength);return new Uint8Array(memory().buffer,p,n);};
 const u32=(p,n)=>{checked(p,4);V().setUint32(p,n,true);};
 const f32=(p,n)=>{checked(p,4);V().setFloat32(p,Math.fround(n),true);};
 const fill=(kind,id,p)=>{checked(p,instance.exports.fixture_size(kind));instance.exports.fixture_fill(kind,id,p);return 0;};
 const raw=(hex,kind,p)=>{const b=Buffer.from(hex,'hex');assert.equal(b.length,instance.exports.fixture_size(kind));checked(p,b.length).set(b);return 0;};
 const readBoard=p=>{let v=0n;checked(p,BB);for(let i=BB-1;i>=0;--i)v=(v<<8n)|BigInt(checked(p+i,1)[0]);return v;};
 const board=(p,value=0n)=>{checked(p,BB).fill(0);for(let i=0;i<BB;++i){checked(p+i,1)[0]=Number(value&255n);value>>=8n;}return 0;};
 const coordinates=value=>{const out=[];for(let y=0;y<H;++y)for(let x=0;x<W;++x)if(value&(1n<<bit(x,y)))out.push({x,y});return out;};
 const visibleMembers=()=>group.members.filter((m,i)=>i!==s.hiddenMember);
 const visible=()=>toBoard([s.position,...visibleMembers(),...(s.enemy?[s.enemyPosition]:[])]);
 const stats=()=>{
  const base=designs.get(11).stats,points={health:0,strength:0,focus:0};
  items.forEach((m,i)=>{for(const key of Object.keys(points))points[key]+=s.items[i]*m[key+'_points'];});
  return {strength:Math.fround(base.base_strength+Math.fround(points.strength*base.strength_growth)),
   focus:Math.fround(base.base_focus+Math.fround(points.focus*base.focus_growth)),
   maxHealth:Math.fround(base.base_health+Math.fround(points.health*base.health_growth))};
 };
 const champion=(id,p)=>{
  assert(id===0||(id===1&&s.enemy));checked(p,44).fill(0);const self=id===0,q=self?s.position:s.enemyPosition,st=stats(),e=enemyDesign.stats;
  [self?0:1,self?11:enemyDesign.id,q.x,q.y].forEach((v,i)=>u32(p+i*4,v));
  [self?(s.alive?s.hp:0):e.base_health,self?st.maxHealth:e.base_health,self?st.strength:e.base_strength,self?st.focus:e.base_focus].forEach((v,i)=>f32(p+16+i*4,v));
  u32(p+32,1);u32(p+36,3);u32(p+40,s.alive?1|8|16|2048:0);return 0;
 };
 const env={
  game_settings:p=>fill(1,0,p),bc_metadata:p=>fill(0,0,p),bc_map_symmetry:p=>{u32(p,0);return 0;},
  consumable_info:(id,p)=>{u32(p,10);u32(p+4,1);return 0;},
  shared_memory_size:p=>{u32(p,0);return 0;},draft_memory_size:p=>{u32(p,0);return 0;},my_champion:()=>0,my_team:()=>0,
  champion_design:(id,p)=>raw(designs.get(id).rawHex,2,p),item_info:(id,p)=>fill(3,id,p),
  champion_identity:(id,p)=>{assert(id===0||(id===1&&s.enemy));u32(p,id===0?0:1);u32(p+4,id===0?11:enemyDesign.id);return 0;},
  champion_ability_props:(id,slot,p)=>{assert(id===0||(id===1&&s.enemy));const b=Buffer.from((id===0?designs.get(11):enemyDesign).rawHex,'hex');checked(p,36).set(b.subarray(12+slot*36,48+slot*36));return 0;},
  champion_items:(id,p)=>{assert.equal(id,0);s.items.forEach((v,i)=>u32(p+i*4,v));return 0;},
  champion_gold:(id,p)=>{assert.equal(id,0);u32(p,s.gold);return 0;},champion_gems:(id,p)=>{assert.equal(id,0);u32(p,0);return 0;},
  champion_is_alive:(id,p)=>{u32(p,id===0?+s.alive:1);return 0;},champion_can_shop:(id,consumable,p)=>{assert.equal(id,0);assert.equal(consumable,0);u32(p,+s.canShop);return 0;},champion,
  champion_at:(x,y,p)=>{const id=x===s.position.x&&y===s.position.y?0:s.enemy&&x===s.enemyPosition.x&&y===s.enemyPosition.y?1:-1;assert(id>=0);u32(p,id);return 0;},
  buy_item:(id,count)=>{
   const meta=items[id],before=stats(),entry={index:s.buys.length,item:id,name:meta.name,count,cost:meta.cost*count,gold_before:s.gold,items_before:[...s.items],stats_before:before};
   s.buys.push(entry);assert(meta.purchasable&&s.canShop&&count>0&&s.gold>=entry.cost);
   if(s.buyError){entry.error=52;return 52;}
   s.gold-=entry.cost;s.items[id]+=count;
   if(meta.combine_count)while(s.items[id]>=meta.combine_count){s.items[id]-=meta.combine_count;++s.items[meta.result];}
   const after=stats(),spawn=M.spawns.teamA;
   if(s.position.x>=spawn.minX&&s.position.x<spawn.maxX&&s.position.y>=spawn.minY&&s.position.y<spawn.maxY)s.hp=Math.min(after.maxHealth,Math.fround(s.hp+after.maxHealth-before.maxHealth));
   Object.assign(entry,{gold_after:s.gold,items_after:[...s.items],stats_after:after});return 0;
  },
  bc_round_number:p=>{u32(p,s.round);return 0;},bc_submitted_gems:(team,p)=>{u32(p,0);return 0;},fuel_remaining:()=>BigInt(s.fuel),
  bc_get_last_error:(p,n)=>{const b=Buffer.from('declared import-spy status');if(p&&n)checked(p,Math.min(n,b.length)).set(b.subarray(0,n));return b.length;},
  bc_map_mask:(kind,p)=>board(p,toBoard(M.terrain[['wall','bush','hole','altar'][kind]])),
  spawn_region:(team,p)=>{const q=M.spawns[team===0?'teamA':'teamB'];[q.minX,q.minY,q.maxX,q.maxY].forEach((v,i)=>u32(p+i*4,v));return 0;},
  map_camp_layout:(p,capacity,count)=>{const words=[];for(const g of M.groups){words.push(g.nativeId,g.goldValue,g.gemValue,g.cooldown,g.members.length);for(const m of g.members)words.push(m.nativeId,m.typeId,m.x,m.y);}u32(count,words.length);for(let i=0;i<Math.min(capacity,words.length);++i)u32(p+i*4,words[i]);return 0;},
  map_camp_info:(id,p)=>{const m=camps.get(id);assert(m);[m.x,m.y,m.typeId].forEach((v,i)=>u32(p+i*4,v));return 0;},map_camp_group_of:(id,p)=>{u32(p,camps.get(id).group);return 0;},
  camp_design:(id,p)=>raw(C.camps.find(x=>x.id===id).rawHex,5,p),
  camp_at:(x,y,p)=>{const m=visibleMembers().find(m=>m.x===x&&m.y===y);assert(m);u32(p,m.nativeId);return 0;},
  camp_state:(id,p)=>{assert(visibleMembers().some(m=>m.nativeId===id));u32(p,2);return 0;},
  camp:(id,p)=>{const m=camps.get(id),index=group.members.findIndex(x=>x.nativeId===id);assert(index>=0&&index!==s.hiddenMember);const meta=C.camps.find(x=>x.id===m.typeId).stats;u32(p,m.x);u32(p+4,m.y);[s.hps[index],meta.base_health,meta.base_strength,meta.base_focus].forEach((v,i)=>f32(p+8+i*4,v));u32(p+24,0);u32(p+28,0);if(s.advanceDuringCamp)s.round=17;return 0;},
  board_vision:p=>board(p,visible()),board_ally_occupancy:p=>board(p,toBoard([s.position])),board_enemy_occupancy:p=>board(p,s.enemy?toBoard([s.enemyPosition]):0n),board_camp_occupancy:p=>board(p,toBoard(visibleMembers())),
  bc_Bitboard_Zero:p=>board(p),bc_Bitboard_One:p=>board(p,allMask),
  bc_Bitboard_ImportRawBuffer:(p,src,n)=>{assert.equal(n,BB);checked(p,n).set(Buffer.from(checked(src,n)));return 0;},
  bc_Bitboard_Copy:(a,b)=>board(b,readBoard(a)),bc_Bitboard_Union:(a,b,p)=>board(p,readBoard(a)|readBoard(b)),bc_Bitboard_Intersect:(a,b,p)=>board(p,readBoard(a)&readBoard(b)),bc_Bitboard_Not:(a,p)=>board(p,allMask^readBoard(a)),
  bc_Bitboard_Set:(p,x,y,value)=>board(p,value?readBoard(p)|(1n<<bit(x,y)):readBoard(p)&~(1n<<bit(x,y))),
  bc_Bitboard_Test:(p,x,y,out)=>{u32(out,Number((readBoard(p)>>bit(x,y))&1n));return 0;},bc_Bitboard_IsZero:(p,out)=>{u32(out,+(readBoard(p)===0n));return 0;},
  bc_Bitboard_NumSetBits:(p,out)=>{u32(out,coordinates(readBoard(p)).length);return 0;},
  bc_Bitboard_GetSetBits:(p,out,capacity,count)=>{const q=coordinates(readBoard(p));u32(count,q.length);q.slice(0,capacity).forEach((v,i)=>{u32(out+i*8,v.x);u32(out+i*8+4,v.y);});return 0;},
  bc_Bitboard_DrawChebyshev:(p,x,y,r)=>{let v=readBoard(p);for(let yy=Math.max(0,y-r);yy<=Math.min(H-1,y+r);++yy)for(let xx=Math.max(0,x-r);xx<=Math.min(W-1,x+r);++xx)v|=1n<<bit(xx,yy);return board(p,v);},
 };
 const wasm=await WebAssembly.compile(fs.readFileSync(path)),wasi=new WASI({version:'preview1',args:[],env:{},preopens:{},returnOnExit:true});
 const imports={env:{},wasi_snapshot_preview1:{...wasi.wasiImport,fd_write:(fd,iovecs,count,written)=>{let total=0;for(let i=0;i<count;++i){const p=V().getUint32(iovecs+i*8,true),n=V().getUint32(iovecs+i*8+4,true);s.logs.push(Buffer.from(checked(p,n)).toString('utf8'));total+=n;}u32(written,total);return 0;}}};
 for(const x of WebAssembly.Module.imports(wasm)){
  if(x.module==='env')imports.env[x.name]=(...args)=>{s.trace.push({name:x.name,args,phase:s.phase,round:s.round,gold:s.gold});const f=env[x.name];if(!f)throw Error('unsupported reached import '+x.name+' '+args);return f(...args);};
  else assert.equal(x.module,'wasi_snapshot_preview1');
 }
 instance=await WebAssembly.instantiate(wasm,imports);wasi.initialize(instance);
 return {s,call:mode=>{s.phase='production-caller';const code=instance.exports.fixture_call(mode);s.phase='inspection';instance.exports.fixture_flush();const p=instance.exports.fixture_error_text();let n=0;while(checked(p+n,1)[0])++n;const error=Buffer.from(checked(p,n)).toString('utf8');const values=Array.from({length:15},(_,i)=>instance.exports.fixture_value(i));const events=s.logs.join('').split('\n').filter(x=>x.startsWith('BC26T')).map(x=>JSON.parse(x.slice(5)));return {code,error,values,gold:s.gold,items:s.items,stats:stats(),hp:s.hp,buys:s.buys,events,logs:s.logs,trace:s.trace,round:s.round};}};
}
let failure;
try{
 for(const scenario of I.cases){
  const results={};for(const arm of ['active','control']){const g=await guest(I.arms.find(x=>x.arm===arm).wasm,scenario.inputs);results[arm]=g.call(scenario.mode);assert.equal(results[arm].code,0,scenario.name+' '+arm+' '+results[arm].error);assert.equal(results[arm].values[12],M.groups.length);assert.equal(results[arm].values[1],-1,'no forced or installed target');}
  records.push({name:scenario.name,mode:scenario.mode,inputs:scenario.inputs,results});
 }
}catch(error){failure={message:error.message,stack:error.stack};process.exitCode=1;}
const report={schema:1,kind:'whole actual Bot/current SDK import-spy caller',pass:!failure,cases:records,failure,
 limits:I.limits,nativeEngineCalls:0,newGames:0,releaseBuilds:0};
fs.writeFileSync(reportPath,JSON.stringify(report,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({pass:report.pass,cases:records.length,failure}));
'''

def digest(data):return hashlib.sha256(data).hexdigest()
def descriptor(p):return {'path':p.resolve().relative_to(ROOT).as_posix(),'sha256':digest(p.read_bytes())}
def write(p,value):
    assert not p.exists(),str(p)
    p.parent.mkdir(parents=True,exist_ok=True)
    p.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8',newline='\n')
def F(value):return struct.unpack('<f',struct.pack('<f',value))[0]
def repeated_hits(health,damage):
    if damage<=0:return 1000000
    result=0
    for hp in health:
        hp=F(hp)
        while hp>0:
            after=F(hp-damage)
            if after==hp or result>=1000000:return 1000000
            hp=after;result+=1
    return result

def equipment(inventory,catalogue):
    tavi=next(c for c in catalogue['champions'] if c['name']=='tavi')['stats']
    strength_points=sum(n*item['strength_points'] for n,item in zip(inventory,catalogue['items']))
    strength=F(tavi['base_strength']+F(strength_points*tavi['strength_growth']))
    monster=any(n and item['passive']==1 for n,item in zip(inventory,catalogue['items']))
    magnitude=next(item['passive_magnitude'] for item in catalogue['items'] if item['passive']==1)
    multiplier=F(F(tavi['camp_damage_multiplier'])*F(1+F(magnitude))) if monster else F(tavi['camp_damage_multiplier'])
    return strength,multiplier

def purchased(inventory,item,count,catalogue):
    result=list(inventory);meta=catalogue['items'][item];result[item]+=count
    while meta['combine_count'] and result[item]>=meta['combine_count']:
        result[item]-=meta['combine_count'];result[meta['result']]+=1
    return result

def choose(health,inventory,gold,catalogue,count_hits=repeated_hits):
    strength,multiplier=equipment(inventory,catalogue)
    before=count_hits(health,F(strength*multiplier));candidates=[]
    for family,item in enumerate([0,6]):
        meta=catalogue['items'][item]
        for count in range(1,min(5,meta['combine_count']-inventory[item])+1):
            cost=count*meta['cost']
            if cost>gold:continue
            future=purchased(inventory,item,count,catalogue)
            projected_strength,projected_multiplier=equipment(future,catalogue)
            after=count_hits(health,F(projected_strength*projected_multiplier))
            if after<before:candidates.append({'family':family,'item':item,'count':count,'cost':cost,'before':before,'after':after,
                'strength':projected_strength,'multiplier':projected_multiplier,'inventory':future})
    return min(candidates,key=lambda x:(x['cost'],x['after'],x['family'],x['count'])) if candidates else None

def validate_callers(raw,inputs):
    catalogue=inputs['catalogue'];records=[]
    selective={'full-current-clusters-turn-shop','actual-breakpoint-return-and-purchase',
        'Monster-combine-boundary-supplied-injury','Power-combine-boundary-supplied-injury','existing-Monster-passive-not-duplicated'}
    for case in raw['cases']:
        config=case['inputs'];health=config.get('hps',[120,120,120]);inventory=config.get('items',[0]*42);gold=config.get('gold',150)
        expected=choose(health,inventory,gold,catalogue)
        active,control=case['results']['active'],case['results']['control']
        if case['name'] in selective:
            assert expected is not None
            assert [(b['item'],b['count']) for b in active['buys']]==[(expected['item'],expected['count'])]
            assert active['gold']==gold-expected['cost'] and active['items']==expected['inventory']
            assert active['stats']['strength']==expected['strength'] and active['values'][8]==expected['multiplier']
            assert active['values'][6]==expected['after'] and active['values'][3:6]==[1,1,gold-expected['cost']]
            activations=[e['d'] for e in active['events'] if e['k']=='activation']
            assert len(activations)==1 and activations[0]['group']=='g_113'
            assert activations[0]['N_before']==expected['before'] and activations[0]['N_after']==expected['after']
            if case['mode']==1:
                assert active['values'][13]==control['values'][13]==1
                assert active['buys']==control['buys'],'Direct private call intentionally bypasses outer core_enabled guard.'
            else:
                assert control['values'][3]==0 and control['gold']==0
        else:
            assert active['values'][3]==0
            assert [(b['item'],b['count']) for b in active['buys']]==[(b['item'],b['count']) for b in control['buys']]
            assert active['gold']==control['gold'] and active['items']==control['items']
            if case['name']=='shop-capability-refusal':assert not active['buys'] and active['gold']==150
            else:assert active['buys'] and active['gold']==0,'Refusal preserves B0 fallback rather than inventing no-buy.'
        records.append({'case':case['name'],'pass':True,'independent_exact_bundle':expected if case['name'] in selective else None,
            'before_exact_basics':repeated_hits(health,F(equipment(inventory,catalogue)[0]*equipment(inventory,catalogue)[1])),
            'after_actual_active_exact_basics':repeated_hits(health,F(active['stats']['strength']*active['values'][8])),
            'after_actual_control_exact_basics':repeated_hits(health,F(control['stats']['strength']*control['values'][8])),
            'selected_group_by_actual_existing_telemetry':'g_113' if case['name'] in selective else None,
            'supplied_injury_not_native_observation':health!=[120,120,120]})
    return records

def rounding_survey(catalogue,map_record):
    cache={}
    def exact(health,damage):
        key=(tuple(health),damage)
        if key not in cache:cache[key]=repeated_hits(health,damage)
        return cache[key]
    def old(health,damage):
        return sum(max(1,math.ceil(F(F(F(hp)-F(.0001))/damage))) for hp in health if hp>F(.0001))
    groups=[]
    monolith_id=next(c['id'] for c in catalogue['camps'] if c['name']=='monolith')
    for group in map_record['groups']:
        if any(m['typeId']==monolith_id for m in group['members']):continue
        health=[F(next(c for c in catalogue['camps'] if c['id']==m['typeId'])['stats']['base_health']) for m in group['members']]
        groups.append((group['key'],health))
    comparisons=0;mismatches=[]
    for monster_emblems in range(3):
      for power_emblems in range(3):
       for monster_shards in range(5):
        for power_shards in range(5):
         inventory=[0]*42;inventory[0]=monster_shards;inventory[1]=monster_emblems;inventory[6]=power_shards;inventory[7]=power_emblems
         for key,health in groups:
            exact_choice=choose(health,inventory,150,catalogue,exact)
            approximate=choose(health,inventory,150,catalogue,old)
            signature=lambda c:None if c is None else (c['item'],c['count'],c['cost'])
            comparisons+=1
            if signature(exact_choice)!=signature(approximate):
                mismatches.append({'group':key,'full_current_member_HP':health,'inventory':inventory,
                    'exact_choice':exact_choice,'approximate_choice':approximate})
    return {'schema':1,'comparisons':comparisons,'legal_equipment_states':225,'eligible_full_HP_groups':len(groups),
        'full_HP_bundle_mismatches':mismatches,'oracle':'Independently repeated float32 subtraction, no epsilon. Equipment recomputed after actual ingredient consumption/result insertion, not production point_delta.',
        'bounds':'Tavi current base/growth,0..4each Monster/Power Strength ingredients,0..2each emblems,150gold, full current LostWorld memberHP. Legal catalogue combinations are not claimed autonomous trajectories.',
        'limits':'Offline candidate search, not production caller execution over every grid row. Supplied injured HP semantic caller cases are separately identified. No generic649/129.8 or unsupported Tavi damage is used.'}

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',required=True);args=ap.parse_args()
    out=(ROOT/args.out).resolve();assert out.is_relative_to(ROOT/'results/population/breakpoint-caller')
    build=ROOT/'.cache/arena/orchestration/breakpoint-caller'/out.name
    assert not out.exists() and not build.exists(),'Use a fresh frozen attempt directory.'
    out.mkdir(parents=True);build.mkdir(parents=True)
    bot=core.Bot('specialists','breakpoint-brigade');sources=core.read_closure(bot)
    assert core.version_of(sources)=='babb2b9cb23d' and len(sources)==7
    catalogue_path=ROOT/'docs/reference/runtime-catalog.json';catalogue_all=json.loads(catalogue_path.read_bytes())['result']
    catalogue={k:catalogue_all[k] for k in ['settings','items','camps','champions','passives']}
    geometry_path=ROOT/'maps/geometry.json';geometry=json.loads(geometry_path.read_bytes())
    geo=next(x for x in geometry['maps'] if x['id']=='00lost_world')
    selected={k:geo[k] for k in ['id','width','height','terrain','spawns','groups']}
    assert geo['mapSha256']==digest((ROOT/'maps/00lost-world.map26a').read_bytes())
    g=next(x for x in geo['groups'] if x['key']=='g_113')
    assert [(m['x'],m['y'],m['typeId']) for m in g['members']]==[(8,3,3),(9,3,3),(8,4,3)]
    assert catalogue['champions'][11]['name']=='tavi'
    header='#pragma once\ninline constexpr cambc::detail::item_info fixture_items[]={\n'
    fields=['cost','purchasable','health_points','strength_points','focus_points','passive','passive_magnitude','combine_count','result','combine_cost']
    for item in catalogue['items']:
        values=[(repr(item[k])+'f' if k=='passive_magnitude' and isinstance(item[k],float) else str(item[k])) for k in fields]
        header+=' {'+','.join(values)+'},\n'
    header+='};\ninline void fixture_settings(cambc::detail::game_settings& v){v={};\n'
    for key,value in catalogue['settings'].items():header+='v.'+key+'='+repr(value)+';\n'
    stride=1<<math.ceil(math.log2(math.ceil(geo['width']/8)))
    board_bytes=stride*math.ceil(geo['height']/8)*8
    header+='}\ninline constexpr cambc::detail::metadata fixture_metadata{'+','.join(map(str,[geo['width'],geo['height'],math.ceil(geo['width']/8),math.ceil(geo['height']/8),stride,board_bytes,board_bytes*16]))+'};\n'
    (build/'host.mjs').write_text(HOST,encoding='utf-8',newline='\n')
    body_records=[{'path':p.relative_to(ROOT).as_posix(),'sha256':digest(b),'utf8':b.decode('utf-8')} for p,b in sorted(sources.items())]
    for p in [Path(__file__),ROOT/'sdk/cpp/cambc.h',ROOT/'sdk/cpp/imports.h',catalogue_path,geometry_path]:
        body_records.append({'path':p.resolve().relative_to(ROOT).as_posix(),'sha256':digest(p.read_bytes()),'utf8':p.read_bytes().decode('utf-8')})
    write(out/'frozen-sources.json',{'schema':1,'source':'specialists/breakpoint-brigade@babb2b9cb23d','bodies':body_records})
    def build_arm(arm):
        base=build/arm;base.mkdir();applied=dict(sources)
        overrides={'core_enabled':'false'} if arm=='control' else {}
        if overrides:applied=core.apply_overrides(bot,applied,overrides)
        key=bot.path/'src/main.cpp';original=applied[key]
        assert original.count(b'class Bot {')==1
        public=original.replace(b'class Bot {',b'class Bot {\npublic:',1)
        assert public.replace(b'class Bot {\npublic:',b'class Bot {',1)==original
        for p,b in applied.items():
            target=base/'repo'/p.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(public if p==key else b)
        (base/'catalogue.h').write_text(header,encoding='utf-8',newline='\n');cpp=base/'fixture.cpp';cpp.write_text(WRAPPER,encoding='utf-8',newline='\n');wasm=base/'caller.wasm'
        flags=['-mexec-model=reactor','-Wno-error=sign-compare']
        command=[str(core.CLANG),*wasi.FLAGS,'-Werror',*flags,'-I'+str(ROOT/'sdk/cpp'),str(cpp),'-L'+str(wasi.EH),'-lunwind','-o',str(wasm)]
        with core.Slots(1,quiet=True):ok,log=wasi.compile_fixture(cpp,wasm,flags=flags)
        (base/'compile.log').write_text(log,encoding='utf-8',newline='\n')
        receipt={'arm':arm,'source_key':core.version_of(applied,overrides),'overrides':overrides,
            'source':{p.relative_to(ROOT).as_posix():digest(b) for p,b in applied.items()},
            'access_patch':'One class Bot public insertion; exact inverse; no target/state injection.',
            'compile_command':command,'compile_ok':ok,'compile_log':descriptor(base/'compile.log'),
            'wrapper':descriptor(cpp),'catalogue_header':descriptor(base/'catalogue.h')}
        if ok:receipt['wasm']=str(wasm);receipt['wasm_sha256']=digest(wasm.read_bytes())
        write(out/('build-'+arm+'.json'),receipt)
        return receipt
    with ThreadPoolExecutor(max_workers=2) as pool:arms=list(pool.map(build_arm,['active','control']))
    assert all(x['compile_ok'] for x in arms),'Preserved compile failure; no caller execution.'
    blocked=set(geo['terrain']['wall']+geo['terrain']['hole']+geo['terrain']['altar'])|{m['tile'] for x in geo['groups'] for m in x['members']}
    enemy_position=next({'x':x,'y':y} for y in range(geo['height']) for x in range(geo['width'])
        if max(abs(x-2),abs(y-2))<=8 and not(x<3 and y<3) and y*geo['width']+x not in blocked)
    cases=[{'name':'full-current-clusters-turn-shop','mode':3,'inputs':{}},
      {'name':'actual-breakpoint-return-and-purchase','mode':1,'inputs':{}},
      {'name':'fuel-refusal-falls-through-to-B0','mode':3,'inputs':{'fuel':44999}},
      {'name':'hidden-spawn-refusal-falls-through-to-B0','mode':3,'inputs':{'hiddenMember':2}},
      {'name':'fresh-round-refusal-falls-through-to-B0','mode':3,'inputs':{'advanceDuringCamp':True}},
      {'name':'near-enemy-refusal-falls-through-to-B0','mode':3,'inputs':{'enemy':True,'enemyPosition':enemy_position}},
      {'name':'shop-capability-refusal','mode':3,'inputs':{'canShop':False}},
      {'name':'Monster-combine-boundary-supplied-injury','mode':3,'inputs':{'items':[4]+[0]*41,'hps':[70,120,120]}},
      {'name':'Power-combine-boundary-supplied-injury','mode':3,'inputs':{'items':[0]*6+[4]+[0]*35,'hps':[75,120,120]}},
      {'name':'existing-Monster-passive-not-duplicated','mode':3,'inputs':{'items':[4,1]+[0]*40,'hps':[110,120,120]}},
      {'name':'dead-turn-keeps-B0-shopping','mode':3,'inputs':{'alive':False}}]
    limits=['Actual production whole Bot and SDK wrappers; supplied host world, equipment and purchase effects are not an official engine.',
      'Current item/settings/design records and full LostWorld public catalogue come from pinned retained JSON. Personal visibility, gold, readiness, HP, enemy absence and fuel are explicit conditional inputs.',
      'No target_ injection or selected-source override; real initialize/catalogue and real shop ranking run.',
      'Tavi starts injured on own spawn to observe real turn→shop and its subsequent injury return without inventing navigation; no historical joint trajectory is claimed.',
      'Supplied injured camp HP in semantic combine/passive cases is not an observed native injury or an autonomous opportunity.',
      'Host bitboard operations implement declared semantics over derived chunk buffers; fuel replies are not metered cost or resumable suspension.',
      'Existing telemetry is flushed only after the production caller returns, during inspection; no additional production logging is inserted.',
      'No full release build, game, native query, evaluator, admission or strength claim.']
    inputs={'schema':1,'arms':arms,'map':selected,'catalogue':catalogue,'cases':cases,'limits':limits,
      'pins':{'sources':descriptor(out/'frozen-sources.json'),'map':descriptor(ROOT/'maps/00lost-world.map26a'),
              'catalogue':descriptor(catalogue_path),'geometry':descriptor(geometry_path),'host':descriptor(build/'host.mjs')}}
    write(out/'caller-inputs.json',inputs)
    command=[str(core.NODE),'--disable-warning=ExperimentalWarning',str(build/'host.mjs'),str(out/'caller-inputs.json'),str(out/'caller-raw.json')]
    with core.Slots(1,quiet=True):proc=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,encoding='utf-8')
    (out/'caller.stdout').write_text(proc.stdout,encoding='utf-8',newline='\n');(out/'caller.stderr').write_text(proc.stderr,encoding='utf-8',newline='\n')
    write(out/'execution.json',{'schema':1,'command':command,'exit_code':proc.returncode,'stdout':descriptor(out/'caller.stdout'),'stderr':descriptor(out/'caller.stderr'),
        'working_source_unchanged':core.read_closure(bot)==sources,'new_games':0,'native_engine_calls':0,'release_builds':0})
    if proc.returncode==0:
        raw=json.loads((out/'caller-raw.json').read_bytes())
        checks=validate_callers(raw,inputs)
        write(out/'caller-validated.json',{'schema':1,'raw':descriptor(out/'caller-raw.json'),'inputs':descriptor(out/'caller-inputs.json'),
            'pass':True,'cases':checks,'new_games':0,'native_engine_calls':0,'release_builds':0})
        write(out/'rounding-survey.json',rounding_survey(catalogue,selected))
    print(proc.stdout,end='');print(proc.stderr,end='',file=sys.stderr)
    return proc.returncode

if __name__=='__main__':raise SystemExit(main())
