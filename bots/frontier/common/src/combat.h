#pragma once
#include "contract.h"
namespace p10 {
using WorkFuelReader=int(*)();
inline WorkFuelReader work_fuel_reader=nullptr;
inline void set_work_fuel_reader(WorkFuelReader reader){work_fuel_reader=reader;}
struct SoloForecast {bool feasible=false;int turns=0;double hp=0;Point position{};Directive first=wait("stationary program has no initial action");};
// Physical stationary work only: no source ranking, hidden allies or future gear.
// The returned prefix has the same cast/basic ordering as Runtime::execute.
inline SoloForecast solo_program(const Actor&actor,const Group&group,Point position,int cap=96,int current_round=0,int preferred_monster=-1,WorkFuelReader fuel_reader=nullptr,int reserve=0){
 SoloForecast out;out.position=position;out.hp=actor.hp;
 if(!actor.combat_details_complete||!actor.equipment_details_complete||!group.complete||!group.program_available||group.monsters.size()>64||actor.strength<=0||cap<=0)return out;
 auto mobs=group.monsters;auto cd=actor.cooldown;double shield=actor.shield;int shield_time=std::max(0,actor.shield_until-current_round);
 if(shield_time==0)shield=0;
 const double multiplier=actor.monster_emblem?1.1:1.;
 auto choose=[&](){int best=-1;for(int i=0;i<int(mobs.size());++i)if(mobs[i].alive&&mobs[i].hp>0&&distance(position,mobs[i].position)<=actor.range){
   if(mobs[i].id==preferred_monster)return i;
   if(best<0||mobs[i].hp<mobs[best].hp||(mobs[i].hp==mobs[best].hp&&mobs[i].id<mobs[best].id))best=i;
  }
  return best;
 };
 auto hit=[&](int i,double amount){if(i<0||!mobs[i].alive||mobs[i].hp<=0)return;const bool kill=mobs[i].hp<=amount*multiplier;mobs[i].hp-=amount*multiplier;if(kill){mobs[i].alive=false;if(actor.type==Champion::vendetta)cd={0,0};}};
 auto basic=[&](int target){hit(target,actor.strength);if(actor.type==Champion::varg)for(int i=0;i<int(mobs.size());++i)if(i!=target&&distance(mobs[i].position,mobs[target].position)<=1)hit(i,actor.strength);};
 for(int turn=0;turn<cap;++turn){
  if(fuel_reader&&fuel_reader()<reserve)return out;
  bool any=false;for(const auto&m:mobs)any|=m.alive&&m.hp>0;
  if(!any){out.feasible=true;out.turns=turn;return out;}
  int target=choose();if(target<0)return out;
  const bool basic_ready=turn>0||!actor.visible||actor.can_attack,cast_ready=turn>0||!actor.visible||actor.can_cast;
  Directive first=wait("stationary cooldown phase");first.group=group.id;first.reserve_reset=true;first.strict_tank=true;first.valid_round=current_round;
  bool cast=false;int slot=-1,spell_target=-1,basic_target=-1;
  if(cast_ready)for(int i=0;i<2;++i)if(cd[i]==0){const auto&a=actor.abilities[i];
   if(a.targets_camps&&(a.damage||a.aoe||a.cc)&&distance(position,mobs[target].position)<=a.range){slot=i;spell_target=target;
    // Install cooldown before damage: Vendetta's credited kills reset it.
    cd[i]=a.max_cooldown>0?a.max_cooldown:cap+1;
    for(int j=0;j<int(mobs.size());++j)if(j==target||(a.aoe&&distance(mobs[j].position,mobs[target].position)<=a.radius)){hit(j,a.modifier*actor.focus);if(a.cc&&mobs[j].alive)mobs[j].stun=std::max(mobs[j].stun,std::max(0,a.duration-1));}
    cast=true;break;
   }else if(a.targets_self&&a.shield&&shield<=0&&a.modifier>0){slot=i;shield=a.modifier*actor.focus;shield_time=a.duration;cd[i]=a.max_cooldown>0?a.max_cooldown:cap+1;cast=true;break;}
  }
  if(basic_ready){basic_target=choose();if(basic_target>=0)basic(basic_target);}
  if(cast){first.task=Task::cast;first.cast_slot=slot;first.monster=spell_target>=0?mobs[spell_target].id:-1;first.second_monster=basic_target>=0?mobs[basic_target].id:-1;}
  else if(basic_target>=0){first.task=Task::attack;first.monster=mobs[basic_target].id;
   if(cast_ready)for(int i=0;i<2;++i)if(cd[i]==0&&actor.abilities[i].targets_self&&actor.abilities[i].reset){int next=choose();if(next>=0){first.cast_slot=i;first.second_monster=mobs[next].id;cd[i]=actor.abilities[i].max_cooldown>0?actor.abilities[i].max_cooldown:cap+1;basic(next);cast=true;}break;}
  }
  if(turn==0)out.first=first;
  bool remains=false;double incoming=0;
  // Retaliation requires actual HP damage and strict target ownership. The
  // general stationary witness gives it no credit; card09 proves its own phase.
  for(auto&m:mobs)if(m.alive&&m.hp>0){remains=true;if(m.stun>0){--m.stun;continue;}if(distance(position,m.position)<=m.range)incoming+=m.strength;}
  if(!remains){out.feasible=true;out.turns=turn+1;return out;}
  incoming*=actor.jungle_emblem?.7:1.;const double absorbed=std::min(shield,incoming);shield-=absorbed;out.hp-=incoming-absorbed;
  if(out.hp<=std::max(1.,actor.max_hp*.08))return out;
  if(shield_time>0&&--shield_time==0)shield=0;
  for(auto&c:cd)c=std::max(0,c-1);
 }
 return out;
}
inline double phase_damage(const Group&g,Point p){double damage=0;for(const auto&m:g.monsters)if(m.alive&&m.hp>0&&distance(p,m.position)<=m.range)damage+=m.strength;return damage;}
// Conditional finite work witness, rebuilt from this owner turn. Neighboring
// monsters are conservatively charged even when a collateral wake is uncertain.
inline SoloForecast stationary_work(const World&w,const Group&g,Point stance,int cap=64,int preferred_monster=-1){
 SoloForecast out;
 if(!w.geometry_complete||!w.actor_observation_complete||!g.observed||g.contested||!g.route_known)return out;
 for(const auto&e:w.enemies)if(e.visible&&e.alive&&distance(e.position,stance)<=e.range+2)return out;
 if(work_fuel_reader&&work_fuel_reader()<w.action_reserve+1500)return out;
 out=solo_program(w.self,g,stance,cap,w.round,preferred_monster,work_fuel_reader,w.action_reserve+500);if(!out.feasible)return out;
 double neighbors=0;for(const auto&other:w.groups)if(other.id!=g.id&&!other.cleared&&!other.respawning){
  const double phase=phase_damage(other,stance);if(phase>0&&(!other.observed||!other.complete)){out.feasible=false;return out;}neighbors+=phase;
 }
 out.hp-=neighbors*(out.turns+2)*(w.self.jungle_emblem?.7:1.);
 if(out.hp<=std::max(1.,w.self.max_hp*.08)){out.feasible=false;return out;}
 out.first.safety_verified=stance==w.self.position;out.first.local_safety_verified=out.first.safety_verified;out.first.safety_round=w.round;out.first.minimum_start_hp=w.self.hp;
 return out;
}
inline double pulse_phase_damage(const World&w,int source,Point p){double value=0;for(const auto&g:w.groups){bool awakened=g.id==source||(!g.complete&&!g.cleared&&!g.respawning);for(const auto&m:g.monsters)awakened|=m.observed&&m.alive&&m.awake;if(awakened)value+=phase_damage(g,p);}return value;}
inline bool collateral_survives(const World&w,Point center,double radius,int source,Point exposure,double hp){
 if(!w.geometry_complete||!w.actor_observation_complete)return false;double incoming=0;
 for(const auto&g:w.groups)if(g.id!=source&&!g.respawning&&!g.cleared){bool struck=false;for(const auto&m:g.monsters)if(m.alive&&distance(m.position,center)<=radius)struck=true;
  if(struck){if(!g.observed||!g.complete)return false;incoming+=phase_damage(g,exposure);}
 }
 return incoming*3<hp;
}
inline bool pulse_survives(const Actor&a,const Group&g,Point attack,Point exit){
 if(!g.complete||!g.program_available||a.strength<=0||phase_damage(g,attack)>=a.hp)return false;
 return distance(attack,exit)<=1&&phase_damage(g,exit)*2+phase_damage(g,attack)<a.hp;
}
} // namespace p10
