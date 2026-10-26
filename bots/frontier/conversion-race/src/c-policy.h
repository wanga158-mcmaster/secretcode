#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-11-16.h"
namespace p10_c13 {
using namespace p10;
struct CPolicy:p10_b13::BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=(std::uint64_t{1}<<unsigned(p10::Champion::marcella));
 p10_c::OwnedOrder capital{};int packetSource=-1,projectedBranches=0,projectedVisit=0;
 Directive step(const World&w,Arm arm){
  economy.consume(w);traces=0;
  if(w.self.channel!=Channel::none||w.capture_active)return BPolicy::step(w,arm);
  if(p10_c::winning_bag(w))return bank("actual winning channel precedes any recurrent development packet");
  auto pending=capital.poll(w,"funded race development packet precedes later deposits",2130);if(pending.task!=Task::independent)return pending;
  if(developing||collector>=0)return BPolicy::step(w,arm);
  const Group*route=nullptr;auto budget=p10_mobile::budget_for(w);
  for(const auto&g:w.groups)if(p10_b::work_source(g)){if(!budget.fuel_ok())return BPolicy::step(w,arm);
   if(!route||g.gems*std::max(1,cycle_ticks(w,*route))>route->gems*std::max(1,cycle_ticks(w,g)))route=&g;}
  if(route&&!p10_c::near_awakening(w)&&w.self.bag==0&&w.remaining()>160){
   const int missing=std::max(0,5-w.self.inventory[int(Item::monster_strength)]);const Item item=w.self.monster_emblem?Item::strength:Item::monster_strength;const int count=w.self.monster_emblem?5:missing;
   const int units=item==Item::strength?p10_c::power_units(w.self.inventory[int(item)],count):count;
   const double currentMult=w.self.monster_emblem?1.1:1.,futureMult=1.1;
   int before=0,after=0;for(const auto&m:route->monsters)if(m.alive&&m.hp>0){if(!budget.fuel_ok())return BPolicy::step(w,arm);const int a=int(std::ceil(m.hp/std::max(1.,w.self.strength*currentMult))),b=int(std::ceil(m.hp/std::max(1.,(w.self.strength+units*w.self.strength_growth)*futureMult)));
    // Max attack-depth preserves Varg's shared splash branches. This is a
    // stat regime screen, never a claim of saved turns with spells/resets.
    before=std::max(before,a);after=std::max(after,b);}
   projectedBranches=std::max(0,before-after);projectedVisit=p10_c::legal_shop(w)?count:w.recall_turns+1+std::max(0,route->to_spawn)+count;
   const int period=std::max(1,std::max(route->respawn,cycle_ticks(w,*route)));const int cycles=std::max(0,(w.remaining()-projectedVisit)/period);
   // Require a different attack-depth regime with several source renewals
   // remaining. Actual current-gear campaigns still choose work afterward;
   // neither arithmetic nor an unserviceable high-gem camp certifies a win.
   if(count>0&&projectedBranches>0&&cycles>=3&&capital.open(w,{{item,count}})){packetSource=route->id;return capital.poll(w,"funded multi-shard race regime precedes actual later deposit campaigns",2131);}
  }
  return BPolicy::step(w,arm);
 }
};
}
