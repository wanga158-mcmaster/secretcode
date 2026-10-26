#pragma once
#include "policy.h"
#include "../../common/src/economic-observation.h"
namespace p10_b15 {
using namespace p10;
enum class Kind { single_hit,splash_reset,burst_hold,mobile_melee,mobile_ranged,loaded_service,recovery,strength_threshold };
struct Compiled {int source=-1,next=-1,epoch=-1;Kind kind=Kind::mobile_melee;int members=0;bool available=false;};
struct BPolicy {
 p10_b::Economy economy{};std::array<Compiled,2>skills{};int active=0,source=-1,traces=0,guardHits=0,compiles=0,blockedSource=-1;
 double blockedStrength=-1;int blockedEpoch=-2;
 // Both representations own the identical primitive vocabulary. Compilation
 // retains circuit descriptors only; every execution rebuilds the same fresh
 // legal prefix and no successful physical forecast is reused across turns.
 static constexpr std::array<Kind,8>primitive_menu{Kind::single_hit,Kind::splash_reset,Kind::burst_hold,Kind::mobile_melee,Kind::mobile_ranged,Kind::loaded_service,Kind::recovery,Kind::strength_threshold};
 int onlineRebuilds=0,compiledReuses=0,sharedGuardChecks=0,mobilePlans=0;
 static bool guard_clear(const World&w,const Group&g,p10_mobile::Budget&budget,Directive&out,Kind&kind){
  if(!p10_b::work_source(g)||g.cleared||!w.self.can_attack||p10_mobile::visible_enemy_can_intervene(w,w.self.position)||!budget.fuel_ok())return false;
  const Monster*center=nullptr;int alive=0;for(const auto&m:g.monsters)if(m.alive&&m.hp>0){++alive;if(!center||m.hp>center->hp)center=&m;}
  if(!center)return false;const double mult=w.self.monster_emblem?1.1:1.;int slot=-1;bool all=true;
  if(distance(center->position,w.self.position)>w.self.range)all=false;
  for(const auto&m:g.monsters)if(m.alive&&m.hp>0){if(!budget.fuel_ok())return false;all&=m.hp<=w.self.strength*mult&&(m.id==center->id||(w.self.type==Champion::varg&&distance(m.position,center->position)<=1));}
  kind=Kind::single_hit;
  if(!all&&w.self.can_cast&&distance(center->position,w.self.position)<=w.self.range&&w.self.type==Champion::varg){
   for(int s=0;s<2;++s)if(w.self.abilities[s].ready&&w.self.abilities[s].targets_self&&w.self.abilities[s].reset)slot=s;
   if(slot>=0){all=center->hp>w.self.strength*mult;for(const auto&m:g.monsters)if(m.alive&&m.hp>0)all&=m.hp<=2*w.self.strength*mult&&distance(m.position,center->position)<=1;if(all)kind=Kind::splash_reset;}
  }
  if(!all&&w.self.can_cast){slot=-1;for(int s=0;s<2;++s){const auto&a=w.self.abilities[s];if(!a.ready||!a.targets_camps||!(a.damage||a.aoe||a.cc)||distance(center->position,w.self.position)>a.range)continue;
    bool hit=alive==1||a.aoe;for(const auto&m:g.monsters)if(m.alive&&m.hp>0)hit&=m.hp<=a.modifier*w.self.focus*mult&&(m.id==center->id||(a.aoe&&distance(m.position,center->position)<=a.radius));if(hit){all=true;slot=s;kind=Kind::burst_hold;break;}
   }}
  if(!all)return false;
  double damage=0;double radius=w.self.type==Champion::varg?1.:0.;if(kind==Kind::burst_hold)radius=w.self.abilities[slot].radius;
  for(const auto&other:w.groups)if(other.id!=g.id&&!other.cleared&&!other.respawning)for(const auto&m:other.monsters)if((m.alive&&m.hp>0)||!other.complete){if(!budget.fuel_ok())return false;
   const bool collateral=distance(center->position,m.position)<=radius;if(collateral&&(!other.observed||!other.complete||!m.observed))return false;
   const bool reaches=distance(w.self.position,m.position)<=m.range;if(reaches&&m.strength>0&&(!other.observed||!other.complete||!m.observed))return false;
   if(reaches&&(m.awake||collateral)){if(!other.observed||!other.complete||!m.observed)return false;damage+=m.strength;}
  }
  if(w.self.hp<=3*damage*(w.self.jungle_emblem?.7:1.)+std::max(1.,w.self.max_hp*.1)||budget.spent>=2)return false;
  ++budget.spent;out={};out.task=kind==Kind::burst_hold?Task::cast:Task::attack;out.group=g.id;out.monster=center->id;out.reserve_reset=out.strict_tank=true;out.cast_slot=kind==Kind::single_hit?-1:slot;
  if(kind==Kind::splash_reset)out.second_monster=center->id;out.safety_verified=true;out.safety_round=w.round;out.minimum_start_hp=w.self.hp;out.valid_round=w.round;return true;
 }
 void compile(const World&w,p10_mobile::Budget&budget){++compiles;skills={};const Group*first=nullptr,*second=nullptr;
  auto value=[&](const Group&g){const int complexity=1+int(g.monsters.size())+std::max(0,g.travel);return (g.gems*4+g.gold*.05)/complexity;};
  for(const auto&g:w.groups){if(!budget.fuel_ok()){skills={};return;}if(p10_b::work_source(g)&&!(g.id==blockedSource&&g.last_payout==blockedEpoch&&w.self.strength==blockedStrength)){
   if(!first||value(g)>value(*first)){second=first;first=&g;}else if(!second||value(g)>value(*second))second=&g;
  }}
  const Group*jobs[2]{first,second};for(int s=0;s<2;++s)if(jobs[s])skills[s]={jobs[s]->id,jobs[1-s]?jobs[1-s]->id:jobs[s]->id,jobs[s]->last_payout,w.self.range>1?Kind::mobile_ranged:Kind::mobile_melee,int(jobs[s]->monsters.size()),true};active=0;
 }
 Directive step(const World&w,Arm arm){
  economy.consume(w);traces=0;auto budget=p10_mobile::budget_for(w);const auto options=p10_b::single_options();
  if(w.self.channel!=Channel::none)return wait("compiled guild consumes receipts and preserves actual service");
  if(w.self.bag>0&&(economy.batch>=2||w.self.bag>=24||p10_b::altar_trip(w)<=3||w.score+w.self.bag>=w.win_gems))return bank("compiled loaded service closes named two-source batch");
  if(w.capture_active)return wait("compiled custody waits for actual reward before renewal");
  bool blocked=false;for(const auto&r:w.receipts)blocked|=r.kind==Receipt::Kind::blocked&&r.round>=w.round-1;
  const auto*current=w.group(skills[active].source);if(blocked&&current){blockedSource=current->id;blockedEpoch=current->last_payout;blockedStrength=w.self.strength;skills[active].available=false;}
  if(arm==Arm::control){
   // Recompute the same bounded catalogue/ranking with the same current input
   // and budget. Cheap exact prefixes remain available to this planner; a
   // generic trajectory is never a prerequisite for using that vocabulary.
   ++onlineRebuilds;compile(w,budget);
  }else if(!skills[active].available||!current||!p10_b::work_source(*current)||current->last_payout!=skills[active].epoch)compile(w,budget);
  else ++compiledReuses;
  for(int attempt=0;attempt<2;++attempt){auto&s=skills[active];const Group*g=w.group(s.source);if(!s.available||!g||!p10_b::work_source(*g)){active=1-active;continue;}source=g->id;
   if(g->cleared)return farm(g->id,"compiled mobile skill reaches physical custody");
   Directive fast;Kind kind=s.kind;
   ++sharedGuardChecks;
   if(guard_clear(w,*g,budget,fast,kind)){++guardHits;s.kind=kind;traces=budget.spent;fast.template_id=1510+int(kind);fast.reason="matched exact one-turn skill with complete three-phase hazard guard";return fast;}
   if(budget.spent>=2)break;++mobilePlans;auto work=p10_mobile::mobile_work(w,*g,budget,options);traces=budget.spent;
   if(work.feasible){s.kind=w.self.range>1?Kind::mobile_ranged:Kind::mobile_melee;for(int n=0;n<work.frames_used;++n){if(work.frames[n].casts&&work.frames[n].basics>1)s.kind=Kind::splash_reset;else if(work.frames[n].casts&&s.kind!=Kind::splash_reset)s.kind=Kind::burst_hold;}
    return p10_b::witnessed(w,work,1520+int(s.kind),"compiled mobile skill admits real stance changes and dead-spawn progress");
   }
   active=1-active;
  }
  // Capital can change the executable cheap vocabulary, but only an actual
  // legal personal purchase occurs; no future stat enters a returned witness.
  if((w.self.at_spawn||w.self.type==Champion::tavi)&&w.own_gold>=w.shard_cost)for(const auto&g:w.groups)if(p10_b::work_source(g)&&g.monsters.size()==1&&g.monsters[0].hp>w.self.strength&&g.monsters[0].hp<=w.self.strength+w.self.strength_growth)return buy(Item::monster_strength,1,"funded one-hit threshold compiles a cheaper useful skill");
  if(w.self.bag>0)return bank("available actual bag closes while mobile skill is unavailable");
  if(w.self.hp<w.self.max_hp*.25||!w.safe_now)return {Task::recover};
  if(const auto*g=w.group(skills[active].source))return p10_b::approach(*g,1529,"bounded circuit replacement/reveal before another guarded compile");return Directive{};
 }
};
}
