"""Run actual Covenant producer turn/after_turn hooks with narrow SDK doubles.

No native games, timing, fuel-safety or competitive claims. The observer,
collection boundaries and action adapters are deterministic doubles. The existing
repair cases use a navigation double; the separate warmup cases use the actual
PublicNavigation, turn flow, menu and evaluator. Both arms use O3, assertions,
strict warnings and the WASI linker's default 64 KiB stack. Failed scratch is
retained.
"""
import argparse
import hashlib
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]


def method(source, signature):
    start = source.index(signature)
    cursor = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[cursor] == '{') - (source[cursor] == '}')
        cursor += 1
    return source[start:cursor]


def macro_source(source):
    """Select the real macro arm so the legacy #else braces cannot confuse extraction."""
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


PRELUDE = r'''
#include "bots/models/covenant/src/runtime.h"
#include "bots/models/common/attempts.h"
#include "bots/models/common/legacy-policy.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#define PLAN4_MACRO_MODE 1
#define PLAN4_ONLINE_EVIDENCE 1
#define PLAN4_OWN_ACQUISITION 1
#define PLAN4_REFERENCE_VERSION "contract-turn-hooks"
namespace plan4 {
struct LogRow {
 LogRow(const char*,const char*,unsigned=2){} template<class T>LogRow& field(const char*,T){return *this;} void emit(){}
};
}
namespace cambc {
using handle_champion_t=int;using handle_camp_group_t=int;
enum class team{ally,enemy};enum class champion_type{other,tavi};
struct vec2{int x=0,y=0;bool operator==(const vec2&)const=default;};
struct bitboard {
 std::uint64_t bits=0;
 bool is_set(vec2 p)const{return p.x>=0&&p.y>=0&&p.x<8&&p.y<8&&(bits&(1ull<<(p.y*8+p.x)));}
 void set(vec2 p){bits|=1ull<<(p.y*8+p.x);}
 bool is_empty()const{return bits==0;}
 friend bitboard operator&(bitboard a,bitboard b){return {a.bits&b.bits};}
 bitboard& operator|=(bitboard b){bits|=b.bits;return *this;}
 bool operator==(const bitboard&)const=default;
};
inline unsigned now=10,fuel=200000;
inline bitboard spawn{},visible{};
inline std::vector<std::string> actions;
struct champion {
 vec2 point{1,0};float hp=80,maximum=100;unsigned bag=0,gold=0;
 bool alive=true,recall=false,tribute=false,teleport=false,mobile=true,attack=true,cast=true;
 operator handle_champion_t()const{return 3;}
 bool is_alive()const{return alive;}vec2 position()const{return point;}
 float health()const{return hp;}float max_health()const{return maximum;}
 bool is_recalling()const{return recall;}bool is_submitting_gems()const{return tribute;}bool is_teleporting()const{return teleport;}
 bool can_move()const{return mobile;}bool can_attack()const{return attack;}bool can_cast()const{return cast;}
 champion_type champion_name()const{return champion_type::other;}int attack_range()const{return 1;}
 float strength()const{return 20;}float focus()const{return 0;}
 unsigned gem_count()const{return bag;}unsigned gold_count()const{return gold;}
 bitboard attack_tiles()const{bitboard b;b.set(point);return b;}
};
inline champion actor;inline champion& self(){return actor;}
inline unsigned round_number(){return now;}inline unsigned fuel_remaining(){return fuel;}
inline unsigned global_submitted_gems(team){return 0;}
struct camp {
 vec2 point{2,0};bool is_alive()const{return true;}bool is_awake()const{return false;}
 vec2 position()const{return point;}vec2 spawn_position()const{return point;}
 float health()const{return 100;}float strength()const{return 0;}int attack_range()const{return 1;}
};
namespace vision {
inline std::vector<camp> camps(){return {};}
inline std::vector<champion> champions(team){return {};}
inline bitboard visible_tiles(){return visible;}
}
namespace constants {inline double submit_gem_duration_multiplier=.1;inline int submit_gem_base_duration=0,submit_gem_minimum_duration=1;}
namespace map {
struct camp_group_alive{};struct camp_group_reward_available{};struct camp_group_respawning{int timer=30;};
using camp_group_state=std::variant<camp_group_alive,camp_group_reward_available,camp_group_respawning>;
struct camp_group {
 int id=10,lifecycle=0;std::vector<camp> members{{}};
 operator handle_camp_group_t()const{return id;}
 const auto& camps()const{return members;}auto begin()const{return members.begin();}auto end()const{return members.end();}
 camp_group_state state()const{if(lifecycle==0)return camp_group_alive{};if(lifecycle==1)return camp_group_reward_available{};return camp_group_respawning{};}
};
inline const bitboard& spawn_region(team){return spawn;}inline int width(){return 8;}
}
}
namespace rt=plan4::runtime;namespace cv=plan4::covenant;
namespace plan4::runtime {
// Collection itself has dedicated SDK fixtures. These boundaries expose no
// fabricated receipt; all production phase receipts below come from Bot hooks.
struct OwnAcquisitionCollector {
 OwnStockFrame stock{};int starts=0,ends=0;
 template<class G>void start(const G&,const std::vector<int>&,int){++starts;stock.clock={int(cambc::now),{3}};stock.known=true;stock.stock_epoch=1;stock.bag=cambc::actor.bag;}
 template<class G>void finish(const G&,const std::vector<int>&,int){++ends;}
 const OwnStockFrame& frame()const{return stock;}const CapturedGroup* capture()const{return nullptr;}
};
}
using cambc::team;using cambc::bitboard;using cambc::camp;using cambc::champion;using cambc::vec2;
int distance(vec2 a,vec2 b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
bitboard around(vec2 p,int){bitboard b;b.set(p);return b;}
int metric(plan4::Point a,plan4::Point b,const void*){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
int cached_route(plan4::Point from,plan4::Point to,plan4::Point* output,int capacity,const void*){
 const int count=std::min(metric(from,to,nullptr),capacity);for(int step=0;step<count;++step){
  from.x+=(to.x>from.x)-(to.x<from.x);from.y+=(to.y>from.y)-(to.y<from.y);output[step]=from;}return count;
}
struct Runtime:rt::Covenant {
 std::vector<rt::PhaseStart> starts;std::vector<rt::PhaseReceipt> receipts;std::vector<bool> admitted;
 cv::Contract snapshot_contract{};
 rt::ModelEvidence evaluation_evidence{};
 int cancels=0,snapshots=0,selections=0,evaluations=0,fuel_on_evaluation=-1;
 Runtime():rt::Covenant(PLAN4_MODEL_ENABLED!=0){}
 bool phase_started(const rt::PhaseStart& p){starts.push_back(p);const bool accepted=rt::Covenant::phase_started(p);admitted.push_back(accepted);return accepted;}
 void phase_completed(const rt::PhaseReceipt& p){receipts.push_back(p);rt::Covenant::phase_completed(p);}
 void selection_canceled(){++cancels;rt::Covenant::selection_canceled();}
 void observe_snapshot(const plan4::Information& i,rt::MemberFrame m={}){++snapshots;rt::Covenant::observe_snapshot(i,m);snapshot_contract=monitor().contract();}
 void selected(const plan4::Information& i,const plan4::Job& j,std::uint64_t epoch,bool productive){++selections;rt::Covenant::selected(i,j,epoch,productive);}
 void evaluate_into(const plan4::Information& i,const plan4::Menu& m,plan4::Evaluation& out){++evaluations;
  if(fuel_on_evaluation>=0)cambc::fuel=unsigned(fuel_on_evaluation);rt::Covenant::evaluate_into(i,m,out);evaluation_evidence=model_evidence();}
};
struct Online {
 rt::MemberFrame member{};rt::LifecycleFrame lifecycle{};rt::ProducerFrame producer{};
 struct Capture{rt::CapturedGroup receipt;};
 template<class G>void start(const G&,const std::vector<int>&,int,int){}
 template<class G>void finish(const G&){}
 const auto& members()const{return member;}const auto& lifecycles()const{return lifecycle;}const auto& producers()const{return producer;}
 int capture_count()const{return 0;}const Capture& capture(int)const{static Capture empty;return empty;}
};
struct Navigation {
 void begin_turn(){}void set_vertex_limit(int){}int lookup()const{return 0;}
 void prepare(plan4::Information&,unsigned,int){}void warm(int,plan4::Point,unsigned,unsigned){}void seal(){}
};
struct Observer {
 plan4::Information* source=nullptr;int observations=0;bool complete=true;unsigned fuel_after=0;
 bool observe_into(plan4::Information& out,int,unsigned){assert(source);++observations;out=*source;
  const auto& me=cambc::actor;out.round=int(cambc::now);out.own.id=3;out.own.alive=me.alive;out.own.position={me.point.x,me.point.y};
  out.own.health=me.hp;out.own.maximum_health=me.maximum;out.own.bag=me.bag;out.own.on_spawn=cambc::spawn.is_set(me.point);
  out.own.channel=me.recall?plan4::Channel::recall:me.tribute?plan4::Channel::tribute:me.teleport?plan4::Channel::other:plan4::Channel::none;
  for(int g=0;g<out.group_count;++g)out.groups[g].provenance=out.groups[g].health_provenance={out.round,plan4::Source::personal_vision,plan4::Status::exact_now};
  if(fuel_after)cambc::fuel=fuel_after;return complete;}
};
struct Segment{bool active()const{return false;}};
struct Bot {
 Runtime runtime_model_;Online online_evidence_;Navigation navigation_;Observer observer_;
 plan4::detour::Cursor alternate_route_;
 std::unique_ptr<plan4::Information> information_storage_=std::make_unique<plan4::Information>();
 std::unique_ptr<plan4::Evaluation> evaluation_storage_=std::make_unique<plan4::Evaluation>();
 std::unique_ptr<Segment> segment_executor_;
 struct Entry{cambc::map::camp_group group;bitboard spawns;float incoming=0;};
 std::vector<Entry> groups_;std::vector<int> group_at_tile_=std::vector<int>(64,-1);
 plan4::Job macro_job_{};
 bool initialized_=true,macro_active_=true,macro_second_=false,macro_seen_=false,movement_pending_=false;
 bool recovering_=false,partial_clear_=false,previous_tribute_=false,kit_seen_=false;
 int target_=-1,macro_started_=10,last_evaluation_round_=-4,last_fallback_row_=-32;
 int physical_vertices_=0,model_vertices_=0,production_requested_farm_group_=-1,kit_observed_round_=-1;
 unsigned previous_bag_=0,previous_score_=0,decision_sequence_=1,monster_shards_=0,jungle_shards_=0,optional_floor_=50000;
 std::uint32_t kit_epoch_=0;std::uint64_t kit_signature_=0;
 int shops=0,farms=0,recovers=0;
 static bool macro_budget(const void* floor){return cambc::fuel>=*static_cast<const unsigned*>(floor);}
 void initialize(){initialized_=true;}void shop(){++shops;}
 void attempt_flush(){}void attempt_poll(int,unsigned,bool){}void attempt_end(plan4::attempts::Outcome){}void attempt_planned(const plan4::Information&){}
 bool macro_segment_execute(){return false;}
 void macro_phase(const char*,unsigned){}
 void macro_flush_phases(){}
 bool macro_bank(const plan4::Information&,int){return true;}
 bool move_to(bitboard){cambc::actions.push_back("move-request");return false;}
 bool macro_farm_fight(const camp&,bool,bool){cambc::actions.push_back("fight");return true;}
 bool bank(bool=false){return false;}bool fight(const champion&,bool,bool){return false;}
 void farm(const std::vector<camp>&,const std::vector<champion>&){++farms;}
 bool recover(const std::vector<camp>&,const std::vector<champion>&){++recovers;cambc::actions.push_back("recover");cambc::actor.recall=true;return true;}
#include "bots/models/common/production-methods.h"
#include "bots/models/common/acquisition-methods.h"
ACTUAL_METHODS
};
void reset(){cambc::now=10;cambc::fuel=200000;cambc::actor={};cambc::spawn={};cambc::spawn.set({0,0});cambc::visible={};cambc::visible.set({2,0});cambc::actions.clear();}
std::unique_ptr<Bot> fixture(int approach=1,unsigned bag=0,unsigned start=10){
 reset();cambc::now=start;cambc::actor.bag=bag;auto b=std::make_unique<Bot>();auto& i=*b->information_storage_;
 b->macro_started_=int(start);if(start!=10)b->last_evaluation_round_=int(start);
 i.round=int(start);i.own.id=3;i.own.position={1,0};i.own.health=80;i.own.maximum_health=100;i.own.bag=bag;i.own.recall_safe=true;i.own_spawn={0,0};i.travel={metric,nullptr};
 i.group_count=1;i.altar_count=1;auto& g=i.groups[0];g.id=10;g.position={1+approach,0};g.health=g.full_health=100;
 g.clear_rounds=g.full_clear_rounds=2;g.gems=24;g.respawn_rounds=30;g.visible=g.occupancy_known=g.all_spawns_visible=true;g.lifecycle=plan4::Lifecycle::living;
 g.provenance=g.health_provenance={int(start),plan4::Source::personal_vision,plan4::Status::exact_now};
 i.altars[0].id=30;i.altars[0].position={0,2};i.altars[0].altar_position={0,3};i.altars[0].lock_known=true;
 b->macro_job_.kind=plan4::JobKind::farm;b->macro_job_.group=0;b->macro_job_.altar=0;
 auto& menu=b->evaluation_storage_->menu;menu.count=2;menu.single_count=1;menu.jobs[0]=b->macro_job_;menu.jobs[1].kind=plan4::JobKind::renew;
 Bot::Entry entry;entry.group.members[0].point={1+approach,0};entry.spawns.set({1+approach,0});
 b->groups_.push_back(entry);b->group_at_tile_[1+approach]=0;b->observer_.source=&i;cambc::visible.set({1+approach,0});
 b->runtime_model_.observe_snapshot(i);b->runtime_model_.selected(i,b->macro_job_,b->decision_sequence_,true);
 b->production_observe_kit();bitboard goals;goals.set({1+approach,0});b->production_begin_travel_mask(goals,approach,10);
 assert(b->runtime_model_.admitted==std::vector<bool>{true});return b;
}
int checks=0;
void check(bool value,const std::source_location where=std::source_location::current()){
 ++checks;if(!value)std::cerr<<"failed check "<<checks<<" at extracted line "<<where.line()<<'\n';assert(value);
}
void boundary(Bot& b,unsigned round){cambc::now=round;cambc::fuel=200000;b.turn();b.after_turn();}
'''


