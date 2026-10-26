#pragma once
#include "model.h"
#include "../../common/src/combat.h"
namespace shield_foundry {
struct Clearance {bool complete=false;int rounds=0,transfers=0,bank=0;double exposure=0,own_hp=0;std::array<double,3>hp{};p10::Directive first{};};
// A finite crew combat suffix. Actual monster deaths remove future exposure;
// shields, Willow pulses and nearest-body assignment remain separate stocks.
inline Clearance clearance(const p10::World&w,const p10::Group&g,const p10::Actor*partner,const p10::Actor*willow,p10::Point vacate={},int(*fuel_reader)()=nullptr){
 Clearance out;if(!p10::usable(g)||g.monsters.size()>12||!w.geometry_complete||!w.observation_complete||!w.enemies.empty())return out;
 std::array<p10::Actor,3>actors{};actors[0]=w.self;int count=1;if(partner&&partner->id!=w.self.id)actors[count++]=*partner;
 if(willow&&willow->id!=w.self.id&&(!partner||willow->id!=partner->id))actors[count++]=*willow;
 std::sort(actors.begin(),actors.begin()+count,[](const auto&a,const auto&b){return a.priority!=b.priority?a.priority>b.priority:a.pick<b.pick;});
 int own=-1;for(int i=0;i<count;++i)if(actors[i].id==w.self.id)own=i;if(own<0)return out;
 for(const auto&other:w.groups)if(other.id!=g.id&&!other.cleared&&!other.respawning)for(const auto&m:other.monsters)if(m.alive&&m.awake&&m.strength>0)for(int a=0;a<count;++a)if(p10::distance(actors[a].position,m.position)<=m.range+1)return out;
 auto mobs=g.monsters;std::array<int,3>expiry{};for(int a=0;a<count;++a)expiry[a]=actors[a].shield_until<0?-1:actors[a].shield_until-w.round;
 auto volley=[&](int actor){double damage=0;for(const auto&m:mobs)if(m.alive&&m.awake&&m.strength>0){int nearest=999;for(int a=0;a<count;++a)if(actors[a].alive&&p10::distance(actors[a].position,m.position)<=m.range)nearest=std::min(nearest,p10::distance(actors[a].position,m.position));
   if(p10::distance(actors[actor].position,m.position)==nearest)damage+=m.strength;}
  return damage;};
 auto hit=[&](int actor,int target){auto&a=actors[actor];const double multiplier=(a.monster_emblem?1.1:1.)*(a.type==p10::Champion::heart?1+.5*(1-std::clamp(a.hp/a.max_hp,0.,1.)):1.);
  const auto center=mobs[target].position;for(auto&m:mobs)if(m.alive&&(m.id==mobs[target].id||(a.type==p10::Champion::varg&&p10::distance(m.position,center)<=1))){m.hp-=a.strength*multiplier;if(m.hp<=0)m.alive=false;}
  for(auto&m:mobs)m.awake=true;
 };
 bool first=false;int old_lead=-1;
 for(int round=0;round<16;++round){
  for(int a=0;a<count;++a){if(fuel_reader&&fuel_reader()<w.action_reserve+500)return Clearance{};auto&body=actors[a];if(round==0&&a<own)continue;if(expiry[a]<=round)body.shield=0;
   p10::Directive d;d.group=g.id;d.valid_round=w.round;d.reserve_reset=true;d.strict_tank=true;d.template_id=40;
   bool cast=false,moved=false;
   if(round==0&&a==own&&vacate.x>=0){body.position=vacate;d.task=p10::Task::move;d.destination=vacate;d.reason="planned strict-nearest exposure transfer before shield gap";d.template_id=42;moved=true;out.transfers=1;}
   else if(body.type==p10::Champion::willow){
    int patient=-1;double urgency=1e30;for(int b=0;b<count;++b)if(b!=a&&actors[b].channel==p10::Channel::none&&p10::distance(body.position,actors[b].position)<=4){const double danger=volley(b),missing=actors[b].max_hp-actors[b].hp;
     if(missing>0&&(missing>=3*body.focus||actors[b].hp+actors[b].shield<danger*3)&&actors[b].hp+actors[b].shield-danger*2<urgency){patient=b;urgency=actors[b].hp+actors[b].shield-danger*2;}}
    if(patient>=0&&body.cooldown[0]==0&&(round>0||a!=own||body.can_cast)){actors[patient].hp=std::min(actors[patient].max_hp,actors[patient].hp+3*body.focus);body.cooldown[0]=body.abilities[0].max_cooldown>0?body.abilities[0].max_cooldown:8;cast=true;d.task=p10::Task::heal;d.actor=actors[patient].id;d.cast_slot=0;d.reason="scheduled productive foundry heal pulse";}
   }
   else {
    const int shield_slot=body.type==p10::Champion::heart?0:1;const double danger=volley(a);
    if(danger>0&&body.cooldown[shield_slot]==0&&(round>0||a!=own||body.can_cast)&&(body.shield<danger*2||expiry[a]<=round+1)){
     body.shield=body.focus*(body.type==p10::Champion::heart?1.5:2.5);expiry[a]=round+5;body.cooldown[shield_slot]=body.abilities[shield_slot].max_cooldown>0?body.abilities[shield_slot].max_cooldown:8;
     cast=true;d.task=p10::Task::shield;d.cast_slot=shield_slot;d.reason="planned staggered shield then productive basic";d.template_id=41;
    }
   }
   if(!moved&&body.type!=p10::Champion::willow&&(round>0||a!=own||body.can_attack)){
    int target=-1;double priority=-1;for(int i=0;i<int(mobs.size());++i)if(mobs[i].alive&&p10::distance(body.position,mobs[i].position)<=body.range){double value=mobs[i].strength*1000+1;if(body.type==p10::Champion::varg)for(const auto&m:mobs)if(m.alive&&p10::distance(m.position,mobs[i].position)<=1)value+=50;if(value>priority){priority=value;target=i;}}
    if(target>=0){if(body.type==p10::Champion::varg)for(const auto&other:w.groups)if(other.id!=g.id&&!other.cleared&&!other.respawning)for(const auto&m:other.monsters)if(m.alive&&p10::distance(m.position,mobs[target].position)<=1)return Clearance{};
     if(d.task==p10::Task::independent){d.task=p10::Task::attack;d.reason="positioned frontliner full-clearance suffix";}d.monster=mobs[target].id;hit(a,target);
     if(!cast&&body.type==p10::Champion::varg&&body.cooldown[0]==0&&(round>0||a!=own||body.can_cast)){
      int second=mobs[target].alive?target:-1;if(second<0)for(int i=0;i<int(mobs.size());++i)if(mobs[i].alive&&p10::distance(body.position,mobs[i].position)<=body.range){second=i;break;}
      if(second>=0){d.cast_slot=0;d.second_monster=mobs[second].id;hit(a,second);body.cooldown[0]=body.abilities[0].max_cooldown>0?body.abilities[0].max_cooldown:8;cast=true;}
     }
    }
   }
   if(body.type==p10::Champion::willow&&d.task==p10::Task::independent){d.task=p10::Task::wait;d.reason="retain actual healer service pad between pulses";}
   if(round==0&&a==own&&d.task!=p10::Task::independent){out.first=d;first=true;}
  }
  bool remains=false;for(const auto&m:mobs)remains|=m.alive;
  if(!remains){out.complete=first;out.rounds=round+1;break;}
  int lead=-1;for(int a=0;a<count;++a){const double incoming=volley(a)*(actors[a].jungle_emblem?.7:1.);if(incoming>0){if(lead<0)lead=a;else lead=3;}const double absorbed=std::min(actors[a].shield,incoming);actors[a].shield-=absorbed;actors[a].hp-=incoming-absorbed;
   if(actors[a].hp<=std::max(20.,actors[a].max_hp*.08))return Clearance{};
  }
  if(old_lead>=0&&lead>=0&&lead<3&&old_lead!=lead)++out.transfers;if(lead>=0&&lead<3)old_lead=lead;
  for(int a=0;a<count;++a)for(auto&cd:actors[a].cooldown)cd=std::max(0,cd-1);
 }
 if(!out.complete)return out;
 out.bank=out.rounds+w.capture_turns+1+std::max(0,g.to_bank)+w.ticks(g.gems)+std::max(0,g.to_spawn);
 out.complete=out.bank<=std::min(96,w.remaining());for(int a=0;a<count;++a)out.hp[a]=actors[a].hp;out.own_hp=actors[own].hp;
 // This safety stamp only overrides generic recovery with the *actual* shield
 // compound and a surviving observed crew suffix, never an unseen healer.
 double damage=0;for(const auto&m:g.monsters)if(m.alive&&m.awake&&p10::distance(w.self.position,m.position)<=m.range)damage+=m.strength;
 double cover=w.self.shield;if(out.first.task==p10::Task::shield)cover=w.self.focus*(w.self.type==p10::Champion::heart?1.5:2.5);
 out.first.minimum_start_hp=std::max(1.,damage*3-cover+30);out.first.safety_round=w.round;out.first.safety_verified=w.self.hp>out.first.minimum_start_hp;
 return out;
}
}
