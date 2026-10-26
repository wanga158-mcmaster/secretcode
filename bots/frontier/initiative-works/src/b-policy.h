#pragma once
#include "policy.h"
#include "../../common/src/physical-turn.h"
namespace p10_initiative {
struct BPolicy:Policy {
 int observedAdoption=-1,releaseIntent=-1;bool income=false,gear=false;
 void receipt_first(const p10::World&w){if(w.own_payout)income=true;if(w.own_purchase)gear=true;}
 p10::Directive step(const p10::World&w,p10::Arm){
  using namespace p10;receipt_first(w);p10_b_local::Turn turn(w);
  auto done=[&](Directive d){fresh_trajectories=turn.budget.spent;return d;};
  if(w.self.channel!=Channel::none||w.capture_active)return done(wait("pipeline preserves actual custody/channel and its observed order"));
  appoint(w);const auto*g=w.group(appointment),*job=w.group(independentSource);
  const Actor*holder=w.self.type==Champion::marcella?&w.self:nullptr;for(const auto&a:w.allies)if(a.type==Champion::marcella&&a.visible&&a.alive)holder=&a;
  const bool holds=holder&&holder->id==w.self.id;
  const Dispatch slot=holds?Dispatch::loaded:w.self.type==Champion::willow?Dispatch::protection:Dispatch::completion;
  const int desired=priority(slot,std::clamp(w.self.pick,0,15));priorityAdopted=w.self.priority==desired;
  if(priorityAdopted)observedAdoption=w.round;
  auto issue=[&](Directive d){d.valid_round=w.round;if(!priorityAdopted){d.priority=desired;issuedPriority=desired;issuedRound=w.round;}return done(d);};
  if(w.self.bag>0)return issue(bank("after actual payout, holder converts its personal custody pipeline output"));
  if(income&&!gear&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi))return issue(buy(w.self.type==Champion::willow?Item::focus:Item::strength,1,"actual pipeline wallet finances only this worker's gear"));
  if(g&&holder&&!holds&&p10_b_local::safe_custody(w,*g,*holder)){
   const bool adopted=priorityAdopted&&holder->priority==priority(Dispatch::loaded,std::clamp(holder->pick,0,15))&&w.self.priority>holder->priority;
   if(adopted&&(releaseIntent!=g->id||p10_b_local::on_spawn(w.self,*g))){releaseIntent=g->id;Directive d;d.task=Task::release;d.group=g->id;d.template_id=102;d.reason="current observed priorities put this released worker before the actually safe holder's bank";return issue(d);}
   if(releaseIntent==g->id&&job)return issue(turn.work(*job,"released pipeline worker executes its separate complete mobile job",103));
   return issue(wait("safe custody is known; actual priority adoption must precede worker release"));
  }
  if(w.self.type==Champion::willow&&holder&&holder->hp<holder->max_hp&&w.self.can_cast){
   const double danger=g?phase_damage(*g,holder->position):0;
   for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].heal&&w.self.abilities[slot].ready&&distance(w.self.position,holder->position)<=w.self.abilities[slot].range&&holder->hp<danger*2+holder->max_hp*.2){Directive d;d.task=Task::heal;d.actor=holder->id;d.cast_slot=slot;d.template_id=101;d.reason="actual prerequisite heal before the custody pipeline can release work";return issue(d);}
  }
  if(g&&g->respawning){releaseIntent=-1;return issue(job?turn.work(*job,"banked pipeline cycles to the next actual physical source",103):ordinary(w));}
  if(w.self.type==Champion::willow&&job)return issue(turn.work(*job,"unneeded pipeline support has its own mobile production appointment",103));
  if(g)return issue(turn.work(*g,"literal mobile clearance precedes the appointed holder's custody phase",100));
  return issue(ordinary(w));
 }
};
}
