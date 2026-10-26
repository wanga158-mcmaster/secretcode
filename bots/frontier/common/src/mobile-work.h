#pragma once
#include "combat.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#ifndef P10_REMOTE_WORK_GUARD
#define P10_REMOTE_WORK_GUARD 0
#endif

// Bounded physical work. It proves one actor's finite local clearance, not source
// selection, custody, banking, future availability or safety against unseen foes.
namespace p10_mobile {
inline constexpr int max_nodes=64,max_mobs=32,max_frames=66;
enum class Failure { none,input,occupied,enemy,unknown_hazard,blocked,health,turn_cap,fuel,trace_budget,geometry_warmup,stale_budget,no_action };
enum class Mode { threat_first,open_spawns };
struct Options {
 int turn_cap=48,settle_phases=2,max_traces=1;Mode mode=Mode::threat_first;int preferred_monster=-1;bool suppress_reset=false;
 // Set only from fresh SDK allowed-target metadata. p10::Ability currently
 // exports camps/self masks but does not export ground-target permissions.
 std::array<bool,2> ground_targets{};
};
struct Budget {
 int actor=-1,round=-1,spent=0; p10::WorkFuelReader fuel_reader=nullptr;int action_reserve=40000;
 bool fuel_ok()const{return !fuel_reader||fuel_reader()>=action_reserve+500;}
};
inline Budget budget_for(const p10::World&w,p10::WorkFuelReader reader=p10::work_fuel_reader){return {w.self.id,w.round,0,reader,w.action_reserve};}
struct Frame {
 int turn=0; p10::Point from{},to{};bool moved=false;
 int casts=0,basics=0,cast_slot=-1,cast_target=-1,first_basic=-1,second_basic=-1;
 double hp=0,shield=0,source_hp=0,camp_damage=0,neighbor_damage=0;
};
struct Result {
 bool feasible=false,conditional=true; Failure failure=Failure::input; Mode mode=Mode::threat_first;
 int actor=-1,round=-1,source=-1,clear_turns=0,moves=0,kills=0,frames_used=0,omitted_pads=0;
 double hp=0,minimum_hp=0; p10::Point position{};p10::Directive first=p10::wait("mobile proof unavailable");
 // Only actual simulated phases have records. Eagerly zeroing66 frames in
 // every candidate consumed the live budget even when no trace was admitted.
 std::vector<Frame> frames;
};
// Diagnostic state is never read by a policy or resumed as a forecast. Runtime
// reports it on the next fresh owner turn, outside the measured policy budget.
struct CallDiagnostics {int actor=-1,round=-1,calls=0,traces=0,source=-1;Failure failure=Failure::none;bool feasible=false;};
inline CallDiagnostics call_diagnostics{};
struct CallProbe {
 const p10::World&w;const p10::Group&g;const Budget&budget;const Result&result;
 ~CallProbe(){if(call_diagnostics.actor!=w.self.id||call_diagnostics.round!=w.round)call_diagnostics={w.self.id,w.round};
  ++call_diagnostics.calls;call_diagnostics.traces=budget.spent;call_diagnostics.source=g.id;call_diagnostics.failure=result.failure;call_diagnostics.feasible=result.feasible;}
};
struct Node {p10::Point p{};std::array<int,4>next{-1,-1,-1,-1};};
struct Mob {p10::Monster m{};bool source=false,known=false;int stun=0;};
struct Geometry {std::vector<Node>nodes;int count=0,origin=-1,omitted=0;std::vector<Mob>mobs;int mobs_count=0;bool positioned_basics=false;Failure failure=Failure::none;};
inline constexpr int max_raw_geometry=256,cache_slots=4;
struct RawPoint {p10::Point p{};int identity=-1,kind=0;bool operator==(const RawPoint&)const=default;};
// Nothing in this cache is a live world head. The coordinate records, camp
// identities and adjacency are immutable map geometry, re-bound on every call.
struct Skeleton {
 int source=-1,spawn_count=0,mob_count=0,pad_count=0,total=0,copied=0,input=0,dedup=0,count=0,omitted=0,adj_i=0,adj_j=1;
 std::uint64_t stamp=0;bool ready=false,invalid=false;
 std::shared_ptr<const p10::SourceGeometry>geometry_owner;
 std::array<RawPoint,max_raw_geometry>raw{};std::array<Node,max_nodes>nodes{};
};
struct SkeletonCache {std::array<Skeleton,cache_slots>slots{};std::uint64_t clock=0;};
inline SkeletonCache default_cache{};
inline bool same_geometry_owner(const std::shared_ptr<const p10::SourceGeometry>&a,const std::shared_ptr<const p10::SourceGeometry>&b){
 return a.get()==b.get()&&!a.owner_before(b)&&!b.owner_before(a);
}
inline bool owner_counts_match(const p10::Group&g){
 if(!g.source_geometry)return true;const auto&o=*g.source_geometry;
 return o.source==g.id&&o.spawns.size()==g.spawns.size()&&o.members.size()==g.monsters.size()&&o.approach_pads.size()==g.approach_pads.size();
}
inline bool current_geometry_binding(const p10::Group&g){
 if(!g.source_geometry)return true;if(!owner_counts_match(g)||g.monsters.size()>12)return false;const auto&o=*g.source_geometry;
 // Only the current physical identity is checked. HP, awake/stun state and
 // every other live value remain in this turn's Group and never enter Skeleton.
 for(std::size_t i=0;i<g.monsters.size();++i)if(g.monsters[i].id!=o.members[i].id||!(g.monsters[i].position==o.members[i].position))return false;
 return true;
}
inline RawPoint raw_point(const p10::Group&g,int index){
 if(g.source_geometry){const auto&o=*g.source_geometry;
  if(index<int(o.spawns.size()))return {o.spawns[index],-1,0};index-=int(o.spawns.size());
  if(index<int(o.members.size()))return {o.members[index].position,o.members[index].id,1};index-=int(o.members.size());
  return {o.approach_pads[index],-1,2};
 }
 if(index<int(g.spawns.size()))return {g.spawns[index],-1,0};index-=int(g.spawns.size());
 if(index<int(g.monsters.size()))return {g.monsters[index].position,g.monsters[index].id,1};index-=int(g.monsters.size());
 return {g.approach_pads[index],-1,2};
}
inline Skeleton* slot_for(SkeletonCache&cache,int source){
 Skeleton*slot=nullptr;for(auto&s:cache.slots)if(s.source==source){slot=&s;break;}
 if(!slot)for(auto&s:cache.slots)if(s.source<0){slot=&s;break;}
 if(!slot){slot=&cache.slots[0];for(auto&s:cache.slots)if(s.stamp<slot->stamp)slot=&s;}
 slot->stamp=++cache.clock;return slot;
}
inline void reset(Skeleton&s,const p10::Group&g){
 // Active prefixes own their initialization. Clearing all unused raw/node
 // capacity here consumed the warmup budget before its first resumable step.
 s.copied=s.input=s.dedup=s.count=s.omitted=s.adj_i=0;s.adj_j=1;s.ready=s.invalid=false;
 s.geometry_owner=g.source_geometry;
 s.source=g.id;s.spawn_count=int(g.spawns.size());s.mob_count=int(g.monsters.size());s.pad_count=int(g.approach_pads.size());s.total=s.spawn_count+s.mob_count+s.pad_count;
 if(s.total>max_raw_geometry)s.invalid=true;
}
inline Failure prepare(SkeletonCache&cache,const p10::Group&g,const Budget&budget,Skeleton*&slot){
 if(!owner_counts_match(g))return Failure::input;
 slot=slot_for(cache,g.id);auto&s=*slot;
 if(!same_geometry_owner(s.geometry_owner,g.source_geometry)||s.source!=g.id||s.spawn_count!=int(g.spawns.size())||s.mob_count!=int(g.monsters.size())||s.pad_count!=int(g.approach_pads.size()))reset(s,g);
 if(s.invalid)return Failure::input;
 // A retained const owner makes identity/count validation O(1) and prevents
 // address reuse while a warmup cursor survives. Ownerless mutable fixtures
 // still validate every previously copied coordinate/order/identity.
 if(!g.source_geometry)for(int i=0;i<s.copied;++i){if((i&7)==0&&!budget.fuel_ok())return Failure::geometry_warmup;
  if(!(s.raw[i]==raw_point(g,i))){reset(s,g);break;}}
 if(s.invalid)return Failure::input;
 int work=0;
 auto enough=[&](){return (++work&7)!=0||budget.fuel_ok();};
 while(s.copied<s.total){if(!enough())return Failure::geometry_warmup;s.raw[s.copied]=raw_point(g,s.copied);++s.copied;}
 if(s.ready)return Failure::none;
 // Insertion and each adjacency pair resume from literal immutable cursors.
 // Aborted warmup never saves a trajectory, HP value, cooldown or occupation.
 while(s.input<s.total){
  if(!enough())return Failure::geometry_warmup;const auto&raw=s.raw[s.input];if(raw.p.x<0||raw.p.y<0){s.invalid=true;return Failure::input;}
  bool duplicate=false;while(s.dedup<s.count){if(!enough())return Failure::geometry_warmup;if(s.nodes[s.dedup++].p==raw.p){duplicate=true;break;}}
  if(!duplicate){if(s.count>=max_nodes-1){if(raw.kind!=2){s.invalid=true;return Failure::input;}++s.omitted;}else{s.nodes[s.count]=Node{};s.nodes[s.count++].p=raw.p;}}
  ++s.input;s.dedup=0;
 }
 while(s.adj_i<s.count){
  if(s.adj_j>=s.count){++s.adj_i;s.adj_j=s.adj_i+1;continue;}
  if(!enough())return Failure::geometry_warmup;const int i=s.adj_i,j=s.adj_j++;
  if(std::abs(s.nodes[i].p.x-s.nodes[j].p.x)+std::abs(s.nodes[i].p.y-s.nodes[j].p.y)==1){
   for(auto&next:s.nodes[i].next)if(next<0){next=j;break;}for(auto&next:s.nodes[j].next)if(next<0){next=i;break;}
  }
 }
 s.ready=true;return Failure::none;
}
inline int manhattan(p10::Point a,p10::Point b){return std::abs(a.x-b.x)+std::abs(a.y-b.y);}
inline bool occupied(const p10::World&w,p10::Point p){
 for(const auto&a:w.allies)if(a.id!=w.self.id&&a.visible&&a.alive&&a.position==p)return true;
 for(const auto&a:w.enemies)if(a.id!=w.self.id&&a.visible&&a.alive&&a.position==p)return true;
 return false;
}
inline bool visible_enemy_can_intervene(const p10::World&w,p10::Point p){
 for(const auto&e:w.enemies)if(e.visible&&e.alive){double reach=e.range+2;
  for(const auto&a:e.abilities)if(a.damage||a.aoe||a.cc||a.dash)reach=std::max(reach,a.range+a.radius+(a.dash?e.range:0)+(a.exclusive?0:1));
  if(p10::distance(e.position,p)<=reach)return true;}
 return false;
}
inline Geometry geometry(const p10::World&w,const p10::Group&g,const Options&o,const Budget&budget,SkeletonCache&cache){
 Geometry out;if(!current_geometry_binding(g)){out.failure=Failure::input;return out;}bool positioned=o.mode==Mode::threat_first&&o.max_traces<=1,has_living=false,all_alive_in_basic=true;
 for(const auto&m:g.monsters){if(!budget.fuel_ok()){out.failure=Failure::fuel;return out;}if(m.alive){const bool in_basic=p10::distance(w.self.position,m.position)<=w.self.range;all_alive_in_basic&=in_basic;if(m.hp>0){has_living=true;positioned&=in_basic;}}}
 if(positioned&&has_living){
  // Every remaining target stays in basic range. The exact ordinary trace
  // needs no route or immutable warmup. open_spawns and opposite-mode retries
  // retain their graph because graph neighbors are part of that target order.
  out.nodes.emplace_back();out.nodes[0].p=w.self.position;out.origin=0;out.count=1;out.positioned_basics=all_alive_in_basic;
 }else{
  Skeleton*cached=nullptr;out.failure=prepare(cache,g,budget,cached);if(out.failure!=Failure::none)return out;
  out.count=cached->count;out.omitted=cached->omitted;out.nodes.reserve(max_nodes);out.nodes.assign(cached->nodes.begin(),cached->nodes.begin()+out.count);
  out.origin=-1;for(int n=0;n<out.count;++n)if(out.nodes[n].p==w.self.position){out.origin=n;break;}
  if(out.origin<0){
   // Attach only an actual current tile adjacent to the local source skeleton.
   // No global origin route or a mutable old actor position is cached.
   const int origin=out.count;int adjacent=0;out.nodes.emplace_back();out.nodes[origin].p=w.self.position;
   for(int n=0;n<out.count;++n)if(manhattan(out.nodes[n].p,w.self.position)==1){out.nodes[origin].next[adjacent++]=n;for(auto&next:out.nodes[n].next)if(next<0){next=origin;break;}}
   // An isolated actual self tile can still fire/cast at the source. It gets
   // no invented movement edge; a later required reposition therefore refuses.
   out.origin=origin;++out.count;
  }
 }
 out.mobs.reserve(max_mobs);
 int lo_x=w.self.position.x,hi_x=lo_x,lo_y=w.self.position.y,hi_y=lo_y;
 for(int i=0;i<out.count;++i){auto p=out.nodes[i].p;lo_x=std::min(lo_x,p.x);hi_x=std::max(hi_x,p.x);lo_y=std::min(lo_y,p.y);hi_y=std::max(hi_y,p.y);}
 // Origin-only routing must still catalogue splash/spell collateral around
 // every possible source target, including centers outside the origin tile.
 for(const auto&m:g.monsters){if(!budget.fuel_ok()){out.failure=Failure::fuel;return out;}lo_x=std::min(lo_x,m.position.x);hi_x=std::max(hi_x,m.position.x);lo_y=std::min(lo_y,m.position.y);hi_y=std::max(hi_y,m.position.y);}
 auto box_distance=[&](p10::Point p){return std::max({lo_x-p.x,p.x-hi_x,lo_y-p.y,p.y-hi_y,0});};
 double radius=w.self.type==p10::Champion::varg?1.:0.;for(const auto&a:w.self.abilities)radius=std::max(radius,a.radius);
 auto append=[&](const p10::Group&v,bool own){
  for(const auto&m:v.monsters){if(out.mobs_count==max_mobs){out.failure=Failure::input;return;}
   out.mobs.emplace_back();auto&item=out.mobs[out.mobs_count++];item.m=m;item.source=own;item.known=v.observed&&v.complete&&m.observed;
   // A one-tick stun may expire at the next camp's start. Never credit that
   // unresolved last tick as immunity; longer durations retain their lower bound.
   item.stun=std::max(0,m.stun-1);
   if(!item.known){item.m.alive=!v.cleared&&!v.respawning;item.m.hp=std::max({m.hp,m.max_hp,1.});}
  }
 };
 append(g,true);if(out.failure!=Failure::none)return out;
 for(const auto&v:w.groups)if(v.id!=g.id&&!v.cleared&&!v.respawning){
  if(!budget.fuel_ok()){out.failure=Failure::fuel;return out;}bool relevant=false;
  for(const auto&m:v.monsters)if((m.alive||!v.complete)&&box_distance(m.position)<=m.range+radius){relevant=true;break;}
  if(relevant){append(v,false);if(out.failure!=Failure::none)return out;}
 }
 return out;
}
inline Result trace(const p10::World&w,const p10::Group&g,const Geometry&geo,const Options&o,Mode mode,Budget&budget){
 Result out;out.actor=w.self.id;out.round=w.round;out.source=g.id;out.mode=mode;out.omitted_pads=geo.omitted;
 if(budget.actor!=w.self.id||budget.round!=w.round){out.failure=Failure::stale_budget;return out;}
 if(!w.self.equipment_details_complete)return out;
 if(budget.spent>=2){out.failure=Failure::trace_budget;return out;}if(!budget.fuel_ok()){out.failure=Failure::fuel;return out;}++budget.spent;
 auto mobs=geo.mobs;auto cooldown=w.self.cooldown;auto position=w.self.position;double hp=w.self.hp,shield=w.self.shield;
 const bool positioned_basics=geo.positioned_basics&&mode==Mode::threat_first&&geo.count==1&&geo.origin==0&&geo.nodes.size()==1&&geo.nodes[0].p==position;
 const double damage_multiplier=w.self.monster_emblem?1.1:1.,camp_multiplier=w.self.jungle_emblem?.7:1.;
 int shield_expiry=w.self.shield_until<0?-1:w.self.shield_until-w.round;
 const double floor=std::max(1.,w.self.max_hp*.08);out.minimum_hp=hp;
 if(shield_expiry<=0)shield=0;
 auto source_alive=[&](){int n=0;for(int i=0;i<geo.mobs_count;++i)if(mobs[i].source&&mobs[i].m.alive&&mobs[i].m.hp>0)++n;return n;};
 auto source_hp=[&](){double sum=0;for(int i=0;i<geo.mobs_count;++i)if(mobs[i].source&&mobs[i].m.alive)sum+=std::max(0.,mobs[i].m.hp);return sum;};
 bool fuel_aborted=false;auto open=[&](int node){const auto p=geo.nodes[node].p;if(occupied(w,p))return false;for(int i=0;i<geo.mobs_count;++i){if((i&7)==0&&!budget.fuel_ok()){fuel_aborted=true;return false;}if(mobs[i].m.alive&&mobs[i].m.hp>0&&mobs[i].m.position==p)return false;}return true;};
 auto target_order=[&](int i){const auto&m=mobs[i].m;double value=m.strength*1000+1;
  // Preference constrains every positioned basic while the requested monster
  // is alive and in basic range. Else ordinary physical work is explicit fallback.
  if(m.id==o.preferred_monster&&(positioned_basics||p10::distance(position,m.position)<=w.self.range))value+=1e12;
  if(mode==Mode::open_spawns){int unlock=0;for(int n=0;n<geo.count;++n)if(manhattan(geo.nodes[n].p,m.position)==1&&open(n))++unlock;value+=unlock*300;}
  return value/std::max(1.,m.hp/std::max(1.,w.self.strength*damage_multiplier));};
 auto choose_basic=[&](){int target=-1;double best=-1;for(int i=0;i<geo.mobs_count;++i)if(mobs[i].source&&mobs[i].m.alive&&(positioned_basics||p10::distance(position,mobs[i].m.position)<=w.self.range)){double q=target_order(i);if(q>best||(q==best&&(target<0||mobs[i].m.id<mobs[target].m.id))){target=i;best=q;}}return target;};
 auto wake=[&](int group){for(int i=0;i<geo.mobs_count;++i)if(mobs[i].m.group==group)mobs[i].m.awake=true;};
 auto hit=[&](int i,double damage){if(i<0||!mobs[i].m.alive)return;auto&m=mobs[i].m;wake(m.group);m.hp-=damage*damage_multiplier;if(m.hp<=0){m.alive=false;++out.kills;if(w.self.type==p10::Champion::vendetta)cooldown={0,0};}};
 auto collateral_known=[&](p10::Point center,double radius){for(int i=0;i<geo.mobs_count;++i)if(mobs[i].m.alive&&!mobs[i].known&&p10::distance(mobs[i].m.position,center)<=radius)return false;return true;};
 auto basic=[&](int target,Frame&frame){const auto center=mobs[target].m.position;if(w.self.type==p10::Champion::varg&&!collateral_known(center,1)){out.failure=Failure::unknown_hazard;return false;}
  if(frame.basics==0)frame.first_basic=mobs[target].m.id;else frame.second_basic=mobs[target].m.id;++frame.basics;
  hit(target,w.self.strength);if(w.self.type==p10::Champion::varg)for(int i=0;i<geo.mobs_count;++i)if(i!=target&&mobs[i].m.alive&&p10::distance(mobs[i].m.position,center)<=1)hit(i,w.self.strength);return true;};
 auto camp=[&](Frame&frame){if(visible_enemy_can_intervene(w,position)){out.failure=Failure::enemy;return false;}double incoming=0,neighbors=0;
  for(int i=0;i<geo.mobs_count;++i){if((i&7)==0&&!budget.fuel_ok()){out.failure=Failure::fuel;return false;}auto&item=mobs[i];auto&m=item.m;if(!m.alive||m.hp<=0)continue;
   const bool reaches=p10::distance(position,m.position)<=m.range;
   if(!item.known&&reaches&&m.strength>0){out.failure=Failure::unknown_hazard;return false;}
   if(item.stun>0){--item.stun;continue;}if(m.awake&&reaches){incoming+=m.strength;if(!item.source)neighbors+=m.strength;}
  }
  incoming*=camp_multiplier;neighbors*=camp_multiplier;const double absorbed=std::min(shield,incoming);shield-=absorbed;hp-=incoming-absorbed;
  frame.hp=hp;frame.shield=shield;frame.source_hp=source_hp();frame.camp_damage=incoming;frame.neighbor_damage=neighbors;
  if(hp<=floor){out.failure=Failure::health;return false;}return true;
 };
 auto record=[&](const Frame&frame){if(out.frames_used<max_frames){out.frames.push_back(frame);++out.frames_used;}};
 const int cap=std::clamp(o.turn_cap,1,max_frames-2),settle=std::clamp(o.settle_phases,0,2);
 out.frames.reserve(cap+settle);
 for(int turn=0;turn<cap;++turn){
  if(!budget.fuel_ok()){out.failure=Failure::fuel;return out;}
  if(shield_expiry<=turn)shield=0;
  Frame frame;frame.turn=turn;frame.from=frame.to=position;
  const bool move_available=turn>0||w.self.can_move,basic_available=turn>0||w.self.can_attack,cast_available=turn>0||w.self.can_cast;
  auto ready=[&](int slot){const auto&a=w.self.abilities[slot];return cast_available&&cooldown[slot]==0&&(turn>0||a.ready)&&(!a.exclusive||turn>0||w.self.can_move);};
  int target=-1,stance=-1,origin=-1;double best=-1;
  std::array<int,max_nodes>parent;
  if(positioned_basics){
   // Geometry already witnessed every alive source member in basic range at
   // this sole current node. No target can require a path or a different pad.
   // Preserve the general path's phase/open/target fuel checkpoints exactly.
   origin=0;if(!open(origin)){out.failure=fuel_aborted?Failure::fuel:Failure::occupied;return out;}
   if(!budget.fuel_ok()){out.failure=Failure::fuel;return out;}
   for(int i=0;i<geo.mobs_count;++i)if(mobs[i].source&&mobs[i].m.alive){
    if((i&3)==0&&!budget.fuel_ok()){out.failure=Failure::fuel;return out;}
    if(!budget.fuel_ok()){out.failure=Failure::fuel;return out;}
    const double q=target_order(i);if(q>best||(q==best&&(target<0||mobs[i].m.id<mobs[target].m.id))){best=q;target=i;stance=origin;}
   }
  }else{
  // Find a legal reachable interaction pad in the current world state. Dead
  // source spawns become graph vertices now; living or occupied tiles never do.
  std::array<int,max_nodes>dist{};dist.fill(-1);parent.fill(-1);std::array<int,max_nodes>queue{};int head=0,tail=0;
  for(int n=0;n<geo.count;++n)if(geo.nodes[n].p==position){origin=n;break;}
  if(origin<0||!open(origin)){out.failure=fuel_aborted?Failure::fuel:Failure::occupied;return out;}dist[origin]=0;queue[tail++]=origin;
  auto interaction=[&](p10::Point p){for(int i=0;i<geo.mobs_count;++i)if(mobs[i].source&&mobs[i].m.alive){if(p10::distance(p,mobs[i].m.position)<=w.self.range)return true;
   for(int slot=0;slot<2;++slot){const auto&a=w.self.abilities[slot];if(ready(slot)&&(a.targets_camps||o.ground_targets[slot])&&(a.damage||a.aoe||a.cc)&&p10::distance(p,mobs[i].m.position)<=a.range)return true;}}
   return false;};
  // A useful positioned action needs no flood. When movement is needed, stop
  // at the nearest interaction layer rather than expanding an entire room.
  while(head<tail){if((head&7)==0&&!budget.fuel_ok()){out.failure=Failure::fuel;return out;}const int n=queue[head++];if(interaction(geo.nodes[n].p))break;for(int next:geo.nodes[n].next)if(next>=0&&dist[next]<0&&open(next)){dist[next]=dist[n]+1;parent[next]=n;queue[tail++]=next;}}
  if(fuel_aborted){out.failure=Failure::fuel;return out;}
  for(int i=0;i<geo.mobs_count;++i)if(mobs[i].source&&mobs[i].m.alive){
   if((i&3)==0&&!budget.fuel_ok()){out.failure=Failure::fuel;return out;}int pad=-1,cost=100000;
   // The discovered queue is exactly the old dist>=0 set. Explicit node-index
   // tie breaks below preserve target/pad choice regardless of queue order.
   for(int reached=0;reached<tail;++reached){const int n=queue[reached];if((n&7)==0&&!budget.fuel_ok()){out.failure=Failure::fuel;return out;}bool can=p10::distance(geo.nodes[n].p,mobs[i].m.position)<=w.self.range;
    for(int slot=0;slot<2;++slot){const auto&a=w.self.abilities[slot];if(ready(slot)&&(a.targets_camps||o.ground_targets[slot])&&(a.damage||a.aoe||a.cc)&&p10::distance(geo.nodes[n].p,mobs[i].m.position)<=a.range)can=true;}
    if(can&&(dist[n]<cost||(dist[n]==cost&&(pad<0||n<pad)))){cost=dist[n];pad=n;}}
   if(pad>=0){double q=target_order(i)/(1+cost*2);if(q>best||(q==best&&(target<0||mobs[i].m.id<mobs[target].m.id))){best=q;target=i;stance=pad;}}
  }
  }
  if(fuel_aborted){out.failure=Failure::fuel;return out;}
  if(target<0){out.failure=Failure::blocked;return out;}
  p10::Directive first=p10::wait("mobile work has no legal initial action");first.group=g.id;first.valid_round=w.round;first.reserve_reset=true;first.strict_tank=true;first.template_id=90+int(mode);
  if(stance!=origin){
   if(!move_available){out.failure=Failure::no_action;return out;}int next=stance;while(parent[next]!=origin&&parent[next]>=0)next=parent[next];if(parent[next]!=origin){out.failure=Failure::blocked;return out;}
   position=geo.nodes[next].p;frame.to=position;frame.moved=true;++out.moves;first.task=p10::Task::move;first.destination=position;first.reason="one verified cardinal mobile-work step";
   // Every move is a separately charged owner turn, even for melee. Thus the
   // literal Task::move prefix and repeated fresh calls reproduce the proof.
  }else{
   int slot=-1,spell_target=-1;bool shielding=false;double benefit=0;
   double next_volley=0;for(int i=0;i<geo.mobs_count;++i)if(mobs[i].m.alive&&p10::distance(position,mobs[i].m.position)<=mobs[i].m.range)next_volley+=mobs[i].m.strength*camp_multiplier;
   for(int i=0;i<2;++i)if(ready(i)){const auto&a=w.self.abilities[i];
    if(a.targets_self&&a.shield&&a.modifier>0&&shield<next_volley*2&&shield_expiry<=turn+1){slot=i;shielding=true;break;}
    if((a.targets_camps||o.ground_targets[i])&&(a.damage||a.aoe||a.cc)&&a.modifier>0&&p10::distance(position,mobs[target].m.position)<=a.range){
     double amount=0;for(int m=0;m<geo.mobs_count;++m)if(mobs[m].m.alive&&(m==target||(a.aoe&&p10::distance(mobs[m].m.position,mobs[target].m.position)<=a.radius)))amount+=std::min(mobs[m].m.hp,a.modifier*w.self.focus*damage_multiplier);
     if(amount>benefit){benefit=amount;slot=i;shielding=false;spell_target=target;}}
   }
   if(slot>=0){const auto&a=w.self.abilities[slot];cooldown[slot]=a.max_cooldown>0?a.max_cooldown:cap+1;frame.casts=1;frame.cast_slot=slot;
    first.task=p10::Task::cast;first.cast_slot=slot;first.reason="exact mobile-work spell then freshly rechecked basic";
    if(shielding){shield=a.modifier*w.self.focus;shield_expiry=turn+std::max(0,a.duration);if(a.duration<=0)shield=0;}
    else{const auto center=mobs[spell_target].m.position;if(a.aoe&&!collateral_known(center,a.radius)){out.failure=Failure::unknown_hazard;return out;}frame.cast_target=mobs[spell_target].m.id;
     if(o.ground_targets[slot]&&!a.targets_camps){first.destination=center;first.monster=-1;}else first.monster=mobs[spell_target].m.id;
     for(int m=0;m<geo.mobs_count;++m)if(mobs[m].m.alive&&(m==spell_target||(a.aoe&&p10::distance(mobs[m].m.position,center)<=a.radius))){hit(m,a.modifier*w.self.focus);if(a.cc)mobs[m].stun=std::max(mobs[m].stun,std::max(0,a.duration-1));}
    }
    if(basic_available){const int follow=choose_basic();if(follow>=0){first.second_monster=mobs[follow].m.id;if(!basic(follow,frame))return out;}}
   }else{
    int initial=basic_available?choose_basic():-1;
    if(initial>=0){first.task=p10::Task::attack;first.monster=mobs[initial].m.id;first.reason="exact mobile-work basic/reset/basic compound";if(!basic(initial,frame))return out;}
    int reset=-1;for(int i=0;i<2;++i)if(!o.suppress_reset&&ready(i)&&w.self.abilities[i].targets_self&&w.self.abilities[i].reset){reset=i;break;}
    const int follow=reset>=0?choose_basic():-1;
    if(follow>=0){const auto&a=w.self.abilities[reset];cooldown[reset]=a.max_cooldown>0?a.max_cooldown:cap+1;frame.casts=1;frame.cast_slot=reset;first.cast_slot=reset;first.second_monster=mobs[follow].m.id;
     if(initial<0)first.task=p10::Task::cast;if(!basic(follow,frame))return out;
    }
   }
  }
  if(turn==0){if(first.task==p10::Task::wait){out.failure=Failure::no_action;return out;}out.first=first;}
  if(!camp(frame)){record(frame);return out;}record(frame);
  for(auto&cd:cooldown)cd=std::max(0,cd-1);
  if(source_alive()==0){
   out.clear_turns=turn+1;
   for(int extra=0;extra<settle;++extra){Frame end;end.turn=turn+1+extra;end.from=end.to=position;if(shield_expiry<=end.turn)shield=0;if(!camp(end)){record(end);return out;}record(end);}
   out.feasible=true;out.failure=Failure::none;out.hp=hp;out.position=position;
   out.first.safety_verified=out.first.local_safety_verified=true;out.first.safety_round=w.round;out.first.minimum_start_hp=w.self.hp;return out;
  }
 }
 out.failure=Failure::turn_cap;return out;
}
inline bool better(const Result&a,const Result&b){return a.feasible&&(!b.feasible||a.clear_turns<b.clear_turns||(a.clear_turns==b.clear_turns&&a.hp>b.hp));}
inline Result mobile_work(const p10::World&w,const p10::Group&g,Budget&budget,SkeletonCache&cache,const Options&requested={}){
 Options o=requested;for(int slot=0;slot<2;++slot)o.ground_targets[slot]|=w.self.abilities[slot].targets_ground;
 Result out;out.actor=w.self.id;out.round=w.round;out.source=g.id;
 CallProbe probe{w,g,budget,out};
 if(budget.actor!=w.self.id||budget.round!=w.round){out.failure=Failure::stale_budget;return out;}
 if(g.id<0||w.self.position.x<0||w.self.position.y<0||!w.self.combat_details_complete||!w.self.equipment_details_complete||!w.self.visible||!w.self.alive||w.self.channel!=p10::Channel::none||w.self.hp<=0||w.self.strength<=0||!p10::usable(g)||!g.living||g.monsters.empty()||g.monsters.size()>12||!w.geometry_complete||!w.actor_observation_complete)return out;
 for(const auto&m:g.monsters)if(!m.observed)return out;
 if(occupied(w,w.self.position)){out.failure=Failure::occupied;return out;}
 if(visible_enemy_can_intervene(w,w.self.position)){out.failure=Failure::enemy;return out;}
 if(budget.spent>=2){out.failure=Failure::trace_budget;return out;}if(!budget.fuel_ok()){out.failure=Failure::fuel;return out;}
#if P10_REMOTE_WORK_GUARD
 // The local proof never invents ingress from an isolated current tile. Before
 // allocating its graph, reject only the identical impossible first interaction:
 // no raw vertex is adjacent and no currently ready attack can reach a member.
 // This is a refusal, never authority to move, attack or ignore a live hazard.
 if(current_geometry_binding(g)){
  bool attached=false,interaction=false;int checks=0;
  const int count=int(g.spawns.size()+g.monsters.size()+g.approach_pads.size());
  for(int i=0;i<count&&!attached;++i){if((++checks&7)==0&&!budget.fuel_ok()){out.failure=Failure::fuel;return out;}attached=manhattan(raw_point(g,i).p,w.self.position)<=1;}
  if(!attached){
   for(const auto&m:g.monsters)if(m.alive&&m.hp>0){
    if(!budget.fuel_ok()){out.failure=Failure::fuel;return out;}
    const int distance=p10::distance(w.self.position,m.position);interaction|=distance<=w.self.range;
    for(int slot=0;slot<2;++slot){const auto&a=w.self.abilities[slot];
     const bool ready=w.self.can_cast&&w.self.cooldown[slot]==0&&a.ready&&(!a.exclusive||w.self.can_move);
     interaction|=ready&&(a.targets_camps||o.ground_targets[slot])&&(a.damage||a.aoe||a.cc)&&distance<=a.range;
    }
   }
   if(!interaction){out.failure=Failure::blocked;return out;}
  }
 }
#endif
 auto geo=geometry(w,g,o,budget,cache);if(geo.failure!=Failure::none){out.failure=geo.failure;return out;}
 out.failure=Failure::fuel;
 for(int attempt=0;attempt<std::clamp(o.max_traces,1,2);++attempt){
  const Mode mode=attempt==0?o.mode:o.mode==Mode::threat_first?Mode::open_spawns:Mode::threat_first;
  if(budget.spent>=2||!budget.fuel_ok())break;
  auto candidate=trace(w,g,geo,o,mode,budget);if(better(candidate,out)||!out.feasible)out=std::move(candidate);
 }
 return out;
}
// Convenience API: a fresh VM starts with an empty four-source immutable cache.
// Card callers still share Budget across every physical-work candidate this turn.
inline Result mobile_work(const p10::World&w,const p10::Group&g,Budget&budget,const Options&o={}){return mobile_work(w,g,budget,default_cache,o);}
} // namespace p10_mobile
