#pragma once
#include "policy.h"
#include "../../common/src/economic-observation.h"
namespace p10_b11 {
using namespace p10;using p10_b::work_source;
struct BPolicy {
 p10_b::Economy economy{};investment::InvestmentCertificate order{};investment::Counts before{};
 int source=-1,alternate=-1,owner=-1,helper=-1,expires=-1,traces=0;bool invested=false,released=false;
 static double opportunity(const World&w,const Group&g,bool capital){
  const double effort=1+g.travel+std::max(1,g.clear_turns)+w.capture_turns+1+g.to_bank+w.ticks(w.self.bag+g.gems);
  return capital?(g.gold+g.gems*.5)/effort:(g.gems*3+g.gold*.03)/effort;
 }
 Directive step(const World&w,Arm arm){
  economy.consume(w);traces=0;auto budget=p10_mobile::budget_for(w);const auto options=p10_b::single_options();
  if(w.self.channel!=Channel::none)return wait("owned frontier preserves actual channel and consumes receipts first");
  if(w.self.bag>0&&(economy.batch>=2||w.self.bag>=24||p10_b::altar_trip(w)<=3||w.score+w.self.bag>=w.win_gems))return bank("two-receipt owned batch closes at real altar service");
  if(order.feasible){
   const auto now=investment::Policy::inventory(w.self,order.family);const auto got=investment::Counts{now.h-before.h,now.s-before.s,now.f-before.f};
   Item next=Item::none;if(got.h<order.itemCounts.h)next=order.family==investment::Family::monster?Item::monster_health:Item::jungle_health;
   else if(got.s<order.itemCounts.s)next=order.family==investment::Family::monster?Item::monster_strength:Item::jungle_strength;
   else if(got.f<order.itemCounts.f)next=order.family==investment::Family::monster?Item::monster_focus:Item::jungle_focus;
   if(next!=Item::none){if(w.round>expires||w.own_gold<w.shard_cost){order.feasible=false;}else if(w.self.bag>0)return bank("funded owned shop follows actual bag conversion");else if(w.self.at_spawn||w.self.type==Champion::tavi)return buy(next,1,"actual owner wallet completes chosen mixed vector");else return {Task::recover};}
   else {order.feasible=false;invested=true;owner=w.self.id;}
  }
  // Adoption is physical. A helper observes both real gear and sole custody,
  // then proves its own different gold/gem circuit using its own current body.
  for(const auto&a:w.allies)if(a.visible&&a.alive&&(a.type==Champion::ryth||a.type==Champion::stellar)&&a.health_shards>0&&(a.strength_shards>0||a.focus_shards>0)){
   const Group*held=nullptr;for(const auto&g:w.groups)if(g.cleared&&p10_b::on_region(a,g))held=&g;
   if(held)for(const auto&g:w.groups)if(g.id!=held->id&&work_source(g)){
    auto r=p10_mobile::mobile_work(w,g,budget,options);traces=budget.spent;if(!r.feasible)break;alternate=g.id;released=true;
    if(p10_b::on_region(w.self,*held)){Directive d;d.task=Task::release;d.group=held->id;d.template_id=1113;d.reason="actual improved owner holds while helper takes independently witnessed alternate";return d;}
    return p10_b::witnessed(w,r,1114,"helper renews its own distinct gold/gem circuit");
   }
  }
  const bool capital=w.self.health_shards+w.self.strength_shards+w.self.focus_shards<5;
  const Group*job=nullptr,*other=nullptr;for(const auto&g:w.groups)if(work_source(g)){
   if(!job||opportunity(w,g,capital)>opportunity(w,*job,capital)){other=job;job=&g;}else if(!other||opportunity(w,g,!capital)>opportunity(w,*other,!capital))other=&g;
  }
  if(!job){if(w.self.bag>0)return bank();return w.safe_now?Directive{}:Directive{Task::recover};}
  source=job->id;alternate=other?other->id:-1;if(job->cleared)return farm(job->id,"actual frontier custody before new investment");
  auto current=p10_mobile::mobile_work(w,*job,budget,options);traces=budget.spent;
  const Actor*partner=nullptr;for(const auto&a:w.allies)if(a.visible&&a.alive&&distance(a.position,job->position)<=8){partner=&a;break;}
  if(partner&&other&&(current.feasible||current.failure==p10_mobile::Failure::health)&&w.own_gold>=2*w.shard_cost&&budget.spent<2&&(w.self.type==Champion::ryth||w.self.type==Champion::stellar)){
   investment::ThresholdProgram p;p.source=job->id;p.owner=w.self.id;p.helper=partner->id;p.independentSource=other->id;p.observed=p.helperVisible=p.independentFeasible=true;
   p.requiredHealth=w.self.max_hp;p.requiredStrength=w.self.strength;p.requiredFocus=w.self.focus;p.approach=job->travel;p.clear=std::max(1,job->clear_turns);p.hold=w.capture_turns+1;p.bankTravel=job->to_bank;p.tribute=w.ticks(job->gems);p.renewal=other->travel+other->to_bank+w.ticks(other->gems);p.shopDetour=w.self.at_spawn?0:std::max(0,w.to_spawn)+w.recall_turns+1;
   auto all=investment::catalogue(w.self,w.own_gold,w.shard_cost,investment::Policy::progress(w.self,investment::Family::monster),investment::Policy::progress(w.self,investment::Family::jungle));
   investment::InvestmentCertificate candidate;double bestWork=1e30;bool screened=true;
   investment::InvestmentCertificate base;base.health=w.self.max_hp;base.strength=w.self.strength;base.focus=w.self.focus;const double baseWork=investment::screened_work(w.self,*job,base);
   for(auto x:all){if(!budget.fuel_ok()){screened=false;break;}if(x.ownCost<=0||(arm==Arm::control&&!investment::common_bundle(x.itemCounts)))continue;x=investment::certify(x,p);if(!x.feasible)continue;
    const double work=investment::screened_work(w.self,*job,x);if(x.health<=investment::screened_loss(w.self,*job,x)+x.health*.15||(work>=baseWork&&x.health<=w.self.max_hp))continue;
    if(!candidate.feasible||work<bestWork||(work==bestWork&&x.ownCost<candidate.ownCost)){candidate=x;bestWork=work;}
   }
   if(!screened)candidate.feasible=false;
   if(candidate.feasible&&candidate.ownCost>0){World future=w;future.self.max_hp=candidate.health;future.self.hp=candidate.health;future.self.strength=candidate.strength;future.self.focus=candidate.focus;
    future.self.monster_emblem|=candidate.family==investment::Family::monster&&candidate.exactPassive;future.self.jungle_emblem|=candidate.family==investment::Family::jungle&&candidate.exactPassive;
    auto funded=p10_mobile::mobile_work(future,*job,budget,options);traces=budget.spent;
    // The second trace is a conditional post-shop regime, never permission to
    // attack with unowned stats. Require the three-cycle cadence to pay detour.
    if(funded.feasible&&(!current.feasible||3*(current.clear_turns-funded.clear_turns)>p.shopDetour)){
     order=candidate;before=investment::Policy::inventory(w.self,candidate.family);helper=partner->id;expires=w.round+96;
     if(w.self.bag>0)return bank("real bag funds future owned frontier visit");
     if(w.self.at_spawn)return buy(candidate.itemCounts.h?(candidate.family==investment::Family::monster?Item::monster_health:Item::jungle_health):candidate.itemCounts.s?(candidate.family==investment::Family::monster?Item::monster_strength:Item::jungle_strength):(candidate.family==investment::Family::monster?Item::monster_focus:Item::jungle_focus),1,"minimal actual owned mixed investment changes later labor cadence");
     return {Task::recover};
    }
   }
  }
  if(current.feasible)return p10_b::witnessed(w,current,1110,capital?"owned capital circuit uses mobile current-gear clearance":"owned gem circuit renews before optional personal shop");
  if(other&&budget.spent<2){auto r=p10_mobile::mobile_work(w,*other,budget,options);traces=budget.spent;if(r.feasible)return p10_b::witnessed(w,r,1111,"alternate independent source preserves actual first income");}
  return w.safe_now?p10_b::approach(*job,1112,"approach/reveal actual chosen circuit without speculative attack"):Directive{Task::recover};
 }
};
}
