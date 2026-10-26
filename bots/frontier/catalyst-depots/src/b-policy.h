#pragma once
#include "policy.h"
#include "../../common/src/physical-turn.h"
#ifndef P10_DEPOT_CALLER_TRACE
#define P10_DEPOT_CALLER_TRACE 0
#endif
namespace p10_depots {
struct LoadedPrice {
 int foregoneGems=0,foregoneGold=0,producerTurns=0,loadedBankTicks=0,savedTicks=0;
 bool worthwhile=false;
};
struct BPolicy:Policy {
#if P10_DEPOT_CALLER_TRACE
 int caller_b_entered=0,caller_b_bank=-1;
#endif
 int depot=-1,producerJob=-1,attemptRound=-1,attemptGroup=-1,attemptMonster=-1,confirmedReset=-1,bankReceipt=-1;
 int manufactureRound=-1,manufactureGroup=-1;double expectedDamage=0,consumerHit=0;
 bool ownIncome=false,invested=false;LoadedPrice price{};
 void receipts(const p10::World&w){
  using namespace p10;
  if(w.own_payout)ownIncome=true;if(w.own_purchase)invested=true;
  if(manufactureRound>=0&&w.round>manufactureRound){
   bool hit=false,stock=false;for(const auto&r:w.receipts)if(r.kind==p10::Receipt::Kind::attack&&r.actor==w.self.id&&r.round==manufactureRound&&r.group==manufactureGroup&&r.amount+1e-6>=expectedDamage)hit=true;
   const auto*g=w.group(manufactureGroup);if(g&&usable(*g)&&harmless(*g))for(const auto&m:g->monsters)stock|=m.alive&&m.hp>0&&m.hp<=consumerHit;
   if(hit&&stock)depot=manufactureGroup;manufactureRound=-1;
  }
  if(attemptRound>=0&&w.round>attemptRound){
   bool hit=false,dead=false;for(const auto&r:w.receipts)if(r.kind==p10::Receipt::Kind::attack&&r.actor==w.self.id&&r.round==attemptRound&&r.group==attemptGroup&&r.amount+1e-6>=expectedDamage)hit=true;
   const auto*g=w.group(attemptGroup);if(g&&g->observed&&g->complete)for(const auto&m:g->monsters)if(m.id==attemptMonster)dead=!m.alive||m.hp<=0;
   if(hit&&dead){confirmedReset=attemptRound;depot=attemptGroup;}attemptRound=-1;
  }
  if(w.own_bank_completed&&confirmedReset>=0)bankReceipt=w.round;
 }
 p10::Directive step(const p10::World&w,p10::Arm){
#if P10_DEPOT_CALLER_TRACE
  caller_b_entered=1;caller_b_bank=-1;
#endif
  using namespace p10;receipts(w);p10_b_local::Turn turn(w);
  auto done=[&](Directive d){fresh_trajectories=turn.budget.spent;return d;};
  if(w.self.channel!=Channel::none||w.capture_active)return done(wait("loaded-return depot preserves actual capture/channel"));
  const Altar*bankPoint=nullptr;for(const auto&a:w.altars)if(a.route_known&&!a.enemy_lock&&a.travel>=0&&(!bankPoint||a.travel<bankPoint->travel))bankPoint=&a;
#if P10_DEPOT_CALLER_TRACE
  caller_b_bank=bankPoint?bankPoint->id:-1;
#endif
  if(w.self.type==Champion::vendetta&&w.self.bag>0&&bankPoint&&bankPoint->travel>1&&w.self.cooldown[1]>1&&w.self.can_attack&&w.self.can_cast&&quiet(w,-1)){
   for(const auto&g:w.groups)if(usable(g)&&harmless(g)&&distance(w.self.position,g.position)+distance(g.position,bankPoint->approach)<=distance(w.self.position,bankPoint->approach)+1)
    for(const auto&m:g.monsters)if(m.alive&&m.hp>0&&m.hp<m.max_hp&&m.hp<=w.self.strength*(w.self.monster_emblem?1.1:1.)&&distance(w.self.position,m.position)<=w.self.range){
     bool substitute=false;for(const auto&other:w.groups)if(other.id!=g.id)for(const auto&target:other.monsters)if(target.observed&&target.alive&&target.hp<=w.self.strength&&distance(w.self.position,target.position)<=w.self.range)substitute=true;
     if(substitute)continue;
     Directive d;d.task=Task::attack;d.group=g.id;d.monster=m.id;d.cast_slot=1;d.destination=bankPoint->approach;d.reserve_reset=d.strict_tank=true;
     d.safety_verified=true;d.safety_round=d.valid_round=w.round;d.minimum_start_hp=1;d.template_id=61;d.reason="fresh loaded-return stock kill buys a nonredundant immediate bank dash";
     attemptRound=w.round;attemptGroup=g.id;attemptMonster=m.id;expectedDamage=m.hp;return done(d);
    }
  }
  if(w.self.bag>0)return done(bank("bank the actual loaded-return job before liquidating its stock"));
  if(bankReceipt>=0&&depot>=0){const auto*g=w.group(depot);if(g&&usable(*g)&&!g->respawning)return done(turn.work(*g,"liquidate the consumed return depot after observed bank receipt",63));depot=-1;}
  if(ownIncome&&!invested&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi))return done(buy(Item::strength,1,"depot supplier invests only its own existing wallet"));
  const Actor*customer=nullptr;for(const auto&a:w.allies)if(a.visible&&a.alive&&a.type==Champion::vendetta&&a.bag>0&&a.cooldown[1]>1)customer=&a;
  const Group*stock=nullptr,*job=nullptr;const Monster*prepare=nullptr;
  if(customer&&ownIncome&&bankPoint&&quiet(w,-1))for(const auto&g:w.groups)if(usable(g)&&harmless(g)&&g.travel<=1&&
      distance(customer->position,g.position)+distance(g.position,bankPoint->approach)<=distance(customer->position,bankPoint->approach)+1){
   const double damage=w.self.strength*(w.self.monster_emblem?1.1:1.);
   for(const auto&m:g.monsters)if(m.alive&&m.hp>damage&&m.hp-damage<=customer->strength*(customer->monster_emblem?1.1:1.)&&distance(w.self.position,m.position)<=w.self.range){stock=&g;prepare=&m;break;}
   if(stock)break;
  }
  for(const auto&g:w.groups)if((!stock||g.id!=stock->id)&&g.id!=depot&&g.route_known&&g.program_available&&!g.respawning&&!g.contested&&g.travel>=0&&g.to_bank>=0&&
     (!job||g.travel<job->travel||(g.travel==job->travel&&g.id<job->id)))job=&g;
  if(stock&&prepare&&job&&w.self.can_attack&&distance(w.self.position,job->position)<=w.self.range+2){
   const auto independent=turn.prove(*job);price={};
   if(independent.feasible){
    price.foregoneGems=int(stock->gems+job->gems);price.foregoneGold=int(stock->gold+job->gold);
    price.producerTurns=1+independent.clear_turns+w.capture_turns+1+job->to_bank+w.ticks(job->gems);
    price.loadedBankTicks=distance(customer->position,bankPoint->approach)+w.ticks(customer->bag);
    price.savedTicks=std::min(2,customer->cooldown[1]-1);
    // Charge the complete withheld depot AND the complete independent producer
    // program. A small bag or a merely speculative future customer cannot pay.
    price.worthwhile=customer->bag>2*price.foregoneGems&&price.savedTicks>1&&price.loadedBankTicks+1<=w.remaining()&&price.producerTurns<=w.remaining();
    if(price.worthwhile){Directive d;d.task=Task::attack;d.group=stock->id;d.monster=prepare->id;d.reserve_reset=d.strict_tank=true;d.safety_verified=true;d.safety_round=d.valid_round=w.round;d.minimum_start_hp=1;d.template_id=62;
     d.reason="manufacture loaded-return service after pricing the entire withheld producer/depot output";
     manufactureRound=w.round;manufactureGroup=stock->id;expectedDamage=std::min(prepare->hp,w.self.strength*(w.self.monster_emblem?1.1:1.));consumerHit=customer->strength*(customer->monster_emblem?1.1:1.);producerJob=job->id;return done(d);}
    return done(p10_b_local::Turn::first(independent,"independent production beats the fully priced loaded-return service",60));
   }
  }
  if(producerJob>=0){const auto*g=w.group(producerJob);if(g&&usable(*g))return done(turn.work(*g,"supplier leaves physical stock for its named independent income",60));}
  return done(job?turn.work(*job,"loaded-return depot supplier's mobile cold-start/renewal production",60):ordinary(w));
 }
};
}
