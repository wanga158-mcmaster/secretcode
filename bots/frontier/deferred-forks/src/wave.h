#pragma once
#include "../../common/src/combat.h"
namespace p10_forks {
struct Policy:LegacyPolicy {
 static constexpr bool observe_open_tiles=true;
 int variant=0,fresh_trajectories=0,committedSource=-1,receiptRound=-1;
 using FuelReader=int(*)();FuelReader fuel_reader=nullptr;
 void set_fuel_reader(FuelReader reader){fuel_reader=reader;}
 static bool physical_before(const p10::World&w,const p10::Group&a,const p10::Group&b){
  int da=p10::distance(w.spawn,a.position),db=p10::distance(w.spawn,b.position);return da!=db?da<db:a.id<b.id;
 }
 void start_fork(const p10::World&w){
  const p10::Group*a=nullptr,*b=nullptr;
  for(const auto&g:w.groups)if(g.program_available&&g.route_known&&!g.respawning&&g.to_bank>=0&&(g.gems>0||g.gold>0)){
   if(!a||physical_before(w,g,*a)){b=a;a=&g;}else if(!b||physical_before(w,g,*b))b=&g;
  }
  sources={a?a->id:-1,b?b->id:-1};fork=Fork{};fork.createdRound=w.round;inspectionStart=-1;
 }
 p10::Directive committed_work(const p10::World&w,const p10::Group&g){
  using namespace p10;if(g.cleared)return farm(g.id,"fork commitment retains its actual payout custody");
  if(!usable(g)){Directive d;d.task=Task::inspect;d.group=g.id;d.destination=g.approach;d.reason="resolve the committed physical source's current receipt premise";return d;}
  if(fresh_trajectories>=2||(fuel_reader?fuel_reader():w.fuel)<=w.action_reserve+2500)return ordinary(w);
  const auto stance=distance(w.self.position,g.position)<=w.self.range?w.self.position:g.approach;
  ++fresh_trajectories;const auto proof=stationary_work(w,g,stance,64);if(!proof.feasible)return ordinary(w);
  if(!(stance==w.self.position)){if(!w.safe_now)return {Task::recover};Directive d;d.task=Task::move;d.group=g.id;d.destination=stance;d.reason="execute actual committed fork approach";return d;}
  auto d=proof.first;d.template_id=31;d.reason="resolved threshold fork executes certified physical work";return d;
 }
 p10::Directive step(const p10::World&w,p10::Arm arm){
  using namespace p10;if(variant==1||arm==Arm::control)return LegacyPolicy::step(w,arm);
  fresh_trajectories=0;
  if(w.own_payout){firstIncome=true;receiptRound=w.round;pendingCapital=w.self.type==Champion::tavi;}
  if(w.own_purchase){pendingCapital=false;purchasedAfter=w.round;}
  if(w.own_bank_completed){sources={-1,-1};committedSource=-1;fork=Fork{};inspectionStart=-1;}
  if(w.self.channel!=Channel::none||w.capture_active)return wait("preserve resolved fork receipt/channel");
  if(w.self.bag>0)return convert_loaded(w);
  if(sources[0]<0)start_fork(w);
  const Group*a=w.group(sources[0]),*b=w.group(sources[1]);
  const bool invalid=!a||!a->route_known||a->respawning||a->contested||!b||!b->route_known||b->respawning||b->contested;
  if(invalid){start_fork(w);a=w.group(sources[0]);b=w.group(sources[1]);committedSource=-1;}
  if(!a)return ordinary(w);
  if(committedSource>=0){const auto*g=w.group(committedSource);if(g&&usable(*g))return committed_work(w,*g);committedSource=-1;}
  if(!b){committedSource=a->id;return committed_work(w,*a);}
  // Retained source IDs and creation time survive actor-relative distance
  // ordering and missed observations. Only physical invalidation retires them.
  const auto pa=advance(w.self.position,a->approach),pb=advance(w.self.position,b->approach);
  bool obstacle=false;const Point next{pa.x,pa.y};
  if(std::find(w.visible_open_tiles.begin(),w.visible_open_tiles.end(),next)==w.visible_open_tiles.end())obstacle=true;
  for(const auto&e:w.enemies)if(e.visible&&e.alive&&distance(e.position,next)<=e.range+1)obstacle=true;
  for(const auto&other:w.allies)if(other.visible&&other.alive&&other.position==next)obstacle=true;
  if(pa.kind==Atomic::move&&pa.same(pb)&&w.self.can_move&&w.safe_now&&!obstacle){
   Directive d;d.task=Task::move;d.destination=next;d.valid_round=w.round;d.template_id=30;
   d.reserve_reset=true;d.reason="persistent physical fork shares this exact useful prefix; defer irreversible capital";return d;
  }
  // At the actual divergence inspect at most two complete executable current
  // continuations. No hypothetical post-purchase Strength or cooldown is used.
  const Group*chosen=nullptr;SoloForecast best;int completion=100000;
  for(const Group*g:{a,b})if(usable(*g)&&fresh_trajectories<2&&(fuel_reader?fuel_reader():w.fuel)>w.action_reserve+2500){
   ++fresh_trajectories;auto proof=stationary_work(w,*g,g->approach,64);
   int cost=g->travel+proof.turns+w.capture_turns+1+g->to_bank+w.ticks(g->gems);
   if(proof.feasible&&(!chosen||cost<completion||(cost==completion&&g->id<chosen->id))){chosen=g;best=proof;completion=cost;}
  }
  if(!chosen){Directive d;d.task=Task::inspect;d.group=a->id;d.destination=a->approach;d.reason="actual branch disagreement needs private current inspection";return w.safe_now?d:Directive{Task::recover};}
  committedSource=chosen->id;lastCommit=w.round;
  // An immediately payable shard is justified only at a witnessed executable
  // kill threshold; purchase consumes this turn and the branch replans next turn.
  if(pendingCapital&&w.self.type==Champion::tavi&&w.own_gold>=w.shard_cost&&w.self.strength_growth>0){
   for(const auto&m:chosen->monsters)if(m.alive&&m.hp>w.self.strength&&m.hp<=w.self.strength+w.self.strength_growth){
    return buy(Item::strength,1,"observed divergence exposes an executable one-basic personal capital threshold");}
  }
  if(chosen->approach==w.self.position){auto d=best.first;d.template_id=31;d.reason="actual divergence commits the complete current-wallet continuation";return d;}
  if(!w.safe_now)return {Task::recover};Directive d;d.task=Task::move;d.group=chosen->id;d.destination=chosen->approach;d.template_id=31;d.reason="commit resolved physical fork after threshold inspection";return d;
 }
};
}
