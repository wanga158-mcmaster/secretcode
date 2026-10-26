#pragma once
#include "../../common/src/combat.h"
namespace p10_initiative {
struct Policy:LegacyPolicy {
 int variant=0,fresh_trajectories=0,issuedPriority=0,issuedRound=-1,releasedSource=-1,appointment=-1,independentSource=-1;
 bool priorityAdopted=false;
 using FuelReader=int(*)();FuelReader fuel_reader=nullptr;
 void set_fuel_reader(FuelReader reader){fuel_reader=reader;}
 static bool physical_before(const p10::World&w,const p10::Group&a,const p10::Group&b){
  int da=p10::distance(w.spawn,a.position),db=p10::distance(w.spawn,b.position);return da!=db?da<db:a.id<b.id;
 }
 void appoint(const p10::World&w){
  const p10::Group*a=nullptr,*b=nullptr;for(const auto&g:w.groups)if(g.program_available){if(!a||physical_before(w,g,*a)){b=a;a=&g;}else if(!b||physical_before(w,g,*b))b=&g;}
  appointment=a?a->id:-1;independentSource=b?b->id:-1;
 }
 static bool custody_safe(const p10::World&w,const p10::Group&g,const p10::Actor&holder){
  if(!g.observed||!g.complete||!g.cleared||g.contested||!holder.visible||!holder.alive||!on_spawn(holder,g)||!w.geometry_complete||!w.observation_complete)return false;
  for(const auto&m:g.monsters)if(m.alive&&m.hp>0)return false;
  for(const auto&e:w.enemies)if(e.visible&&e.alive&&p10::distance(e.position,holder.position)<=8)return false;
  double hazard=0;for(const auto&other:w.groups)if(other.id!=g.id&&!other.respawning&&!other.cleared){double incoming=p10::phase_damage(other,holder.position);if(incoming>0&&(!other.observed||!other.complete))return false;hazard+=incoming;}
  return holder.hp>hazard*(w.capture_turns+2)+std::max(1.,holder.max_hp*.1);
 }
 p10::Directive work(const p10::World&w,const p10::Group&g){
  using namespace p10;if(g.cleared)return farm(g.id,"actual appointed holder retains the custody phase");
  if(!usable(g)){Directive d;d.task=Task::inspect;d.group=g.id;d.destination=g.approach;d.reason="inspect deterministic physical initiative appointment";return w.safe_now?d:Directive{Task::recover};}
  if(fresh_trajectories>=2||(fuel_reader?fuel_reader():w.fuel)<=w.action_reserve+2500)return ordinary(w);
  const auto stance=distance(w.self.position,g.position)<=w.self.range?w.self.position:g.approach;
  ++fresh_trajectories;const auto proof=stationary_work(w,g,stance,64);if(!proof.feasible)return ordinary(w);
  if(!(stance==w.self.position)){if(!w.safe_now)return {Task::recover};Directive d;d.task=Task::move;d.group=g.id;d.destination=stance;d.reason="approach the fixed source-to-custody appointment";return d;}
  auto d=proof.first;d.template_id=50;d.reason="initiative worker performs current certified source work";return d;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  using namespace p10;if(variant==1||arm==Arm::control)return LegacyPolicy::step(w,arm);
  fresh_trajectories=0;if(w.own_payout)firstIncome=true;if(w.own_purchase)bought=true;
  if(w.self.channel!=Channel::none||w.capture_active)return wait("hold actual initiative custody/channel without changing its order");
  if(firstIncome&&!bought&&w.self.bag==0&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi))return buy(w.self.type==Champion::willow?Item::focus:Item::strength,1,"actual personal payout buys the renewal worker's gear");
  appoint(w);const auto*g=w.group(appointment),*next=w.group(independentSource);
  const Actor*holder=w.self.type==Champion::marcella?&w.self:nullptr;
  for(const auto&a:w.allies)if(a.type==Champion::marcella&&a.visible&&a.alive)holder=&a;
  const bool ownHolder=holder&&holder->id==w.self.id;
  Dispatch wanted=ownHolder?Dispatch::loaded:Dispatch::completion;
  if(w.self.type==Champion::willow)wanted=Dispatch::protection;
  const int desired=priority(wanted,std::clamp(w.self.pick,0,15));
  // Read actual adoption. Issuing set_priority cannot change current-round
  // slots, and a failed/unobserved issue never certifies another actor's order.
  priorityAdopted=w.self.priority==desired;
  auto finish=[&](Directive d){d.valid_round=w.round;if(!priorityAdopted){d.priority=desired;issuedPriority=desired;issuedRound=w.round;}return d;};
  if(w.self.bag>0)return finish(bank("holder banks only its observed personal payout after worker release"));
  if(w.self.type==Champion::willow&&holder&&holder->hp<holder->max_hp&&w.self.can_cast){
   for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].heal&&w.self.abilities[slot].ready&&distance(w.self.position,holder->position)<=w.self.abilities[slot].range){Directive d;d.task=Task::heal;d.cast_slot=slot;d.actor=holder->id;d.reason="observed prerequisite healing precedes custody work";return finish(d);}
  }
  if(g&&holder&&!ownHolder&&custody_safe(w,*g,*holder)){
   const int holderOrder=priority(Dispatch::loaded,std::clamp(holder->pick,0,15));
   const bool observedOrder=priorityAdopted&&holder->priority==holderOrder&&w.self.priority>holder->priority;
   if(observedOrder&&releasedSource!=g->id){releasedSource=g->id;Directive d;d.task=Task::release;d.group=g->id;d.template_id=51;d.reason="observed adopted order and safe real custody release worker before holder's bank";return finish(d);}
   if(releasedSource==g->id&&on_spawn(w.self,*g)){Directive d;d.task=Task::release;d.group=g->id;d.reason="release is not yet physically witnessed; leave the holder's spawn first";return finish(d);}
   if(releasedSource==g->id&&next)return finish(work(w,*next));
   return finish(wait("retain custody until both actual adopted priorities and holder safety are witnessed"));
  }
  if(g&&g->respawning){releasedSource=-1;if(next)return finish(work(w,*next));}
  if(w.self.type==Champion::willow&&next)return finish(work(w,*next));
  if(g)return finish(work(w,*g));
  return finish(ordinary(w));
 }
};
}