CASES = r'''
void begin_explicit_repair(Bot& b){
 auto& i=*b.information_storage_;i.groups[0].lifecycle=plan4::Lifecycle::respawning;i.groups[0].respawn_remaining=30;i.groups[0].health=0;
 b.groups_[0].group.lifecycle=2;boundary(b,11);
 check(b.runtime_model_.monitor().contract().failed);check(!b.macro_active_&&b.recovering_&&b.recovers==1);
 check(b.runtime_model_.starts.size()==2&&b.runtime_model_.receipts.size()==1);
 check(b.runtime_model_.receipts[0].phase==rt::ProductionPhase::travel&&!b.runtime_model_.receipts[0].completed&&b.runtime_model_.receipts[0].censor==rt::PhaseCensor::interrupted);
 check(b.runtime_model_.admitted==std::vector<bool>({true,true}));
 check(b.production_contract_restoring()&&b.production_tracker_.pending_key()->start_round==11);
 check(!b.production_tracker_.pending_key()->emergency&&!b.production_clock().emergency);
}
void restore_keeps_its_identity_through_all_turn_returns(){
 auto b=fixture();const int deadline=b->runtime_model_.monitor().contract().deadline;begin_explicit_repair(*b);
 const auto key=*b->production_tracker_.pending_key();const int cancels=b->runtime_model_.cancels;
 for(unsigned r=12;r<=16;++r){
  cambc::actor.recall=r==12;cambc::actor.tribute=r==13;cambc::actor.teleport=r==14;
  cambc::actor.mobile=cambc::actor.attack=cambc::actor.cast=r!=15;
  if(r==16){cambc::actor.point={0,0};cambc::actor.hp=90;}
  boundary(*b,r);
  check(b->kit_observed_round_==int(r)&&b->production_contract_restoring());
  check(b->production_tracker_.pending_key()->phase_sequence==key.phase_sequence);
  check(b->runtime_model_.starts.size()==2&&b->runtime_model_.receipts.size()==1&&b->runtime_model_.cancels==cancels);
 }
 // An END full-HP observation cannot substitute for the next owner START.
 cambc::actor.hp=100;b->after_turn();check(b->production_contract_restoring());
 // Stay on the no-action return at START; completion must occur in macro_tick.
 cambc::actor.mobile=cambc::actor.attack=cambc::actor.cast=false;boundary(*b,17);
 check(!b->production_tracker_.pending()&&!b->production_contract_restore_);
 check(b->runtime_model_.receipts.size()==2);const auto& receipt=b->runtime_model_.receipts.back();
 check(receipt.completed&&receipt.endpoint_observed&&!receipt.emergency&&receipt.phase==rt::ProductionPhase::restore&&receipt.end_round==17);
 // A later ordinary snapshot consumes the actual receipt as a repair. The
 // repaired promise closes with its original deadline/epoch, without renewal.
 const auto failed=b->runtime_model_.monitor().contract();
 cambc::actor.mobile=cambc::actor.attack=cambc::actor.cast=true;boundary(*b,18);
 const auto& after=b->runtime_model_.snapshot_contract;
 check(!after.failed&&after.stage==cv::Stage::done&&after.deadline==deadline&&after.epoch==failed.epoch&&after.phase_sequence==failed.phase_sequence+1);
 check(after.restored&&!after.converted);check(b->runtime_model_.model_evidence().samples==1);
 check(b->own_acquisition_.starts==8&&b->own_acquisition_.ends==9);
 std::cout<<"case restore-owner-start: ordered abort/start, channel/no-action/spawn holds, repair reconciliation\n";
}
void planned_recovery_does_not_reabort(){
 auto b=fixture();begin_explicit_repair(*b);const auto key=*b->production_tracker_.pending_key();const int cancels=b->runtime_model_.cancels;
 cambc::actor.recall=false;boundary(*b,12);
 check(b->recovers==2&&b->runtime_model_.cancels==cancels&&b->production_contract_restoring());
 check(b->runtime_model_.starts.size()==2&&b->production_tracker_.pending_key()->phase_sequence==key.phase_sequence);
 std::cout<<"case recovery-dispatch: planned restore bypasses emergency abort\n";
}
void after_turn_refreshes_a_new_owner_round_before_end_poll(){
 auto b=fixture();begin_explicit_repair(*b);b->turn();
 cambc::now=12; // Model a suspension after the turn's early channel return.
 b->after_turn();check(b->kit_observed_round_==12&&b->production_contract_restoring());
 cambc::actor.alive=false;cambc::now=13;b->after_turn();
 check(!b->production_tracker_.pending()&&!b->production_contract_restore_);
 check(b->kit_observed_round_==12); // Death must not fabricate a combat-kit sample.
 check(b->runtime_model_.receipts.back().censor==rt::PhaseCensor::interrupted&&!b->runtime_model_.receipts.back().completed);
 std::cout<<"case end-refresh: early return, fresh kit, death cancellation\n";
}
void dead_turn_cancels_even_below_the_kit_sampling_floor(){
 auto b=fixture();begin_explicit_repair(*b);cambc::now=12;cambc::fuel=64000;cambc::actor.alive=false;
 b->turn();b->after_turn();
 check(!b->production_tracker_.pending()&&!b->production_contract_restore_);
 check(b->kit_observed_round_==11);
 check(b->runtime_model_.receipts.back().censor==rt::PhaseCensor::interrupted&&!b->runtime_model_.receipts.back().completed);
 std::cout<<"case dead-turn: cancellation does not require a living combat-kit sample\n";
}
void death_and_epoch_changes_cannot_keep_a_restore_exemption(){
 for(int mode=0;mode<2;++mode){auto b=fixture();begin_explicit_repair(*b);
  if(mode==0)cambc::actor.alive=false;else ++b->decision_sequence_;
  boundary(*b,12);check(!b->production_tracker_.pending()&&!b->production_contract_restore_);
  const auto& receipt=b->runtime_model_.receipts.back();check(!receipt.completed&&receipt.censor==(mode==0?rt::PhaseCensor::interrupted:rt::PhaseCensor::mode_change));
 }
 std::cout<<"case interruptions: death and job epoch invalidate planned restoration\n";
}
void live_capture_is_preserved_past_old_macro_horizon(){
 auto b=fixture();auto& i=*b->information_storage_;i.in_progress_group_id=10;
 i.groups[0].lifecycle=plan4::Lifecycle::cleared;i.groups[0].reward_available=true;b->groups_[0].group.lifecycle=1;
 cambc::actor.point={2,0};
 // A newly visible capture can appear after the prior approach's timer. The
 // real macro_decide protected-progress return precedes the old horizon abort.
 b->macro_started_=-plan4::max_horizon;cambc::now=11;b->turn();b->after_turn();
 check(b->macro_active_&&b->runtime_model_.cancels==0&&b->recovers==0&&cambc::actions.empty());
 check(!b->runtime_model_.monitor().contract().failed);
 // Also exercise the actual direct ownership hold before macro_decide.
 b->target_=0;cambc::actor.point={2,0};boundary(*b,12);
 check(b->macro_active_&&b->runtime_model_.cancels==0&&b->recovers==0&&cambc::actions.empty());
 check(b->observer_.observations==2);
 std::cout<<"case live-capture: protected decision and direct hold survive old horizon\n";
}
void planned_renew_protects_a_new_capture_past_its_timer(){
 auto b=fixture();auto& i=*b->information_storage_;
 b->production_cancel(rt::PhaseCensor::interrupted);++b->decision_sequence_;
 b->macro_job_={};b->macro_job_.kind=plan4::JobKind::renew;
 b->runtime_model_.selected(i,b->macro_job_,b->decision_sequence_,true);
 check(b->production_begin_restore(i));const auto key=*b->production_tracker_.pending_key();
 i.in_progress_group_id=10;i.groups[0].lifecycle=plan4::Lifecycle::cleared;i.groups[0].reward_available=true;
 b->groups_[0].group.lifecycle=1;b->macro_started_=-plan4::max_horizon;cambc::actor.point={2,0};
 boundary(*b,11);
 check(b->macro_active_&&b->recovers==0&&b->runtime_model_.cancels==0&&cambc::actions.empty());
 check(b->production_tracker_.pending()&&b->production_tracker_.pending_key()->phase_sequence==key.phase_sequence);
 check(!b->runtime_model_.monitor().contract().failed&&b->runtime_model_.monitor().contract().stage==cv::Stage::restore);
 std::cout<<"case planned-renew-capture: new live progress precedes old recovery timer\n";
}
void contract_issued_recovery_protects_a_new_live_capture(){
 auto b=fixture();begin_explicit_repair(*b);const auto key=*b->production_tracker_.pending_key();
 const auto actions=cambc::actions;const int cancels=b->runtime_model_.cancels;
 cambc::actor.recall=false;cambc::actor.point={2,0};b->target_=0;
 auto& i=*b->information_storage_;i.in_progress_group_id=10;i.groups[0].lifecycle=plan4::Lifecycle::cleared;i.groups[0].reward_available=true;
 b->groups_[0].group.lifecycle=1;boundary(*b,12);
 check(b->recovers==1&&cambc::actions==actions&&b->runtime_model_.cancels==cancels);
 check(b->recovering_&&b->production_contract_restoring()&&b->production_tracker_.pending_key()->phase_sequence==key.phase_sequence);
 std::cout<<"case contract-recovery-capture: actual issued recovery preserves newly occupied live capture\n";
}
void accepted_active_job_reaches_the_same_complete_selector(){
 auto b=fixture();b->last_evaluation_round_=10;
 const auto key=*b->production_tracker_.pending_key();const auto accepted=b->runtime_model_.monitor().contract();
 const auto job=b->macro_job_;const int selected=b->runtime_model_.selections;
 for(unsigned r=11;r<=13;++r){boundary(*b,r);check(b->runtime_model_.evaluations==0);}
 boundary(*b,14);
 check(b->runtime_model_.evaluations==1&&b->evaluation_storage_->evaluated_count==2&&!b->evaluation_storage_->budget_exhausted);
 check(b->evaluation_storage_->chosen==0&&b->macro_active_);
 check(b->runtime_model_.selections==selected&&b->runtime_model_.cancels==0);
 check(b->decision_sequence_==1&&b->macro_started_==10&&!b->macro_second_);
 check(b->macro_job_.kind==job.kind&&b->macro_job_.group==job.group&&b->macro_job_.altar==job.altar);
 check(b->production_tracker_.pending()&&b->production_tracker_.pending_key()->phase_sequence==key.phase_sequence&&b->production_tracker_.pending_key()->job_epoch==key.job_epoch);
 const auto& retained=b->runtime_model_.monitor().contract();
 check(retained.epoch==accepted.epoch&&retained.phase_sequence==accepted.phase_sequence&&retained.deadline==accepted.deadline&&!retained.failed);
 std::cout<<"case active-retention: actual four-turn complete full-menu consultation keeps key, phase and original promise\n";
}
void a_complete_different_key_selection_replaces_the_old_phase(){
 auto b=fixture();auto& i=*b->information_storage_;i.group_count=2;
 i.groups[0].gems=1;i.groups[1]=i.groups[0];i.groups[1].id=11;i.groups[1].gems=48;i.groups[1].position={1,1};
 Bot::Entry second;second.group.id=11;second.group.members[0].point={1,1};second.spawns.set({1,1});b->groups_.push_back(second);
 b->evaluation_storage_->menu.jobs[1]=b->macro_job_;b->evaluation_storage_->menu.jobs[1].group=1;
 b->evaluation_storage_->menu.jobs[1].menu_rank=1;b->evaluation_storage_->menu.single_count=2;b->last_evaluation_round_=10;
 for(unsigned r=11;r<=14;++r)boundary(*b,r);
 check(b->runtime_model_.evaluations==1&&b->evaluation_storage_->evaluated_count==2&&!b->evaluation_storage_->budget_exhausted);
 check(b->evaluation_storage_->chosen==1&&b->macro_job_.group==1&&b->macro_active_);
 check(b->decision_sequence_==2&&b->macro_started_==14&&b->runtime_model_.selections==2&&b->runtime_model_.cancels==1);
 check(!b->production_tracker_.pending()&&b->runtime_model_.receipts.size()==1);
 check(!b->runtime_model_.receipts[0].completed&&b->runtime_model_.receipts[0].job_epoch==1&&b->runtime_model_.receipts[0].censor==rt::PhaseCensor::interrupted);
 check(b->runtime_model_.monitor().contract().key.first_group==11);
 std::cout<<"case active-replacement: complete selector replaces physical key and cancels former phase\n";
}
void an_actual_budget_incomplete_evaluation_retains_the_contract(){
 auto b=fixture();b->last_evaluation_round_=10;b->runtime_model_.fuel_on_evaluation=134999;
 const auto accepted=b->runtime_model_.monitor().contract();const auto key=*b->production_tracker_.pending_key();
 for(unsigned r=11;r<=14;++r)boundary(*b,r);
 check(b->runtime_model_.evaluations==1&&b->evaluation_storage_->evaluated_count<2&&b->evaluation_storage_->budget_exhausted);
 check(b->macro_active_&&b->macro_job_.group==0&&b->runtime_model_.selections==1&&b->runtime_model_.cancels==0);
 check(b->decision_sequence_==1&&b->macro_started_==10&&b->production_tracker_.pending());
 check(b->production_tracker_.pending_key()->phase_sequence==key.phase_sequence&&b->production_tracker_.pending_key()->job_epoch==key.job_epoch);
 const auto& after=b->runtime_model_.monitor().contract();
 check(after.deadline==accepted.deadline&&after.epoch==accepted.epoch&&after.phase_sequence==accepted.phase_sequence&&!after.failed);
 std::cout<<"case active-budget: real evaluator budget failure retains executing key and phase\n";
}
void delayed_restore_confirmation_requires_current_physical_proof(){
 for(int mode=0;mode<3;++mode){
  auto b=fixture();begin_explicit_repair(*b);boundary(*b,12);
  cambc::actor.recall=false;cambc::actor.point={0,0};cambc::actor.hp=90;boundary(*b,13);
  cambc::actor.hp=100;cambc::actor.mobile=cambc::actor.attack=cambc::actor.cast=false;boundary(*b,14);
  check(!b->production_tracker_.pending()&&b->runtime_model_.receipts.back().completed&&b->runtime_model_.receipts.back().end_round==14);
  const auto before=b->runtime_model_.monitor().contract();check(before.failed&&!before.restored);
  // These are consumer-source checks after real turn-produced endpoints. A
  // fresh observer double varies one confirmation premise; no receipt is
  // injected or altered, and the positive wiring case uses another full turn.
  cambc::now=mode==0?16:15;if(mode==1)cambc::actor.hp=90;if(mode==2)cambc::actor.point={1,0};
  auto& i=*b->information_storage_;b->observer_.observe_into(i,0,0);b->macro_acquisition_observe(i);
  const auto& after=b->runtime_model_.monitor().contract();
  check(after.failed&&!after.restored&&after.deadline==before.deadline&&after.epoch==before.epoch&&after.phase_sequence==before.phase_sequence);
  check(b->runtime_model_.model_evidence().samples==0);
 }
 std::cout<<"case restore-confirmation-negative: older receipt, damaged body and leaving spawn cannot close repair\n";
}
void supported_ordinary_consultation_does_not_claim_a_disagreement(unsigned start=10){
 auto b=fixture(2,24,start);auto& i=*b->information_storage_;i.travel.route_prefix=cached_route;
 i.altars[0].position={6,2};i.altars[0].altar_position={6,3};
 b->last_evaluation_round_=int(start);
 b->evaluation_storage_->menu=plan4::make_menu(i);const int candidates=b->evaluation_storage_->menu.count;
 check(b->evaluation_storage_->menu.single_count==1&&candidates>=4);
 if(start!=10){
  auto initial_model=std::make_unique<rt::Covenant>(PLAN4_MODEL_ENABLED!=0);
  auto initial=std::make_unique<plan4::Evaluation>();initial_model->observe_snapshot(i);
  rt::evaluate_model(i,b->evaluation_storage_->menu,*initial_model,*initial);
  check(initial->evaluated_count==candidates&&!initial->budget_exhausted&&initial->chosen>=0);
  check(!initial_model->model_evidence().coupling_enabled&&i.settings.max_rounds==2000);
  check(initial->menu.jobs[initial->chosen].kind==plan4::JobKind::farm&&initial->menu.jobs[initial->chosen].group==0&&initial->menu.jobs[initial->chosen].altar==0);
  std::cout<<"case ordinary-consultation: initial start="<<start<<" flatChosen="<<initial->chosen
   <<" kind="<<int(initial->menu.jobs[initial->chosen].kind)<<" fixtureAcceptedKind="<<int(b->macro_job_.kind);
  for(int n=0;n<candidates;++n){const auto& f=initial->forecasts[n];
   std::cout<<" candidate["<<n<<"]kind="<<int(initial->menu.jobs[n].kind)<<",secured="<<f.our_secured
    <<",z="<<f.terminal_z<<",first="<<f.first_conversion_round<<",objectivePaths="<<f.objective.paths;}
  std::cout<<'\n';
 }
 const auto accepted=b->runtime_model_.monitor().contract();
 for(unsigned step=1;step<=4;++step){cambc::actor.point={int(step)+1,1};boundary(*b,start+step);}
 const auto& evidence=b->runtime_model_.evaluation_evidence;const int chosen=b->evaluation_storage_->chosen;
 check(b->runtime_model_.evaluations==1&&b->evaluation_storage_->evaluated_count==candidates&&!b->evaluation_storage_->budget_exhausted);
 check(evidence.supported&&evidence.coupling_enabled==(PLAN4_MODEL_ENABLED!=0));
 check(chosen>=0&&chosen<candidates);
 check(b->evaluation_storage_->menu.jobs[chosen].kind==((start==10||PLAN4_MODEL_ENABLED)?plan4::JobKind::bank:plan4::JobKind::farm));
 check(int(start+4)<accepted.deadline&&!b->runtime_model_.snapshot_contract.failed);
 check(b->runtime_model_.snapshot_contract.deadline==accepted.deadline&&b->runtime_model_.snapshot_contract.epoch==accepted.epoch&&
  b->runtime_model_.snapshot_contract.phase_sequence==accepted.phase_sequence&&b->runtime_model_.snapshot_contract.completed_mask==accepted.completed_mask);
 std::cout<<"case ordinary-consultation: start="<<start<<" consult="<<start+4<<" deadline="<<accepted.deadline
  <<" actualChosen="<<chosen<<" kind="<<int(b->evaluation_storage_->menu.jobs[chosen].kind);
 if(start!=10)for(int n=0;n<candidates;++n){const auto& f=b->evaluation_storage_->forecasts[n];
  std::cout<<" candidate["<<n<<"]kind="<<int(b->evaluation_storage_->menu.jobs[n].kind)<<",secured="<<f.our_secured
   <<",z="<<f.terminal_z<<",first="<<f.first_conversion_round<<",objectivePaths="<<f.objective.paths;}
 std::cout<<'\n';
}
void failed_optional_observation_leaves_three_productive_turns(){
 reset();auto b=fixture();b->macro_abort("test-reset");cambc::actor.hp=cambc::actor.maximum;
 b->observer_.complete=false;b->observer_.fuel_after=55000;
 const int observed=b->observer_.observations,farms=b->farms;
 for(unsigned round=10;round<=14;++round){
  cambc::now=round;cambc::fuel=100000;b->turn();b->after_turn();
  const int attempts=round==14?2:1;
  check(b->observer_.observations==observed+attempts);
  check(b->last_evaluation_round_==int(round==14?14:10));
  check(b->farms==farms+int(round>=13?3:round-10));
 }
}
void safe_damaged_fallback_continues_but_real_recovery_stays_protected(){
 reset();auto b=fixture();b->macro_abort("test-reset");
 b->observer_.complete=false;b->observer_.fuel_after=65000;
 for(unsigned round=10;round<=13;++round){
  cambc::now=round;cambc::fuel=100000;b->turn();b->after_turn();
  check(b->farms==int(round-9));check(b->recovers==0);check(!cambc::actor.recall);
 }
 reset();b=fixture();b->macro_abort("test-reset");cambc::actor.hp=10;cambc::fuel=100000;
 b->turn();b->after_turn();check(b->recovers==1);check(b->farms==0);check(cambc::actor.recall);
 cambc::now=11;cambc::fuel=100000;b->turn();b->after_turn();check(b->recovers==1);check(b->farms==0);
 reset();b=fixture();b->macro_abort("test-reset");cambc::actor.point={0,0};cambc::fuel=100000;
 b->turn();b->after_turn();check(b->farms==0);check(b->recovers==0);check(b->observer_.observations==0);
 reset();b=fixture();b->macro_abort("test-reset");cambc::actor.point={2,0};cambc::fuel=100000;
 b->target_=0;b->groups_[0].group.lifecycle=1;
 b->turn();b->after_turn();check(b->farms==0);check(b->recovers==0);check(b->observer_.observations==0);
}
int main(){
 safe_damaged_fallback_continues_but_real_recovery_stays_protected();
 failed_optional_observation_leaves_three_productive_turns();
 planned_recovery_does_not_reabort();
 death_and_epoch_changes_cannot_keep_a_restore_exemption();live_capture_is_preserved_past_old_macro_horizon();planned_renew_protects_a_new_capture_past_its_timer();
 after_turn_refreshes_a_new_owner_round_before_end_poll();dead_turn_cancels_even_below_the_kit_sampling_floor();
 accepted_active_job_reaches_the_same_complete_selector();a_complete_different_key_selection_replaces_the_old_phase();
 an_actual_budget_incomplete_evaluation_retains_the_contract();restore_keeps_its_identity_through_all_turn_returns();
 delayed_restore_confirmation_requires_current_physical_proof();supported_ordinary_consultation_does_not_claim_a_disagreement();
 supported_ordinary_consultation_does_not_claim_a_disagreement(1898);
 contract_issued_recovery_protects_a_new_live_capture();std::cout<<"checks "<<checks<<'\n';
}
'''


