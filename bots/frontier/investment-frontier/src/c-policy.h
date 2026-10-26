#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-11-16.h"
namespace p10_c11 {
using namespace p10;
struct CPolicy:p10_b11::BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=(std::uint64_t{1}<<unsigned(p10::Champion::ryth))|(std::uint64_t{1}<<unsigned(p10::Champion::stellar));
 p10_c::OwnedOrder capital{};int lastFundedPayout=-1;bool mixedPacket=false;
 Directive step(const World&w,Arm arm){
  economy.consume(w);traces=0;
  if(w.self.channel!=Channel::none||w.capture_active)return BPolicy::step(w,arm);
  if(p10_c::winning_bag(w))return bank("regional investor liquidates an actual winning bag");
  auto pending=capital.poll(w,"regional owned reinvestment visit, with actual stock before renewed labor",2110);if(pending.task!=Task::independent)return pending;
  if(order.feasible)return BPolicy::step(w,arm);
  const bool investor=w.self.type==Champion::ryth||w.self.type==Champion::stellar;
  const bool boundary=p10_c::legal_shop(w)||economy.lastPayout>lastFundedPayout;
  if(investor&&boundary&&!p10_c::near_awakening(w)&&w.remaining()>160){
   bool opened=false;const int m=std::max(0,5-w.self.inventory[int(Item::monster_strength)]),j=std::max(0,5-w.self.inventory[int(Item::jungle_health)]);
   const int affordable=w.shard_cost>0?std::max(0,w.own_gold/w.shard_cost):0;
   if(!w.self.monster_emblem&&m)opened=capital.open(w,{{Item::monster_strength,std::min(m,affordable)}});
   else if(!w.self.jungle_emblem&&j)opened=capital.open(w,{{Item::jungle_health,std::min(j,affordable)}});
   else if(w.self.type==Champion::stellar)opened=capital.open(w,{{Item::focus,5}});
   else {double pressure=0;for(const auto&g:w.groups)if(g.observed&&g.complete)pressure=std::max(pressure,g.incoming);
    mixedPacket=pressure*3>w.self.max_hp*.25||w.self.hp<w.self.max_hp*.5;
    if(mixedPacket){const bool reverse=(w.self.inventory[int(Item::strength)]+w.self.inventory[int(Item::health)])/5%2;opened=capital.open(w,{{Item::health,reverse?3:2},{Item::strength,reverse?2:3}});}
    else opened=capital.open(w,{{Item::strength,5}});
   }
   if(opened){lastFundedPayout=economy.lastPayout;return capital.poll(w,"owned regional packet grows the next solo circuit and released-helper capacity",2111);}
  }
  // Ren and other released workers keep the B organization and private income;
  // no investor spends their wallets or presumes their future production.
  return BPolicy::step(w,arm);
 }
};
}
