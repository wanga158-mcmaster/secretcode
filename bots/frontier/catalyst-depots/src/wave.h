#pragma once
#include "../../common/src/combat.h"
namespace p10_depots {
struct Policy:LegacyPolicy {
 int variant=0,fresh_trajectories=0,manufactured=-1,preparerJob=-1,consumed=-1,lastOwnBank=-1;
 int preparationRound=-1,preparationGroup=-1;double preparationDamage=0,consumerDamage=0;
 using FuelReader=int(*)();FuelReader fuel_reader=nullptr;
 void set_fuel_reader(FuelReader f){fuel_reader=f;}
 bool budget(const p10::World&w)const{return (fuel_reader?fuel_reader():w.fuel)>w.action_reserve+2500;}
 static bool harmless(const p10::Group&g){for(const auto&m:g.monsters)if(!m.observed||(m.alive&&m.strength>0))return false;return true;}
 static bool quiet(const p10::World&w,int source){
  if(!w.geometry_complete||!w.observation_complete)return false;
  for(const auto&e:w.enemies)if(e.visible&&e.alive&&p10::distance(e.position,w.self.position)<=e.range+2)return false;
  for(const auto&g:w.groups)if(g.id!=source)for(const auto&m:g.monsters)if(m.alive&&m.strength>0&&p10::distance(m.position,w.self.position)<=m.range)return false;
  return true;
 }
 p10::Directive produce(const p10::World&w,const p10::Group&g,const char*why){
  using namespace p10;if(!budget(w)||fresh_trajectories>=2)return ordinary(w);
  const auto stance=distance(w.self.position,g.approach)<=w.self.range?w.self.position:g.approach;
  ++fresh_trajectories;const auto proof=stationary_work(w,g,stance,64);
  if(!proof.feasible)return ordinary(w);
  if(!(stance==w.self.position)){if(!w.safe_now)return {Task::recover};Directive d;d.task=Task::move;d.group=g.id;d.destination=stance;d.reason="depot worker approaches its independent productive appointment";return d;}
  auto d=proof.first;d.reason=why;d.template_id=10;return d;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  using namespace p10;if(variant==1||arm==Arm::control)return LegacyPolicy::step(w,arm);
  fresh_trajectories=0;
  if(preparationRound>=0&&w.round>preparationRound){
   bool receipt=false,ready=false;for(const auto&r:w.receipts)if(r.kind==p10::Receipt::Kind::attack&&r.actor==w.self.id&&r.group==preparationGroup&&r.round==preparationRound&&r.amount+1e-6>=preparationDamage)receipt=true;
   const auto*g=w.group(preparationGroup);if(g&&usable(*g)&&harmless(*g))for(const auto&m:g->monsters)ready|=m.alive&&m.hp>0&&m.hp<=consumerDamage;
   if(receipt&&ready)manufactured=preparationGroup;preparationRound=-1;
  }
  if(pendingRound>=0&&w.round>pendingRound){
   bool attack=false,dead=false;for(const auto&r:w.receipts)if(r.kind==p10::Receipt::Kind::attack&&r.round==pendingRound&&r.actor==w.self.id&&r.group==liquidation&&r.amount>=pendingHp)attack=true;
   const auto*g=w.group(liquidation);if(g&&g->observed&&g->complete)for(const auto&m:g->monsters)if(m.id==pendingMonster)dead=!m.alive||m.hp<=0;
   if(attack&&dead){consumed=liquidation;confirmedUse=pendingRound;}pendingRound=-1;
  }
  if(w.own_payout){bootstrapped=true;firstIncome=w.last_payout_group;if(w.last_payout_group!=manufactured)lastJob=w.last_payout_group;}
  if(w.own_purchase)lastPurchase=w.round;
  if(w.own_bank_completed){lastOwnBank=w.round;if(consumed>=0&&confirmedUse>=0)bankAfterUse=w.round;}
  if(w.self.channel!=Channel::none||w.capture_active)return wait("preserve personally witnessed depot custody/service");
  if(w.self.bag>0&&w.self.type!=Champion::vendetta)return bank("preparer secures its independent bag");
  if(bootstrapped&&lastPurchase<0&&w.own_gold>=w.shard_cost&&(w.self.at_spawn||w.self.type==Champion::tavi))return buy(Item::strength,1,"depot capital uses only this preparer's actual wallet");
  const Actor*visibleConsumer=nullptr;for(const auto&a:w.allies)if(a.visible&&a.alive&&a.type==Champion::vendetta&&a.cooldown[1]>1)visibleConsumer=&a;
  const Group*physicalDepot=nullptr;
  for(const auto&g:w.groups)if(usable(g)&&harmless(g))for(const auto&m:g.monsters)if(m.alive&&m.hp>0){
   bool candidate=w.self.type==Champion::vendetta&&w.self.cooldown[1]>1&&m.hp<m.max_hp&&m.hp<=w.self.strength&&distance(w.self.position,m.position)<=w.self.range;
   const double hit=w.self.strength*(w.self.monster_emblem?1.1:1.);
   candidate|=visibleConsumer&&bootstrapped&&g.travel<=2&&m.hp>hit&&m.hp-hit<=visibleConsumer->strength*(visibleConsumer->monster_emblem?1.1:1.);
   if(candidate&&(!physicalDepot||g.travel<physicalDepot->travel||(g.travel==physicalDepot->travel&&g.id<physicalDepot->id)))physicalDepot=&g;
  }
  const Group*job=nullptr;
  for(const auto&g:w.groups)if(g.id!=manufactured&&g.id!=consumed&&(!physicalDepot||g.id!=physicalDepot->id)&&usable(g)&&g.gems>0&&g.travel>=0&&g.to_bank>=0&&
     (!job||g.travel<job->travel||(g.travel==job->travel&&g.id<job->id)))job=&g;
  // Consumption is a fresh physical proof, independent of another actor's RAM.
  // A damaged harmless monster is real stock; a fresh untouched cluster is not.
  if(w.self.type==Champion::vendetta&&w.self.can_attack&&w.self.can_cast&&w.self.cooldown[1]>1&&quiet(w,-1)){
   Point destination{};int dependent=-1;
   if(w.self.bag>0){for(const auto&a:w.altars)if(a.route_known&&a.travel>1&&(!a.enemy_lock)){destination=a.approach;dependent=-2;break;}}
   else if(job){destination=job->approach;dependent=job->id;}
   if(dependent!=-1)for(const auto&g:w.groups)if(usable(g)&&g.id!=dependent&&harmless(g)&&distance(w.self.position,destination)>1){
    for(const auto&m:g.monsters)if(m.alive&&m.hp>0&&m.hp<m.max_hp&&m.hp<=w.self.strength&&distance(w.self.position,m.position)<=w.self.range){
     if(dependent>=0&&distance(w.self.position,g.position)+distance(g.position,destination)>distance(w.self.position,destination)+2)continue;
     bool substitute=false;if(job)for(const auto&other:job->monsters)if(other.alive&&other.hp<=w.self.strength&&distance(w.self.position,other.position)<=w.self.range)substitute=true;
     if(substitute)continue;
     Directive d;d.task=Task::attack;d.group=g.id;d.monster=m.id;d.cast_slot=1;d.destination=destination;d.reserve_reset=d.strict_tank=true;
     d.safety_verified=true;d.safety_round=d.valid_round=w.round;d.minimum_start_hp=1;d.template_id=11;
     d.reason="reinspect manufactured harmless stock: nonredundant kill then refreshed outward/loaded dash";
     liquidation=g.id;pendingRound=w.round;pendingMonster=m.id;pendingHp=m.hp;return d;
    }
   }
  }
  if(w.self.bag>0)return bank("consumer converts actual production before deferred liquidation");
  if(consumed>=0&&bankAfterUse>=0){const auto*g=w.group(consumed);if(g&&usable(*g)&&!g->respawning)return produce(w,*g,"liquidate the consumed physical depot after banking its dependent job");consumed=-1;}
  if((w.self.type==Champion::marcella||w.self.type==Champion::tavi)&&bootstrapped&&quiet(w,-1)){
   const Actor*consumer=visibleConsumer;
   if(consumer&&job)for(const auto&g:w.groups)if(g.id!=job->id&&usable(g)&&g.travel<=2&&g.monsters.size()<=8&&
      distance(consumer->position,g.position)+distance(g.position,job->position)<=distance(consumer->position,job->position)+2){
    const Monster*prepare=nullptr;bool allObserved=true;int attackers=0;const Monster*lastAttacker=nullptr;
    for(const auto&m:g.monsters){allObserved&=m.observed;if(m.alive&&m.strength>0){++attackers;lastAttacker=&m;}
     const double hit=w.self.strength*(w.self.monster_emblem?1.1:1.);
     if(m.alive&&m.strength==0&&m.hp>hit&&m.hp-hit<=consumer->strength*(consumer->monster_emblem?1.1:1.))prepare=&m;}
    const Monster*target=attackers==1&&lastAttacker->hp<=w.self.strength*(w.self.monster_emblem?1.1:1.)?lastAttacker:attackers==0?prepare:nullptr;
    if(!allObserved||!target)continue;
    if(distance(w.self.position,target->position)>w.self.range){if(w.safe_now){Directive d;d.task=Task::move;d.group=g.id;d.destination=g.approach;d.reason="approach on-route depot manufacturing pad";return d;}continue;}
    if(!w.self.can_attack)continue;
    preparationGroup=g.id;preparationRound=w.round;preparationDamage=std::min(target->hp,w.self.strength*(w.self.monster_emblem?1.1:1.));consumerDamage=consumer->strength*(consumer->monster_emblem?1.1:1.);preparerJob=job->id;
    Directive d;d.task=Task::attack;d.group=g.id;d.monster=target->id;d.reserve_reset=d.strict_tank=true;
    d.safety_verified=true;d.safety_round=d.valid_round=w.round;d.minimum_start_hp=1;d.template_id=12;d.reason="one exact basic leaves harmless physical stock and releases the preparer";return d;
   }
  }
  if(manufactured>=0){const auto*g=w.group(manufactured);if(g&&harmless(*g)&&preparerJob>=0){const auto*independent=w.group(preparerJob);if(independent&&usable(*independent))return produce(w,*independent,"preparer leaves manufactured stock for independent personal income");}}
  if(job)return produce(w,*job,"depot organization's independent cold-start/renewal income");
  return ordinary(w);
 }
};
}