TERRAIN_SDK = r'''
enum uninitialized_t { uninitialized };
struct bitboard {
 std::array<std::uint64_t,16> bits{};
 bitboard()=default;bitboard(uninitialized_t){}
 bool is_set(vec2 p)const{return p.x>=0&&p.y>=0&&p.x<32&&p.y<32&&(bits[p.y/2]&(1ull<<((p.y%2)*32+p.x)));}
 void set(vec2 p){assert(p.x>=0&&p.y>=0&&p.x<32&&p.y<32);bits[p.y/2]|=1ull<<((p.y%2)*32+p.x);}
 bool is_empty()const{for(auto word:bits)if(word)return false;return true;}
 std::vector<vec2> positions()const{std::vector<vec2> out;for(int y=0;y<32;++y)for(int x=0;x<32;++x)if(is_set({x,y}))out.push_back({x,y});return out;}
 friend bitboard operator&(bitboard a,const bitboard& b){for(int n=0;n<16;++n)a.bits[n]&=b.bits[n];return a;}
 friend bitboard operator|(bitboard a,const bitboard& b){return a|=b;}
 bitboard& operator|=(const bitboard& b){for(int n=0;n<16;++n)bits[n]|=b.bits[n];return *this;}
 bool operator==(const bitboard&)const=default;
};
'''


