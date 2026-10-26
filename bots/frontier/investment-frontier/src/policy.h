#pragma once
#include "model.h"
namespace p10::investment {
struct Policy {
 int variant=0; // 0 schedule frontier; 1 retained conservative threshold basin
 InvestmentCertificate committed{};Counts purchased{},baseline{};int source=-1,purchaseStart=-1,adoptionUntil=-1;
 bool payout=false,banked=false,renewed=false;int lastRound=-1;
 using FuelReader=int(*)();FuelReader fuel_reader=nullptr;int fresh_trajectories=0,local_fuel=0;
 void set_fuel_reader(FuelReader reader){fuel_reader=reader;}
 static int trace_budget(const Group&g,double strength){
  const int phases=std::min(80,8+int(std::ceil(std::max(0.,g.hp)/std::max(1.,strength))));
  // Conservative source guard, not a current-engine measured fuel certificate.
  // Larger complete groups become unavailable rather than losing members.
  return 6000+220*phases*int(g.monsters.size());
 }
 bool trace(const World&w,const Actor&a,const Group&g,double health,double strength,double focus,CombatTrace&out){
  if(fresh_trajectories>=2)return false;
  const int fresh=fuel_reader?fuel_reader():local_fuel;
  local_fuel=std::min(local_fuel,fresh);const int cost=variant?trace_budget(g,strength):1200+60*std::min(80,8+int(std::ceil(std::max(0.,g.hp)/std::max(1.,strength))))*int(g.monsters.size());
  if(local_fuel<std::max(w.action_reserve,w.entry_fuel*40/100)+cost)return false;
  ++fresh_trajectories;out=solo(a,g,health,strength,focus);local_fuel-=cost;return true;
 }
 static Counts progress(const Actor&a,Family f){return f==Family::monster?Counts{a.inventory[int(Item::monster_health)]%5,a.inventory[int(Item::monster_strength)]%5,a.inventory[int(Item::monster_focus)]%5}:Counts{a.inventory[int(Item::jungle_health)]%5,a.inventory[int(Item::jungle_strength)]%5,a.inventory[int(Item::jungle_focus)]%5};}
 static Counts inventory(const Actor&a,Family f){return f==Family::monster?Counts{a.inventory[int(Item::monster_health)],a.inventory[int(Item::monster_strength)],a.inventory[int(Item::monster_focus)]}:Counts{a.inventory[int(Item::jungle_health)],a.inventory[int(Item::jungle_strength)],a.inventory[int(Item::jungle_focus)]};}
 Directive step(const World&w,Arm arm){
  fresh_trajectories=0;local_fuel=w.fuel;
  if(lastRound>=0&&w.round>lastRound+1)committed.feasible=false;lastRound=w.round;
  if(w.self.channel!=Channel::none&&w.safe_now)return wait();
  if(w.own_payout&&source<0)source=w.last_payout_group;
  if(w.own_payout&&w.last_payout_group==source)payout=true;if(payout&&w.own_bank_completed)banked=true;
  if(banked&&w.productive_group>=0&&w.productive_group!=source)renewed=true;
  if(w.self.bag>0)return bank("owned frontier conversion");
  if(!w.safe_now)return {Task::recover};
  const bool investor=w.self.type==Champion::ryth||w.self.type==Champion::stellar;
  // Visible adoption: a helper leaves only after seeing actual changed gear,
  // a solo-capable investor and a different independently feasible source.
  if(!investor){for(const auto&a:w.allies)if(a.visible&&a.alive&&(a.type==Champion::ryth||a.type==Champion::stellar)&&a.health_shards+a.strength_shards+a.focus_shards>0){
    for(const auto&g:w.groups)if(usable(g)&&distance(a.position,g.position)<=5){CombatTrace t;if(!trace(w,a,g,a.max_hp,a.strength,a.focus,t))return ordinary(w);if(t.feasible)for(const auto&other:w.groups)if(other.id!=g.id&&usable(other)&&other.safe_solo)return farm(other.id,"visible funded worker releases helper to independent source");}}
   return ordinary(w);}
  if(committed.feasible){const auto*g=w.group(committed.changedProgram);if(!g||w.round-purchaseStart>96||!g->program_available||(g->observed&&(g->respawning||g->contested))){committed.feasible=false;return ordinary(w);}
   const Counts now=inventory(w.self,committed.family);purchased={now.h-baseline.h,now.s-baseline.s,now.f-baseline.f};
   Item item=Item::none;if(purchased.h<committed.itemCounts.h)item=committed.family==Family::monster?Item::monster_health:Item::jungle_health;
   else if(purchased.s<committed.itemCounts.s)item=committed.family==Family::monster?Item::monster_strength:Item::jungle_strength;
   else if(purchased.f<committed.itemCounts.f)item=committed.family==Family::monster?Item::monster_focus:Item::jungle_focus;
   if(item!=Item::none){if(w.own_gold<w.shard_cost){committed.feasible=false;return ordinary(w);}if(w.self.at_spawn||w.self.type==Champion::tavi)return buy(item,1,"one actually affordable owned frontier shard");return {Task::recover};}
   if(!g->observed||!g->complete){Directive d;d.task=Task::inspect;d.group=g->id;d.reason="funded frontier returns to reobserve source before changed labor";return d;}
   CombatTrace traced;if(!trace(w,w.self,*g,w.self.max_hp,w.self.strength,w.self.focus,traced))return ordinary(w);if(!traced.feasible){committed.feasible=false;return ordinary(w);}
   source=g->id;return farm(source,"funded solo custody and helper release");
  }
  if(w.own_gold<w.shard_cost||w.fuel<w.action_reserve+(variant?12000:3000))return ordinary(w);
  const auto all=catalogue(w.self,w.own_gold,w.shard_cost,progress(w.self,Family::monster),progress(w.self,Family::jungle));
  for(const auto&g:w.groups){if(!usable(g)||g.to_bank<0||g.travel<0||g.monsters.empty())continue;
   const Actor*helper=nullptr;for(const auto&a:w.allies)if(a.visible&&a.alive&&distance(a.position,g.position)<=5){helper=&a;break;}if(!helper)continue;
   const Group*other=nullptr;for(const auto&h:w.groups)if(h.id!=g.id&&usable(h)&&h.safe_solo){other=&h;break;}if(!other)continue;
   // Algebraic threshold screening precedes the two fresh trajectories. No
   // hit-count approximation grants permission to buy or start a source.
   ThresholdProgram p;p.source=g.id;p.owner=w.self.id;p.helper=helper->id;p.independentSource=other->id;p.observed=p.helperVisible=p.independentFeasible=true;
   p.requiredHealth=variant?std::max(w.self.max_hp,g.incoming*(std::max(1,g.clear_turns)+2)*1.2):w.self.max_hp;
   p.requiredStrength=variant?std::max(w.self.strength,g.hp/32.):w.self.strength;p.requiredFocus=w.self.focus;
   p.clear=std::max(0,g.clear_turns);p.approach=g.travel;p.hold=w.capture_turns+1;p.bankTravel=g.to_bank;p.tribute=w.ticks(g.gems);p.renewal=other->travel+other->to_bank+w.ticks(other->gems);p.shopDetour=w.self.at_spawn?0:std::max(0,w.to_spawn)+w.recall_turns;
   auto candidate=variant?select(all,p,arm):schedule_candidate(all,w.self,g,p,arm);if(!candidate.feasible||candidate.ownCost==0)continue;
   Actor upgraded=w.self;upgraded.at_spawn=true;upgraded.hp=w.self.max_hp;upgraded.monster_emblem|=candidate.family==Family::monster&&candidate.exactPassive;upgraded.jungle_emblem|=candidate.family==Family::jungle&&candidate.exactPassive;
   CombatTrace upgradedTrace,currentTrace;if(!trace(w,upgraded,g,candidate.health,candidate.strength,candidate.focus,upgradedTrace))break;if(!upgradedTrace.feasible)continue;
   // Already feasible current gear does not establish an organizational change.
   if(!trace(w,w.self,g,w.self.max_hp,w.self.strength,w.self.focus,currentTrace))break;
   if(currentTrace.feasible&&(variant||upgradedTrace.turns>=currentTrace.turns))continue;
   committed=candidate;baseline=inventory(w.self,candidate.family);purchaseStart=w.round;source=g.id;
   Item first=candidate.itemCounts.h?(candidate.family==Family::monster?Item::monster_health:Item::jungle_health):candidate.itemCounts.s?(candidate.family==Family::monster?Item::monster_strength:Item::jungle_strength):(candidate.family==Family::monster?Item::monster_focus:Item::jungle_focus);
   return w.self.at_spawn?buy(first,1,"minimal owned vector changes labor and receipt owner"):Directive{Task::recover};
  }
  if(w.self.at_spawn&&w.own_gold>=w.shard_cost)return buy(w.self.type==Champion::ryth?Item::jungle_health:Item::monster_focus,1,"cheapest currently useful increment; no frontier claim");
  return ordinary(w);
 }
};
} // namespace p10::investment
