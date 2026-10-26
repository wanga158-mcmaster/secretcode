#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-11-16.h"
namespace p10_c14 {
using namespace p10;
struct CPolicy:p10_b14::BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=(std::uint64_t{1}<<unsigned(p10::Champion::cassi))|(std::uint64_t{1}<<unsigned(p10::Champion::issac));
 p10_c::OwnedOrder capital{};pair::Recipient capitalRecipient=pair::Recipient::cassi;int servicedPayout=-1;
 static pair::Recipient next_financier(const Actor&c,const Actor&i,Arm arm){
  if(arm==Arm::control)return pair::Recipient::cassi;
  if(c.inventory[int(Item::monster_focus)]<5)return pair::Recipient::cassi;
  if(i.inventory[int(Item::jungle_health)]<5)return pair::Recipient::issac;
  // Public current inventory makes the same appointment in both bodies. No
  // partner wallet or promised purchase enters this custody decision.
  const int cPackets=c.inventory[int(Item::focus)]/5,iPackets=(i.inventory[int(Item::health)]+i.inventory[int(Item::strength)])/5;
  if(i.hp<i.max_hp*.45&&c.hp>=c.max_hp*.45)return pair::Recipient::issac;
  return iPackets<cPackets?pair::Recipient::issac:pair::Recipient::cassi;
 }
 Directive step(const World&w,Arm arm){
  economy.consume(w);traces=0;ownWitness=partnerWitness=false;
  if(w.self.channel!=Channel::none)return BPolicy::step(w,arm);
  if(p10_c::winning_bag(w))return bank("linked actual winning bag precedes recurrent capital");
  auto pending=capital.poll(w,"committed twin spends only its personally funded recurring packet",2140);if(pending.task!=Task::independent)return pending;
  if(recipe!=Item::none)return BPolicy::step(w,arm);
  const bool paired=w.self.type==Champion::cassi||w.self.type==Champion::issac;
  if(!paired){
   if(!w.capture_active&&!p10_c::near_awakening(w)&&w.remaining()>160&&(p10_c::legal_shop(w)||economy.lastPayout>servicedPayout)){
    const Item item=!w.self.monster_emblem?Item::monster_strength:(!w.self.jungle_emblem?Item::jungle_health:Item::strength);
    const int count=item==Item::strength?5:std::max(0,5-w.self.inventory[int(item)]);
    if(count&&capital.open(w,{{item,count}})){servicedPayout=economy.lastPayout;return capital.poll(w,"independent linked-roster worker funds its own expanding circuit",2141);}
   }
   return BPolicy::step(w,arm);
  }
  const Actor*partner=twin(w);if(!partner)return BPolicy::step(w,arm);
  const Actor&c=w.self.type==Champion::cassi?w.self:*partner;const Actor&i=w.self.type==Champion::issac?w.self:*partner;
  capitalRecipient=next_financier(c,i,arm);recipient=capitalRecipient;
  const bool owns=(capitalRecipient==pair::Recipient::cassi)==(w.self.type==Champion::cassi);
  if(!committedService&&(w.self.bag>0||partner->bag>0)){
   committedService=true;serviceStart=w.round;
   if(c.hp<c.max_hp*.3)order=pair::ServiceOrder::cassi_first;
   else if(i.hp<i.max_hp*.3)order=pair::ServiceOrder::issac_first;
   else order=capitalRecipient==pair::Recipient::cassi?pair::ServiceOrder::cassi_first:pair::ServiceOrder::issac_first;
  }
  if(w.self.bag>0||partner->bag>0||serviceStart>=0)return BPolicy::step(w,arm);
  const Actor&holder=capitalRecipient==pair::Recipient::cassi?c:i;
  for(const auto&g:w.groups)if(p10_b::work_source(g)&&g.cleared&&(p10_b::on_region(c,g)||p10_b::on_region(i,g))){
   source=g.id;if(!owns&&p10_b::on_region(holder,g)){Directive d;d.task=Task::release;d.group=g.id;d.template_id=2142;d.reason="actual recurrent financier holds before peaceful custody release";return d;}
   if(owns||p10_b::on_region(w.self,g))return farm(g.id,"real custody funds the next linked growth packet");
  }
  if(owns&&!w.capture_active&&!p10_c::near_awakening(w)&&w.remaining()>160&&(p10_c::legal_shop(w)||economy.lastPayout>servicedPayout)){
   const Item starter=w.self.type==Champion::cassi?Item::monster_focus:Item::jungle_health;
   const int missing=std::max(0,5-w.self.inventory[int(starter)]);
   const Item item=missing?starter:(w.self.type==Champion::cassi?Item::focus:(w.self.inventory[int(Item::health)]<=w.self.inventory[int(Item::strength)]?Item::health:Item::strength));
   if(capital.open(w,{{item,missing?missing:5}})){servicedPayout=economy.lastPayout;return capital.poll(w,"public recipient personally funds its prerequisite or next recurring Power packet",2143);}
  }
  // B retains its separate self mobile and whole-duration actual twin hazard
  // proof. Future capital never certifies either linked body's present action.
  return BPolicy::step(w,arm);
 }
};
}