PHASE_LOG_SINK = r'''
namespace phase_trace {
struct Row {
 unsigned version=0;std::vector<std::pair<std::string,std::string>> fields;
 std::string value(std::string_view key)const{for(const auto& field:fields)if(field.first==key)return field.second;return {};}
};
inline std::vector<Row> rows;inline std::vector<std::string> events;
inline unsigned formats=0,synthetic_debit=0;inline bool jump_on_observer_emit=false;
inline void(*on_emit)(const Row&)=nullptr;
inline void reset(){rows.clear();events.clear();formats=synthetic_debit=0;jump_on_observer_emit=false;on_emit=nullptr;}
}
namespace plan4 {
struct LogRow {
 bool phase=false;phase_trace::Row row;
 LogRow(const char* kind,const char*,unsigned version=2):phase(std::string_view(kind)=="phase"){
  if(phase){++phase_trace::formats;phase_trace::events.push_back("phase-format");row.version=version;}
 }
 template<class T>LogRow& field(const char* key,T value){
  if(phase){std::ostringstream text;if constexpr(std::is_enum_v<T>)text<<static_cast<std::underlying_type_t<T>>(value);else text<<value;
   row.fields.emplace_back(key,text.str());}
  return *this;
 }
 void emit(){if(phase){phase_trace::events.push_back("phase-emit");phase_trace::rows.push_back(row);if(phase_trace::on_emit)phase_trace::on_emit(row);}}
};
}
'''


