#pragma once
#include "../../common/src/mobile-work.h"
namespace p10_foundry_local {
using namespace p10;
struct Pair {
 static constexpr int separation=6;
 int selections=0,reappointments=0,witnesses=0,last_owned=-1,last_bank=-1,bank_return=-1,returns=0;
 int avoid=-1,avoid_round=-1,avoid_epoch=-2;double avoid_hp=0,avoid_strength=0,avoid_focus=0,avoid_shield=0,avoid_group_hp=0;Point avoid_position{};std::array<int,2>avoid_cd{};std::uint64_t avoid_actors=0;
 bool deferred=false;
 // Only immutable source identity/geometry survives a deferred startup. No
 // Group*, actor stats, lifecycle, health, refusal result or ranking is retained.
 struct Geometry {
  struct Node {int id=-1;Point p{};std::shared_ptr<const SourceGeometry>owner;};
  struct Edge {int a=0,b=0,span=0;};
  std::array<Node,128>nodes{};std::array<Edge,512>edges{};
  Point spawn{};int count=-1,a=0,b=1,size=0,steps=0,last_round=-1;bool ready=false,overflow=false;
  bool matches(const World&w)const {
   if(count!=int(w.groups.size())||!(spawn==w.spawn))return false;
   for(int i=0;i<count;++i){const auto&g=w.groups[i];if(nodes[i].id!=g.id||!(nodes[i].p==g.position)||nodes[i].owner!=g.source_geometry)return false;}return true;
  }
  void bind(const World&w){
   count=int(w.groups.size());spawn=w.spawn;a=0;b=1;size=steps=0;last_round=-1;ready=count<2;overflow=false;
   for(int i=0;i<count;++i)nodes[i]={w.groups[i].id,w.groups[i].position,w.groups[i].source_geometry};
  }
  bool prepare(const World&w,WorkFuelReader reader){
   auto fuel=[&]{return !reader||reader()>=w.action_reserve+2000;};
   if(w.groups.size()>nodes.size()||!fuel())return false;
   if(!matches(w))bind(w);if(ready)return true;if(overflow||last_round==w.round)return false;last_round=w.round;
   // At most64 unordered comparisons/owner turn. A guard failure keeps this
   // immutable cursor; a new frame never resumes a mutable best-so-far choice.
   for(int done=0;done<64&&a<count-1;++done){
    if((done&7)==0&&!fuel())return false;
    const auto&p=nodes[a].p;const auto&q=nodes[b].p;const int d=distance(p,q);
    if(d<=separation){if(size==int(edges.size())){overflow=true;return false;}edges[size++]={a,b,std::max(distance(spawn,p),distance(spawn,q))+3*d};}
    ++steps;if(++b==count){++a;b=a+1;}
   }
   ready=a>=count-1;return ready;
  }
 }geometry;
 static bool contains(const std::array<int,2>&cells,int id){return id>=0&&(cells[0]==id||cells[1]==id);}
 static std::uint64_t actors(const World&w){std::uint64_t h=1469598103934665603ull;for(const auto*a:{&w.allies,&w.enemies})for(const auto&v:*a)if(v.visible&&v.alive){h=(h^std::uint64_t(v.id+1))*1099511628211ull;h=(h^std::uint64_t(v.position.x+1))*1099511628211ull;h=(h^std::uint64_t(v.position.y+1))*1099511628211ull;}return h;}
 bool refused(const World&w,const Group&g)const{return avoid==g.id&&w.round<=avoid_round+12&&g.last_payout==avoid_epoch&&g.hp==avoid_group_hp&&w.self.hp==avoid_hp&&w.self.strength==avoid_strength&&w.self.focus==avoid_focus&&w.self.shield==avoid_shield&&w.self.position==avoid_position&&w.self.cooldown==avoid_cd&&actors(w)==avoid_actors;}
 static int birth_work(const Group&g,double strength){if(strength<=0||g.monsters.empty())return 1000;int n=0;for(const auto&m:g.monsters){if(m.max_hp<=0||!std::isfinite(m.max_hp))return 1000;n+=int(std::ceil(m.max_hp/strength));if(n>64)return n;}return n;}
 static bool source(const Group&g){return g.id>=0&&g.route_known&&g.program_available&&!g.contested&&!g.monsters.empty();}
 bool valid(const World&w,const std::array<int,2>&cells)const{const auto*a=w.group(cells[0]);const auto*b=w.group(cells[1]);return a&&b&&a->id!=b->id&&source(*a)&&source(*b)&&distance(a->position,b->position)<=separation&&!refused(w,*a)&&!refused(w,*b);}
 bool appoint(const World&w,std::array<int,2>&cells,WorkFuelReader reader,bool force=false){
  deferred=false;if(!force&&valid(w,cells))return true;
  const auto current_reader=reader?reader:work_fuel_reader;
  auto fuel=[&]{return !current_reader||current_reader()>=w.action_reserve+2000;};
  if(!geometry.prepare(w,current_reader)){deferred=true;return false;}
  // Reject an impossible current-body pair before constructing/ranking128
  // mutable entries. This uses the original64/48, reward and HP conditions;
  // no work or shield from a future partner is credited.
  bool capacity=true;const double strength=w.self.strength*(w.self.monster_emblem?1.1:1.);
  auto birth=[&](const Group&g){int work=0;if(!(strength>0)||!std::isfinite(strength)||g.monsters.empty()||g.monsters.size()>12)return 1000;for(const auto&m:g.monsters){if(!fuel()){capacity=false;return 1000;}if(!(m.max_hp>0)||!std::isfinite(m.max_hp))return 1000;const double n=std::ceil(m.max_hp/strength);if(n>64-work)return 1000;work+=int(n);}return work;};
  bool possible=false;for(int k=0;k<geometry.size&&!possible;++k){if(!fuel()){deferred=true;return false;}const auto&e=geometry.edges[k];
   for(int direction=0;direction<2;++direction){const auto&a=w.groups[direction?e.b:e.a];const auto&b=w.groups[direction?e.a:e.b];if(!source(a)||!source(b)||a.respawning||b.respawning||refused(w,a)||refused(w,b)||a.gems<=0||b.gems+b.gold<=0)continue;const int aw=birth(a),bw=birth(b);if(!capacity){deferred=true;return false;}if(aw>64||bw>48)continue;double damage=0;for(const auto&m:b.monsters){if(!fuel()){deferred=true;return false;}damage+=std::max(0.,m.strength);}if(damage>0&&w.self.hp<=damage*(bw+2)*(w.self.jungle_emblem?.7:1.)+std::max(30.,w.self.max_hp*.1))continue;possible=true;break;}
  }
  if(!possible){if(force||!valid(w,cells))cells={-1,-1};return false;}
  // This is a proposal screen over full-birth public stats, not a promised
  // partner trajectory. Every actual body still pays its own fresh <=48 proof.
  struct Entry {const Group*g=nullptr;int work=1000;double damage=0;};std::array<Entry,128>e{};if(w.groups.size()>e.size()){deferred=true;return false;}
  int n=0;for(const auto&g:w.groups){if((n&3)==0&&!fuel()){deferred=true;return false;}auto&v=e[n++];v.g=&g;if(!source(g)||g.respawning||refused(w,g))continue;v.work=birth_work(g,w.self.strength*(w.self.monster_emblem?1.1:1.));for(const auto&m:g.monsters)v.damage+=std::max(0.,m.strength);}
  int first=-1,second=-1,best_span=100000,best_i=1000,best_j=1000;double best_value=-1;
  for(int k=0;k<geometry.size;++k){if((k&3)==0&&!fuel()){deferred=true;return false;}const auto&edge=geometry.edges[k];
   for(int direction=0;direction<2;++direction){const int i=direction?edge.b:edge.a,j=direction?edge.a:edge.b;const auto&a=e[i];const auto&b=e[j];if(a.work>64||a.g->gems<=0||b.work>48||b.g->gems+b.g->gold<=0)continue;
    // The second duty must be independently plausible for this actual body;
    // shield/ally damage is never fabricated by this cheap admission screen.
    if(b.damage>0&&w.self.hp<=b.damage*(b.work+2)*(w.self.jungle_emblem?.7:1.)+std::max(30.,w.self.max_hp*.1))continue;
    const int span=edge.span;
    const double value=a.g->gems*6+b.g->gems*2+(a.g->gold+b.g->gold)*.05;
    if(span<best_span||(span==best_span&&(value>best_value||(value==best_value&&(i<best_i||(i==best_i&&j<best_j)))))){first=a.g->id;second=b.g->id;best_span=span;best_value=value;best_i=i;best_j=j;}
   }
  }
  if(!fuel()){deferred=true;return false;}
  if(first<0){if(force||!valid(w,cells))cells={-1,-1};return false;}
  if(cells[0]>=0&&cells!=std::array<int,2>{first,second})++reappointments;
  cells={first,second};++selections;if(bank_return>=0&&!contains(cells,bank_return))bank_return=first;return true;
 }
 bool refuse(const World&w,const Group&g,p10_mobile::Failure f){
  const bool durable=f==p10_mobile::Failure::turn_cap||f==p10_mobile::Failure::blocked||(f==p10_mobile::Failure::health&&w.self.hp>=w.self.max_hp*.9);
  if(!g.observed||!g.complete||!durable)return false;
  avoid=g.id;avoid_round=w.round;avoid_epoch=g.last_payout;avoid_group_hp=g.hp;avoid_hp=w.self.hp;avoid_strength=w.self.strength;avoid_focus=w.self.focus;avoid_shield=w.self.shield;avoid_position=w.self.position;avoid_cd=w.self.cooldown;avoid_actors=actors(w);return true;
 }
 void observe(const World&w,const std::array<int,2>&cells){
  if(w.own_payout&&contains(cells,w.last_payout_group))last_owned=w.last_payout_group;
  const int round=w.last_bank_round>=0?w.last_bank_round:w.round;
  if(w.own_bank_completed&&round>last_bank){last_bank=round;if(last_owned>=0){bank_return=contains(cells,last_owned)?last_owned:cells[0];++returns;}}
 }
 Directive return_to_pair(const World&w,const std::array<int,2>&cells){
  if(bank_return<0)return {};const Group*g=w.group(contains(cells,bank_return)?bank_return:cells[0]);
  if(!g||!source(*g))return wait("local Foundry service return awaits a represented pair");
  if(g->respawning||g->contested){const auto*other=w.group(g->id==cells[0]?cells[1]:cells[0]);if(other&&source(*other)&&!other->respawning)g=other;}
  bank_return=g->id;
  if(!g->observed||!g->complete||distance(w.self.position,g->position)>4){Directive d;d.task=Task::inspect;d.group=g->id;d.reserve_reset=true;d.template_id=2026;d.reason="actual Foundry bank returns to its local source/service cell before optional capital";return d;}
  bank_return=-1;if(g->cleared&&!g->contested){Directive d;d.task=Task::capture;d.group=g->id;d.template_id=2024;d.reason="returned Foundry owner observes actual cleared custody";return d;}return {};
 }
};
}
