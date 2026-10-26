#pragma once
#include "mobile-work.h"

// Finite physical primitive only: current retained damage, then self-only
// cardinal retreat. Known sleeping, unstruck camps may cover the endpoint;
// fresh wake changes invalidate it. No capture, profit, future gear or allied work.
namespace p10_partial {
using namespace p10;
inline constexpr int max_nodes=64,max_mobs=32,max_escape=8;
enum class Prefix {basic,reset_basic,shield_basic,spell_basic,escape_only,hold_only};
enum class Failure {none,input,stale_budget,trace_budget,fuel,geometry,occupied,unknown_hazard,enemy,no_action,no_progress,no_exit,health};
// Optional scalar-only telemetry. No forecast, World or handles are retained.
// Stages identify the bounded operation in progress, including censored work.
enum class ProofStage {entry=0,validation=1,cast_failure=2,geometry=3,catalogue=4,occupancy=5,prefix=6,escape=7,first_phase=8,move_phase=9,settle_phase=10,complete=11};
struct ProofDiagnostic {
 int actor=-1,round=-1,source=-1,target=-1;Prefix prefix=Prefix::basic;
 ProofStage stage=ProofStage::entry;Failure failure=Failure::input;
 int fuel_before=-1,fuel_after=-1,spent_before=0,spent_after=0,action_reserve=0;
 // Optional fixed checkpoints; never sampled inside a loop. Unreached=-1.
 int geometry_fuel=-1,catalogue_fuel=-1,occupancy_fuel=-1,escape_fuel=-1,phase_fuel=-1,phase_end_fuel=-1;
 int groups_visited=0,groups_skipped=0,members_copied=0;
 bool completed=false,feasible=false;
 ProofStage child_stage=ProofStage::entry;Failure child_failure=Failure::none;
 int child_fuel_before=-1,child_fuel_after=-1;bool child_completed=false;
};
struct Options {ProofDiagnostic*diagnostic=nullptr;Prefix prefix=Prefix::basic;int target=-1,slot=-1,escape_cap=6,settle=2;Point endpoint{};double health_floor=.1;};
struct Phase {Point position{};double incoming=0,hp=0,shield=0;};
// Complete first phase, at most eight separately charged moves, two settles.
// Only live elements participate in the vector-like read interface. No phase
// is silently dropped: append refuses before writing beyond this hard bound.
struct PhaseSequence {
 std::array<Phase,max_escape+3>storage{};std::size_t used=0;
 std::size_t size()const{return used;}bool empty()const{return used==0;}
 static constexpr std::size_t capacity(){return max_escape+3;}
 Phase*data(){return storage.data();}const Phase*data()const{return storage.data();}
 Phase*begin(){return data();}const Phase*begin()const{return data();}
 Phase*end(){return data()+used;}const Phase*end()const{return data()+used;}
 Phase&operator[](std::size_t i){return storage[i];}const Phase&operator[](std::size_t i)const{return storage[i];}
 Phase&front(){return storage[0];}const Phase&front()const{return storage[0];}
 Phase&back(){return storage[used-1];}const Phase&back()const{return storage[used-1];}
 bool append(Phase phase){if(used==capacity())return false;storage[used++]=phase;return true;}
};
struct Result {
 bool feasible=false,conditional_actors=true,failed_cast_branch=false;Failure failure=Failure::input;
 int actor=-1,round=-1,source=-1,casts=0,basics=0,kills=0,moves=0,phases=0;
 double retained_damage=0,source_hp_after=0,hp=0,shield=0,failed_cast_hp=0;
 Point endpoint{};Directive first=wait("partial physical witness unavailable");
 std::array<Point,max_escape>exit{};PhaseSequence phase;
};
struct Mob {Monster m{};bool known=false,source=false;int stun=0;};
struct Node {Point p{};int distance=-1,parent=-1;};
// Only appended slots have a live object. Unused capacity is neither read nor
// value-initialized. These trivial, call-local objects need no heap or teardown.
template<class T,std::size_t N>struct ActiveStorage {
 static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);
 union Slot {T value;Slot()noexcept{}~Slot(){}};
 std::array<Slot,N>storage;std::size_t used=0;
 ActiveStorage()=default;
 ActiveStorage(const ActiveStorage&)=delete;ActiveStorage&operator=(const ActiveStorage&)=delete;
 bool append(T value){if(used==N)return false;std::construct_at(&storage[used].value,std::move(value));++used;return true;}
 T&operator[](std::size_t i){return storage[i].value;}
 const T&operator[](std::size_t i)const{return storage[i].value;}
};
// Fresh owner-call-local point index. Slots store node index+1, so zero is
// empty even for(0,0); equality uses both full coordinates, never the hash.
// At most64 nodes in128 slots leaves an empty slot on every valid lookup.
struct PointIndex {
 std::array<std::uint8_t,128>slots{};
 static unsigned hash(Point p){std::uint32_t v=std::uint32_t(p.x)*0x9e3779b1u^std::uint32_t(p.y)*0x85ebca77u;v^=v>>16;return v&127u;}
 // Return slot, -1 on live fuel abort, -2 on an invalid saturated table.
 template<class Nodes>int locate(Point p,const Nodes&nodes,const p10_mobile::Budget&budget)const{
  const unsigned first=hash(p);
  for(unsigned probe=0;probe<slots.size();++probe){
   if((probe&7u)==0&&!budget.fuel_ok())return -1;
   const unsigned at=(first+probe)&127u;const auto value=slots[at];
   if(value==0||nodes[value-1].p==p)return int(at);
  }return -2;
 }
};

