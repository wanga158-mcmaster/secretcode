#pragma once
#include "policy.h"
#include "../../common/src/economic-observation.h"
namespace p10_b16 {
using namespace p10;
enum class Organization {home,hit_home,hit_replacement,replacement_only};
enum class Cause {home_acquisition,replacement_acquisition,conditional_denial,proven_denial,committed_acquisition};
struct Schedule {
 Organization kind=Organization::home;int source=-1,bankAt=0,renewal=0,nominalReentry=0,adverseReentry=0,baselineReentry=0;
 Gems ownBank=0;double displaced=0,ownService=0,nominalDenied=0;
 bool feasible=false,failedHitSafe=false,unknownSubstitutes=true;
};
struct BPolicy {
 static constexpr bool observe_enemy_exits=true;
 p10_b::Economy economy{};interdiction::AttemptBudget attempts{};std::array<int,2>appointment{-1,-1};std::array<Schedule,4>schedules{};
 int source=-1,target=-1,traces=0,acquiredRound=-1;bool replacementOwned=false,interdictionWitness=false;Organization selected=Organization::home;
 Cause cause=Cause::home_acquisition;bool denialConditional=false;int acquisitionSelections=0,denialSelections=0,replacementRefusals=0;
 int actualAcquisitionSource=-1,actualAcquisitionPayout=-1,actualAcquisitionBank=-1;
 void observe_acquisition(const World&){
  // A committed appointment is not a payout, bank, enemy death or denial.
  if(replacementOwned&&economy.lastSource==source&&economy.lastPayout>=acquiredRound){actualAcquisitionSource=source;actualAcquisitionPayout=economy.lastPayout;}
  if(replacementOwned&&actualAcquisitionSource==source&&actualAcquisitionPayout>=acquiredRound&&economy.lastBank>=actualAcquisitionPayout){actualAcquisitionBank=economy.lastBank;replacementOwned=false;}
 }
 static Gems personal_share(const World&w,const Group&g){
  int holders=1,lowest=w.self.id;for(const auto&a:w.allies)if(a.id!=w.self.id&&a.visible&&a.alive&&p10_b::on_region(a,g)){++holders;lowest=std::min(lowest,a.id);}
  return g.gems/holders+(lowest==w.self.id?g.gems%holders:0);
 }
 static bool better_acquisition(const Schedule&a,const Schedule&b){
  return a.feasible&&(!b.feasible||a.ownService>b.ownService+1e-9||(std::abs(a.ownService-b.ownService)<=1e-9&&(a.bankAt<b.bankAt||(a.bankAt==b.bankAt&&a.ownBank>b.ownBank))));
 }
 Directive step(const World&w,Arm arm){
  economy.consume(w);observe_acquisition(w);traces=0;interdictionWitness=denialConditional=false;auto budget=p10_mobile::budget_for(w);const auto options=p10_b::single_options();
  if(w.self.channel!=Channel::none)return wait("interdiction appointment consumes receipts before preserving real service");
  if(w.self.bag>0&&(economy.lastSource==source||economy.batch>=2||w.self.bag>=24||p10_b::altar_trip(w)<=3))return bank("producer closes its actual named acquisition receipt");
  if(w.capture_active)return wait("source duty retains actual uncontested capture");
  if(const auto*g=w.group(source);g&&usable(*g)&&g->cleared&&personal_share(w,*g)>0)return farm(g->id,"committed source receives actual custody before any capital or new role");
  const Actor*capability=nullptr;for(const auto&e:w.enemies)if(e.visible&&e.alive&&((e.type==Champion::prism&&e.bag>0)||e.type==Champion::cassi||e.type==Champion::issac)){capability=&e;break;}
  // The exact same bounded interception selection/executor is used by both.
  if(w.self.type==Champion::ren||w.self.type==Champion::vendetta){
   if(attempts.active){const Actor*current=w.actor(attempts.target);if(attempts.current(w.round,current&&current->visible&&current->alive,w.safe_now))return interdiction::Policy::intercept(w,*current);}
   if(capability&&w.safe_now&&distance(w.self.position,capability->position)<=4&&w.self.hp>capability->strength*3+w.self.max_hp*.2&&capability->hp<=w.self.strength*3+w.self.focus&&attempts.begin(w.round,capability->id))return interdiction::Policy::intercept(w,*capability);
  }
  const Group*home=nullptr,*replacement=nullptr;
  auto opening=[&](const Group&g){return 1+g.travel+std::max(1,g.clear_turns)+w.capture_turns+1+g.to_bank+w.ticks(w.self.bag+personal_share(w,g))+g.to_bank;};
  for(const auto&g:w.groups){if(!budget.fuel_ok())return wait("source catalogue defers before the same live action reserve");if(!p10_b::work_source(g))continue;
   if(!home||personal_share(w,g)*opening(*home)>personal_share(w,*home)*opening(g))home=&g;
   if(capability&&distance(capability->position,g.position)<=8&&(!replacement||distance(capability->position,g.position)<distance(capability->position,replacement->position)))replacement=&g;
  }
  if(!home){if(w.self.bag>0)return bank();return w.safe_now?Directive{}:Directive{Task::recover};}
  if(replacementOwned){if(const auto*g=w.group(source);g&&p10_b::work_source(*g))replacement=g;else{replacementOwned=false;++replacementRefusals;}}
  appointment={home->id,replacement?replacement->id:-1};
  auto prove=[&](const Group&g){if(!g.cleared)return p10_mobile::mobile_work(w,g,budget,options);p10_mobile::Result out;out.actor=w.self.id;out.round=w.round;out.feasible=true;out.position=w.self.position;out.hp=w.self.hp;out.first=farm(g.id,"actual cleared acquisition requires fresh SDK entry/custody");return out;};
  auto homeWork=prove(*home);auto replacementWork=homeWork;
  if(replacement&&replacement->id!=home->id&&budget.spent<2)replacementWork=prove(*replacement);
  traces=budget.spent;
  bool crew=false;if(capability)for(const auto&a:w.allies)crew|=a.visible&&a.alive&&(a.type==Champion::ren||a.type==Champion::vendetta)&&distance(a.position,capability->position)<=4;
  const bool hit=capability&&crew&&capability->hp<=capability->max_hp*.4;
  int substitute=100000;const bool unknown=true; // Enemy gold/other workers remain unresolved in every actual frame.
  if(capability)for(const auto&g:w.groups){if(!budget.fuel_ok())return wait("re-entry algebra defers before the common live reserve");if((!replacement||g.id!=replacement->id)&&g.observed&&g.complete&&g.route_known){const int arrival=w.respawn_turns+distance(capability->position,g.position)+std::max(1,g.clear_turns)+w.capture_turns+1;substitute=std::min(substitute,arrival);}}
  for(int id=0;id<4;++id){auto&s=schedules[id];s={};s.kind=static_cast<Organization>(id);const bool take=id>=2,hasHit=id==1||id==2;const Group*g=take?replacement:home;const auto&proof=take?replacementWork:homeWork;
   if(!g||!proof.feasible||(hasHit&&!hit))continue;s.source=g->id;s.ownBank=personal_share(w,*g);s.bankAt=proof.clear_turns+w.capture_turns+1+std::max(0,g->to_bank)+w.ticks(w.self.bag+s.ownBank);
   // First receipt -> personal bank -> named return to home work. This value
   // ranks a represented complete service cycle; it is not fabricated future
   // repeated payouts or a claim that the unseen bank route is already safe.
   s.renewal=s.bankAt+std::max(0,home->to_bank);s.ownService=double(s.ownBank)/std::max(1,s.renewal);
   s.failedHitSafe=proof.feasible;s.feasible=s.ownBank>0&&s.renewal<=std::min(160,w.remaining());s.unknownSubstitutes=unknown;
   if(take&&capability){const int enemyTravel=distance(capability->position,g->position),work=std::max(1,g->clear_turns)+w.capture_turns+1;
    s.baselineReentry=w.respawn_turns+enemyTravel+work;
    const int respawnAt=proof.clear_turns+w.capture_turns+1+std::max(0,g->respawn);
    const int blockedCompletion=std::max(w.respawn_turns+enemyTravel,respawnAt)+work;
    s.nominalReentry=std::min(substitute,blockedCompletion);s.adverseReentry=unknown?0:s.nominalReentry;
    const int delayed=std::max(0,s.nominalReentry-s.baselineReentry);
    s.nominalDenied=double(g->gems)*delayed/std::max(1,work+enemyTravel);
   }
  }
  const int homeId=hit?1:0,replacementId=hit?2:3;int chosen=-1;
  if(replacement&&!schedules[replacementId].feasible)++replacementRefusals;
  if(schedules[homeId].feasible)chosen=homeId;
  if(replacement&&better_acquisition(schedules[replacementId],chosen<0?Schedule{}:schedules[chosen]))chosen=replacementId;
  // Strong control owns the entire independently feasible acquisition menu.
  // Active begins at the same acquisition optimum and can depart from it only
  // for explicitly priced incremental re-entry delay after the SAME hit.
  cause=chosen>=2?Cause::replacement_acquisition:Cause::home_acquisition;
  if(arm==Arm::active&&chosen>=0&&chosen<2&&hit&&replacement&&replacement->id!=home->id){auto&r=schedules[2];const auto&base=schedules[chosen];
   r.displaced=std::max(0.,base.ownService*r.renewal-double(r.ownBank));
   if(r.feasible&&r.nominalReentry>r.baselineReentry+8&&r.nominalDenied>r.displaced+1){chosen=2;cause=r.unknownSubstitutes?Cause::conditional_denial:Cause::proven_denial;denialConditional=r.unknownSubstitutes;interdictionWitness=!r.unknownSubstitutes;++denialSelections;}
  }
  if(replacementOwned&&replacement){
   if(schedules[replacementId].feasible){chosen=replacementId;cause=Cause::committed_acquisition;}
   else{replacementOwned=false;++replacementRefusals;}
  }
  if(chosen>=0){selected=static_cast<Organization>(chosen);const bool take=chosen>=2;const auto&proof=take?replacementWork:homeWork;const Group*g=take?replacement:home;source=g->id;
   if(take&&!replacementOwned){replacementOwned=true;acquiredRound=w.round;target=capability?capability->id:-1;++acquisitionSelections;}
   return p10_b::witnessed(w,proof,1610+chosen,cause==Cause::conditional_denial?"conditional nominal re-entry appointment; adverse substitutes stay unresolved":take?"same-menu complete own acquisition service, no denial credit":"best complete home acquisition service under the same physical menu");
  }
  if(w.self.bag>0)return bank("actual owned bag closes while every represented acquisition is refused");
  if(!w.safe_now||w.self.hp<w.self.max_hp*.25)return {Task::recover};return p10_b::approach(*home,1615,"no complete represented acquisition; honest home reveal before fresh replan");
 }
};
}
