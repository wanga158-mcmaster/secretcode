// Reuse the returned observation fixture's controlled SDK declarations rather
// than copy an ABI-shaped fake. Its own test main is not run in this executable.
#define main observations_fixture_main
#include "observations.cpp"
#undef main

namespace cambc::detail {
enum class entity_kind:std::uint32_t{champion=1,camp=2};
int sdk_bc_round_number(std::uint32_t*);
int sdk_attack(std::uint32_t,std::uint32_t);
int sdk_cast(std::uint32_t,std::uint32_t,std::uint32_t,std::int32_t,std::int32_t,std::int32_t*);
int sdk_move_to(std::int32_t,std::int32_t,std::int32_t*);
int sdk_camp_state(std::uint32_t,std::uint32_t*);
int sdk_camp(std::uint32_t,camp_state*);
int sdk_camp_buffs(std::uint32_t,camp_buff_data*,std::uint32_t,std::uint32_t*);
}
#include "../src/execution.h"

// This is the real single-action executor and observation movement adapter.
// Own-body/ability/champion readback is a controlled seam. Command mocks report
// raw outcomes and mutate their own separate physical state. Fake admission
// verifies reserve/order semantics; it does not qualify native fuel prices.
struct CommandBudget:Budget {
    std::uint64_t held_tail{},query_bound{2000},command_bound{2000},pure_bound{20000},command_hold{};
    unsigned commands{},command_closes{};bool deny{},deny_command{},fail_command_close{};
    bool admit(std::uint64_t){return !deny&&!poisoned;}
    void reserve(std::uint64_t value){held_tail=value;}
    bool before_command(){CHECK(!in_span);if(deny_command||poisoned)return false;in_span=true;++commands;command_hold=held_tail;return true;}
    bool after_command(){CHECK(in_span);in_span=false;++command_closes;if(fail_command_close){poisoned=true;return false;}return true;}
    void interrupted(){in_span=false;poisoned=true;}
};
struct CommandObs:A::Observations {
    G::SelfFact physical{};G::AbilityFact ability{};A::MicroChampion other{};
    std::array<std::uint8_t,25> floor{};unsigned self_reads{},ability_reads{},champion_reads{},movement_reads{};
    CommandObs(){
        D::reset();native_facts()={};epoch=7;native_begin_input();floor.fill(1);CHECK(set_terrain(5,5,floor));
        physical.id=0;physical.epoch=native_facts().epoch;physical.pose={1,1};physical.team=0;
        physical.hp=50;physical.max_hp=100;physical.strength=20;physical.move_speed=1;physical.attack_range=1;physical.gems=26;
        physical.alive=physical.raw_m=physical.raw_a=physical.raw_c=true;
        physical.body_actual=physical.restrictions_actual=physical.buffs_actual=physical.channel_actual=true;
        physical.walked_actual=physical.gems_actual=physical.custody_actual=true;native_facts().self=physical;
        ability.epoch=physical.epoch;ability.cooldown_actual=true;ability.descriptor.actual=ability.descriptor.present=true;
        ability.descriptor.kind=G::AbilityKind::damage;ability.descriptor.target_mask=G::target_camps;ability.descriptor.range=3;
        other.epoch=physical.epoch;other.id=2;other.pose={2,1};other.team=1;other.hp=80;other.max_hp=100;
        other.alive=other.body_actual=other.buffs_actual=other.visible=other.visibility_actual=true;
    }
    template<class B> bool native_read_self(B& budget,G::SelfFact& out,bool gems=false,bool=false){
        ++self_reads;if(!budget.before_query())return false;D::touch(D::Import::body);
        if(!budget.after_query()){native_facts().reject();return false;}
        out=physical;out.epoch=native_facts().epoch;out.ordinary_walked=walked;out.walked_actual=true;
        if(gems)out.gems_actual=out.custody_actual=true;return true;
    }
    template<class B> bool native_read_ability(B& budget,unsigned slot,G::AbilityFact& out){
        ++ability_reads;CHECK(slot<2);if(!budget.before_query())return false;D::touch(D::Import::design);
        if(!budget.after_query()){native_facts().reject();return false;}out=ability;out.epoch=native_facts().epoch;return true;
    }
    template<class B> bool native_read_champion(B& budget,G::NativeId id,A::MicroChampion& out){
        ++champion_reads;if(!budget.before_query())return false;D::touch(D::Import::body);CHECK(id==other.id);
        if(!budget.after_query()){native_facts().reject();return false;}out=other;out.epoch=native_facts().epoch;return true;
    }
    template<class B> bool movement(B& budget,const G::SelfFact& me,A::Point point,A::BlockKind& reason,bool& permitted){
        ++movement_reads;return A::Observations::movement(budget,me,point,reason,permitted);
    }
};
namespace command_mock {
inline CommandObs* obs{};inline unsigned commands{},round_queries{},changed_round_on{},feedback_at_callback{};
inline std::uint32_t kind{},id{},slot{};inline A::Point destination{};inline std::uint32_t camp_life{1};inline float camp_hp{80};
inline int code{},applied{1};inline bool throws{};
inline void reset(CommandObs& value){obs=&value;commands=round_queries=changed_round_on=feedback_at_callback=0;kind=id=slot=0;
    destination={};camp_life=1;camp_hp=80;code=0;applied=1;throws=false;}
inline void command(){CHECK(D::budget&&D::budget->in_span&&obs);++commands;if(throws)throw std::runtime_error("controlled SDK command failure");}
}
namespace cambc::detail {
inline int sdk_bc_round_number(std::uint32_t* out){touch(static_cast<Import>(8));++command_mock::round_queries;
    *out=command_mock::changed_round_on==command_mock::round_queries?8:7;return 0;}
inline int sdk_attack(std::uint32_t kind,std::uint32_t id){
    command_mock::command();command_mock::kind=kind;command_mock::id=id;
    if(command_mock::code)return command_mock::code;
    auto& obs=*command_mock::obs;obs.physical.raw_a=false;if(obs.physical.ranged)obs.physical.raw_m=false;
    if(kind==std::uint32_t(entity_kind::camp)){command_mock::camp_hp-=20;if(command_mock::camp_hp<=0)command_mock::camp_life=0;}
    else obs.other.hp-=20;return 0;
}
inline int sdk_cast(std::uint32_t slot,std::uint32_t kind,std::uint32_t id,std::int32_t x,std::int32_t y,std::int32_t* out){
    command_mock::command();command_mock::slot=slot;command_mock::kind=kind;command_mock::id=id;command_mock::destination={x,y};
    if(command_mock::code)return command_mock::code;*out=command_mock::applied;if(!*out)return 0;
    auto& obs=*command_mock::obs;obs.physical.raw_c=false;
    if(obs.ability.descriptor.kind==G::AbilityKind::reset)obs.physical.raw_a=true;
    else if(obs.ability.descriptor.kind==G::AbilityKind::heal){if(id==obs.physical.id)obs.physical.hp+=30;else obs.other.hp+=30;}
    else if(obs.ability.descriptor.kind==G::AbilityKind::shield)obs.physical.shield+=30;
    else if(obs.ability.descriptor.kind==G::AbilityKind::dash)obs.physical.pose={x,y};
    else if(kind==std::uint32_t(entity_kind::camp)){command_mock::camp_hp-=30;if(command_mock::camp_hp<=0)command_mock::camp_life=0;}
    else if(kind==std::uint32_t(entity_kind::champion))obs.other.hp-=30;
    return 0;
}
inline int sdk_move_to(std::int32_t x,std::int32_t y,std::int32_t* out){
    command_mock::command();command_mock::destination={x,y};if(command_mock::code)return command_mock::code;
    *out=command_mock::applied;if(*out){command_mock::obs->physical.pose={x,y};command_mock::obs->physical.raw_m=false;}return 0;
}
inline int sdk_camp_state(std::uint32_t id,std::uint32_t* out){touch(Import::camp);CHECK(id==0);*out=command_mock::camp_life;return 0;}
inline int sdk_camp(std::uint32_t id,camp_state* out){touch(Import::camp);CHECK(id==0);*out={2,1,command_mock::camp_hp,100,10};return 0;}
inline int sdk_camp_buffs(std::uint32_t id,camp_buff_data*,std::uint32_t capacity,std::uint32_t* out){touch(Import::buffs);CHECK(id==0&&capacity==3);*out=0;return 0;}
}
struct CommandFixture {
    CommandObs obs;CommandBudget budget;A::ExecutionContext context{};A::ExecutionResult callback{};
    unsigned records{},imports_at_command{};
    CommandFixture(){
        command_mock::reset(obs);D::budget=&budget;context.actual_self=obs.physical;budget.held_tail=777;
        G::CampFact camp{};camp.epoch=obs.native_facts().epoch;camp.id=camp.group_id=0;camp.member_ordinal=camp.group_ordinal=0;
        camp.pose={2,1};camp.hp=80;camp.max_hp=100;camp.life=G::Life::awake;camp.life_actual=camp.body_actual=true;
        // No optional buff estimate is needed to execute an affordable Basic.
        camp.buffs_actual=false;context.camp=camp;
    }
    A::MicroAction action(A::MicroKind kind=A::MicroKind::basic_camp,G::NativeId target=0,unsigned slot=0){
        return {kind,slot,target,0,kind==A::MicroKind::move?A::Point{2,1}:A::Point{2,1},obs.native_facts().epoch};
    }
    void recorded(const A::ExecutionResult& value){
        ++records;callback=value;
        if(value.attempted){
            CHECK(command_mock::commands==1&&obs.self_reads==0&&obs.champion_reads==0);
            CHECK(value.pending&&!value.after_actual&&value.after_epoch==obs.native_facts().epoch);
            CHECK(value.after.epoch==value.after_epoch&&value.after_epoch.attempt==value.before.epoch.attempt+1);
            CHECK(!obs.native_facts().self.body_actual);
            CHECK(budget.command_closes==1&&!budget.in_span);imports_at_command=D::count;
        }
    }
    A::ExecutionResult run(const A::MicroAction& chosen){return A::execute_action(obs,budget,chosen,context,[&](const auto& value){recorded(value);});}
};