inline Result partial_work(const World&w,const Group&g,p10_mobile::Budget&budget,const Options&o={}){
 Result out;out.actor=w.self.id;out.round=w.round;out.source=g.id;out.conditional_actors=!w.opposition_complete;
 auto*diag=o.diagnostic;
 auto sample=[&](){return budget.fuel_reader?budget.fuel_reader():-1;};
 if(diag){*diag={};diag->actor=w.self.id;diag->round=w.round;diag->source=g.id;diag->target=o.target;diag->prefix=o.prefix;diag->spent_before=budget.spent;diag->action_reserve=budget.action_reserve;diag->fuel_before=sample();}
 auto stage=[&](ProofStage value){if(!diag)return;diag->stage=value;
  switch(value){
  case ProofStage::geometry:diag->geometry_fuel=sample();break;
  case ProofStage::catalogue:diag->catalogue_fuel=sample();break;
  case ProofStage::occupancy:diag->occupancy_fuel=sample();break;
  case ProofStage::escape:diag->escape_fuel=sample();break;
  case ProofStage::first_phase:diag->phase_fuel=sample();break;
  default:break;
  }
 };
 auto fail=[&](Failure f){out.failure=f;if(diag){diag->failure=f;diag->feasible=out.feasible;diag->spent_after=budget.spent;diag->fuel_after=sample();if(diag->phase_fuel>=0)diag->phase_end_fuel=diag->fuel_after;diag->completed=true;}return out;};
 stage(ProofStage::validation);
 if(budget.actor!=w.self.id||budget.round!=w.round)return fail(Failure::stale_budget);
 if(budget.spent>=2)return fail(Failure::trace_budget);
 if(!budget.fuel_ok())return fail(Failure::fuel);
 const bool retreat=o.prefix==Prefix::escape_only||o.prefix==Prefix::hold_only;
 if(!w.self.visible||!w.self.alive||w.self.hp<=0||w.self.channel!=Channel::none||!w.geometry_complete||!w.actor_observation_complete||!w.self.combat_details_complete||!w.self.equipment_details_complete||g.monsters.empty()||g.monsters.size()>max_mobs)return fail(out.failure);
 if(!retreat&&(!usable(g)||!g.living||w.self.strength<=0))return fail(out.failure);
 const bool cast_prefix=o.prefix==Prefix::reset_basic||o.prefix==Prefix::shield_basic||o.prefix==Prefix::spell_basic;
 if(cast_prefix){
  stage(ProofStage::cast_failure);
  if(o.slot<0||o.slot>1||!w.self.can_cast||!w.self.abilities[o.slot].ready||w.self.cooldown[o.slot]>0)return fail(Failure::no_action);
  if(budget.spent+2>2)return fail(Failure::trace_budget);
  // Reproduce runtime failure semantics, rather than spending a safety stamp
  // to assume a shield/reset succeeded. Our certified Task::cast stops on a
  // failed cast; Task::attack already hit once before its explicit reset.
  Options missing=o;missing.slot=-1;missing.prefix=Prefix::hold_only;
  if(o.prefix==Prefix::reset_basic){for(const auto&m:g.monsters)if(m.alive&&m.hp>0&&w.self.can_attack&&distance(w.self.position,m.position)<=w.self.range){missing.prefix=Prefix::basic;break;}}
  ProofDiagnostic child;if(diag)missing.diagnostic=&child;
  auto branch=partial_work(w,g,budget,missing);
  if(diag){diag->child_stage=child.stage;diag->child_failure=child.failure;diag->child_fuel_before=child.fuel_before;diag->child_fuel_after=child.fuel_after;diag->child_completed=child.completed;}
  if(!branch.feasible)return fail(branch.failure);
  out.failed_cast_branch=true;out.failed_cast_hp=branch.hp;
 }
 if(budget.spent>=2)return fail(Failure::trace_budget);
 ++budget.spent; // One fresh complete prefix+retreat trajectory. Never resumed.
 stage(ProofStage::geometry);
 ActiveStorage<Node,max_nodes>nodes;if(!nodes.append({w.self.position}))return fail(Failure::geometry);int count=1;
 PointIndex point_index;point_index.slots[PointIndex::hash(w.self.position)]=1;
 for(const auto&p:w.visible_open_tiles){
  if(!budget.fuel_ok())return fail(Failure::fuel);if(p.x<0||p.y<0)return fail(Failure::geometry);
  const int at=point_index.locate(p,nodes,budget);if(at<0)return fail(at==-1?Failure::fuel:Failure::geometry);
  if(point_index.slots[at]==0){if(count==max_nodes)return fail(Failure::geometry);point_index.slots[at]=std::uint8_t(count+1);if(!nodes.append({p}))return fail(Failure::geometry);++count;}
 }
 // visible_open_tiles is the sole free-tile witness. Static approach pads,
 // dead future spawns and an immutable terrain route do not prove occupancy.
 ActiveStorage<Mob,max_mobs>mobs;int members=0;double initial_hp=0;
 int lx=w.self.position.x,hx=lx,ly=w.self.position.y,hy=ly;
 for(int n=0;n<count;++n){lx=std::min(lx,nodes[n].p.x);hx=std::max(hx,nodes[n].p.x);ly=std::min(ly,nodes[n].p.y);hy=std::max(hy,nodes[n].p.y);}
 for(const auto&m:g.monsters){lx=std::min(lx,m.position.x);hx=std::max(hx,m.position.x);ly=std::min(ly,m.position.y);hy=std::max(hy,m.position.y);}
 double collateral=w.self.type==Champion::varg?1.:0.;if(o.slot>=0&&o.slot<2)collateral=std::max(collateral,w.self.abilities[o.slot].radius);
 auto box_distance=[&](Point p){return std::max({lx-p.x,p.x-hx,ly-p.y,p.y-hy,0});};
 stage(ProofStage::catalogue);
 for(const auto&other:w.groups){
  if(diag)++diag->groups_visited;
  if(!budget.fuel_ok())return fail(Failure::fuel);if(other.cleared)continue;
  if(other.id!=g.id&&hazard_envelope_disjoint(other,w.round,lx,hx,ly,hy,collateral)){if(diag)++diag->groups_skipped;continue;}
  if(other.respawning){
   if(other.respawn_remaining<0||other.respawn_remaining<=std::clamp(o.escape_cap,0,max_escape)+3)for(const auto&m:other.monsters){if(!budget.fuel_ok())return fail(Failure::fuel);if(box_distance(m.position)<=m.range+collateral)return fail(Failure::unknown_hazard);}
   continue;
  }
  bool relevant=other.id==g.id;
  for(const auto&m:other.monsters){if(!budget.fuel_ok())return fail(Failure::fuel);if(!relevant)relevant=((m.alive&&m.hp>0)||!other.complete)&&box_distance(m.position)<=m.range+collateral;}
  if(!relevant)continue;
  for(const auto&m:other.monsters){
   if(!budget.fuel_ok())return fail(Failure::fuel);if(members==max_mobs)return fail(Failure::input);
   if(!mobs.append({m,other.observed&&other.complete&&m.observed,other.id==g.id,std::max(0,m.stun-1)}))return fail(Failure::input);++members;if(diag)++diag->members_copied;auto&v=mobs[members-1];
   if(!v.known){v.m.alive=true;v.m.hp=std::max({m.hp,m.max_hp,1.});}
   if(v.source&&v.m.alive)initial_hp+=std::max(0.,v.m.hp);
  }
 }
 auto occupied=[&](Point p){if(p10_mobile::occupied(w,p))return true;for(int i=0;i<members;++i)if(mobs[i].m.alive&&mobs[i].m.hp>0&&mobs[i].m.position==p)return true;return false;};
 stage(ProofStage::occupancy);
 if(occupied(w.self.position))return fail(Failure::occupied);
 const double multiplier=w.self.monster_emblem?1.1:1.,mitigation=w.self.jungle_emblem?.7:1.,floor=std::max(1.,w.self.max_hp*std::clamp(o.health_floor,0.,.9));
 double hp=w.self.hp,shield=w.self.shield;int shield_expiry=w.self.shield_until<0?-1:w.self.shield_until-w.round;
 if(shield_expiry<=0)shield=0;
 auto wake=[&](int id){for(int i=0;i<members;++i)if(mobs[i].m.group==id)mobs[i].m.awake=true;};
 auto hit=[&](int id,double damage){auto&m=mobs[id].m;if(!m.alive)return;wake(m.group);m.hp-=damage*multiplier;if(m.hp<=0){m.alive=false;++out.kills;}};
 bool unknown=false;
 auto collateral_known=[&](Point center,double radius){for(int i=0;i<members;++i)if(mobs[i].m.alive&&!mobs[i].known&&distance(center,mobs[i].m.position)<=radius)return false;return true;};
 auto choose=[&](){int best=-1;for(int i=0;i<members;++i)if(mobs[i].source&&mobs[i].m.alive&&distance(w.self.position,mobs[i].m.position)<=w.self.range){if(mobs[i].m.id==o.target)return i;if(best<0||mobs[i].m.id<mobs[best].m.id)best=i;}return best;};
 auto basic=[&](int id){const auto center=mobs[id].m.position;if(w.self.type==Champion::varg&&!collateral_known(center,1)){unknown=true;return false;}++out.basics;hit(id,w.self.strength);if(w.self.type==Champion::varg)for(int i=0;i<members;++i)if(i!=id&&mobs[i].m.alive&&distance(center,mobs[i].m.position)<=1)hit(i,w.self.strength);return true;};
 stage(ProofStage::prefix);
 Directive first;first.group=g.id;first.valid_round=w.round;first.reserve_reset=first.strict_tank=true;first.template_id=1900+int(o.prefix);
 int target=choose();
 if(o.prefix==Prefix::basic||o.prefix==Prefix::reset_basic){
  if(o.prefix==Prefix::basic&&(!w.self.can_attack||target<0))return fail(Failure::no_action);
  first.task=Task::attack;
  if(w.self.can_attack&&target>=0){first.monster=mobs[target].m.id;if(!basic(target))return fail(Failure::unknown_hazard);}
  if(o.prefix==Prefix::reset_basic){
   if(o.slot<0||o.slot>1)return fail(Failure::no_action);const auto&a=w.self.abilities[o.slot];
   if(!w.self.can_cast||!a.ready||w.self.cooldown[o.slot]>0||!a.targets_self||!a.reset)return fail(Failure::no_action);
   target=choose();
   // A killing first basic has already completed this prefix. Do not discard
   // its safe suffix because there is no remaining target worth resetting for.
   if(target>=0){++out.casts;first.cast_slot=o.slot;first.second_monster=mobs[target].m.id;
    if(out.basics==0)first.task=Task::cast;if(!basic(target))return fail(Failure::unknown_hazard);}
   else if(out.basics==0)return fail(Failure::no_action);
  }
 }else if(o.prefix==Prefix::shield_basic||o.prefix==Prefix::spell_basic){
  if(o.slot<0||o.slot>1||!w.self.can_cast)return fail(Failure::no_action);const auto&a=w.self.abilities[o.slot];
  if(!a.ready||w.self.cooldown[o.slot]>0)return fail(Failure::no_action);
  first.task=Task::cast;first.cast_slot=o.slot;++out.casts;
  if(o.prefix==Prefix::shield_basic){if(!a.shield||!a.targets_self||a.modifier<=0||a.duration<=0)return fail(Failure::no_action);shield=a.modifier*w.self.focus;shield_expiry=a.duration;}
  else{
   int spell=-1;for(int i=0;i<members;++i)if(mobs[i].source&&mobs[i].m.alive&&mobs[i].m.id==o.target&&distance(w.self.position,mobs[i].m.position)<=a.range)spell=i;
   if(spell<0||!a.targets_camps||!(a.damage||a.cc)||a.modifier<=0)return fail(Failure::no_action);
   const auto center=mobs[spell].m.position;if(a.aoe&&!collateral_known(center,a.radius))return fail(Failure::unknown_hazard);first.monster=mobs[spell].m.id;
   for(int i=0;i<members;++i)if(mobs[i].m.alive&&(i==spell||(a.aoe&&distance(center,mobs[i].m.position)<=a.radius))){if(!budget.fuel_ok())return fail(Failure::fuel);hit(i,a.modifier*w.self.focus);if(a.cc)mobs[i].stun=std::max(mobs[i].stun,std::max(0,a.duration-1));}
  }
  target=w.self.can_attack?choose():-1;if(target>=0){first.second_monster=mobs[target].m.id;if(!basic(target))return fail(Failure::unknown_hazard);}
 }else first.task=o.prefix==Prefix::escape_only?Task::move:Task::wait;
 if(unknown)return fail(Failure::unknown_hazard);
 double after=0;for(int i=0;i<members;++i)if(mobs[i].source&&mobs[i].m.alive)after+=std::max(0.,mobs[i].m.hp);
 out.source_hp_after=after;out.retained_damage=initial_hp-after;
 if(!retreat&&out.retained_damage<=0)return fail(Failure::no_progress);
 const int cap=std::clamp(o.escape_cap,0,max_escape);bool aborted=false;
 auto enemy=[&](Point p,int ticks){for(const auto&e:w.enemies)if(e.visible&&e.alive){double reach=e.range+ticks+1;for(const auto&a:e.abilities)if(a.damage||a.cc||a.aoe||a.dash)reach=std::max(reach,a.range+a.radius+(a.dash?e.range:0)+ticks+1);if(distance(e.position,p)<=reach)return true;}return false;};
 auto safe_endpoint=[&](Point p){if(o.endpoint.x>=0&&p!=o.endpoint)return false;for(int i=0;i<members;++i)if(mobs[i].m.alive&&mobs[i].m.strength>0&&(mobs[i].m.awake||!mobs[i].known)&&distance(p,mobs[i].m.position)<=mobs[i].m.range)return false;return !enemy(p,cap+1);};
 // BFS chooses a physical escape only. It ranks no source, gear or profits.
 stage(ProofStage::escape);
 std::array<int,max_nodes>queue;nodes[0].distance=0;queue[0]=0;int head=0,tail=1,end=-1;
 while(head<tail){if(!budget.fuel_ok())return fail(Failure::fuel);const int n=queue[head++];if(safe_endpoint(nodes[n].p)){end=n;break;}if(nodes[n].distance>=cap)continue;
  std::array<int,4>adjacent{};int adjacent_count=0;
  for(const auto&delta:std::array<Point,4>{{{1,0},{-1,0},{0,1},{0,-1}}}){
   const auto x=std::int64_t(nodes[n].p.x)+delta.x,y=std::int64_t(nodes[n].p.y)+delta.y;
   if(x<std::numeric_limits<int>::min()||x>std::numeric_limits<int>::max()||y<std::numeric_limits<int>::min()||y>std::numeric_limits<int>::max())continue;
   const int at=point_index.locate({int(x),int(y)},nodes,budget);if(at<0)return fail(at==-1?Failure::fuel:Failure::geometry);
   if(point_index.slots[at])adjacent[adjacent_count++]=point_index.slots[at]-1;
  }
  // The former scan considered all node IDs ascending. Preserve that exact
  // order, hence the same BFS endpoint, parent chain and literal first move.
  std::sort(adjacent.begin(),adjacent.begin()+adjacent_count);
  if(!budget.fuel_ok())return fail(Failure::fuel);
  for(int i=0;i<adjacent_count;++i){const int next=adjacent[i];if(nodes[next].distance<0&&!occupied(nodes[next].p)){nodes[next].distance=nodes[n].distance+1;nodes[next].parent=n;queue[tail]=next;++tail;}}
 }
 if(end<0)return fail(Failure::no_exit);
 out.moves=nodes[end].distance;out.endpoint=nodes[end].p;int cursor=end;for(int n=out.moves-1;n>=0;--n){out.exit[n]=nodes[cursor].p;cursor=nodes[cursor].parent;}
 auto camp=[&](Point p,int turn){
  if(!budget.fuel_ok()){aborted=true;out.failure=Failure::fuel;return false;}if(enemy(p,turn+1)){out.failure=Failure::enemy;return false;}if(shield_expiry<=turn)shield=0;double damage=0;
  for(int i=0;i<members;++i){if(!budget.fuel_ok()){aborted=true;out.failure=Failure::fuel;return false;}auto&v=mobs[i];if(!v.m.alive||v.m.hp<=0)continue;// Range cannot affect a known sleeping/zero-damage or stunned phase.
   // Unknown positive damage still checks before any stun decrement.
   const bool needs_range=(!v.known&&v.m.strength>0)||(v.stun<=0&&v.m.awake&&v.m.strength!=0);
   const bool reaches=needs_range&&distance(p,v.m.position)<=v.m.range;
   if(!v.known&&reaches&&v.m.strength>0){out.failure=Failure::unknown_hazard;return false;}if(v.stun>0){--v.stun;continue;}if(v.m.awake&&reaches)damage+=v.m.strength;
  }
  damage*=mitigation;const double absorbed=std::min(shield,damage);shield-=absorbed;hp-=damage-absorbed;
  if(!out.phase.append({p,damage,hp,shield})){out.failure=Failure::input;return false;}++out.phases;if(hp<=floor){out.failure=Failure::health;return false;}return true;
 };
 int tick=0;
 stage(ProofStage::first_phase);
 if(o.prefix!=Prefix::escape_only){if(!camp(w.self.position,tick++))return fail(out.failure);}
 if(out.moves>0&&!w.self.can_move&&o.prefix==Prefix::escape_only)return fail(Failure::no_action);
 stage(ProofStage::move_phase);
 for(int n=0;n<out.moves;++n)if(!camp(out.exit[n],tick++))return fail(out.failure);
 stage(ProofStage::settle_phase);
 for(int n=0;n<2;++n)if(!camp(out.endpoint,tick++))return fail(out.failure);
 if(aborted||!budget.fuel_ok())return fail(Failure::fuel);
 if(o.prefix==Prefix::escape_only){if(out.moves==0){first=wait("fresh actor already occupies the verified camp-free exit");}else{first.task=Task::move;first.destination=out.exit[0];}}
 first.reason=o.prefix==Prefix::escape_only?"fresh self-only cardinal exit commitment":"current retained damage plus separately charged next-owner escape";
 // This finite local certificate covers fresh visible actors and camp phases.
 // It is conditional on unseen adversaries and must be rebuilt next owner.
 first.safety_verified=first.local_safety_verified=true;first.safety_round=w.round;first.minimum_start_hp=w.self.hp;
 out.first=first;out.hp=hp;out.shield=shield;out.feasible=true;stage(ProofStage::complete);return fail(Failure::none);
}
}
