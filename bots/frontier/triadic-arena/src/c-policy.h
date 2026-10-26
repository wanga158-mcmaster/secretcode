#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-11-16.h"
namespace p10_c12 {
using namespace p10;
struct CPolicy:p10_b12::BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=std::uint64_t{0};
 p10_c::OwnedOrder capital{};int servicedBank=-1;
 Directive step(const World&w,Arm arm){
  economy.consume(w);traces=0;
  if(w.self.channel!=Channel::none||w.capture_active)return BPolicy::step(w,arm);
  if(p10_c::winning_bag(w))return bank("district banks actual terminal inventory before future combat capital");
  auto pending=capital.poll(w,"district-funded capital service returns an actually stronger border worker",2120);if(pending.task!=Task::independent)return pending;
  const auto*encounter=w.group(source);const bool fighting=encounter&&district[1]==source&&encounter->living&&p10_c::near_awakening(w);
  const bool boundary=p10_c::legal_shop(w)||(economy.lastBank>servicedBank&&w.self.bag==0);
  if(boundary&&!fighting&&!p10_c::near_awakening(w)&&w.remaining()>160){
   const bool ren=w.self.type==Champion::ren;const Item first=ren?Item::monster_focus:Item::monster_strength;const int m=std::max(0,5-w.self.inventory[int(first)]),j=std::max(0,5-w.self.inventory[int(Item::jungle_health)]);bool opened=false;
   if(!w.self.monster_emblem&&m)opened=capital.open(w,{{first,m}});
   else if(!w.self.jungle_emblem&&j)opened=capital.open(w,{{Item::jungle_health,j}});
   else opened=capital.open(w,{{ren?Item::focus:Item::strength,5}});
   if(opened){servicedBank=economy.lastBank;return capital.poll(w,ren?"personal district receipts fund recurring Ren control/burst growth":"personal district receipts fund recurring direct/splash growth",2121);}
  }
  return BPolicy::step(w,arm);
 }
};
}
