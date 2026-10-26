#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-06-10.h"
namespace p10_exposure {
struct CPolicy:BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=std::uint64_t{0};
 p10_c_capital::Income capitalIncome;
 p10::Item reinvest(const p10::World&w)const{
  using namespace p10;using p10_c_capital::stock;
  // Independent work first pays the service organization. Ren's recurring
  // reaction/cast capital is deliberately different from Willow's earned wage.
  if(stock(w,Item::monster_strength)<5)return Item::monster_strength;
  if(w.self.type==Champion::ren||w.self.type==Champion::stellar){if(stock(w,Item::jungle_focus)<5)return Item::jungle_focus;return stock(w,Item::focus)/5<=stock(w,Item::strength)/10?Item::focus:Item::strength;}
  if(w.self.type==Champion::willow){if(stock(w,Item::jungle_health)<5)return Item::jungle_health;return stock(w,Item::strength)/5<=stock(w,Item::focus)/5?Item::strength:Item::focus;}
  if(stock(w,Item::jungle_health)<5)return Item::jungle_health;return Item::strength;
 }
 p10::Directive independent(const p10::World&w){
  using namespace p10;p10_b_local::Turn turn(w);const Group*job=nullptr;
  for(const auto&g:w.groups)if(g.program_available&&g.route_known&&!g.respawning&&!g.contested&&g.travel>=0&&g.to_bank>=0&&
   (!job||distance(g.position,w.spawn)<distance(job->position,w.spawn)||(distance(g.position,w.spawn)==distance(job->position,w.spawn)&&g.id<job->id)))job=&g;
  auto d=job?turn.work(*job,"same-menu exposure control performs direct work without priced reaction/service organization",94):ordinary(w);fresh_trajectories=turn.budget.spent;return d;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  using namespace p10;using namespace p10_c_capital;capitalIncome.read(w);receipt_first(w);fresh_trajectories=0;
  if(protected_turn(w))return wait("service capital waits for actual service/custody completion");
  if(w.self.bag>0)return bank("real service wages are banked before their next production cycle");
  capital=true;
  if(capitalIncome.funded&&affordable(w)>0&&legal_shop(w)){
   const auto item=reinvest(w);const int missing=(item==Item::strength||item==Item::focus)?5:5-stock(w,item);
   return purchase(w,item,missing,"only this worker's current earnings finance recurring reaction/service capital");
  }
  if(capitalIncome.funded&&w.own_gold>=5*w.shard_cost&&!legal_shop(w)&&w.safe_now){Directive d;d.task=Task::recover;d.reason="self-funded service worker replenishes its own paid production capacity";return d;}
  return arm==Arm::active?BPolicy::step(w,arm):independent(w);
 }
};
}
