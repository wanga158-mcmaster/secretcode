#pragma once
#include "b-policy.h"
#include "../../common/src/c-capital-06-10.h"
namespace p10_forks {
struct CPolicy:BPolicy {
 static constexpr bool owns_capital=true;
 static constexpr bool observe_ally_details=false;
 static constexpr std::uint64_t observe_ally_detail_types=std::uint64_t{0};
 p10_c_capital::Income capitalIncome;int capitalBranch=-1;
 p10::Item reinvest(const p10::World&w){
  using namespace p10;using p10_c_capital::stock;
  if(stock(w,Item::monster_strength)<5)return Item::monster_strength;
  if(stock(w,Item::jungle_health)<5)return Item::jungle_health;
  // This fork alone uses a literal observed spell/basic threshold to choose
  // between its recurring raw-stat branches. No upgraded state is certified.
  const auto*g=w.group(committedSource);if(!g)g=w.group(sources[0]);
  if(g&&usable(*g))for(const auto&m:g->monsters)if(m.alive&&m.hp>0){
   for(const auto&a:w.self.abilities)if(a.damage&&a.targets_camps&&a.modifier>0&&w.self.focus_growth>0&&m.hp>a.modifier*w.self.focus&&m.hp<=a.modifier*(w.self.focus+w.self.focus_growth))return Item::focus;
  }
  return Item::strength;
 }
 bool common_prefix(const p10::World&w)const{
  const auto*a=w.group(sources[0]),*b=w.group(sources[1]);if(!a||!b)return false;
  const auto x=advance(w.self.position,a->approach),y=advance(w.self.position,b->approach);
  return x.kind==Atomic::move&&x.same(y)&&w.self.can_move&&w.safe_now&&std::find(w.visible_open_tiles.begin(),w.visible_open_tiles.end(),p10::Point{x.x,x.y})!=w.visible_open_tiles.end();
 }
 p10::Directive independent(const p10::World&w){
  using namespace p10;p10_b_local::Turn turn(w);const Group*job=nullptr;
  for(const auto&g:w.groups)if(g.program_available&&g.route_known&&!g.respawning&&!g.contested&&g.travel>=0&&g.to_bank>=0&&
   (!job||physical_before(w,g,*job)))job=&g;
  auto d=job?turn.work(*job,"same-menu fork control commits one immutable source before any shared prefix",83):ordinary(w);fresh_trajectories=turn.budget.spent;return d;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  using namespace p10;using namespace p10_c_capital;capitalIncome.read(w);observe_receipts(w);fresh_trajectories=0;
  if(protected_turn(w))return wait("capital fork preserves its completed physical prefix/channel");
  if(w.self.bag>0)return convert_loaded(w);
  if(sources[0]<0)start_fork(w);
  // Both arms have this same actual-wallet menu. Active Tavi retains the
  // productive common prefix before spending in the field; spawn visits buy
  // their real funded batch without a fictitious post-purchase forecast.
  const bool defer=arm==Arm::active&&!w.self.at_spawn&&common_prefix(w);
  if(capitalIncome.funded&&affordable(w)>0&&legal_shop(w)&&!defer){
   const auto item=reinvest(w);const int missing=(item==Item::strength||item==Item::focus)?5:5-stock(w,item);capitalBranch=committedSource;pendingPurchase=-1;
   return purchase(w,item,missing,"actual divergence funds another recurring capital branch; observe stats then reprove");
  }
  if(capitalIncome.funded&&w.own_gold>=5*w.shard_cost&&!legal_shop(w)&&!defer&&w.safe_now){Directive d;d.task=Task::recover;d.reason="funded irreversible fork returns for its next real capital branch";return d;}
  // B's terminal first-purchase flag is deliberately disabled; C owns the
  // entire recurring menu, including threshold selection after many receipts.
  actualPurchase=std::max(0,actualPurchase);
  return arm==Arm::active?BPolicy::step(w,arm):independent(w);
 }
};
}
