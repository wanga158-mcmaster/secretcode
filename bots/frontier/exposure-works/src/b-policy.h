#pragma once
#include "policy.h"
#include "../../common/src/physical-turn.h"
namespace p10_exposure {
struct BPolicy:Policy {
 int serviceTurns=0,foregoneJobGems=0,independentJob=-1,actualCustody=-1,lastBank=-1;bool income=false,capital=false;
 void receipt_first(const p10::World&w){if(w.own_payout)income=true;if(w.own_purchase)capital=true;if(w.own_bank_completed)lastBank=w.round;}
 p10::Directive step(const p10::World&w,p10::Arm){
  using namespace p10;receipt_first(w);p10_b_local::Turn turn(w);auto done=[&](Directive d){fresh_trajectories=turn.budget.spent;return d;};
  if(w.self.channel!=Channel::none||w.capture_active)return done(wait("reaction organization preserves its actual service/custody receipt"));
  if(w.self.bag>0)return done(bank("actual reaction/service payout is converted before renewed work"));
  if(income&&!capital&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi))return done(buy(w.self.type==Champion::ren||w.self.type==Champion::willow?Item::focus:Item::strength,1,"reaction capital is bought only from this actor's existing wallet"));
  const Actor*ren=w.self.type==Champion::ren?&w.self:nullptr;for(const auto&a:w.allies)if(a.type==Champion::ren&&a.visible&&a.alive)ren=&a;
  const Group*primary=nullptr;for(const auto&g:w.groups)if(g.program_available&&g.route_known&&!g.respawning&&!g.contested&&g.to_bank>=0&&
    (!primary||distance(g.position,ren?ren->position:w.spawn)<distance(primary->position,ren?ren->position:w.spawn)||(distance(g.position,ren?ren->position:w.spawn)==distance(primary->position,ren?ren->position:w.spawn)&&g.id<primary->id)))primary=&g;
  if(!primary)return done(ordinary(w));
  const Group*job=independentJob>=0?w.group(independentJob):nullptr;
  if(!job||job->id==primary->id||job->respawning||job->contested){job=nullptr;for(const auto&g:w.groups)if(g.id!=primary->id&&g.program_available&&g.route_known&&!g.respawning&&!g.contested&&g.travel>=0&&g.to_bank>=0&&
    (!job||g.travel<job->travel||(g.travel==job->travel&&g.id<job->id))){job=&g;}
   independentJob=job?job->id:-1;
  }
  if(ren&&w.self.id!=ren->id){
   if(p10_b_local::safe_custody(w,*primary,*ren)&&actualCustody!=primary->id){++serviceTurns;actualCustody=primary->id;Directive d;d.task=Task::release;d.group=primary->id;d.template_id=93;d.reason="actual independently safe Ren holder releases the priced service worker";return done(d);}
   int healSlot=-1;if(w.self.type==Champion::willow&&w.self.can_cast&&ren->hp<ren->max_hp)for(int slot=0;slot<2;++slot)if(w.self.abilities[slot].heal&&w.self.abilities[slot].ready&&distance(w.self.position,ren->position)<=w.self.abilities[slot].range)healSlot=slot;
   if(healSlot>=0){
    double nextThreat=p10::phase_damage(*primary,ren->position);for(const auto&other:w.groups)if(other.id!=primary->id&&!other.cleared&&!other.respawning)nextThreat+=p10::phase_damage(other,ren->position);
    bool valuable=ren->bag>0||ren->hp<=2*nextThreat+std::max(1.,ren->max_hp*.1);
    if(job&&usable(*job)&&distance(w.self.position,job->position)<=w.self.range+2){const auto independent=turn.prove(*job);
     if(independent.feasible){const int cycle=independent.clear_turns+w.capture_turns+1+job->to_bank+w.ticks(job->gems);foregoneJobGems=cycle>0?int((job->gems+cycle-1)/cycle):int(job->gems);}
    }
    // Service is one paid owner turn away from a named complete independent
    // program. Missing HP alone does not manufacture an indefinite healer job.
    if(valuable){++serviceTurns;Directive d;d.task=Task::heal;d.actor=ren->id;d.cast_slot=healSlot;d.group=primary->id;d.template_id=92;d.reason="pay one current lifesaving/loaded-holder service turn, pricing the named independent job";return done(d);}
   }
   return done(job?turn.work(*job,"released reaction service worker's named independent mobile production",90):Directive{Task::recover});
  }
  if(w.self.type==Champion::ren&&usable(*primary)&&w.self.can_attack&&w.self.shield<=0&&w.geometry_complete&&w.observation_complete&&w.enemies.empty()&&turn.budget.fuel_ok()){
   const Monster*weak=nullptr,*other=nullptr;int living=0;bool observed=true;
   for(const auto&m:primary->monsters){observed&=m.observed;living+=m.alive&&m.hp>0;if(m.alive&&m.hp>0&&m.strength>0&&m.stun<=0&&m.hp<=.2*w.self.focus&&strictly_nearest(w,m,w.self))weak=&m;}
   if(weak)for(const auto&m:primary->monsters)if(m.alive&&m.id!=weak->id&&m.hp>0&&m.hp<=w.self.strength*(w.self.monster_emblem?1.1:1.)&&distance(w.self.position,m.position)<=w.self.range)other=&m;
   const double minimum=(weak?weak->strength*(w.self.jungle_emblem?.7:1.):0)+neighbors(w,primary->id,w.self.position)*3+std::max(1.,w.self.max_hp*.1);
   if(observed&&living==2&&weak&&other&&w.self.hp>minimum){++turn.budget.spent;Directive d;d.task=Task::attack;d.group=primary->id;d.monster=other->id;d.reserve_reset=d.strict_tank=true;d.safety_verified=true;d.safety_round=d.valid_round=w.round;d.minimum_start_hp=minimum;d.template_id=91;
    d.reason="one actual positive strictly-nearest reaction pays the weak attacker while direct work kills the other";return done(d);}
  }
  return done(turn.work(*primary,"reaction organization's real mobile approach/prepare/clearance prefix",90));
 }
};
}
