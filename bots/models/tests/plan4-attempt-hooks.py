"""Exercise actual attempt dispatch/helpers with a narrow, action-counting SDK double.

This is an offline source-bound check, not an engine or native-fuel validation.
The named collect_preaction boundary uses a deterministic double; the actual SDK
collector has a separate pinned-SDK source test. This harness checks its callers.
The farm movement path uses the real macro_move admission prefix and thin
move_to wrapper, followed by a controlled pending/ready/blocked route remainder.
Both arms use -O3 and the WASI linker's default 64 KiB stack. Failed scratch is
retained; successful scratch is verified before and after its removal.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time


ROOT = Path(__file__).resolve().parents[3]
INPUTS = tuple(ROOT / path for path in (
    'bots/models/tests/plan4-attempt-hooks.py',
    'bots/models/common/attempt-preaction.h', 'bots/models/common/attempts.h',
    'bots/models/common/attempt-methods.h',
    'bots/models/common/legacy.h', 'bots/models/common/macro-methods.h',
    'bots/models/common/movement-methods.h',
    'bots/models/common/pure.h', 'bots/models/common/alternate-route.h', 'bots/models/common/telemetry.h',
    'bots/models/common/legacy-policy.h', 'bots/models/tools/kernel_attempt_log.py',
))


def method(source, signature):
    start = source.index(signature)
    cursor = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[start:cursor]


def macro_source(source):
    """Retain the actual PLAN4_MACRO_MODE=true branches before brace parsing."""
    stack, output = [], []
    for line in source.splitlines():
        directive = re.match(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', line)
        if directive:
            kind, expression = directive.groups()
            visible = all(frame[1] for frame in stack)
            if kind in ('if', 'ifdef', 'ifndef'):
                hook = 'PLAN4_MACRO_MODE' in expression
                assert not hook or (kind == 'if' and expression.strip() == 'PLAN4_MACRO_MODE')
                if not hook and visible:
                    output.append(line)
                stack.append([hook, True])
            elif kind in ('else', 'elif'):
                assert stack
                if stack[-1][0]:
                    assert kind == 'else'
                    stack[-1][1] = False
                elif visible:
                    output.append(line)
            else:
                assert stack
                hook, _ = stack.pop()
                if not hook and visible:
                    output.append(line)
        elif all(frame[1] for frame in stack):
            output.append(line)
    assert not stack
    return '\n'.join(output)


def movement_admission(source):
    """Use the real entry guards; route execution has its own full-body fixture."""
    body = method(source, 'bool macro_move(')
    boundary = body.index('    const unsigned fuel_before = ')
    return body[:boundary] + '    return fixture_route(goals, danger);\n}'


PRELUDE = r'''
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include "pure.h"
#include "alternate-route.h"
#include "telemetry.h"
#include "attempts.h"
#include "legacy-policy.h"
#define PLAN4_MACRO_MODE 1
#define PLAN4_REFERENCE_VERSION "attempt-hooks"
namespace cambc {
using handle_champion_t = int;
using handle_camp_group_t = int;
enum class team { ally, enemy };
struct vec2 { int x=0,y=0; bool operator==(const vec2&)const=default; };
// Eight by eight tiles are sufficient for these branch witnesses. This is a
// test mask, not an implementation of engine visibility or navigation.
struct bitboard {
 std::uint64_t bits=0;
 bool is_empty()const{return bits==0;}
 bool is_set(vec2 p)const{return (bits>>(p.y*8+p.x))&1;}
 void set(vec2 p){bits|=std::uint64_t(1)<<(p.y*8+p.x);}
 bitboard operator~()const{return {~bits};}
 bitboard operator&(bitboard b)const{return {bits&b.bits};}
 bitboard& operator|=(bitboard b){bits|=b.bits;return *this;}
};
struct camp {
 vec2 spawn{4,4};bool alive=true;float hp=10,incoming=0;
 vec2 spawn_position()const{return spawn;}
 vec2 position()const{return spawn_position();}
 bool is_alive()const{return alive;}
 float health()const{return hp;}
 float strength()const{return incoming;}
};
namespace map {
struct camp_group_respawning {int timer=1;};
struct camp_group_reward_available {};
using group_state=std::variant<int,camp_group_respawning,camp_group_reward_available>;
struct group {
 int id=70;bool reward=true;
 explicit operator handle_camp_group_t()const{return id;}
 unsigned gem_value()const{return gems;}
 unsigned gold_value()const{return gold;}
 group_state state()const{return reward ? group_state{camp_group_reward_available{}}:group_state{0};}
 std::vector<camp> members{camp{}};
 unsigned gems=20,gold=0;
 auto begin()const{return members.begin();}auto end()const{return members.end();}
};
using camp_group=group;
int width(){return 8;}
bitboard spawn_region(team){bitboard result;result.set({0,0});return result;}
}
struct lock {vec2 at{2,1};vec2 position()const{return at;}};
}
namespace simulation {
unsigned fuel=100000,bag=10,now=20,score=0;
float hp=87.25f,max_hp=100.5f;
bool alive=true,channel=false,visible_lock=false,hidden_lock=false,no_channel=false,roll_on_alive=false;
int actor=10,optional_reads=0,channel_reads=0;
int collector_calls=0;
const plan4::Job* collected_job=nullptr;
const cambc::map::camp_group* collected_group=nullptr;
bool collector_unavailable=false,collector_stale=false;
std::uint32_t collected_strength=0x41280000u; // Deterministic 10.5f, not an SDK claim.
cambc::vec2 position{1,1};
std::vector<std::string> actions;
enum class MoveResult {ready,pending,blocked};
MoveResult move_result=MoveResult::ready;
std::vector<std::uint64_t> movement_goals;
bool fight_ready=true;
unsigned can_move_debit=0,first_can_move_fuel=0;
int can_move_reads=0;
void reset(){
 fuel=100000;bag=10;now=20;score=0;hp=87.25f;max_hp=100.5f;alive=true;channel=false;
 visible_lock=hidden_lock=no_channel=roll_on_alive=false;optional_reads=channel_reads=0;
 collector_calls=0;collected_job=nullptr;collected_group=nullptr;
 collector_unavailable=collector_stale=false;collected_strength=0x41280000u;
 position={1,1};actions.clear();move_result=MoveResult::ready;movement_goals.clear();fight_ready=true;
 can_move_debit=first_can_move_fuel=0;can_move_reads=0;++actor;
}
}
namespace cambc {
unsigned fuel_remaining(){return simulation::fuel;}
unsigned round_number(){return simulation::now;}
unsigned global_submitted_gems(team){return simulation::score;}
struct champion {
 explicit operator handle_champion_t()const{return simulation::actor;}
 bool operator!=(const champion&)const{return false;}
 bool is_alive()const{
  if(simulation::roll_on_alive){simulation::roll_on_alive=false;++simulation::now;}
  return simulation::alive;
 }
 unsigned gem_count()const{++simulation::optional_reads;return simulation::bag;}
 vec2 position()const{++simulation::optional_reads;return simulation::position;}
 float health()const{++simulation::optional_reads;return simulation::hp;}
 float max_health()const{++simulation::optional_reads;return simulation::max_hp;}
 float strength()const{return 10;}float focus()const{return 0;}
 bool is_submitting_gems()const{++simulation::channel_reads;return simulation::channel;}
 bool can_attack()const{return true;}
 bool can_move()const{
  if(!simulation::can_move_reads++)simulation::first_can_move_fuel=simulation::fuel;
  simulation::fuel-=simulation::can_move_debit;simulation::can_move_debit=0;return true;
 }
 int attack_range()const{return 1;}
 bitboard attack_tiles()const{return {~std::uint64_t(0)};}
 void attack(const lock&)const{simulation::actions.push_back("attack-lock");}
 void submit_gems(vec2)const{
  simulation::actions.push_back(simulation::hidden_lock ? "submit-lock-attack" :
      simulation::no_channel ? "submit-no-channel" : "submit-channel");
  simulation::channel=!simulation::hidden_lock&&!simulation::no_channel;
 }
};
champion& self(){static champion value;return value;}
namespace vision {
std::vector<lock> altar_locks(team){return simulation::visible_lock?std::vector<lock>{lock{}}:std::vector<lock>{};}
bitboard visible_tiles(){return {~std::uint64_t(0)};}
std::vector<camp> camps(){return {};}
}
}
namespace plan4::attempts {
// Deliberately independent of SDK collection: observe actual source arguments
// and return explicit fixture values. No game action or hidden-state query.
PreactionEvidence collect_preaction(const Identity& key,const Observation& sample,
 const Job* job=nullptr,const cambc::map::camp_group* group=nullptr){
 ++simulation::collector_calls;simulation::collected_job=job;simulation::collected_group=group;
 PreactionEvidence value;value.actor=sample.actor;value.round=sample.round;
 if(simulation::collector_unavailable||simulation::fuel<60000)return value;
 value.kit_known=true;value.champion=2;value.ability0=3;value.ability1=4;value.attack_range=1;
 value.strength_bits=simulation::collected_strength;value.focus_bits=0x3fc00000u;
 value.item_count=2;value.items[0]={3,2};value.items[1]={8,1};
 value.context_known=true;value.context=1;
 value.composition_known=key.kind==Kind::bank||group;
 if(group){value.composition_count=2;value.composition[0]=0;value.composition[1]=4;}
 if(job){value.travel_known=job->nominal_travel>=0;value.travel=job->nominal_travel;
  value.nominal_duration_known=job->nominal_completion>0;value.nominal_duration=job->nominal_completion;}
 if(simulation::collector_stale)--value.round;
 return value;
}
}
using cambc::vec2;using cambc::team;using cambc::bitboard;using cambc::camp;using cambc::champion;
int distance(vec2 a,vec2 b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
bitboard around(vec2 p,int){bitboard b;b.set(p);return b;}
struct Bot {
 struct Entry {
  cambc::map::group group{};vec2 anchor{4,4};bitboard spawns{};
  bool monolith=false;unsigned unavailable_until=0;float health=10,incoming=0;
  Entry(){spawns.set(anchor);}
 };
 struct Runtime {void selection_canceled(){}} runtime_model_;
 plan4::detour::Cursor alternate_route_;
 plan4::Job macro_job_{};
 bool macro_active_=false,movement_pending_=false,movement_initialized_=true,fixture_preparing_=false;
 bool recovering_=false,partial_clear_=false,has_non_monolith_gems_=false;
 unsigned decision_sequence_=1,previous_bag_=0,previous_score_=0;
 bool previous_tribute_=false;
 int target_=-1,identity_=0,monster_shards_=0,jungle_shards_=0;
 int production_requested_bank_=-1;
 std::vector<Entry> groups_;
 std::vector<int> group_at_tile_=std::vector<int>(64,-1);
 std::vector<vec2> altars_{{2,1}};
 // Real macro_move admission and thin move_to run before this controlled route
 // remainder. The full macro_move body is checked by movement-telemetry.
 bool fixture_route(bitboard goals,const bitboard*){
  simulation::movement_goals.push_back(goals.bits);
  movement_initialized_=true;
  movement_pending_=simulation::move_result==simulation::MoveResult::pending;
  if(simulation::move_result!=simulation::MoveResult::ready)return false;
  simulation::actions.push_back("move");return true;
 }
 bool recover(const std::vector<camp>&,const std::vector<champion>&){simulation::actions.push_back("recover");return true;}
 bool fight(const camp&,bool,bool){
  if(!simulation::fight_ready)return false;
  simulation::actions.push_back("fight");return true;
 }
#include "attempt-methods.h"
ACTUAL_METHODS
};
int checks=0;
void check(bool condition){++checks;assert(condition);}
void no_action(const Bot& b,unsigned expected){check(b.attempt_recorder_.pending()==expected);check(simulation::actions.empty());}
void flush_once(Bot& b){
 const auto before=b.attempt_recorder_.pending();
 const bool begin=before&&b.attempt_recorder_.front()->type==plan4::attempts::EventType::begin;
 std::ostringstream captured;auto* output=std::cout.rdbuf(captured.rdbuf());b.attempt_flush();std::cout.rdbuf(output);
 const auto raw=captured.str();std::istringstream rows(raw);std::vector<std::string> lines;std::string line;
 while(std::getline(rows,line))lines.push_back(line);
 check(lines.size()<=2);
 if(begin&&!lines.empty()){
  check(lines.size()==2&&lines[0].starts_with("p4 attempt ")&&lines[1].starts_with("p4 attempt-bin "));
  check(b.attempt_recorder_.pending()==before-1); // Retain the event until both rows were emitted.
 }
 for(std::size_t n=0;n<lines.size();++n){
  if(lines[n].starts_with("p4 attempt ")&&lines[n].find(" edge=0 ")!=std::string::npos)
   check(n+1<lines.size()&&lines[n+1].starts_with("p4 attempt-bin "));
  if(lines[n].starts_with("p4 attempt-bin "))check(n&&lines[n-1].find(" edge=0 ")!=std::string::npos);
 }
 std::cout<<raw;
}
void flush(Bot& b){
 const auto actions=simulation::actions;
 simulation::fuel=100000;
 while(b.attempt_recorder_.front()){
  const auto before=b.attempt_recorder_.pending();flush_once(b);check(b.attempt_recorder_.pending()<before);
 }
 check(simulation::actions==actions);
}
void done(Bot& b){
 flush(b);
 std::cout<<"collector calls="<<simulation::collector_calls<<'\n';
 std::cout<<"actions";for(const auto& action:simulation::actions)std::cout<<' '<<action;std::cout<<'\n';
}
void start_case(const char* name){simulation::reset();std::cout<<"case "<<name<<'\n';}
std::unique_ptr<plan4::Information> info(){
 auto result=std::make_unique<plan4::Information>();
 result->altar_count=1;result->altars[0].id=90;
 result->altars[0].position={1,1};result->altars[0].altar_position={2,1};
 result->group_count=1;result->groups[0].id=70;return result;
}
void planned(Bot& b){b.macro_job_.kind=plan4::JobKind::bank;b.macro_job_.altar=0;
 b.macro_job_.nominal_travel=5;b.macro_job_.nominal_completion=11;b.macro_active_=true;}
'''


CASES = r'''
void reward_groups(Bot& bot,bool living=false){
 bot.groups_.resize(2);
 for(int n=0;n<2;++n){
  auto& entry=bot.groups_[n];entry.anchor=n==0?vec2{6,6}:vec2{3,1};
  entry.spawns={};entry.spawns.set(entry.anchor);entry.group.id=70+n;
  entry.group.gems=n==0?40:3;entry.group.reward=n!=0||!living;
  entry.group.members={camp{entry.anchor}};
  bot.group_at_tile_[entry.anchor.y*8+entry.anchor.x]=n;
 }
}
void farm_admission_cases(){
 using namespace simulation;
 const std::vector<champion> no_allies;bool all_deferred=true;
 for(const bool living:{false,true})for(const bool initialized:{false,true}){
  reset();Bot bot;reward_groups(bot,living);bot.movement_initialized_=initialized;
  fight_ready=false;move_result=MoveResult::ready;fuel=45000;can_move_debit=1;
  const std::vector<camp> camps=living?std::vector<camp>{camp{bot.groups_[0].anchor}}:std::vector<camp>{};
  const auto primary=bot.groups_[0].spawns.bits;
  bot.farm(camps,no_allies);
  const int first_target=bot.target_;const bool pending=bot.movement_pending_;
  const unsigned marker=bot.groups_[0].unavailable_until,remaining=fuel,admission_fuel=first_can_move_fuel;
  const auto route_attempts=movement_goals.size(),action_count=actions.size();
  bool deferred=first_target==0&&pending&&marker==0&&remaining==44999&&admission_fuel==45000
   &&route_attempts==0&&action_count==0&&bot.groups_.size()==2&&bot.groups_[1].unavailable_until==0;
  ++now;fuel=100000;bot.farm(camps,no_allies);
  deferred&=bot.target_==0&&!bot.movement_pending_&&bot.groups_[0].unavailable_until==0
   &&movement_goals==std::vector<std::uint64_t>{primary}&&actions==std::vector<std::string>{"move"};
  std::cout<<"farm-admission "<<(living?"living":"reward")<<" initialized="<<initialized
   <<" selectionFuel="<<admission_fuel<<" admissionFuel="<<remaining<<" firstTarget="<<first_target
   <<" pending="<<pending<<" unavailableUntil="<<marker<<" routeAttempts="<<route_attempts
   <<" actions="<<action_count<<" resumedTarget="<<bot.target_<<" resumedGoals="<<movement_goals.back()<<std::endl;
  all_deferred&=deferred;
 }
 check(all_deferred);
 std::cout<<"farm-admission: real macro_move guards defer low fuel for reward/living, initialized/uninitialized; fresh high-fuel round resumes primary\n";
}
void reward_approach_cases(){
 using namespace simulation;
 const std::vector<camp> no_camps;const std::vector<champion> no_allies;
 reset();{
  Bot bot;reward_groups(bot);move_result=MoveResult::pending;
  const unsigned started=now;const auto primary=bot.groups_[0].spawns.bits;
  const auto alternate=bot.groups_[1].spawns.bits;
  bot.farm(no_camps,no_allies);
  const int first_target=bot.target_;const bool first_pending=bot.movement_pending_;
  const unsigned marker=bot.groups_[0].unavailable_until;
  check(first_target==0&&first_pending&&movement_goals==std::vector<std::uint64_t>{primary});
  check(bot.groups_.size()==2&&bot.groups_[1].unavailable_until==0&&primary!=alternate&&actions.empty());
  ++now;move_result=MoveResult::ready;bot.farm(no_camps,no_allies);
  std::cout<<"reward-approach pending: round="<<started<<" firstTarget="<<first_target
   <<" pending="<<first_pending<<" unavailableUntil="<<marker<<" resumedTarget="<<bot.target_
   <<" primaryGoals="<<primary<<" alternateGoals="<<alternate<<" resumedGoals="<<movement_goals.back()<<std::endl;
  // Check after the second real selection so a failing witness records both
  // the incorrect suppression and its diversion to the retained alternative.
  check(marker==0&&bot.groups_[0].unavailable_until==0);
  check(bot.target_==0&&!bot.movement_pending_&&movement_goals==std::vector<std::uint64_t>({primary,primary}));
  check(actions==std::vector<std::string>{"move"});
 }
 reset();{
  Bot bot;reward_groups(bot);move_result=MoveResult::blocked;
  const unsigned started=now;const auto primary=bot.groups_[0].spawns.bits;
  bot.farm(no_camps,no_allies);
  check(bot.target_==0&&!bot.movement_pending_&&bot.groups_[0].unavailable_until==started+3&&actions.empty());
  for(unsigned elapsed=1;elapsed<3;++elapsed){
   now=started+elapsed;move_result=MoveResult::ready;bot.farm(no_camps,no_allies);
   check(bot.target_==1&&bot.groups_[0].unavailable_until==started+3);
   check(movement_goals.back()==bot.groups_[1].spawns.bits);
  }
  now=started+3;bot.farm(no_camps,no_allies);
  check(bot.target_==0&&movement_goals.back()==primary&&bot.groups_[0].unavailable_until==started+3);
 }
 reset();{
  Bot bot;reward_groups(bot);position=bot.groups_[0].anchor;move_result=MoveResult::pending;
  bot.farm(no_camps,no_allies);
  check(bot.target_==0&&!bot.movement_pending_&&bot.groups_[0].unavailable_until==0);
  check(movement_goals.empty()&&actions.empty()&&bot.attempt_recorder_.pending()==0);
 }
 reset();{
  Bot bot;reward_groups(bot,true);fight_ready=false;move_result=MoveResult::pending;
  const auto primary=bot.groups_[0].spawns.bits;
  const std::vector<camp> living{camp{bot.groups_[0].anchor}};
  bot.farm(living,no_allies);
  check(bot.target_==0&&bot.movement_pending_&&bot.groups_[0].unavailable_until==0&&actions.empty());
  ++now;move_result=MoveResult::ready;bot.farm(living,no_allies);
  check(bot.target_==0&&!bot.movement_pending_&&bot.groups_[0].unavailable_until==0);
  check(movement_goals==std::vector<std::uint64_t>({primary,primary})&&actions==std::vector<std::string>{"move"});
 }
 std::cout<<"reward-approach: actual farm/move_to pending resume, blocked three-round suppression, capture hold and living pending passed\n";
}
int main(){
 using namespace simulation;using namespace plan4::attempts;
 farm_admission_cases();
 reward_approach_cases();
 const std::vector<camp> camps;const std::vector<champion> champions;
 start_case("bootstrap");{
  Bot b;hp=max_hp;check(b.macro_bootstrap(camps,champions));
  check(b.attempt_recorder_.pending()==1);const auto first=*b.attempt_recorder_.front();
  check(first.identity.kind==Kind::bank&&first.identity.origin==Origin::bootstrap&&first.start.bag==10);
  check(first.start.hp_bits==std::bit_cast<std::uint32_t>(max_hp));
  check(first.preaction.kit_known&&first.preaction.composition_known&&first.preaction.context_known);
  check(!first.preaction.travel_known&&!first.preaction.nominal_duration_known);
  check(collected_job==nullptr&&collected_group==nullptr&&collector_calls==1);
  ++now;check(b.macro_bootstrap(camps,champions));check(b.attempt_recorder_.pending()==1);
  check(collector_calls==1);
  check(actions==std::vector<std::string>({"submit-channel","submit-channel"}));
  b.attempt_end(Outcome::aborted);done(b);
 }
 start_case("ineligible");{
  Bot b;hp=max_hp;bag=0;check(!b.macro_bootstrap(camps,champions));no_action(b,0);
  bag=10;b.altars_.clear();check(!b.macro_bootstrap(camps,champions));no_action(b,0);
  b.altars_.push_back({2,1});fuel=59999;check(b.macro_bootstrap(camps,champions));no_action(b,0);
  b.groups_.emplace_back();fuel=44999;b.farm(camps,champions);no_action(b,0);
  fuel=100000;bag=1;b.altars_={{7,7}};check(!b.bank(false));no_action(b,0);done(b);
 }
 start_case("selection-only");{
  Bot b;auto i=info();planned(b);no_action(b,0);
  check(!b.macro_bank(*i,-1));no_action(b,0);
  bag=0;planned(b);check(b.macro_bank(*i,0));no_action(b,0);done(b);
 }
 start_case("planned-bank");{
  Bot b;auto i=info();planned(b);check(b.macro_bank(*i,0));
  const auto& e=*b.attempt_recorder_.front();
  check(e.identity.kind==Kind::bank&&e.identity.origin==Origin::planned&&e.identity.job_token==1&&e.identity.altar_id==90);
  check(collected_job==&b.macro_job_&&collected_group==nullptr&&collector_calls==1);
  check(e.preaction.travel_known&&e.preaction.travel==5&&e.preaction.nominal_duration_known&&e.preaction.nominal_duration==11);
  check(b.attempt_previous_channel_&&channel_reads==1);
  ++now;bag=0;channel=false;b.attempt_poll(now,bag,channel);
  check(!b.attempt_recorder_.active());done(b);
 }
 start_case("one-camp-continuation");{
  Bot b;b.groups_.emplace_back();b.farm(camps,champions);
  check(b.attempt_recorder_.pending()==1&&b.target_==0);
  const auto& e=*b.attempt_recorder_.front();check(e.identity.kind==Kind::one_camp&&e.identity.group_id==70);
  check(b.bank(true));check(b.attempt_recorder_.pending()==1);
  ++now;bag=0;channel=false;b.attempt_poll(now,bag,channel);done(b);
 }
 start_case("planned-one-camp-continuation");{
  Bot b;auto i=info();planned(b);b.macro_job_.kind=plan4::JobKind::farm;b.macro_job_.group=0;
  b.groups_.emplace_back();
  no_action(b,0);b.attempt_planned(*i);no_action(b,1);
  check(collected_job==&b.macro_job_&&collected_group==&b.groups_[0].group&&collector_calls==1);
  check(b.macro_bank(*i,0));check(b.attempt_recorder_.pending()==1);
  check(collector_calls==1);
  const auto& e=*b.attempt_recorder_.front();
  check(e.identity.kind==Kind::one_camp&&e.identity.origin==Origin::planned&&e.identity.group_id==70&&e.identity.altar_id==90);
  ++now;bag=0;channel=false;b.attempt_poll(now,bag,channel);done(b);
 }
 start_case("replacement");{
  Bot b;auto i=info();planned(b);check(b.macro_bank(*i,0));
  ++b.decision_sequence_;check(b.macro_bank(*i,0));
  check(b.attempt_recorder_.pending()==3&&b.attempt_recorder_.active_attempt_id()==2);
  b.macro_abort("replacement-test");check(!b.macro_active_);done(b);
 }
 for(int mode=0;mode<3;++mode){
  start_case(mode==0?"visible-lock":mode==1?"hidden-lock":"no-channel");
  Bot b;auto i=info();planned(b);visible_lock=mode==0;hidden_lock=mode==1;no_channel=mode==2;
  check(b.macro_bank(*i,0));check(!b.attempt_previous_channel_);
  check(channel_reads==(mode==0?0:1));
  ++now;bag=0;channel=false;score=100;b.attempt_poll(now,bag,false);
  check(b.attempt_recorder_.active());b.macro_finish();done(b);
 }
 start_case("same-round");{
  Bot b;auto i=info();planned(b);check(b.macro_bank(*i,0));
  bag=0;channel=false;b.attempt_poll(now,bag,false);
  check(b.attempt_recorder_.active());b.macro_finish();done(b);
 }
 start_case("gap");{
  Bot b;auto i=info();planned(b);check(b.macro_bank(*i,0));
  now+=2;bag=0;channel=false;b.attempt_poll(now,bag,false);
  check(!b.attempt_recorder_.active());done(b);
 }
 start_case("death");{
  Bot b;auto i=info();planned(b);check(b.macro_bank(*i,0));
  ++now;bag=0;channel=false;alive=false;b.attempt_poll(now,bag,false);
  check(!b.attempt_recorder_.active());done(b);
 }
 start_case("unknown");{
  Bot b;auto i=info();planned(b);check(b.macro_bank(*i,0));
  bag=0;channel=false;roll_on_alive=true;const auto sampled=now;b.attempt_poll(sampled,bag,false);
  check(!b.attempt_recorder_.active());done(b);
 }
 start_case("missing-body");{
  Bot b;auto i=info();planned(b);fuel=59999;
  const auto before=optional_reads;b.attempt_planned(*i);check(optional_reads==before);no_action(b,1);
  const auto& e=*b.attempt_recorder_.front();check(!e.start.bag_known&&!e.start.hp_known&&!e.start.position_known);
  b.attempt_end(Outcome::aborted);fuel=69999;b.attempt_flush();no_action(b,2);done(b);
 }
 start_case("helper-no-actions");{
  Bot b;auto i=info();planned(b);const auto before=fuel;
  b.attempt_planned(*i);b.attempt_planned(*i);b.attempt_poll(now,bag,false);
  b.attempt_tribute_observed();b.attempt_end(Outcome::aborted);no_action(b,2);
  flush(b);check(fuel==before);done(b);
 }
 start_case("preaction-snapshot");{
  Bot b;auto i=info();planned(b);b.macro_job_.kind=plan4::JobKind::farm;b.macro_job_.group=0;
  b.groups_.emplace_back();b.groups_[0].group.id=71;b.groups_.emplace_back();
  b.attempt_planned(*i);no_action(b,1);
  check(collector_calls==1&&collected_job==&b.macro_job_&&collected_group==&b.groups_[1].group);
  const auto first=b.attempt_recorder_.front()->preaction;
  check(first.actor==actor&&first.round==int(now)&&first.composition_known&&first.composition_count==2);
  check(first.kit_known&&first.strength_bits==0x41280000u&&first.focus_bits==0x3fc00000u);
  check(first.items[0].kind==3&&first.items[0].count==2&&first.items[1].kind==8&&first.items[1].count==1);
  b.macro_job_.nominal_travel=20;b.macro_job_.nominal_completion=40;collected_strength=0x42c80000u;
  ++now;b.attempt_planned(*i);check(collector_calls==1);no_action(b,1);
  b.attempt_end(Outcome::aborted);check(b.attempt_recorder_.pending()==2);
  fuel=69999;flush_once(b);check(b.attempt_recorder_.pending()==2);
  fuel=100000;flush_once(b);check(b.attempt_recorder_.pending()==1);
  const auto& terminal=*b.attempt_recorder_.front();check(terminal.type==EventType::end);
  check(terminal.preaction.strength_bits==first.strength_bits&&terminal.preaction.round==first.round);
  check(terminal.preaction.travel==5&&terminal.preaction.nominal_duration==11&&terminal.preaction.context==1);
  check(terminal.preaction.composition_count==2&&terminal.preaction.composition[0]==0&&terminal.preaction.composition[1]==4);
  b.attempt_planned(*i);check(collector_calls==1&&b.attempt_recorder_.pending()==1);done(b);
 }
 start_case("preaction-unknown-retry");{
  Bot b;auto i=info();planned(b);collector_unavailable=true;
  b.attempt_planned(*i);check(collector_calls==1);no_action(b,1);
  collector_unavailable=false;++now;b.attempt_planned(*i);check(collector_calls==1);no_action(b,1);
  const auto& frozen=b.attempt_recorder_.front()->preaction;
  check(!frozen.kit_known&&!frozen.composition_known&&!frozen.context_known&&!frozen.travel_known&&!frozen.nominal_duration_known);
  b.attempt_end(Outcome::aborted);b.attempt_planned(*i);check(collector_calls==1);no_action(b,2);done(b);
 }
 start_case("preaction-stale");{
  Bot b;auto i=info();planned(b);collector_stale=true;b.attempt_planned(*i);
  check(collector_calls==1);const auto& frozen=b.attempt_recorder_.front()->preaction;
  check(frozen.actor==-1&&frozen.round==-1&&!frozen.kit_known&&!frozen.travel_known);
  b.attempt_end(Outcome::aborted);no_action(b,2);done(b);
 }
 std::cout<<"checks "<<checks<<'\n';
}
'''


def validate_output(raw, parser):
    cases = {}
    current = None
    for line in raw.splitlines(keepends=True):
        if line.startswith(b'case '):
            current = line[5:].strip().decode('ascii')
            assert current not in cases, current
            cases[current] = bytearray()
        elif current is not None and line.startswith((b'p4 attempt ', b'p4 attempt-bin ')):
            cases[current].extend(line)
    expected = {
        'bootstrap': ['aborted'], 'ineligible': [], 'selection-only': [],
        'planned-bank': ['completed'], 'one-camp-continuation': ['completed'],
        'planned-one-camp-continuation': ['completed'],
        'replacement': ['replaced', 'aborted'], 'visible-lock': ['unknown'],
        'hidden-lock': ['unknown'], 'no-channel': ['unknown'], 'same-round': ['unknown'],
        'gap': ['observation_gap'], 'death': ['death'], 'unknown': ['unknown'],
        'missing-body': ['aborted'], 'helper-no-actions': ['aborted'],
        'preaction-snapshot': ['aborted'], 'preaction-unknown-retry': ['aborted'],
        'preaction-stale': ['aborted'],
    }
    assert set(cases) == set(expected), set(cases)
    rows = 0
    for name, outcomes in expected.items():
        if not outcomes:
            assert not cases[name], name
            continue
        result = parser.parse(bytes(cases[name]), expected_build='attempt-hooks')
        censored = any(outcome in ('unknown', 'observation_gap') for outcome in outcomes)
        expected_problems = [('endpoint_unavailable', 'unavailable')] if censored else []
        assert [(p['code'], p['severity']) for p in result['problems']] == expected_problems, (name, result['problems'])
        assert result['valid'] and not result['fitSupported'] and not result['fullGameSealed'], name
        attempts = result['attempts']
        assert [a['outcome'] for a in attempts] == outcomes, (name, attempts)
        assert all(not a['fitSupported'] and a['start']['binKnown'] == 0 for a in attempts), name
        assert all(a['end']['binKnown'] == 0 for a in attempts), name
        assert result['records'] == 2 * len(outcomes), name
        assert result['companionMatchingRows'] == len(outcomes), name
        assert len(result['companionRows']) == len(outcomes), name
        for attempt, companion in zip(attempts, result['companionRows']):
            assert companion['paired'], (name, companion)
            assert all(companion[key] == attempt['start'][key] for key in
                       ('v', 'build', 'schema', 'actor', 'round', 'id', 'event', 'kind', 'group', 'altar')), name
            expected_mask = (0 if name in ('missing-body', 'preaction-unknown-retry', 'preaction-stale') else
                             7 if name in ('bootstrap', 'one-camp-continuation') else 31)
            assert attempt['preaction']['known'] == expected_mask, (name, attempt['preaction'])
            assert attempt['binFieldsComplete'] == (expected_mask == 31), name
        if name in ('bootstrap', 'one-camp-continuation'):
            assert not attempts[0]['preaction']['travel']['known'] and not attempts[0]['preaction']['nominal']['known']
        if name == 'preaction-snapshot':
            preaction = attempts[0]['preaction']
            assert preaction['travel']['value'] == 5 and preaction['nominal']['value'] == 11
            assert preaction['kit']['value']['strengthBits'] == 0x41280000
            assert preaction['kit']['value']['focusBits'] == 0x3fc00000
            assert preaction['kit']['value']['items'] == [{'kind': 3, 'count': 2}, {'kind': 8, 'count': 1}]
            assert preaction['composition']['value'] == [0, 4]
        if name in ('gap', 'unknown', 'same-round', 'visible-lock', 'hidden-lock', 'no-channel'):
            assert all(a['endpoint'] is None and a['elapsedRounds'] is None for a in attempts), name
        if name == 'missing-body':
            assert attempts[0]['startState']['bag'] is None and attempts[0]['startState']['hp'] is None
        if name in ('planned-bank', 'one-camp-continuation', 'planned-one-camp-continuation'):
            assert attempts[0]['elapsedRounds'] == 1
        rows += result['records'] + len(result['companionRows'])
    assert b'checks ' in raw
    return len(cases), rows


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--compiler', required=True)
    cli.add_argument('--node', required=True)
    args = cli.parse_args()
    started = time.perf_counter()
    original = {path: path.read_bytes() for path in INPUTS}
    hashes = {path: hashlib.sha256(raw).hexdigest() for path, raw in original.items()}
    legacy = macro_source(original[ROOT / 'bots/models/common/legacy.h'].decode())
    macros = original[ROOT / 'bots/models/common/macro-methods.h'].decode().replace('\r\n', '\n')
    admission = movement_admission(original[ROOT / 'bots/models/common/movement-methods.h'].decode())
    methods = [admission, method(legacy, 'bool move_to('), method(legacy, 'bool bank(bool force = false)'),
               method(legacy, 'void farm(')]
    methods += [method(macros, signature) for signature in (
        'void macro_abort(', 'void macro_finish(', 'void macro_tribute_started(',
        'bool macro_bank(', 'bool macro_bootstrap(',
    )]
    # Parse the exact logger output with the production consumer, without pycache.
    parser = type('AttemptParser', (), {})()
    scope = {'__name__': 'attempt_hooks_parser', '__file__': str(INPUTS[-1])}
    exec(compile(original[INPUTS[-1]], str(INPUTS[-1]), 'exec'), scope)
    parser.parse = scope['parse']
    cache = ROOT / '.cache/models-tests'
    cache.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix='attempt-hooks-', dir=cache)).resolve()
    print(f'Scratch: {scratch}', flush=True)
    prepared = time.perf_counter()
    runs = []
    eh = Path(args.compiler).parent.parent / 'share/wasi-sysroot/lib/wasm32-wasip1/eh'
    try:
        # Snapshot headers so both compiles use identical verified source bytes.
        for path in INPUTS:
            if path.suffix == '.h':
                (scratch / path.name).write_bytes(original[path])
        for arm in (0, 1):
            stem = f'attempt-hooks-{arm}'
            source = scratch / (stem + '.cpp')
            source.write_text(f'#define PLAN4_MODEL_ENABLED {arm}\n'
                              + PRELUDE.replace('ACTUAL_METHODS', '\n'.join(methods)) + CASES)
            module = scratch / (stem + '.wasm')
            command = [args.compiler, str(source), '-std=c++20', '-O3', '-Wall', '-Wextra', '-Werror',
                       '-fwasm-exceptions', '-mllvm', '-wasm-use-legacy-eh=false',
                       '--no-wasm-opt', f'-L{eh}', '-lunwind', '-o', str(module)]
            compiled = subprocess.run(command, capture_output=True)
            (scratch / (stem + '.compile.log')).write_bytes(compiled.stdout + compiled.stderr)
            assert compiled.returncode == 0, compiled.stderr.decode(errors='replace')[:3500]
            driver = scratch / (stem + '.mjs')
            driver.write_text("import fs from 'node:fs';import {WASI} from 'node:wasi';"
                              "const w=new WASI({version:'preview1',args:[],env:{}});"
                              f"const m=await WebAssembly.compile(fs.readFileSync(new URL('./{stem}.wasm',import.meta.url)));"
                              "const i=await WebAssembly.instantiate(m,{wasi_snapshot_preview1:w.wasiImport,env:{}});w.start(i);")
            run = subprocess.run([args.node, str(driver)], capture_output=True)
            (scratch / (stem + '.stdout.log')).write_bytes(run.stdout)
            (scratch / (stem + '.stderr.log')).write_bytes(run.stderr)
            assert run.returncode == 0, (run.stdout + run.stderr).decode(errors='replace')[-3500:]
            count, rows = validate_output(run.stdout, parser)
            runs.append(run.stdout)
            print(f'arm={arm}: {count} source-bound scenarios, {rows} parsed base/companion rows; O3/default 64 KiB WASI stack')
        assert runs[0] == runs[1], 'Arm action traces or logger semantics differ'
        tested = time.perf_counter()
        assert all(path.read_bytes() == original[path] for path in INPUTS), 'Input changed during harness run'
        assert hashes == {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in INPUTS}
        assert all(path.is_file() and path.parent == scratch for path in scratch.iterdir())
        expected_suffixes = {'.h', '.cpp', '.wasm', '.mjs', '.log'}
        assert all(path.suffix in expected_suffixes for path in scratch.iterdir())
        # This exact freshly-created directory has been checked under .cache.
        assert scratch.parent == cache.resolve() and scratch.name.startswith('attempt-hooks-')
        shutil.rmtree(scratch)
        assert not scratch.exists(), 'Successful scratch cleanup failed'
        finished = time.perf_counter()
        print(f'Preparation {prepared-started:.3f}s; compile/check {tested-prepared:.3f}s; postcheck/cleanup {finished-tested:.3f}s')
        print(f'Verified {len(INPUTS)} input hashes ({sum(map(len, original.values()))} bytes) and scratch cleanup: {scratch}')
        print('Both arms have identical action/log output; recording adds no SDK game action. Native fuel effect remains unproved.')
        print('Collection is a deterministic boundary double; actual SDK collector correctness is outside this caller harness.')
    except BaseException:
        print(f'FAILED: retained scratch and logs at {scratch}', flush=True)
        raise


if __name__ == '__main__':
    main()