def navigation_prelude(navigation, legacy, macros):
    """Keep the production caller; replace only the explicit boundary doubles."""
    source = PRELUDE.replace('#include "bots/models/common/legacy-policy.h"',
                             '#include "bots/models/common/legacy-policy.h"\n'
                             '#define MODEL4_OBSERVER_NO_SDK 1\n'
                             '#include "bots/models/common/nav.h"\n'
                             '#include "bots/models/common/observer.h"\n'
                             '#include "bots/models/common/movement.h"\n'
                             '#include "bots/models/common/phase-diagnostics.h"\n'
                             '#include <sstream>')
    start, end = source.index('namespace plan4 {\nstruct LogRow'), source.index('namespace cambc {')
    source = source[:start] + PHASE_LOG_SINK + source[end:]
    start, end = source.index('struct bitboard {'), source.index('inline unsigned now=')
    source = source[:start] + TERRAIN_SDK + source[end:]
    source = source.replace('inline bitboard spawn{},visible{};',
                            'inline bitboard spawn{},visible{},wall_tiles{},hole_tiles{},altar_tiles{},enemy_spawn{};\n'
                            'inline bool jump_on_walls=false,jump_after_shop=false,jump_on_next_alive=false;\n'
                            'inline bool allow_buy=true,throw_on_buy=false,jump_on_gold=false;\n'
                            'inline unsigned strength_reads=0,focus_reads=0,gold_reads=0,buy_calls=0;\n'
                            'inline float live_strength=20,live_focus=0;\n'
                            'inline const int* kit_round_at_purchase=nullptr;inline std::vector<int> purchase_kit_rounds;\n'
                            'inline int board_width=10,board_height=10;')
    source = source.replace('struct champion {', r'''
struct item_type {
 unsigned value;
 constexpr explicit item_type(unsigned v):value(v){}
 unsigned cost()const{return 30;}explicit operator unsigned()const{return value;}
 static const item_type monster_shard_of_strength,jungle_shard_of_health,power_shard_of_strength;
};
inline const item_type item_type::monster_shard_of_strength{0};
inline const item_type item_type::jungle_shard_of_health{1};
inline const item_type item_type::power_shard_of_strength{2};
struct champion {''')
    source = source.replace('float strength()const{return 20;}float focus()const{return 0;}',
                            'float strength()const{++strength_reads;return live_strength;}float focus()const{++focus_reads;return live_focus;}')
    source = source.replace('unsigned gold_count()const{return gold;}', r'''unsigned gold_count()const{
 ++gold_reads;
 if(jump_after_shop){jump_after_shop=false;jump_on_next_alive=true;}
 if(jump_on_gold){jump_on_gold=false;++now;live_strength=41;live_focus=9;const_cast<champion*>(this)->maximum=140;}
 return gold;
 }
 bool can_buy(item_type item,unsigned count)const{return allow_buy&&count&&gold>=item.cost()*count;}
 void buy(item_type item,unsigned count){
  ++buy_calls;if(kit_round_at_purchase)purchase_kit_rounds.push_back(*kit_round_at_purchase);
  if(throw_on_buy)throw std::runtime_error("fixture purchase failure");
  gold-=item.cost()*count;
  if(unsigned(item)==unsigned(item_type::jungle_shard_of_health)){maximum+=25*count;hp+=25*count;}
  else live_strength+=3*count;
 }''')
    source = source.replace('using cambc::team;', 'using cambc::item_type;using cambc::team;')
    source = source.replace('bool is_alive()const{return alive;}',
                            'bool is_alive()const{if(jump_on_next_alive){jump_on_next_alive=false;++now;return false;}return alive;}')
    source = source.replace('void shop(){++shops;}', method(legacy, 'void shop()'))
    source = source.replace('Runtime():rt::Covenant',
                            'std::vector<rt::CombatKit> combat_kits;\n'
                            'void observe_combat(const rt::CombatKit& kit){combat_kits.push_back(kit);rt::Covenant::observe_combat(kit);}\n'
                            'Runtime():rt::Covenant')
    source = source.replace('++evaluations;', '++evaluations;phase_trace::events.push_back("decision");')
    source = source.replace('int physical_vertices_=0,',
                            'unsigned phase_seen_=0;plan4::DeferredPhases phase_samples_;\n int physical_vertices_=0,')
    source = source.replace('void macro_phase(const char*,unsigned){}',
                            '\n'.join(method(macros, signature) for signature in
                                      ('void macro_observer_phase(', 'void macro_phase(', 'void macro_flush_phases(')))
    source = source.replace('void macro_flush_phases(){}', '')
    source = source.replace('inline const bitboard& spawn_region(team){return spawn;}inline int width(){return 8;}',
                            'inline const bitboard& spawn_region(team side){return side==team::ally?spawn:enemy_spawn;}\n'
                            'inline int width(){return board_width;}inline int height(){return board_height;}\n'
                            'inline const bitboard& walls(){if(jump_on_walls){jump_on_walls=false;++now;}return wall_tiles;}\n'
                            'inline const bitboard& holes(){return hole_tiles;}inline const bitboard& altars(){return altar_tiles;}')
    source = source.replace('namespace rt=plan4::runtime;',
                            'namespace plan4 {\n' + method(navigation, 'class PublicNavigation') + ';\n}\n'
                            'namespace rt=plan4::runtime;')
    start, end = source.index('struct Navigation {'), source.index('struct Observer {')
    source = source[:start] + 'using Navigation=plan4::PublicNavigation;\n' + source[end:]
    source = source.replace('bool observe_into(plan4::Information& out,int,unsigned)',
                            'bool observe_into(plan4::Information& out,plan4::TravelLookup travel,unsigned)')
    source = source.replace('struct Observer {',
                            'struct Observer {\n plan4::ObserverDiagnostics stop;\n'
                            'const auto& diagnostics()const{return stop;}')
    source = source.replace('++observations;out=*source;', '++observations;out=*source;out.travel=travel;')
    source = source.replace('int shops=0,farms=0,recovers=0;',
                            'int shops=0,farms=0,recovers=0,bootstrap_banks=0;\n'
                            'unsigned fallback_bag=0;float fallback_health=0;vec2 fallback_position{};')
    source = source.replace('bool bank(bool=false){return false;}',
                            'bool bank(bool bootstrap=false){bootstrap_banks+=bootstrap?1:0;return false;}')
    source = source.replace('){++farms;}',
                            '){++farms;phase_trace::events.push_back("farm-action");fallback_bag=cambc::actor.bag;fallback_health=cambc::actor.hp;fallback_position=cambc::actor.point;}')
    source = source.replace('template<class G>void finish(const G&){',
                            'template<class G>void finish(const G&){phase_trace::events.push_back("online-finish");')
    source = source.replace('int){++ends;}', 'int){++ends;phase_trace::events.push_back("own-finish");}')
    return source


