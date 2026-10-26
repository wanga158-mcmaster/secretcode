#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-11-16.h"
namespace p10_c15 {
using namespace p10;
struct CPolicy:p10_b15::BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=std::uint64_t{0};
 p10_c::OwnedOrder capital{};int servicedPayout=-1,branchSavings=0,newSkills=0;Item compiledPacket=Item::none;
 Directive step(const World&w,Arm arm){
  economy.consume(w);traces=0;
  if(w.self.channel!=Channel::none||w.capture_active)return BPolicy::step(w,arm);
  if(p10_c::winning_bag(w))return bank("guild closes actual winning inventory before maintenance");
  auto pending=capital.poll(w,"funded compiler maintenance grows executable skills before renewed circuits",2150);if(pending.task!=Task::independent)return pending;
  const bool boundary=p10_c::legal_shop(w)||economy.lastPayout>servicedPayout;
  if(boundary&&w.remaining()>160&&!p10_c::near_awakening(w)){
   Item item=Item::strength;int count=5;branchSavings=newSkills=0;
   if(!w.self.monster_emblem){item=Item::monster_strength;count=std::max(0,5-w.self.inventory[int(item)]);}
   else if(!w.self.jungle_emblem){item=Item::jungle_health;count=std::max(0,5-w.self.inventory[int(item)]);}
   else {
    const double mult=1.1,futureS=w.self.strength+p10_c::power_units(w.self.inventory[int(Item::strength)],5)*w.self.strength_growth;
    const double futureF=w.self.focus+p10_c::power_units(w.self.inventory[int(Item::focus)],5)*w.self.focus_growth;int strengthGain=0,focusGain=0,focusSkills=0;
    auto budget=p10_mobile::budget_for(w);bool complete=true;
    for(const auto&g:w.groups)if(p10_b::work_source(g)&&!g.cleared){
     if(!budget.fuel_ok()){complete=false;break;}
     int sBefore=0,sAfter=0;for(const auto&m:g.monsters)if(m.alive&&m.hp>0){
      if(!budget.fuel_ok()){complete=false;break;}
      sBefore+=int(std::ceil(m.hp/std::max(1.,w.self.strength*mult)));sAfter+=int(std::ceil(m.hp/std::max(1.,futureS*mult)));
     }
     if(!complete)break;strengthGain+=std::max(0,sBefore-sAfter);
     for(const auto&a:w.self.abilities)if(a.targets_camps&&(a.damage||a.cc)&&a.modifier>0){
      int before=0,after=0,alive=0;for(const auto&m:g.monsters)if(m.alive&&m.hp>0){++alive;before+=m.hp>a.modifier*w.self.focus*mult;after+=m.hp>a.modifier*futureF*mult;}
      focusGain+=std::max(0,before-after);if(before>0&&after==0&&(alive==1||a.aoe))++focusSkills;
     }
    }
    if(!complete)return BPolicy::step(w,arm);
    // Each packet changes this compiler's own vocabulary; productive current
    // skills do not bypass maintenance, and no one-shard failure is required.
    if(focusGain+focusSkills>strengthGain){item=Item::focus;branchSavings=focusGain;newSkills=focusSkills;}
    else{branchSavings=strengthGain;newSkills=strengthGain>0;}
    // Preserve open-ended growth even in a currently cheap district. These
    // funded Power packets can replace circuits after later observation.
   }
   if(count>0&&capital.open(w,{{item,count}})){compiledPacket=item;servicedPayout=economy.lastPayout;return capital.poll(w,"successful current work funds a recurrent complete compiler growth regime",2151);}
  }
  return BPolicy::step(w,arm);
 }
};
}
