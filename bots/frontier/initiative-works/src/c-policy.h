#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-06-10.h"
namespace p10_initiative {
struct CPolicy:BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=std::uint64_t{0};
 p10_c_capital::Income capitalIncome;int fundedReturn=-1;
 p10::Item reinvest(const p10::World&w)const{
  using namespace p10;using p10_c_capital::stock;
  if(stock(w,Item::monster_strength)<5)return Item::monster_strength;
  if(w.self.type==Champion::willow)return stock(w,Item::focus)/5<=stock(w,Item::strength)/5?Item::focus:Item::strength;
  if(stock(w,Item::jungle_health)<5)return Item::jungle_health;
  return Item::strength;
 }
 p10::Directive independent(const p10::World&w){
  using namespace p10;p10_b_local::Turn turn(w);appoint(w);const auto*g=w.group(w.self.type==Champion::willow?independentSource:appointment);
  auto d=g?turn.work(*g,"same-menu pipeline control retains a fixed owner and performs independent mobile work",104):ordinary(w);fresh_trajectories=turn.budget.spent;return d;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  using namespace p10;using namespace p10_c_capital;capitalIncome.read(w);receipt_first(w);fresh_trajectories=0;
  if(protected_turn(w))return wait("actual custody/channel precedes a funded capital pipeline transition");
  gear=true;
  Directive d;bool capitalAction=false;
  if(w.self.bag>0){d=bank("pipeline converts this owner's real bag before servicing capital");capitalAction=true;}
  else if(capitalIncome.funded&&affordable(w)>0&&legal_shop(w)){
   const auto item=reinvest(w);const int missing=(item==Item::strength||item==Item::focus)?5:5-stock(w,item);
   d=purchase(w,item,missing,"actual pipeline owner buys its next personal work batch after receipt");capitalAction=true;
  }else if(capitalIncome.funded&&w.own_gold>=5*w.shard_cost&&!legal_shop(w)&&w.safe_now){fundedReturn=w.round;d.task=Task::recover;d.reason="funded owner enters the real spawn-capital-resume pipeline";capitalAction=true;}
  if(capitalAction){if(arm==Arm::active){d.priority=priority(w.self.bag>0?Dispatch::loaded:Dispatch::circulation,std::clamp(w.self.pick,0,15));d.valid_round=w.round;}return d;}
  return arm==Arm::active?BPolicy::step(w,arm):independent(w);
 }
};
}
