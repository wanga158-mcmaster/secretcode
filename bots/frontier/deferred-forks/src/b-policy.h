#pragma once
#include "policy.h"
#include "../../common/src/physical-turn.h"
namespace p10_forks {
struct BPolicy:Policy {
 int pendingPurchase=-1,actualPurchase=-1,cooldownBranch=-1,prefixActions=0;bool sawIncome=false;
 void observe_receipts(const p10::World&w){
  if(w.own_payout){sawIncome=true;receiptRound=w.round;}
  if(w.own_purchase){actualPurchase=w.round;pendingPurchase=-1;}
  if(w.own_bank_completed){sources={-1,-1};fork=Fork{};committedSource=-1;pendingPurchase=-1;}
 }
 // An optimistic bound schedules only this pair of retained continuations.
 // It supplies no witness, predicted upgrade, ally damage or first action.
 int optimistic_finish(const p10::World&w,const p10::Group&g,const p10_mobile::Budget&budget,bool&ready,bool&positioned)const{
  using namespace p10;double hp=0;int living=0;
  if(g.monsters.size()>12){positioned=false;return 0;}
  for(const auto&m:g.monsters){if(!budget.fuel_ok()){ready=false;return 0;}if(m.alive&&m.hp>0){hp+=m.hp;++living;positioned&=distance(w.self.position,m.position)<=w.self.range;}}
  if(g.to_bank<0||living==0||!std::isfinite(hp))return 0;
  const double multiplier=w.self.monster_emblem?1.1:1.;
  // Mobile work permits at most two basics and one successful cast per owner
  // phase. Credit every such action every phase, ignoring range/cooldowns and
  // counting all source members for splash: this can only finish too early.
  double upper=2*std::max(0.,w.self.strength)*multiplier*(w.self.type==Champion::varg?living:1),spell=0;
  for(const auto&a:w.self.abilities){if(!budget.fuel_ok()){ready=false;return 0;}if((a.targets_camps||a.targets_ground)&&(a.damage||a.aoe||a.cc))spell=std::max(spell,std::max(0.,a.modifier*w.self.focus)*multiplier*(a.aoe?living:1));}
  upper+=spell;if(!(upper>0)||!std::isfinite(upper))return 0;
  const int clear=int(std::min(100000.,std::ceil(hp/upper)));
  return clear+w.capture_turns+1+g.to_bank+w.ticks(g.gems);
 }
 p10::Directive step(const p10::World&w,p10::Arm){
  using namespace p10;observe_receipts(w);p10_b_local::Turn turn(w);auto done=[&](Directive d){fresh_trajectories=turn.budget.spent;return d;};
  if(w.self.channel!=Channel::none||w.capture_active)return done(wait("a witnessed fork receipt owns the current channel/custody"));
  if(w.self.bag>0)return done(convert_loaded(w));
  if(pendingPurchase>=0&&actualPurchase<pendingPurchase){if(w.round-pendingPurchase<2)return done(wait("issued capital still needs its actual receipt"));pendingPurchase=-1;}
  if(sources[0]<0)start_fork(w);
  const Group*a=w.group(sources[0]),*b=w.group(sources[1]);
  if(!a||!a->route_known||a->respawning||a->contested||(b&&(!b->route_known||b->respawning||b->contested))){
   start_fork(w);a=w.group(sources[0]);b=w.group(sources[1]);committedSource=-1;
  }
  if(!a)return done(ordinary(w));
  if(committedSource>=0){const auto*g=w.group(committedSource);if(g&&usable(*g))return done(turn.work(*g,"actual-capital/cooldown fork follows its committed mobile job",82));committedSource=-1;}
  if(!b){committedSource=a->id;return done(turn.work(*a,"only physical fork continuation remains",82));}
  const auto left=advance(w.self.position,a->approach),right=advance(w.self.position,b->approach);const Point tile{left.x,left.y};
  const bool open=std::find(w.visible_open_tiles.begin(),w.visible_open_tiles.end(),tile)!=w.visible_open_tiles.end();
  bool blocked=false;for(const auto&e:w.enemies)if(e.visible&&e.alive&&distance(e.position,tile)<=e.range+2)blocked=true;
  for(const auto&ally:w.allies)if(ally.visible&&ally.alive&&ally.position==tile)blocked=true;
  if(left.kind==Atomic::move&&left.same(right)&&open&&!blocked&&w.self.can_move&&w.safe_now){
   ++prefixActions;Directive d;d.task=Task::move;d.destination=tile;d.valid_round=w.round;d.reserve_reset=true;d.template_id=80;
   d.reason="both named productive mobile continuations still share this physical prefix; irreversible capital stays deferred";return done(d);
  }
  const Group*chosen=nullptr;p10_mobile::Result best;int earliest=100000;
  std::array<const Group*,2> continuations{a,b};std::array<int,2> lower{};std::array<bool,2> positioned{true,true};bool ready=true;
  for(int i=0;i<2;++i)lower[i]=optimistic_finish(w,*continuations[i],turn.budget,ready,positioned[i]);
  if(!ready)return done(wait("retained fork bounds preserve the action reserve"));
  // Restrict the scheduling change to two actually positioned continuations.
  // A deceptively cheap but disconnected off-approach branch stays second.
  if(positioned[0]&&positioned[1]&&(lower[1]<lower[0]||(lower[1]==lower[0]&&b->id<a->id))){std::swap(continuations[0],continuations[1]);std::swap(lower[0],lower[1]);}
  for(int i=0;i<2;++i){const auto*g=continuations[i];
   if(!usable(*g)||distance(w.self.position,g->position)>w.self.range+2||turn.budget.spent>=2)continue;
   // A complete current witness beats this alternative even if every future
   // phase used the deliberately overstated damage above. Preserve ID ties.
   if(chosen&&(lower[i]>earliest||(lower[i]==earliest&&g->id>=chosen->id)))continue;
   auto proof=turn.prove(*g);const int finish=proof.clear_turns+w.capture_turns+1+g->to_bank+w.ticks(g->gems);
   if(proof.feasible&&(!chosen||finish<earliest||(finish==earliest&&g->id<chosen->id))){chosen=g;best=proof;earliest=finish;}
  }
  if(!chosen){Directive d;d.task=Task::inspect;d.group=a->id;d.destination=a->approach;d.reason="actual divergence requires current private local mobile premises";return done(w.safe_now?d:Directive{Task::recover});}
  committedSource=chosen->id;lastCommit=w.round;
  // The branch proof uses current stats only. Buying actual affordable gear is
  // its own irreversible prefix; the next owner turn must observe the receipt
  // and instantiate a new physical program, never a fictitious upgraded actor.
  if(sawIncome&&actualPurchase<0&&w.self.type==Champion::tavi&&w.own_gold>=w.shard_cost&&w.self.strength_growth>0)
   for(const auto&m:chosen->monsters)if(m.alive&&m.hp>w.self.strength&&m.hp<=w.self.strength+w.self.strength_growth){pendingPurchase=w.round;committedSource=-1;return done(buy(Item::strength,1,"real divergence exposes this current-wallet one-basic threshold; reprove after receipt"));}
  if(w.self.type==Champion::vendetta&&w.self.cooldown[0]>0&&best.first.task==Task::attack)
   for(const auto&m:chosen->monsters)if(m.id==best.first.monster&&m.hp<=w.self.strength*(w.self.monster_emblem?1.1:1.))cooldownBranch=chosen->id;
  return done(p10_b_local::Turn::first(best,"resolved actual capital/cooldown fork executes its exact mobile first directive",82));
 }
};
}
