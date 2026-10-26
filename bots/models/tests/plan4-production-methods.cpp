// Focused SDK double for the actual included Bot methods. No engine calls.
#include "../common/production-evidence.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <memory>
#include <variant>
#include <vector>
#define PLAN4_REFERENCE_VERSION "test-production-v1"
namespace plan4 {struct LogRow {LogRow(const char*,const char*){}template<class T>LogRow& field(const char*,T){return *this;}void emit(){}};}
namespace cambc {
using handle_champion_t=int;using handle_camp_group_t=int;
enum class team{ally,enemy};struct vec2{int x=0,y=0;};
struct bitboard {
    std::array<bool,100> bits{};
    bool is_set(vec2 p)const{return p.x>=0 && p.y>=0 && p.x<10 && p.y<10 && bits[p.y*10+p.x];}
    void set(vec2 p){bits[p.y*10+p.x]=true;}
    bool is_empty()const{for(bool b:bits)if(b)return false;return true;}
    friend bitboard operator&(const bitboard& a,const bitboard& b){bitboard out;for(int n=0;n<100;++n)out.bits[n]=a.bits[n]&&b.bits[n];return out;}
    friend bool operator==(const bitboard& a,const bitboard& b){return a.bits==b.bits;}
};
inline int round=10,fuel=100000,queries=0;inline bool jump_on_state=false,drop_on_vision=false;
inline bitboard vision_mask{},spawn_mask{};
struct champion {
    vec2 point{1,1};float hp=50,maximum=100;unsigned bag=24;bool alive=true,recall=false,tribute=false,teleport=false;
    operator handle_champion_t()const{return 3;}
    bool is_alive()const{++queries;return alive;}vec2 position()const{++queries;return point;}
    float health()const{++queries;return hp;}float max_health()const{++queries;return maximum;}
    bool is_recalling()const{++queries;return recall;}bool is_submitting_gems()const{++queries;return tribute;}
    bool is_teleporting()const{++queries;return teleport;}unsigned gem_count()const{++queries;return bag;}
    int champion_name()const{return 1;}int attack_range()const{return 1;}float strength()const{return 20;}float focus()const{return 0;}
};
inline champion actor;inline champion& self(){return actor;}
inline int round_number(){return round;}inline int fuel_remaining(){return fuel;}
struct camp{vec2 spawn{};vec2 spawn_position()const{return spawn;}};
namespace vision {inline bitboard visible_tiles(){++queries;if(drop_on_vision)fuel=59999;return vision_mask;}}
namespace constants {inline double submit_gem_duration_multiplier=.1;inline int submit_gem_base_duration=0,submit_gem_minimum_duration=1;}
namespace map {
struct camp_group_alive{};struct camp_group_reward_available{};struct camp_group_respawning{int timer=0;};
using camp_group_state=std::variant<camp_group_alive,camp_group_reward_available,camp_group_respawning>;
struct camp_group {
    int id=20;mutable int state_calls=0;std::vector<camp> members{{{4,4}}};int lifecycle=0;
    operator handle_camp_group_t()const{return id;}const auto& camps()const{return members;}
    camp_group_state state()const{assert(vision_mask.is_set(members[0].spawn));++state_calls;
        if(jump_on_state)++round;
        if(lifecycle==0)return camp_group_alive{};if(lifecycle==1)return camp_group_reward_available{};return camp_group_respawning{20};}
};
inline const bitboard& spawn_region(team){return spawn_mask;}
}
}
namespace {
using cambc::bitboard;using cambc::camp;using cambc::team;
struct Sink {
    std::vector<plan4::runtime::PhaseStart> starts;std::vector<plan4::runtime::PhaseReceipt> receipts;
    bool phase_started(const plan4::runtime::PhaseStart& start){starts.push_back(start);return true;}
    void phase_completed(const plan4::runtime::PhaseReceipt& receipt){receipts.push_back(receipt);}
    void observe_kit(std::uint64_t,std::uint32_t){}void observe_combat(const plan4::runtime::CombatKit&){}
};
struct Bot {
    Sink runtime_model_;std::uint32_t kit_epoch_=2;int kit_observed_round_=10;bool kit_seen_=true,recovering_=false;
    std::uint64_t kit_signature_=0;unsigned monster_shards_=0,jungle_shards_=0,decision_sequence_=1;bool previous_tribute_=false;
    struct Entry{cambc::map::camp_group group;bitboard spawns;};std::vector<Entry> groups_;
    bool macro_active_=true,macro_second_=false;plan4::Job macro_job_{};
    std::unique_ptr<plan4::Information> information_storage_=std::make_unique<plan4::Information>();
    Bot(){Entry entry;entry.spawns.set({4,4});groups_.push_back(entry);macro_job_.kind=plan4::JobKind::farm;macro_job_.group=0;
        auto& info=*information_storage_;info.round=10;info.own.id=3;info.own.position={1,1};info.own.health=50;info.own.maximum_health=100;
        info.own.recall_safe=true;info.own_spawn={0,0};info.group_count=1;auto& group=info.groups[0];group.id=20;group.visible=group.all_spawns_visible=true;
        group.lifecycle=plan4::Lifecycle::living;group.clear_rounds=4;group.health_provenance={10,plan4::Source::personal_vision,plan4::Status::exact_now};}
#include "../common/production-methods.h"
};
void reset(){cambc::round=10;cambc::fuel=100000;cambc::queries=0;cambc::jump_on_state=cambc::drop_on_vision=false;
    cambc::actor={};cambc::vision_mask={};cambc::vision_mask.set({4,4});cambc::spawn_mask={};cambc::spawn_mask.set({0,0});}
void mask_is_frozen_not_a_radius_and_same_key_is_not_restarted(){
    reset();auto bot=std::make_unique<Bot>();bitboard goals;goals.set({3,3});bot->production_begin_travel_mask(goals,4,20);
    assert(bot->production_tracker_.pending());const auto sequence=bot->production_tracker_.pending_key()->phase_sequence;
    bot->production_begin_travel_mask(goals,2,20);assert(bot->production_tracker_.pending_key()->phase_sequence==sequence);
    cambc::actor.point={3,2};bot->production_poll(true);assert(bot->runtime_model_.receipts.empty());
    cambc::actor.point={3,3};bot->production_poll(true);assert(bot->runtime_model_.receipts.size()==1 && bot->runtime_model_.receipts[0].completed && bot->runtime_model_.receipts[0].group_id==20);
    goals={};goals.set({5,5});bot->production_begin_travel_mask(goals,3,21);goals={};goals.set({6,6});bot->production_begin_travel_mask(goals,4,21);
    assert(bot->runtime_model_.receipts.size()==2 && !bot->runtime_model_.receipts[1].completed && bot->runtime_model_.receipts[1].censor==plan4::runtime::PhaseCensor::mode_change);
}
void clear_needs_matching_real_action_and_authoritative_visible_state(){
    reset();auto bot=std::make_unique<Bot>();assert(bot->production_begin_clear(*bot->information_storage_,0));
    bot->groups_[0].group.lifecycle=1;bot->production_poll(true);assert(!bot->runtime_model_.receipts.back().completed);
    reset();bot=std::make_unique<Bot>();assert(bot->production_prepare_clear_target({{4,4}}));assert(!bot->production_clear_target({{5,5}}));
    assert(bot->production_clear_target({{4,4}}));bot->production_clear_action(true);bot->groups_[0].group.lifecycle=1;bot->production_poll(true);
    assert(bot->runtime_model_.receipts.back().completed && bot->runtime_model_.receipts.back().phase==plan4::runtime::ProductionPhase::clear && bot->runtime_model_.receipts.back().group_id==20);
    reset();bot=std::make_unique<Bot>();assert(bot->production_begin_clear(*bot->information_storage_,0));bot->production_clear_action(true);
    const int queries=bot->groups_[0].group.state_calls;cambc::vision_mask={};bot->production_poll();
    assert(bot->groups_[0].group.state_calls==queries && bot->runtime_model_.receipts.back().censor==plan4::runtime::PhaseCensor::lost_sight);
    reset();bot=std::make_unique<Bot>();bitboard goals;goals.set({9,9});bot->production_begin_travel_mask(goals,8,20);
    bot->information_storage_->groups[0].health_provenance={};assert(!bot->production_prepare_clear_target({{4,4}}));
    assert(!bot->production_tracker_.pending() && bot->runtime_model_.receipts.back().censor==plan4::runtime::PhaseCensor::mode_change);
    reset();bot=std::make_unique<Bot>();assert(bot->production_begin_clear(*bot->information_storage_,0));bot->production_clear_action(true);
    cambc::round=11;bot->kit_observed_round_=11;bot->groups_[0].group.lifecycle=1;bot->production_poll();
    assert(!bot->runtime_model_.receipts.back().completed && bot->runtime_model_.receipts.back().censor==plan4::runtime::PhaseCensor::ambiguous_endpoint);
}
void exact_restore_is_owner_start_and_emergency_is_censored(){
    reset();auto bot=std::make_unique<Bot>();assert(bot->production_begin_restore(*bot->information_storage_));
    assert(bot->production_tracker_.pending_key()->nominal_duration==8);
    for(int r=11;r<=17;++r){cambc::round=r;bot->kit_observed_round_=r;cambc::actor.recall=true;bot->production_poll();assert(bot->production_tracker_.pending());}
    cambc::round=18;bot->kit_observed_round_=18;cambc::actor.point={0,0};cambc::actor.recall=false;cambc::actor.hp=100;
    bot->production_poll(true);assert(bot->production_tracker_.pending());bot->production_poll();
    assert(bot->runtime_model_.receipts.back().completed && bot->runtime_model_.receipts.back().end_round==18);
    reset();bot=std::make_unique<Bot>();bot->recovering_=true;assert(!bot->production_begin_restore(*bot->information_storage_));
    assert(bot->runtime_model_.receipts.back().censor==plan4::runtime::PhaseCensor::safety && !bot->runtime_model_.receipts.back().completed);
}
void planned_restore_survives_recovery_recall_and_spawn_until_full_owner_start(){
    for(bool already_recovering:{false,true}) {
        reset();auto bot=std::make_unique<Bot>();bot->production_observe_kit(true);bot->recovering_=already_recovering;
        assert(bot->production_begin_restore(*bot->information_storage_,true));
        const auto start=*bot->production_tracker_.pending_key();
        assert(start.phase==plan4::runtime::ProductionPhase::restore && start.nominal_duration==8 && !start.emergency &&
            bot->production_contract_restoring() && !bot->production_clock().emergency);
        bot->recovering_=true;
        for(int r=11;r<=14;++r) {
            cambc::round=r;cambc::actor.recall=true;bot->production_observe_kit();bot->production_poll();bot->production_poll(true);
            assert(bot->kit_observed_round_==r && bot->production_tracker_.pending() && bot->production_contract_restoring() &&
                bot->production_tracker_.pending_key()->phase_sequence==start.phase_sequence && bot->runtime_model_.receipts.empty());
            assert(bot->production_begin_restore(*bot->information_storage_,true)); // The existing planned phase keeps its identity.
        }
        cambc::actor.recall=false;cambc::actor.point={0,0};
        for(int r=15;r<=16;++r) {
            cambc::round=r;cambc::actor.hp=r==15?70:90;bot->production_observe_kit();bot->production_poll();bot->production_poll(true);
            assert(bot->production_tracker_.pending() && bot->runtime_model_.receipts.empty() && !bot->production_clock().emergency);
        }
        cambc::round=17;cambc::actor.hp=100;bot->production_observe_kit();bot->production_poll(true);
        assert(bot->production_tracker_.pending() && bot->runtime_model_.receipts.empty());
        cambc::round=18;bot->production_observe_kit();bot->production_poll();
        assert(!bot->production_tracker_.pending() && !bot->production_contract_restore_ && !bot->production_contract_restoring());
        assert(bot->runtime_model_.starts.size()==1 && bot->runtime_model_.receipts.size()==1);
        const auto& receipt=bot->runtime_model_.receipts.back();
        assert(receipt.completed && receipt.endpoint_observed && !receipt.emergency && receipt.censor==plan4::runtime::PhaseCensor::none &&
            receipt.start_round==10 && receipt.end_round==18 && receipt.phase_sequence==start.phase_sequence &&
            receipt.requested_mode==3 && receipt.executed_mode==3 && receipt.stat_epoch==start.stat_epoch);
        assert(bot->recovering_ && bot->production_clock().emergency);
        // A completed planned restoration does not exempt the next spontaneous recovery.
        auto& info=*bot->information_storage_;info.round=18;info.own.position={0,0};info.own.on_spawn=true;cambc::actor.hp=info.own.health=50;
        assert(!bot->production_begin_restore(info) && bot->runtime_model_.receipts.size()==2 &&
            bot->runtime_model_.receipts.back().emergency && bot->runtime_model_.receipts.back().censor==plan4::runtime::PhaseCensor::safety &&
            !bot->runtime_model_.receipts.back().completed && !bot->production_contract_restore_);
    }
}
void interrupted_planned_restore_cannot_exempt_spontaneous_recovery(){
    for(int interruption=0;interruption<3;++interruption) {
        reset();auto bot=std::make_unique<Bot>();bot->production_observe_kit(true);bot->recovering_=true;
        assert(bot->production_begin_restore(*bot->information_storage_,true) && bot->production_contract_restoring());
        if(interruption==0) bot->production_cancel(plan4::runtime::PhaseCensor::interrupted);
        if(interruption==1) {++bot->decision_sequence_;bot->production_poll();}
        if(interruption==2) {cambc::actor.alive=false;bot->production_poll();cambc::actor.alive=true;}
        assert(!bot->production_tracker_.pending() && !bot->production_contract_restore_ && !bot->production_contract_restoring() &&
            bot->runtime_model_.receipts.size()==1 && !bot->runtime_model_.receipts.back().completed &&
            bot->runtime_model_.receipts.back().censor==(interruption==1?plan4::runtime::PhaseCensor::mode_change:plan4::runtime::PhaseCensor::interrupted));
        assert(!bot->production_begin_restore(*bot->information_storage_) && bot->runtime_model_.receipts.size()==2 &&
            bot->runtime_model_.receipts.back().censor==plan4::runtime::PhaseCensor::safety && bot->runtime_model_.receipts.back().emergency &&
            !bot->runtime_model_.receipts.back().completed && !bot->production_contract_restore_);
    }
}
void nonrestore_phases_never_inherit_the_planned_restore_exemption(){
    for(int next=0;next<3;++next) {
        reset();auto bot=std::make_unique<Bot>();bot->production_observe_kit(true);bot->recovering_=true;
        assert(bot->production_begin_restore(*bot->information_storage_,true));
        if(next==0) bot->production_begin_travel({3,3},0,2);
        if(next==1) assert(!bot->production_begin_clear(*bot->information_storage_,0));
        if(next==2) bot->production_begin_bank(24);
        assert(!bot->production_tracker_.pending() && !bot->production_contract_restore_ && !bot->production_contract_restoring() &&
            bot->runtime_model_.starts.size()==2 && bot->runtime_model_.receipts.size()==2);
        const auto& first=bot->runtime_model_.receipts[0];const auto& second=bot->runtime_model_.receipts[1];
        const auto expected=next==0?plan4::runtime::ProductionPhase::travel:next==1?plan4::runtime::ProductionPhase::clear:plan4::runtime::ProductionPhase::bank;
        assert(!first.completed && first.phase==plan4::runtime::ProductionPhase::restore && first.censor==plan4::runtime::PhaseCensor::mode_change &&
            !second.completed && second.phase==expected && second.emergency && second.censor==plan4::runtime::PhaseCensor::safety);
        // Even a mistaken flag on a nonrestore begin cannot grant an exemption.
        assert(!bot->production_begin(expected,2,next==0?0:next==1?2:1,-1,true) && !bot->production_contract_restore_ &&
            bot->runtime_model_.receipts.back().censor==plan4::runtime::PhaseCensor::safety);
    }
}
void source_round_budget_and_member_bounds_prevent_dynamic_queries(){
    reset();auto bot=std::make_unique<Bot>();bot->groups_[0].group.members.resize(33,{{4,4}});
    assert(!bot->production_begin_clear(*bot->information_storage_,0) && bot->groups_[0].group.state_calls==0);
    reset();bot=std::make_unique<Bot>();cambc::fuel=59999;assert(!bot->production_begin_clear(*bot->information_storage_,0) && cambc::queries==0);
    reset();bot=std::make_unique<Bot>();cambc::drop_on_vision=true;assert(!bot->production_begin_clear(*bot->information_storage_,0) && bot->groups_[0].group.state_calls==0);
    reset();bot=std::make_unique<Bot>();cambc::jump_on_state=true;assert(!bot->production_begin_clear(*bot->information_storage_,0) && bot->runtime_model_.starts.empty());
    reset();bot=std::make_unique<Bot>();assert(bot->production_begin_clear(*bot->information_storage_,0));bot->production_clear_action(true);
    ++bot->decision_sequence_;bot->production_poll();assert(bot->runtime_model_.receipts.back().censor==plan4::runtime::PhaseCensor::mode_change);
}
void move_action_receipt_keeps_exact_round_and_skipped_group_poll_is_not_continuity(){
    reset();auto bot=std::make_unique<Bot>();bitboard goals;goals.set({3,3});bot->production_begin_travel_mask(goals,4,20);
    const int queries=cambc::queries;cambc::fuel=40000;bot->production_move_result({3,3},true,10);
    assert(cambc::queries==queries && bot->runtime_model_.receipts.back().completed && bot->runtime_model_.receipts.back().end_round==10);
    reset();bot=std::make_unique<Bot>();bot->production_begin_travel_mask(goals,4,20);cambc::fuel=34000;bot->production_move_result({3,3},true,10);
    cambc::fuel=100000;cambc::round=11;bot->kit_observed_round_=11;cambc::actor.point={3,3};bot->production_poll();
    assert(!bot->runtime_model_.receipts.back().completed && bot->runtime_model_.receipts.back().censor==plan4::runtime::PhaseCensor::ambiguous_endpoint);
    reset();bot=std::make_unique<Bot>();bot->production_observe_kit(true);assert(bot->production_begin_clear(*bot->information_storage_,0));bot->production_clear_action(true);
    cambc::round=11;bot->production_observe_kit();assert(bot->production_tracker_.last_observed_round()==10);
    cambc::round=12;bot->production_observe_kit();bot->groups_[0].group.lifecycle=1;bot->production_poll();
    assert(!bot->runtime_model_.receipts.back().completed && bot->runtime_model_.receipts.back().censor==plan4::runtime::PhaseCensor::lost_sight);
}
}
int main(){mask_is_frozen_not_a_radius_and_same_key_is_not_restarted();clear_needs_matching_real_action_and_authoritative_visible_state();
    exact_restore_is_owner_start_and_emergency_is_censored();source_round_budget_and_member_bounds_prevent_dynamic_queries();
    move_action_receipt_keeps_exact_round_and_skipped_group_poll_is_not_continuity();
    planned_restore_survives_recovery_recall_and_spawn_until_full_owner_start();interrupted_planned_restore_cannot_exempt_spontaneous_recovery();
    nonrestore_phases_never_inherit_the_planned_restore_exemption();
    std::puts("production methods: 8 SDK-double scenarios passed; no engine or capture attribution");}
