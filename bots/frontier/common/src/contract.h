#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace p10 {
using Gems=std::int64_t;
enum class Arm { active, control };
inline bool team_starts(bool own_is_team_b,int round){return round%2==1?!own_is_team_b:own_is_team_b;}
inline int equivalent_shards(int loose,int emblems){return loose+5*emblems;}
enum class Champion { varg,stellar,vendetta,willow,tavi,vesper,marcella,prism,ren,ryth,heart,cassi,issac,other };
struct Point { int x=-1,y=-1; bool operator==(const Point&) const=default; };
inline int distance(Point a,Point b) {return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
enum class Channel { none,tribute,recall,teleport };
enum class Item { none,strength,health,focus,monster_strength,jungle_health,durability_health,home_health,treasure_strength,lock,monster_focus,jungle_focus,monster_health,jungle_strength };
struct Ability {
 bool ready=false,damage=false,aoe=false,reset=false,shield=false,heal=false,cc=false,dash=false,exclusive=false;
 double modifier=0,radius=0,range=0; int cooldown=0,duration=0,max_cooldown=0;
 bool targets_camps=true,targets_self=true,targets_ground=false;
};
struct Actor {
 int id=-1,pick=-1; Champion type=Champion::other; Point position{};
 bool visible=false,alive=false,ally=false,at_spawn=false,can_move=false,can_attack=false,can_cast=false;
 double hp=0,max_hp=0,strength=0,focus=0,shield=0; int range=1,priority=0,shield_until=-1;
 Gems bag=0; Channel channel=Channel::none; int channel_remaining=0;
 std::array<Ability,2> abilities{}; std::array<int,2> cooldown{0,0};
 int strength_shards=0,health_shards=0,focus_shards=0; bool monster_emblem=false,jungle_emblem=false;
 double strength_growth=0,health_growth=0,focus_growth=0;
 std::array<int,14> inventory{};
 bool combat_details_complete=true;
 // Independent from live combat readiness: false means equipment is unobserved,
 // never an observed absence of shards/passives. Camp forecasts must refuse it.
 bool equipment_details_complete=true;
};
struct Monster {
 int id=-1,group=-1; Point position{}; bool observed=false,alive=true,awake=false;
 double hp=0,max_hp=0,strength=0; int range=0,respawn=0,stun=0;
};
struct SourceGeometry {
 struct Member {int id=-1;Point position{};};
 int source=-1;std::vector<Point>spawns,approach_pads;std::vector<Member>members;
};
struct HazardEnvelope {
 int source=-1,round=-1,members=0;Point minimum{},maximum{};bool valid=false;
 const SourceGeometry*geometry_owner=nullptr;
};
struct Group {
 int id=-1; Point position{},approach{}; std::vector<Point> spawns,approach_pads; std::vector<Monster> monsters;
 bool observed=false,complete=false,living=false,cleared=false,respawning=false,contested=false,occupied=false;
 bool route_known=false,safe_solo=false,program_available=true; int travel=-1,to_bank=-1,to_spawn=-1;
 int respawn=0,respawn_remaining=-1,last_payout=-1,hold_since=-1,own_holders=0,lowest_holder=-1;
 Gems gems=0,gold=0; double hp=0,incoming=0; int clear_turns=-1;
 std::shared_ptr<const SourceGeometry>source_geometry;
 HazardEnvelope hazard_envelope{};
};
// Fresh broad phase only. Include every member, even a corpse, unseen member or
// future respawn. An unavailable envelope always falls back to exact collection.
inline void begin_hazard_envelope(Group&g,int round){
 auto&e=g.hazard_envelope;e={};e.source=g.id;e.round=round;e.geometry_owner=g.source_geometry.get();
 e.valid=round>=0&&g.id>=0&&e.geometry_owner&&e.geometry_owner->source==g.id&&!g.monsters.empty()&&e.geometry_owner->members.size()==g.monsters.size();
 e.minimum={std::numeric_limits<int>::max(),std::numeric_limits<int>::max()};e.maximum={std::numeric_limits<int>::min(),std::numeric_limits<int>::min()};
}
inline void extend_hazard_envelope(Group&g,const Monster&m,std::size_t index){
 auto&e=g.hazard_envelope;if(!e.valid)return;
 if(!e.geometry_owner||e.geometry_owner!=g.source_geometry.get()||m.position.x<0||m.position.y<0||m.range<0||index!=std::size_t(e.members)||index>=e.geometry_owner->members.size()||e.geometry_owner->members[index].id!=m.id||e.geometry_owner->members[index].position!=m.position){e.valid=false;return;}
 const std::int64_t lx=std::int64_t(m.position.x)-m.range,hx=std::int64_t(m.position.x)+m.range,ly=std::int64_t(m.position.y)-m.range,hy=std::int64_t(m.position.y)+m.range;
 if(lx<std::numeric_limits<int>::min()||ly<std::numeric_limits<int>::min()||hx>std::numeric_limits<int>::max()||hy>std::numeric_limits<int>::max()){e.valid=false;return;}
 e.minimum.x=std::min(e.minimum.x,int(lx));e.minimum.y=std::min(e.minimum.y,int(ly));e.maximum.x=std::max(e.maximum.x,int(hx));e.maximum.y=std::max(e.maximum.y,int(hy));++e.members;
}
inline bool hazard_envelope_disjoint(const Group&g,int round,int lx,int hx,int ly,int hy,double collateral){
 const auto&e=g.hazard_envelope;
 if(!e.valid||e.source!=g.id||e.round!=round||e.geometry_owner!=g.source_geometry.get()||!g.source_geometry||e.members!=int(g.monsters.size())||e.minimum.x>e.maximum.x||e.minimum.y>e.maximum.y||lx>hx||ly>hy||!std::isfinite(collateral)||collateral<0)return false;
 return double(e.maximum.x)+collateral<lx||double(e.minimum.x)-collateral>hx||double(e.maximum.y)+collateral<ly||double(e.minimum.y)-collateral>hy;
}
struct Altar {int id=-1;Point position{},approach{};bool visible=false,enemy_lock=false,friendly_lock=false,route_known=false;int travel=-1,lock_hits=0;};
struct Receipt {
 enum class Kind { none,attack,cast,payout,bank,purchase,death,blocked,heal,priority } kind=Kind::none;
 int round=-1,group=-1,actor=-1,item=0; Gems gems=0; int gold=0; double amount=0;
};
// Economic receipts survive a skipped policy turn and bounded combat history.
// Only a completed current-turn policy call acknowledges the exposed inbox.
struct EconomicInbox {
 std::array<Receipt,3> pending{};
 static int slot(Receipt::Kind k){return k==Receipt::Kind::payout?0:k==Receipt::Kind::bank?1:k==Receipt::Kind::purchase?2:-1;}
 void note(const Receipt&r){const int i=slot(r.kind);if(i>=0)pending[i]=r;}
 bool has(Receipt::Kind k)const{const int i=slot(k);return i>=0&&pending[i].kind==k;}
 void append(std::vector<Receipt>&out)const{for(const auto&r:pending)if(r.kind!=Receipt::Kind::none&&std::none_of(out.begin(),out.end(),[&](const auto&v){return v.kind==r.kind&&v.round==r.round&&v.actor==r.actor&&v.group==r.group;}))out.push_back(r);}
 void acknowledge(){pending={};}
};
struct Profile {const char* slug="p10";std::array<Champion,5> draft{Champion::varg,Champion::tavi,Champion::marcella,Champion::vendetta,Champion::willow};};
struct World {
 int round=0,picks=3,max_rounds=2000,win_gems=1000,capture_turns=5,recall_turns=5,respawn_turns=60;
 int own_gold=0,shard_cost=30,lock_cost=10,lock_hits=5,entry_fuel=100000,fuel=100000,action_reserve=40000;
 double tribute_multiplier=.1,spawn_healing=1.;int tribute_base=0,tribute_minimum=1;
 Gems score=0,enemy_score=0; Actor self{};std::vector<Actor> allies,enemies;std::vector<Group> groups;std::vector<Altar> altars;
 std::vector<Receipt> receipts;Receipt last{};
 std::vector<Point> visible_open_tiles;
 bool safe_now=true,capture_active=false,own_payout=false,own_bank_completed=false,own_purchase=false,has_lock=false;
 bool geometry_complete=true,observation_complete=true,actor_observation_complete=true,opposition_complete=false,own_is_team_b=false,our_team_starts=false; int productive_group=-1,last_payout_group=-1,last_payout_round=-1,last_bank_round=-1;
 Point spawn{};int to_spawn=-1;
 int ticks(Gems bag) const {if(bag<=0)return 0;return std::max(tribute_minimum,int(std::ceil(double(bag)*tribute_multiplier))+tribute_base);}
 const Group* group(int id)const {for(const auto&g:groups)if(g.id==id)return &g;return nullptr;}
 const Actor* actor(int id)const {if(self.id==id)return &self;for(const auto&a:allies)if(a.id==id)return &a;for(const auto&a:enemies)if(a.id==id)return &a;return nullptr;}
 const Actor* pick(int i)const {if(self.pick==i)return &self;for(const auto&a:allies)if(a.pick==i)return &a;return nullptr;}
 const Altar* altar(int id)const {for(const auto&a:altars)if(a.id==id)return &a;return nullptr;}
 int remaining()const{return std::max(0,max_rounds-round);}
};
enum class Task { independent,farm,capture,bank,recover,move,inspect,attack,heal,shield,cast,wait,buy,lock,priority,release };
struct Directive {
 Task task=Task::independent;int group=-1,actor=-1,monster=-1,altar=-1;Point destination{};
 int cast_slot=-1,priority=0;Item item=Item::none;int buy_count=1;
 bool pulse=false,reserve_reset=false,strict_tank=false;const char* reason="ordinary income";
 int valid_round=-1,template_id=0,expected_min=-1,expected_max=-1;
 bool safety_verified=false,local_safety_verified=false;int safety_round=-1;double minimum_start_hp=0;int second_monster=-1;
 // Only an actual one-basic camp witness may request its separate experimental
 // executor allowance. This never applies to movement, resets or casts.
 bool finite_basic_prefix=false;
};
inline bool fresh_certificate(const World&w,const Directive&d){
 return d.safety_verified&&d.safety_round==w.round&&w.geometry_complete&&w.actor_observation_complete&&(w.observation_complete||d.local_safety_verified)&&w.self.hp>=d.minimum_start_hp&&d.minimum_start_hp>0;
}
// A complete immutable catalogue plus a fresh actor frame permits local work.
// Uncollected monsters keep their base reach/strength and explicit unknownness.
inline double possible_phase_damage(const World&w,Point p){double incoming=0;
 for(const auto&g:w.groups)if(!g.cleared&&!g.respawning)for(const auto&m:g.monsters)
  if(m.alive&&m.strength>0&&distance(m.position,p)<=m.range&&(!g.complete||!m.observed||m.awake))incoming+=m.strength;
 return incoming;
}
inline std::vector<std::size_t> observation_order(const World&w,int held,int intent,bool visible_only=false){
 std::vector<std::size_t> order;order.reserve(w.groups.size());std::vector<int> tiers(w.groups.size(),3);
 auto tier=[&](const Group&g){if(g.id==held)return 0;for(const auto&m:g.monsters)if(m.strength>0&&distance(m.position,w.self.position)<=m.range+1)return 1;return g.id==intent?2:3;};
 for(std::size_t i=0;i<w.groups.size();++i)if(!visible_only||w.groups[i].observed){order.push_back(i);tiers[i]=tier(w.groups[i]);}
 std::sort(order.begin(),order.end(),[&](auto i,auto j){const auto&a=w.groups[i];const auto&b=w.groups[j];if(tiers[i]!=tiers[j])return tiers[i]<tiers[j];const int ad=a.travel<0?100000:a.travel,bd=b.travel<0?100000:b.travel;return ad!=bd?ad<bd:a.id<b.id;});return order;
}
inline bool observation_required(const World&w,const Group&g,int held,int intent){
 if(g.id==held||g.id==intent)return true;
 int lx=w.self.position.x,hx=lx,ly=w.self.position.y,hy=ly;
 for(auto p:w.visible_open_tiles){lx=std::min(lx,p.x);hx=std::max(hx,p.x);ly=std::min(ly,p.y);hy=std::max(hy,p.y);}
 // This only budgets observation. Unread entries remain unknown hazards; the
 // later physical witness must still reject any relevant missing member.
 for(const auto&m:g.monsters)if(std::max({lx-m.position.x,m.position.x-hx,ly-m.position.y,m.position.y-hy,0})<=m.range+1)return true;
 return false;
}
template<class Fuel>inline bool partial_walk_safe(const World&w,Point p,Fuel fuel_ok){
 if(!fuel_ok()||!w.self.equipment_details_complete||!w.geometry_complete||!w.actor_observation_complete)return false;
 double incoming=0;
 for(const auto&g:w.groups)if(!g.cleared&&!g.respawning)for(const auto&m:g.monsters){
  if(!fuel_ok())return false;
  if(m.alive&&m.strength>0&&distance(m.position,p)<=m.range){
   if(!g.complete||!m.observed)return false;
   if(m.awake)incoming+=m.strength;
  }
 }
 return incoming*2*(w.self.jungle_emblem?.7:1.)+std::max(1.,w.self.max_hp*.08)<w.self.hp;
}
inline bool partial_walk_safe(const World&w,Point p){return partial_walk_safe(w,p,[]{return true;});}
enum class HoldObservation { lost,unknown,capturing,payout };
inline HoldObservation observe_hold(const World&w,const Group&g,int last_round,int snapshot_round,Gems start_bag,int start_gold,int passive_gold){
 const bool here=std::find(g.spawns.begin(),g.spawns.end(),w.self.position)!=g.spawns.end();
 if(last_round!=w.round-1||!here||!w.self.alive)return HoldObservation::lost;
 if(!g.observed||!g.complete)return HoldObservation::unknown;
 const bool received=w.self.bag>start_bag||(g.gems==0&&w.own_gold>start_gold+passive_gold*std::max(0,w.round-snapshot_round));
 if(g.respawning&&received)return HoldObservation::payout;
 return g.cleared&&!g.contested?HoldObservation::capturing:HoldObservation::lost;
}
inline Directive farm(int group,const char*why="productive source"){Directive d;d.task=Task::farm;d.group=group;d.reason=why;return d;}
inline Directive bank(const char*why="complete conversion"){Directive d;d.task=Task::bank;d.reason=why;return d;}
inline Directive wait(const char*why="preserve safe work"){Directive d;d.task=Task::wait;d.reason=why;return d;}
inline Directive buy(Item item,int count=1,const char*why="personal capital"){Directive d;d.task=Task::buy;d.item=item;d.buy_count=count;d.reason=why;return d;}
inline bool usable(const Group&g){return g.program_available&&g.observed&&g.complete&&g.route_known&&!g.respawning&&!g.contested;}
inline int cycle_ticks(const World&w,const Group&g){return g.travel+std::max(0,g.clear_turns)+w.capture_turns+1+std::max(0,g.to_bank)+w.ticks(w.self.bag+g.gems);}
// Ordinary fallback is shared execution plumbing only; card models choose their
// own organizations, endpoint and horizon rather than sharing an admission brain.
inline Directive ordinary(const World&w){
 if(w.self.bag>0)return bank();
 if(!w.safe_now)return {Task::recover};
 const Group*best=nullptr;
 for(const auto&g:w.groups)if(usable(g)&&g.safe_solo&&g.travel>=0&&g.to_bank>=0&&cycle_ticks(w,g)<=w.remaining())
   if(!best||g.travel<best->travel||(g.travel==best->travel&&g.id<best->id))best=&g;
 return best?farm(best->id):Directive{};
}
struct DraftContext {int picks=3,width=0,height=0;std::vector<Group> groups;std::vector<Altar> altars;std::vector<Actor> champions;Point spawn{};};
} // namespace p10