static void basic_zero_camp_records_before_feedback_and_uses_actual_effects(){
    ++groups;CommandFixture f;const auto result=f.run(f.action());
    CHECK(result.attempted&&result.returned&&result.outcome==A::MicroOutcome::applied&&result.recorded&&f.records==1);
    CHECK(result.after_actual&&!result.pending&&result.after.raw_a==false&&result.after.raw_m&&result.after.gems_actual&&result.after.gems==26);
    CHECK(result.camp_after&&result.camp_after->hp==60&&result.camp_after->body_actual&&result.camp_after->buffs_actual);
    CHECK(command_mock::kind==std::uint32_t(cambc::detail::entity_kind::camp)&&command_mock::id==0);
    CHECK(f.imports_at_command<D::count&&f.budget.command_hold>=777+9*f.budget.query_bound&&f.budget.held_tail==777);
    CHECK(result.after_epoch.revision==result.before.epoch.revision+1&&result.after_epoch.attempt==1);
}
static void unseen_move_false_preserves_resources_and_publishes_no_future_pose(){
    ++groups;CommandFixture f;D::visible=0;command_mock::applied=0;
    const auto result=f.run(f.action(A::MicroKind::move));
    CHECK(result.attempted&&result.returned&&result.outcome==A::MicroOutcome::refused&&f.records==1);
    CHECK(result.after_actual&&!result.pending&&result.after.pose==result.before.pose&&result.after.raw_m&&result.after.raw_a&&result.after.raw_c);
    CHECK(!f.obs.walked&&!result.walk_locked&&f.callback.after.pose==A::Point{}&&!f.callback.after_actual);
    CHECK(command_mock::commands==1&&f.budget.held_tail==777);
}
static void visible_prevalidation_is_not_a_raw_attempt_or_a_resource_spend(){
    ++groups;CommandFixture f;D::occupancy[1]=1;const auto result=f.run(f.action(A::MicroKind::move));
    CHECK(result.prevalidation&&result.outcome==A::MicroOutcome::not_attempted&&!result.attempted&&!result.returned&&!result.pending);
    CHECK(result.blocker==A::BlockKind::champion&&result.after_actual&&result.after.epoch==result.before.epoch);
    CHECK(result.after.raw_m&&result.after.raw_a&&result.after.raw_c&&result.after.pose==result.before.pose);
    CHECK(f.records==1&&command_mock::commands==0&&f.budget.commands==0&&f.budget.held_tail==777);
}
static void known_return_survives_feedback_denial_and_command_span_failure(){
    ++groups;
    for(unsigned scenario=0;scenario<2;++scenario){
        CommandFixture f;if(scenario)f.budget.fail_command_close=true;
        const auto result=A::execute_action(f.obs,f.budget,f.action(),f.context,[&](const auto& value){
            f.recorded(value);if(!scenario)f.budget.allow=f.budget.before;
        });
        CHECK(result.attempted&&result.outcome==A::MicroOutcome::applied&&result.pending&&!result.after_actual&&f.records==1);
        CHECK(f.callback.outcome==A::MicroOutcome::applied&&result.after_epoch.attempt==1&&result.after.epoch==result.after_epoch);
        CHECK(command_mock::commands==1&&f.obs.self_reads==0&&f.budget.held_tail==777&&!f.budget.in_span);
        if(scenario)CHECK(f.obs.native_facts().epoch.round==-1&&f.budget.poisoned);
    }
}
static void unexpected_sdk_failure_is_recorded_unknown_then_propagated(){
    ++groups;
    for(unsigned scenario=0;scenario<2;++scenario){
        CommandFixture f;command_mock::throws=scenario==0;command_mock::code=scenario?123:0;bool caught=false;
        try{f.run(f.action());}catch(...){caught=true;}
        CHECK(caught&&f.records==1&&f.callback.attempted&&f.callback.pending&&f.callback.outcome==A::MicroOutcome::unknown);
        CHECK(f.callback.returned==(scenario==1)&&!f.callback.after_actual&&f.callback.after_epoch.attempt==1);
        CHECK(f.budget.command_closes==1&&!f.budget.in_span&&f.budget.held_tail==777&&f.obs.self_reads==0);
        CHECK(f.obs.native_facts().epoch.round==-1);
    }
}
static void actual_reset_and_exclusive_cast_history_do_not_fabricate_another_walk(){
    ++groups;
    for(bool succeeds:std::array<bool,2>{false,true}){
        CommandFixture f;f.obs.physical.raw_a=false;f.context.actual_self=f.obs.physical;
        f.obs.ability.descriptor.kind=G::AbilityKind::reset;f.obs.ability.descriptor.target_mask=G::target_self;
        f.obs.ability.descriptor.exclusive_with_movement=true;command_mock::applied=succeeds;
        auto reset=f.action(A::MicroKind::cast_self);reset.point=f.obs.physical.pose;
        const auto result=f.run(reset);
        CHECK(result.outcome==(succeeds?A::MicroOutcome::applied:A::MicroOutcome::refused)&&result.after_actual&&!result.pending);
        CHECK(command_mock::kind==0&&command_mock::id==0&&command_mock::destination==A::Point{-1,-1});
        CHECK(result.after.raw_a==succeeds&&result.after.raw_c==!succeeds&&result.after.raw_m&&result.walk_locked==succeeds);
        CHECK(result.after_epoch.revision==result.before.epoch.revision&&result.after_epoch.attempt==1);
        if(succeeds){
            f.context.actual_self=result.after;f.context.walk_locked=result.walk_locked;unsigned callbacks=0;
            const auto move=A::execute_action(f.obs,f.budget,f.action(A::MicroKind::move),f.context,[&](const auto& receipt){++callbacks;CHECK(!receipt.attempted);});
            CHECK(move.prevalidation&&!move.attempted&&move.after.raw_m&&callbacks==1&&command_mock::commands==1);
        }
    }
    CommandFixture f;f.obs.ability.descriptor.kind=G::AbilityKind::reset;f.obs.ability.descriptor.target_mask=G::target_self;
    auto reset=f.action(A::MicroKind::cast_self);reset.point=f.obs.physical.pose;
    const auto unused=f.run(reset);CHECK(unused.prevalidation&&!unused.attempted&&command_mock::commands==0); // A must actually be spent.
}
static void champion_zero_id_and_heal_masks_are_checked_against_actual_teams(){
    ++groups;
    {
        CommandFixture f;f.obs.physical.id=1;f.context.actual_self=f.obs.physical;f.obs.other.id=0;f.context.champion=f.obs.other;
        const auto result=f.run(f.action(A::MicroKind::basic_champion,0));
        CHECK(result.outcome==A::MicroOutcome::applied&&result.champion_after&&result.champion_after->id==0&&result.champion_after->hp==60);
        CHECK(command_mock::kind==std::uint32_t(cambc::detail::entity_kind::champion)&&command_mock::id==0);
    }
    for(unsigned scenario=0;scenario<3;++scenario){
        CommandFixture f;f.obs.ability.descriptor.kind=G::AbilityKind::heal;
        f.obs.ability.descriptor.target_mask=scenario?G::target_allies:G::target_self;
        const auto target=scenario?2u:0u;f.obs.other.id=target;f.obs.other.team=scenario==2?1:0;
        f.obs.other.hp=50;f.context.champion=f.obs.other;
        const auto result=f.run(f.action(A::MicroKind::cast_champion,target));
        if(scenario==2)CHECK(result.prevalidation&&!result.attempted&&command_mock::commands==0&&f.obs.physical.raw_c);
        else CHECK(result.outcome==A::MicroOutcome::applied&&result.champion_after&&result.champion_after->hp==80&&command_mock::id==target);
    }
}
static void missing_ground_trajectory_cannot_authorize_a_dash_or_disable_basic(){
    ++groups;CommandFixture f;f.obs.ability.descriptor.kind=G::AbilityKind::dash;f.obs.ability.descriptor.target_mask=G::target_ground;
    auto dash=f.action(A::MicroKind::cast_ground);const auto denied=f.run(dash);
    CHECK(denied.prevalidation&&!denied.attempted&&command_mock::commands==0&&f.obs.physical.raw_c&&f.obs.physical.raw_a);
    f.records=0;const auto basic=f.run(f.action());CHECK(basic.outcome==A::MicroOutcome::applied&&command_mock::commands==1);
    CommandFixture ground;ground.obs.ability.descriptor.kind=G::AbilityKind::aoe_damage;ground.obs.ability.descriptor.target_mask=G::target_ground|G::target_camps;
    const auto action=ground.action(A::MicroKind::cast_ground);ground.context.ground=A::MicroGround{action.epoch,action.point,true,true,true};
    const auto result=ground.run(action);CHECK(result.outcome==A::MicroOutcome::applied&&command_mock::kind==0&&command_mock::id==0&&command_mock::destination==action.point);
}
static void resource_and_round_denial_never_reach_the_command(){
    ++groups;
    for(unsigned scenario=0;scenario<4;++scenario){
        CommandFixture f;if(scenario==0)f.budget.deny=true;if(scenario==1)f.budget.deny_command=true;
        if(scenario>=2)command_mock::changed_round_on=scenario==2?1:2;
        const auto result=f.run(f.action());CHECK(!result.attempted&&command_mock::commands==0&&f.budget.held_tail==777);
        CHECK(f.records==0&&f.obs.physical.raw_a&&f.obs.physical.raw_m&&f.obs.physical.raw_c);
    }
}
static A::MicroMove observed_move(const A::MicroAction& action){
    A::MicroMove move{};move.epoch=action.epoch;move.point=action.point;move.observed_legal=true;return move;
}
static void exact_paid_move_observation_is_reused_but_remains_conditional(){
    ++groups;
    for(bool applies:std::array<bool,2>{false,true}){
        CommandFixture f;const auto action=f.action(A::MicroKind::move);f.context.move=observed_move(action);
        // An SDK-observed legal unseen destination can still hide a blocker.
        D::visible=0;D::occupancy[1]=applies?0:1;command_mock::applied=applies;
        const auto result=f.run(action);
        CHECK(f.obs.movement_reads==0&&result.attempted&&result.returned&&result.after_actual&&!result.pending);
        CHECK(result.outcome==(applies?A::MicroOutcome::applied:A::MicroOutcome::refused));
        CHECK(command_mock::round_queries==3&&command_mock::commands==1&&f.records==1);
        if(applies)CHECK(result.after.pose==action.point&&!result.after.raw_m&&f.obs.walked);
        else CHECK(result.after.pose==result.before.pose&&result.after.raw_m&&result.after.raw_a&&result.after.raw_c&&!f.obs.walked);
        for(unsigned i=0;i<D::count;++i)CHECK(D::calls[i]!=D::Import::visibility&&D::calls[i]!=D::Import::camp&&
            D::calls[i]!=D::Import::champion&&D::calls[i]!=D::Import::teleport);
    }
}
static void stale_or_different_move_observation_gets_fresh_validation(){
    ++groups;
    for(unsigned scenario=0;scenario<6;++scenario){
        CommandFixture f;const auto action=f.action(A::MicroKind::move);auto move=observed_move(action);
        if(scenario==0)++move.epoch.attempt;
        if(scenario==1)++move.epoch.revision;
        if(scenario==2)move.point={3,1};
        if(scenario==3)move.ability_slot=0;
        if(scenario==4)move.observed_legal=false;
        if(scenario==5)f.context.actual_self.body_actual=false;
        f.context.move=move;D::occupancy[1]=1;
        const auto result=f.run(action);
        CHECK(f.obs.movement_reads==1&&result.prevalidation&&!result.attempted&&result.blocker==A::BlockKind::champion);
        CHECK(command_mock::commands==0&&result.after.raw_m&&result.after.raw_a&&result.after.raw_c);
        CHECK(f.obs.self_reads==unsigned(scenario==5)&&f.budget.held_tail==777);
    }
}
static void a_paid_move_observation_cannot_override_current_resources_or_walk_lock(){
    ++groups;
    for(unsigned scenario=0;scenario<3;++scenario){
        CommandFixture f;const auto action=f.action(A::MicroKind::move);f.context.move=observed_move(action);
        if(scenario==0)f.context.walk_locked=true;
        if(scenario==1)f.context.actual_self.raw_m=f.obs.physical.raw_m=false;
        if(scenario==2)f.context.actual_self.move_restricted=f.obs.physical.move_restricted=true;
        const auto result=f.run(action);
        CHECK(result.prevalidation&&!result.attempted&&f.obs.movement_reads==0&&command_mock::commands==0);
        CHECK(result.after.raw_m==(scenario!=1)&&result.after.move_restricted==(scenario==2));
    }
}
int main(){
    basic_zero_camp_records_before_feedback_and_uses_actual_effects();
    unseen_move_false_preserves_resources_and_publishes_no_future_pose();
    visible_prevalidation_is_not_a_raw_attempt_or_a_resource_spend();
    known_return_survives_feedback_denial_and_command_span_failure();
    unexpected_sdk_failure_is_recorded_unknown_then_propagated();
    actual_reset_and_exclusive_cast_history_do_not_fabricate_another_walk();
    champion_zero_id_and_heal_masks_are_checked_against_actual_teams();
    missing_ground_trajectory_cannot_authorize_a_dash_or_disable_basic();
    resource_and_round_denial_never_reach_the_command();
    exact_paid_move_observation_is_reused_but_remains_conditional();
    stale_or_different_move_observation_gets_fresh_validation();
    a_paid_move_observation_cannot_override_current_resources_or_walk_lock();
    std::printf("PASS execution: %u checks, %u groups\n",checks,groups);
}