NAVIGATION_CASES = r'''
// Independent breadth-first oracle over the public SDK terrain fixture. It
// neither calls navigation nor fills the production distance cache.
int oracle(plan4::Point from,plan4::Point to){
 const int width=cambc::board_width,height=cambc::board_height;
 std::array<int,1024> distances;distances.fill(-1);std::array<plan4::Point,1024> queue;
 auto open=[&](plan4::Point p){return p.x>=0&&p.y>=0&&p.x<width&&p.y<height&&
  !cambc::wall_tiles.is_set({p.x,p.y})&&!cambc::hole_tiles.is_set({p.x,p.y})&&
  !cambc::altar_tiles.is_set({p.x,p.y})&&!cambc::enemy_spawn.is_set({p.x,p.y});};
 if(!open(from)||!open(to))return -1;
 int head=0,tail=0;queue[tail++]=from;distances[from.y*width+from.x]=0;
 while(head<tail){const auto at=queue[head++];if(at==to)return distances[at.y*width+at.x];
  for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;
   const plan4::Point next{at.x+dx,at.y+dy};if(!open(next))continue;const int key=next.y*width+next.x;
   if(distances[key]>=0)continue;distances[key]=distances[at.y*width+at.x]+1;queue[tail++]=next;
  }
 }
 return -1;
}
void check_limits(const Bot& b){
 check(b.navigation_.vertex_visits()>=0&&b.model_vertices_>=0&&b.physical_vertices_>=0);
 check(b.navigation_.vertex_visits()+b.model_vertices_<=192);
 check(b.navigation_.vertex_visits()+b.model_vertices_+b.physical_vertices_<=256);
}
int check_completed_routes(const Bot& b){
 const auto lookup=b.navigation_.lookup();int completed=0;
 for(int y=0;y<cambc::board_height;++y)for(int x=0;x<cambc::board_width;++x){
  const plan4::Point from{x,y};
  for(const auto to:std::array<plan4::Point,2>{{{6,1},{6,3}}}){
   const int actual=lookup.distance(from,to,lookup.context);
   if(actual>=0){check(actual==oracle(from,to));++completed;}
  }
 }
 return completed;
}
std::unique_ptr<Bot> warm_fixture(){
 reset();cambc::board_width=cambc::board_height=10;
 phase_trace::reset();
 cambc::wall_tiles={};cambc::hole_tiles={};cambc::altar_tiles={};cambc::enemy_spawn={};cambc::jump_on_walls=false;
 cambc::jump_after_shop=cambc::jump_on_next_alive=false;
 cambc::allow_buy=true;cambc::throw_on_buy=cambc::jump_on_gold=false;
 cambc::strength_reads=cambc::focus_reads=cambc::gold_reads=cambc::buy_calls=0;
 cambc::live_strength=20;cambc::live_focus=0;cambc::kit_round_at_purchase=nullptr;cambc::purchase_kit_rounds.clear();
 for(int y=0;y<=6;++y)cambc::wall_tiles.set({4,y});
 cambc::actor.point={1,1};cambc::actor.hp=80;
 auto b=std::make_unique<Bot>();b->macro_active_=false;b->group_at_tile_.assign(100,-1);
 auto& i=*b->information_storage_;i.round=10;i.own.id=3;i.own.position={1,1};
 i.own.health=80;i.own.maximum_health=100;i.own.recall_safe=true;i.own_spawn={0,0};
 i.group_count=1;i.altar_count=1;auto& g=i.groups[0];g.id=10;g.position={6,1};
 g.health=g.full_health=100;g.clear_rounds=g.full_clear_rounds=2;g.gems=24;g.respawn_rounds=30;
 g.visible=g.occupancy_known=g.all_spawns_visible=true;g.lifecycle=plan4::Lifecycle::living;
 i.altars[0].id=30;i.altars[0].position={6,3};i.altars[0].altar_position={6,4};i.altars[0].lock_known=true;
 Bot::Entry entry;entry.group.members[0].point={6,1};entry.spawns.set({6,1});b->groups_.push_back(entry);b->group_at_tile_[16]=0;
 b->observer_.source=&i;b->observer_.fuel_after=55000;return b;
}
void observe_low_then_warm_on_three_real_backoff_turns(){
 auto b=warm_fixture();auto& i=*b->information_storage_;
 cambc::fuel=100000;b->turn();b->after_turn();
 check(b->observer_.observations==1&&b->last_evaluation_round_==10&&!b->macro_active_);
 check(!b->navigation_.initialized()&&b->navigation_.vertex_visits()==0);
 check(b->runtime_model_.evaluations==1&&b->evaluation_storage_->menu.single_count==0);
 for(int n=0;n<b->evaluation_storage_->menu.count;++n)check(b->evaluation_storage_->menu.jobs[n].kind==plan4::JobKind::renew||b->evaluation_storage_->menu.jobs[n].kind==plan4::JobKind::hold);
 check(b->farms==0);check_limits(*b);
 const std::array<vec2,3> positions{{{1,2},{2,2},{2,1}}};
 const std::array<unsigned,3> bags{{7,0,11}};const std::array<float,3> health{{72,65,90}};
 int warm_visits=0,completed=0;
 for(int step=0;step<3;++step){
  cambc::actor.point=positions[step];cambc::actor.bag=bags[step];cambc::actor.hp=health[step];
  cambc::now=unsigned(11+step);cambc::fuel=100000;b->turn();b->after_turn();
  check(b->observer_.observations==1&&b->runtime_model_.evaluations==1&&b->last_evaluation_round_==10);
  check(i.round==10&&i.own.position==(plan4::Point{1,1})&&i.own.health==80&&i.own.bag==0);
  check(b->farms==step+1&&b->recovers==0);
  check(b->fallback_position==positions[step]&&b->fallback_bag==bags[step]&&b->fallback_health==health[step]);
  check(b->bootstrap_banks==(step==0?1:step==1?1:2));
  check_limits(*b);warm_visits+=b->navigation_.vertex_visits();completed=check_completed_routes(*b);
 }
 check(warm_visits>0&&completed>0);
 cambc::actor.point={2,1};cambc::actor.hp=76;cambc::actor.bag=13;
 cambc::now=14;cambc::fuel=100000;b->turn();b->after_turn();
 check(b->observer_.observations==2&&b->runtime_model_.evaluations==2&&b->last_evaluation_round_==14);
 check(i.round==14&&i.own.position==(plan4::Point{2,1})&&i.own.health==76&&i.own.bag==13);
 check(b->navigation_.vertex_visits()==0);check_limits(*b);
 const auto& menu=b->evaluation_storage_->menu;bool farm=false,bank=false;
 for(int n=0;n<menu.count;++n){farm|=menu.jobs[n].kind==plan4::JobKind::farm;bank|=menu.jobs[n].kind==plan4::JobKind::bank;}
 check(menu.single_count==1&&farm&&bank);
 const auto lookup=b->navigation_.lookup();
 check(lookup.symmetric_distance);
 auto directed=std::make_unique<plan4::Information>(i);directed->travel.symmetric_distance=false;
 const auto symmetric_menu=plan4::make_menu(i),directed_menu=plan4::make_menu(*directed);
 check(symmetric_menu.count==directed_menu.count&&symmetric_menu.single_count==directed_menu.single_count&&symmetric_menu.truncated==directed_menu.truncated);
 for(int n=0;n<symmetric_menu.count;++n){const auto& a=symmetric_menu.jobs[n];const auto& b=directed_menu.jobs[n];
  check(a.kind==b.kind&&a.group==b.group&&a.second_group==b.second_group&&a.altar==b.altar&&a.menu_rank==b.menu_rank&&a.nominal_travel==b.nominal_travel&&a.nominal_completion==b.nominal_completion);
 }
 const int own_group=lookup.distance(i.own.position,i.groups[0].position,lookup.context);
 const int own_altar=lookup.distance(i.own.position,i.altars[0].position,lookup.context);
 const int group_altar=lookup.distance(i.groups[0].position,i.altars[0].position,lookup.context);
 check(own_group==oracle(i.own.position,i.groups[0].position)&&own_group>metric(i.own.position,i.groups[0].position,nullptr));
 check(own_altar==oracle(i.own.position,i.altars[0].position)&&group_altar==oracle(i.groups[0].position,i.altars[0].position));
 completed=check_completed_routes(*b);
 std::cout<<"case actual-navigation-warm: observations=2 backoff=3 warmVisits="<<warm_visits<<" BFS-completed="<<completed<<" farm=1 bank=1 fresh-stock-and-health=1\n";
}
void warm_round_jump_refuses_the_inherited_fallback(){
 auto b=warm_fixture();cambc::fuel=100000;b->turn();b->after_turn();
 check(b->observer_.observations==1&&!b->navigation_.initialized());
 cambc::now=11;cambc::fuel=100000;cambc::actor.bag=9;cambc::actor.hp=73;
 cambc::jump_on_walls=true;const int farms=b->farms,banks=b->bootstrap_banks;
 b->turn();b->after_turn();
 check(cambc::now==12&&!cambc::jump_on_walls);
 check(b->observer_.observations==1&&b->last_evaluation_round_==10);
 check(b->farms==farms&&b->bootstrap_banks==banks&&b->navigation_.vertex_visits()==0);check_limits(*b);
 std::cout<<"case actual-navigation-round-jump: warm terrain suspension stops work and fallback\n";
}
void false_eligibility_after_round_jump_refuses_the_inherited_fallback(){
 auto b=warm_fixture();cambc::fuel=100000;b->turn();b->after_turn();
 check(b->observer_.observations==1&&!b->navigation_.initialized());
 cambc::now=11;cambc::fuel=100000;cambc::actor.bag=9;cambc::actor.hp=73;
 const int farms=b->farms,banks=b->bootstrap_banks;const auto gold_reads=cambc::gold_reads;
 const auto actions=cambc::actions;
 // The actual shop's gold query arms the next owner is_alive query.
 // That query is the real backoff eligibility test: it suspends into round12
 // and returns false, so execution never enters the warmup eligibility body.
 cambc::jump_after_shop=true;b->turn();b->after_turn();
 check(cambc::now==12&&!cambc::jump_after_shop&&!cambc::jump_on_next_alive);
 check(cambc::gold_reads==gold_reads+1&&b->observer_.observations==1&&b->runtime_model_.evaluations==1&&b->last_evaluation_round_==10);
 check(!b->navigation_.initialized()&&b->navigation_.vertex_visits()==0&&b->navigation_.completed_route_entries()==0&&b->navigation_.pending_fields()==0);
 check(b->farms==farms&&b->bootstrap_banks==banks&&cambc::actions==actions);check_limits(*b);
 std::cout<<"case actual-navigation-false-eligibility-jump: false owner getter stops warm work and fallback\n";
}
std::uint64_t expected_kit_version(float strength,float focus,float maximum,unsigned monster,unsigned jungle){
 // Hash the expected fixture payload, including maximum health (which is
 // represented by the version rather than a separate CombatKit field).
 const auto bits=[](float value){unsigned out;__builtin_memcpy(&out,&value,sizeof(out));return out;};
 std::uint64_t result=1469598103934665603ull;
 for(unsigned value:std::array<unsigned,7>{{0,1,bits(strength),bits(focus),bits(maximum),monster,jungle}})result=(result^value)*1099511628211ull;
 return result;
}
void same_round_no_purchase_reuses_the_tick_kit(){
 for(bool denied:{false,true}){
  auto b=warm_fixture();b->last_fallback_row_=10;
  cambc::actor.gold=denied?60:0;cambc::allow_buy=!denied;cambc::fuel=100000;
  b->turn();b->after_turn();
  check(b->observer_.observations==1&&b->runtime_model_.evaluations==1); // Actual macro_decide caller reached.
  check(cambc::buy_calls==0&&cambc::strength_reads==1&&cambc::focus_reads==1);
  check(b->runtime_model_.combat_kits.size()==1&&b->kit_observed_round_==10&&b->kit_epoch_==1);
  const auto version=b->kit_signature_;check(version==expected_kit_version(20,0,100,0,0));
  cambc::fuel=100000;b->turn();b->after_turn();
  check(cambc::strength_reads==1&&cambc::focus_reads==1&&b->runtime_model_.combat_kits.size()==1);
  check(b->kit_signature_==version&&b->kit_observed_round_==10&&b->kit_epoch_==1);
 }
 std::cout<<"case actual-shop-no-purchase: zero gold and denied purchase retain one same-round kit observation\n";
}
void actual_purchases_refresh_the_same_round_kit(){
 auto b=warm_fixture();b->last_fallback_row_=10;cambc::actor.gold=180;cambc::fuel=100000;
 cambc::kit_round_at_purchase=&b->kit_observed_round_;b->turn();b->after_turn();
 check(b->observer_.observations==1&&b->runtime_model_.evaluations==1);
 check(cambc::buy_calls==2&&cambc::actor.gold==0&&b->monster_shards_==5&&b->jungle_shards_==1);
 check(cambc::purchase_kit_rounds==std::vector<int>({-1,-1}));
 check(cambc::strength_reads==2&&cambc::focus_reads==2&&b->runtime_model_.combat_kits.size()==2);
 const auto& before=b->runtime_model_.combat_kits[0];const auto& after=b->runtime_model_.combat_kits[1];
 check(before.clock.round==10&&before.strength==20&&before.focus==0&&before.monster_shards==0&&before.jungle_shards==0&&before.stat_epoch==1);
 check(after.clock.round==10&&after.strength==35&&after.focus==0&&after.monster_shards==5&&after.jungle_shards==1&&after.stat_epoch==2&&after.exact);
 check(cambc::actor.maximum==125&&b->information_storage_->own.maximum_health==125);
 check(before.version==expected_kit_version(20,0,100,0,0)&&after.version==expected_kit_version(35,0,125,5,1));
 check(b->kit_observed_round_==10&&b->kit_epoch_==2&&b->kit_signature_==after.version&&before.version!=after.version);
 std::cout<<"case actual-shop-purchase: two real bulk buys invalidate before mutation and refresh exact stats/version/epoch\n";
}
void a_throwing_actual_purchase_invalidates_the_old_kit(){
 auto b=warm_fixture();b->last_fallback_row_=10;cambc::actor.gold=30;cambc::fuel=100000;
 cambc::kit_round_at_purchase=&b->kit_observed_round_;cambc::throw_on_buy=true;bool threw=false;
 try{b->turn();}catch(const std::runtime_error& error){threw=std::string_view(error.what())=="fixture purchase failure";}
 check(threw&&cambc::buy_calls==1&&cambc::purchase_kit_rounds==std::vector<int>{-1});
 check(b->kit_observed_round_==-1&&b->kit_epoch_==1&&b->kit_signature_==expected_kit_version(20,0,100,0,0));
 check(b->runtime_model_.combat_kits.size()==1&&b->observer_.observations==0&&b->runtime_model_.evaluations==0);
 check(b->monster_shards_==0&&cambc::actor.gold==30&&cambc::actions.empty());
 cambc::throw_on_buy=false;b->production_observe_kit();
 check(b->kit_observed_round_==10&&b->runtime_model_.combat_kits.size()==2&&cambc::strength_reads==2&&cambc::focus_reads==2);
 std::cout<<"case actual-shop-failure: throwing buy leaves old kit invalid until a fresh observation\n";
}
void shop_round_change_observes_the_new_owner_kit(){
 auto b=warm_fixture();b->last_fallback_row_=10;cambc::fuel=100000;cambc::jump_on_gold=true;
 b->turn();b->after_turn();
 check(cambc::now==11&&!cambc::jump_on_gold&&cambc::buy_calls==0);
 check(b->observer_.observations==1&&b->runtime_model_.evaluations==1&&b->information_storage_->round==11);
 check(cambc::strength_reads==2&&cambc::focus_reads==2&&b->runtime_model_.combat_kits.size()==2);
 const auto& before=b->runtime_model_.combat_kits[0];const auto& after=b->runtime_model_.combat_kits[1];
 check(before.clock.round==10&&after.clock.round==11&&after.strength==41&&after.focus==9&&after.stat_epoch==2&&after.exact);
 check(after.version==expected_kit_version(41,9,140,0,0)&&after.version!=before.version&&b->kit_signature_==after.version);
 check(b->kit_observed_round_==11&&b->kit_epoch_==2&&b->information_storage_->own.maximum_health==140);
 std::cout<<"case actual-shop-round-change: resumed gold query refreshes kit before fresh macro observation\n";
}
void install_synthetic_phase_stream(){
 phase_trace::on_emit=[](const phase_trace::Row& row){
  if(row.value("name")!="observer")return;
  cambc::fuel=phase_trace::synthetic_debit>cambc::fuel?0:cambc::fuel-phase_trace::synthetic_debit;
  if(phase_trace::jump_on_observer_emit)++cambc::now;
 };
}
std::size_t event_index(std::string_view event){
 const auto found=std::find(phase_trace::events.begin(),phase_trace::events.end(),event);
 return std::size_t(found-phase_trace::events.begin());
}
void actual_caller_defers_phase_formatting_until_after_its_action(){
 auto b=warm_fixture();b->last_fallback_row_=10;b->observer_.fuel_after=63000;
 auto& stop=b->observer_.stop;stop.stop_stage=7;stop.stop_group=-7;stop.stop_member=-9;stop.groups=1;stop.visited=12;
 stop.collection_fuel=64000;stop.stop_fuel=-1;stop.stop_floor=50000;stop.stop_margin=6000;
 phase_trace::synthetic_debit=3400;install_synthetic_phase_stream();cambc::fuel=100000;b->turn();
 check(b->observer_.observations==1&&b->runtime_model_.evaluations==1&&b->farms==1);
 check(cambc::fuel==63000&&phase_trace::formats==0&&phase_trace::rows.empty());
 check(b->phase_samples_.pending_mask==15&&b->phase_samples_.count==4&&b->phase_seen_==0);
 check(event_index("decision")<event_index("farm-action"));
 b->macro_phase("observer",100000);b->macro_phase("navigation",63000);
 check(b->phase_samples_.count==4&&phase_trace::formats==0); // Same-kind captures are capped before emission.
 // These storage mutations happen after the actual decision/action. A flush
 // must use the captured payload rather than reading the mutable observer DTO.
 auto& info=*b->information_storage_;info.round=77;info.observation_truncated=info.public_layout_truncated=true;
 stop.stop_stage=99;stop.stop_group=98;stop.stop_member=97;stop.groups=96;stop.visited=95;
 stop.collection_fuel=94;stop.stop_fuel=93;stop.stop_floor=92;stop.stop_margin=91;
 const auto actions=cambc::actions;b->after_turn();
 check(b->farms==1&&cambc::actions==actions&&cambc::fuel==59600);
 check(phase_trace::formats==4&&phase_trace::rows.size()==4&&b->phase_seen_==15);
 check(b->phase_samples_.pending_mask==0&&b->phase_samples_.count==0);
 check(event_index("farm-action")<event_index("online-finish")&&event_index("online-finish")<event_index("own-finish")&&event_index("own-finish")<event_index("phase-format"));
 const auto& row=phase_trace::rows[0];
 check(row.version==4&&row.value("name")=="observer"&&row.value("actor")=="3"&&row.value("round")=="10");
 check(row.value("before")=="100000"&&row.value("after")=="63000"&&row.value("truncated")=="0"&&row.value("layoutTruncated")=="0");
 check(row.value("stopStage")=="7"&&row.value("stopGroup")=="-7"&&row.value("stopMember")=="-9"&&row.value("groups")=="1"&&row.value("visited")=="12");
 check(row.value("collectionFuel")=="64000"&&row.value("stopFuel")=="-1"&&row.value("stopFloor")=="50000"&&row.value("stopMargin")=="6000");
 const std::array<const char*,3> names{{"navigation","menu","evaluation"}};
 for(unsigned n=1;n<4;++n)check(phase_trace::rows[n].version==2&&phase_trace::rows[n].value("name")==names[n-1]&&phase_trace::rows[n].value("round")=="10"&&phase_trace::rows[n].value("after")=="63000");
 b->after_turn();check(phase_trace::formats==4&&b->farms==1); // No repeated rows or post-flush action.
 std::cout<<"case actual-phase-deferred: decision/farm precede all formatting; owned rows flush last; synthetic observer-stream debit=3400\n";
}
void actual_caller_omits_stale_or_unaffordable_phase_rows(){
 for(int mode=0;mode<4;++mode){
  auto b=warm_fixture();b->last_fallback_row_=10;b->observer_.fuel_after=63000;cambc::fuel=100000;b->turn();
  check(b->farms==1&&b->phase_samples_.count==4&&phase_trace::formats==0);
  if(mode==0)++cambc::now; // A stale sample must not format even its first row.
  if(mode==1)cambc::fuel=15999;
  if(mode==2){phase_trace::synthetic_debit=50000;install_synthetic_phase_stream();}
  if(mode==3){phase_trace::jump_on_observer_emit=true;install_synthetic_phase_stream();}
  const int farms=b->farms,banks=b->bootstrap_banks;const auto actions=cambc::actions;
  b->after_turn();
  const unsigned emitted=mode<2?0:1;
  check(phase_trace::formats==emitted&&phase_trace::rows.size()==emitted&&b->phase_seen_==(emitted?1u:0u));
  check(b->phase_samples_.pending_mask==0&&b->phase_samples_.count==0);
  check(b->farms==farms&&b->bootstrap_banks==banks&&cambc::actions==actions);
  if(emitted)check(phase_trace::rows[0].value("name")=="observer");
  cambc::fuel=63000;b->after_turn();check(phase_trace::formats==emitted&&b->farms==farms);
 }
 std::cout<<"case actual-phase-omissions: stale clock, initial low fuel, stream depletion and stream round jump omit rows without fallback/action\n";
}
int main(){observe_low_then_warm_on_three_real_backoff_turns();warm_round_jump_refuses_the_inherited_fallback();false_eligibility_after_round_jump_refuses_the_inherited_fallback();
 same_round_no_purchase_reuses_the_tick_kit();actual_purchases_refresh_the_same_round_kit();a_throwing_actual_purchase_invalidates_the_old_kit();shop_round_change_observes_the_new_owner_kit();
 actual_caller_defers_phase_formatting_until_after_its_action();actual_caller_omits_stale_or_unaffordable_phase_rows();std::cout<<"checks "<<checks<<'\n';}
'''


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument('--compiler', required=True)
    cli.add_argument('--node', required=True)
    args = cli.parse_args()
    paths = [Path(__file__).resolve()]
    paths += sorted((ROOT / 'bots/models/common').glob('*.h'))
    paths += sorted((ROOT / 'bots/models/covenant/src').glob('*.h'))
    original = {path: path.read_bytes() for path in paths}
    hashes = {path: hashlib.sha256(raw).hexdigest() for path, raw in original.items()}
    legacy = macro_source(original[ROOT / 'bots/models/common/legacy.h'].decode())
    macros = original[ROOT / 'bots/models/common/macro-methods.h'].decode()
    methods = [method(legacy, signature) for signature in ('void turn()', 'void after_turn()')]
    methods += [method(macros, signature) for signature in
                ('void macro_abort(', 'void macro_finish()', 'void macro_tick()', 'bool macro_farm_move(', 'bool macro_bootstrap(', 'bool macro_decide(')]
    cache = ROOT / '.cache/models-tests'
    cache.mkdir(parents=True, exist_ok=True)
    scratch = Path(tempfile.mkdtemp(prefix='contract-turn-hooks-', dir=cache)).resolve()
    print(f'Scratch: {scratch}', flush=True)
    try:
        for path, raw in original.items():
            if path.suffix == '.h':
                target = scratch / path.relative_to(ROOT)
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(raw)
        eh = Path(args.compiler).parent.parent / 'share/wasi-sysroot/lib/wasm32-wasip1/eh'
        runs = {}
        fixtures = (('repair', PRELUDE, CASES),
                    ('navigation', navigation_prelude(original[ROOT / 'bots/models/common/nav.h'].decode(), legacy, macros), NAVIGATION_CASES))
        for label, arm, prelude, cases in [(label, arm, prelude, cases)
                                          for label, prelude, cases in fixtures for arm in (0, 1)]:
            stem = f'contract-turn-hooks-{label}-{arm}'
            source = scratch / (stem + '.cpp')
            source.write_text(f'#define PLAN4_MODEL_ENABLED {arm}\n' + prelude.replace('ACTUAL_METHODS', '\n'.join(methods)) + cases)
            module = scratch / (stem + '.wasm')
            command = [args.compiler, str(source), '-I', str(scratch), '-std=c++20', '-O3', '-UNDEBUG', '-Wall', '-Wextra', '-Werror',
                       '-fwasm-exceptions', '-mllvm', '-wasm-use-legacy-eh=false', '--no-wasm-opt', f'-L{eh}', '-lunwind', '-o', str(module)]
            compiled = subprocess.run(command, capture_output=True)
            (scratch / (stem + '.compile.log')).write_bytes(compiled.stdout + compiled.stderr)
            assert compiled.returncode == 0, compiled.stderr.decode(errors='replace')[:5000]
            driver = scratch / (stem + '.mjs')
            driver.write_text("import fs from 'node:fs';import {WASI} from 'node:wasi';"
                              "const w=new WASI({version:'preview1',args:[],env:{}});"
                              f"const m=await WebAssembly.compile(fs.readFileSync(new URL('./{stem}.wasm',import.meta.url)));"
                              "const i=await WebAssembly.instantiate(m,{wasi_snapshot_preview1:w.wasiImport,env:{}});w.start(i);")
            run = subprocess.run([args.node, str(driver)], capture_output=True)
            (scratch / (stem + '.stdout.log')).write_bytes(run.stdout)
            (scratch / (stem + '.stderr.log')).write_bytes(run.stderr)
            assert run.returncode == 0, (run.stdout + run.stderr).decode(errors='replace')[-5000:]
            runs.setdefault(label, []).append(run.stdout)
            print(f'fixture={label} arm={arm}: ' + run.stdout.decode().strip().replace('\n', '; '))
        for label, outcomes in runs.items():
            comparable = [re.sub(rb'case ordinary-consultation:[^\n]*\n', b'case ordinary-consultation: observed arm choice\n', run) for run in outcomes]
            assert comparable[0] == comparable[1], f'{label}: arm outcomes differ outside the verified model choice'
        assert all(path.read_bytes() == raw for path, raw in original.items()), 'Input changed during harness run'
        assert hashes == {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
        assert scratch.parent == cache.resolve() and scratch.name.startswith('contract-turn-hooks-')
        assert all(path.suffix in {'.h', '.cpp', '.wasm', '.mjs', '.log'} for path in scratch.rglob('*') if path.is_file())
        shutil.rmtree(scratch)
        assert not scratch.exists()
        print(f'Both arms passed; {len(paths)} source inputs verified; successful scratch removed.')
        print('Supported ordinary consultation, initial FARM admission and measured late-round arm choices verified.')
        print('Actual navigation: three backoff warmups, BFS detours, fresh state, FARM/BANK menu and round-jump refusal verified.')
        print('Source-bound turn-flow only; SDK collector, engine execution and native fuel safety remain separate checks.')
    except BaseException:
        print(f'FAILED: retained scratch and logs at {scratch}', flush=True)
        raise


if __name__ == '__main__':
    main()
