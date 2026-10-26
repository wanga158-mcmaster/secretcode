#include <array>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include "../src/executable_policy.h"

namespace P=mosaic::executable_policy;
namespace executable_policy=mosaic::executable_policy;
struct PureExecutor {
#define MOSAIC_EXECUTABLE_OBSERVATION_PURE_ONLY
#include "../src/executable_observation.h"
#undef MOSAIC_EXECUTABLE_OBSERVATION_PURE_ONLY
#define MOSAIC_EXECUTABLE_EXECUTION_CORE_ONLY
#include "../src/executable_execution.h"
#undef MOSAIC_EXECUTABLE_EXECUTION_CORE_ONLY
#define MOSAIC_EXECUTABLE_CONTROLLER_PURE_ONLY
#include "../src/executable_controller.h"
#undef MOSAIC_EXECUTABLE_CONTROLLER_PURE_ONLY
};
using E=PureExecutor;

static P::Snapshot ready_snapshot(bool ranged=false){
    P::Snapshot result{};result.stamp={7,11,3,5,0};result.authority_lease=1;result.self=7;result.pose={10,10};
    result.body_actual=result.restrictions_actual=result.channel_actual=true;
    result.authority.dead=false;result.authority.raw_m=result.authority.raw_a=result.authority.raw_c=true;
    result.attack_range=ranged?3:1;result.target_count=1;
    result.targets[0]={P::TargetKind::camp,42,{11,10},true,true,true,false};
    result.health_actual=result.shield_actual=true;result.health=100;result.shield=0;
    result.targets[0].health_actual=result.targets[0].shield_actual=true;result.targets[0].health=30;
    result.move_count=3;result.moves[0]={{11,10},true,true,false};
    result.moves[1]={{10,11},true,true,true};result.moves[2]={{9,10},true,true,true};
    result.gems_actual=true;result.gems=30;result.bank_target_actual=true;result.bank_unlocked=true;
    result.altar={10,9};result.bank_range=1;
    return result;
}
static P::Action basic(){P::Action action{};action.kind=P::ActionKind::basic;action.target_kind=P::TargetKind::camp;action.target_id=42;return action;}
static P::Action move(P::Point point){P::Action action{};action.kind=P::ActionKind::move;action.target_kind=P::TargetKind::ground;action.point=point;return action;}
static P::Token token(const P::Snapshot& snapshot,P::Revision lease=0){return {snapshot.stamp,lease?lease:snapshot.authority_lease,true};}
static P::Policy policy(const P::Snapshot& snapshot,std::initializer_list<P::Action> actions){
    P::Policy result{};result.token=token(snapshot);result.owner={2,4,6};result.purpose=P::Purpose::work;
    for(const auto& action:actions)result.success.actions[result.success.count++]=action;
    // These are declared guarded closures, not optional unpriced alternatives.
    // After a successful Basic, usable_attack is false; that branch then closes
    // as unsupported while preserving the actual prefix.
    result.on_refusal.declared=result.on_changed.declared=true;
    result.on_refusal.guard=result.on_changed.guard=P::Guard::usable_attack;
    result.on_refusal.word.count=result.on_changed.word.count=1;
    result.on_refusal.word.actions[0]=result.on_changed.word.actions[0]=basic();return result;
}

// The fake models actual SDK feedback, not prospective effects. A living camp
// occupies the first destination until the host actually applies Basic. The
// executor must ask for the next actual packet before authorizing its Move.
struct FakeHost {
    P::Snapshot packet=ready_snapshot(),before_attempt{};
    P::Token authority=::token(packet);
    P::Lease readonly_lease;
    std::uint64_t remaining=10000,reserved{},query_cost=2,command_cost=5,commit_cost=1;
    std::array<P::Action,16> commands{};
    std::array<P::Receipt,16> receipts{};
    unsigned command_count{},receipt_count{},validations{},feedbacks{},consumptions{},waits{},reconciliations{};
    unsigned fail_move_count{},change_on_validation{},missing_feedback_at{},partial_feedback_at{},fail_commit_at{},closures{},fail_close_at{};
    bool kill_on_basic=true,restrict_move_after_basic{},feedback_grants_move{},invalid_consume{};
    bool partial_life_known{};
    bool command_seen_with_old_authority{};
    P::Revision serial{},lease=1;
    std::uint64_t fuel()const{return remaining;}
    P::Snapshot& snapshot(){return packet;}
    const P::Snapshot& snapshot()const{return packet;}
    P::Token token()const{return authority;}
    void reserve(std::uint64_t value){reserved=value;}
    void spend(std::uint64_t amount){assert(remaining>=amount);remaining-=amount;}
    E::ExecutableActionCost cost(const P::Action&)const{
        E::ExecutableActionCost result{};
        result.validation.sdk_queries=1;result.validation.compute_bound=2;
        result.feedback.sdk_queries=1;result.feedback.compute_bound=4;
        result.refusal_close.sdk_queries=1;result.refusal_close.compute_bound=4;result.commit.compute_bound=1;return result;
    }
    bool validate(const P::Action& action,P::Snapshot& out){
        spend(query_cost+2);++validations;++packet.stamp.observation;
        if(change_on_validation==validations){packet.targets[0].visible=false;packet.targets[0].actual=true;}
        out=packet;return P::authorize(out,action);
    }
    bool consume(P::Token& supplied){
        ++consumptions;before_attempt=packet;
        if(invalid_consume)return false;
        auto next=packet.stamp;++next.host_attempt;
        const bool consumed=P::consume(supplied,next);
        if(consumed){packet.stamp=next;authority.valid=false;readonly_lease.revoke();}return consumed;
    }
    E::ExecutableAttempt attempt(const P::Action& action){
        command_seen_with_old_authority=command_seen_with_old_authority||authority.valid;
        spend(command_cost);assert(command_count<commands.size());commands[command_count++]=action;
        E::ExecutableAttempt result{};result.attempted=true;
        if(action.kind==P::ActionKind::move&&fail_move_count){--fail_move_count;result.refused=true;return result;}
        result.applied=true;++packet.stamp.effect;
        if(action.kind==P::ActionKind::basic){
            packet.authority.raw_a=false;if(packet.attack_range>1)packet.authority.raw_m=false;
            if(kill_on_basic){packet.targets[0].alive=false;packet.targets[0].health=0;packet.moves[0].open=true;
                packet.capture_actual=true;packet.group_clear=packet.reward_available=true;packet.group=4;packet.uncontested=true;}
            if(restrict_move_after_basic)packet.authority.restrictions|=1;
        }else if(action.kind==P::ActionKind::move){
            packet.pose=action.point;packet.authority.raw_m=false;packet.authority.walked=true;
            packet.on_region=packet.pose==packet.targets[0].point;
            if(packet.attack_range>1)packet.authority.raw_a=false;
        }else if(action.kind==P::ActionKind::recall){packet.authority.channel=P::Channel::recall;packet.authority.recall_started=true;}
        return result;
    }
    bool feedback(const P::Action& action,const E::ExecutableAttempt& attempted,P::Snapshot& out,P::Receipt& receipt){
        spend(query_cost+4);++feedbacks;
        if(missing_feedback_at==feedbacks){
            packet.body_actual=packet.restrictions_actual=packet.channel_actual=false;
            packet.health_actual=packet.shield_actual=packet.gems_actual=packet.capture_actual=false;
            for(unsigned n=0;n<packet.target_count;++n){packet.targets[n].actual=false;packet.targets[n].health_actual=false;packet.targets[n].shield_actual=false;}
            out=packet;return false;
        }
        if(attempted.refused){for(unsigned n=0;n<packet.move_count;++n)if(packet.moves[n].point==action.point)packet.moves[n].open=false;}
        if(feedback_grants_move)packet.authority.raw_m=true;
        ++packet.stamp.observation;packet.authority_lease=++lease;authority={packet.stamp,lease,true};out=packet;
        receipt={};receipt.owner={2,4,6};receipt.action=action;receipt.outcome=attempted.refused?P::Outcome::refused:P::Outcome::accepted;
        receipt.before=before_attempt.stamp;receipt.after=packet.stamp;receipt.before_pose=before_attempt.pose;receipt.after_pose=packet.pose;
        receipt.before_authority=before_attempt.authority;receipt.after_authority=packet.authority;receipt.serial=++serial;
        receipt.health_actual=receipt.shield_actual=receipt.gems_actual=true;
        receipt.before_health=before_attempt.health;receipt.after_health=packet.health;
        receipt.before_shield=before_attempt.shield;receipt.after_shield=packet.shield;
        receipt.before_gems=before_attempt.gems;receipt.after_gems=packet.gems;
        if(action.target_kind==P::TargetKind::camp&&action.target_id==42){receipt.target_life_actual=true;
            receipt.target_id=42;receipt.target_kind=P::TargetKind::camp;receipt.target_alive=packet.targets[0].alive;}
        receipt.actual_attempt=attempted.attempted;receipt.actual_body=receipt.actual_targets=receipt.actual_occupancy=true;
        if(partial_feedback_at==feedbacks){
            packet.shield_actual=packet.gems_actual=packet.capture_actual=false;
            packet.targets[0].actual=partial_life_known;packet.targets[0].health_actual=packet.targets[0].shield_actual=false;
            receipt.actual_targets=receipt.actual_occupancy=false;receipt.target_life_actual=partial_life_known;
            out=packet;return false;
        }
        return true;
    }
    bool close_refusal(const P::Action&,const E::ExecutableAttempt&,P::Snapshot& out,P::Receipt& receipt){
        spend(query_cost+4);++closures;if(fail_close_at==closures)return false;
        ++packet.stamp.observation;out=packet;receipt.after=packet.stamp;
        receipt.after_pose=packet.pose;receipt.after_authority=packet.authority;return true;
    }
    bool commit(const P::Receipt& receipt){
        spend(commit_cost);assert(receipt_count<receipts.size());receipts[receipt_count++]=receipt;
        return fail_commit_at!=receipt_count;
    }
    bool finite_wait(const P::Action&,P::Snapshot& out){++waits;out=packet;return false;}
    void reconcile_unknown(){++reconciliations;}
    void service_events(const P::Receipt& receipt,P::ServiceOwner& service){
        if(packet.capture_actual&&packet.group_clear&&receipt.target_life_actual&&!receipt.target_alive&&service.stage()==P::Stage::work){
            P::ServiceReceipt event{};event.owner=receipt.owner;event.event=P::ServiceEvent::clear;event.stamp=packet.stamp;
            event.serial=receipt.serial;event.actual=true;event.group=packet.group;event.pose=packet.pose;assert(service.service(event));}
    }
};

static E::ExecutableExecutionBudget budget(){
    E::ExecutableExecutionBudget result{};result.declared=true;result.price={2,5,10};result.admission_bound=3;result.branch_limit=6;return result;
}
static void alternate(P::Policy& value,bool changed=false,P::Point point={9,10}){
    auto& branch=changed?value.on_changed:value.on_refusal;branch.declared=true;branch.guard=P::Guard::usable_move;
    branch.word.count=1;branch.word.actions[0]=move(point);branch.exclude_point=!changed;
    branch.excluded={10,11};
}
static void actual_kill_then_move(){
    FakeHost host;host.packet.gems=0;host.authority=::token(host.packet);
    auto value=policy(host.packet,{basic(),move({11,10})});E::ExecutableExecutionState state{};
    assert(!P::authorize(host.packet,value.success.actions[1]));
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.command_count==2&&host.commands[0].kind==P::ActionKind::basic&&host.commands[1].kind==P::ActionKind::move);
    assert(host.feedbacks==2&&host.receipt_count==2&&host.consumptions==2&&!host.command_seen_with_old_authority);
    assert(host.packet.pose==P::Point(11,10)&&!host.packet.targets[0].alive);
    assert(host.receipts[0].after_pose==P::Point(10,10)&&host.receipts[1].after_pose==P::Point(11,10));
    assert(host.receipts[0].after.effect==host.receipts[0].before.effect+1);
    assert(host.receipts[0].target_life_actual&&!host.receipts[0].target_alive&&host.receipts[0].health_actual);
    assert(outcome.successful==2&&outcome.attempts==2&&outcome.refusals==0&&outcome.actual_effect&&outcome.closed);
    assert(outcome.service_key==value.owner&&outcome.needs_continuation&&outcome.next_required==P::NextRequired::capture);
}
static void spent_and_restricted_move_are_distinct(){
    FakeHost ranged;ranged.packet=ready_snapshot(true);ranged.authority=::token(ranged.packet);
    auto value=policy(ranged.packet,{basic(),move({11,10})});E::ExecutableExecutionState state{};
    const auto spent=E::executable_execute(ranged,value,budget(),state);
    assert(ranged.command_count==1&&ranged.receipt_count==1&&!ranged.packet.authority.raw_m&&!(ranged.packet.authority.restrictions&1));
    assert(!spent.raw_m_after&&!spent.restricted_move_after&&spent.actual_effect&&spent.needs_continuation);
    FakeHost restricted;restricted.restrict_move_after_basic=true;value=policy(restricted.packet,{basic(),move({11,10})});state={};
    const auto denied=E::executable_execute(restricted,value,budget(),state);
    assert(restricted.command_count==1&&restricted.packet.authority.raw_m&&(restricted.packet.authority.restrictions&1));
    assert(denied.raw_m_after&&denied.restricted_move_after&&denied.actual_effect);
    assert(restricted.receipts[0].after_authority.raw_m&&ranged.receipts[0].after_authority.raw_m==false);
}
static void false_keeps_actual_move_and_uses_declared_branch(){
    FakeHost host;host.fail_move_count=1;auto value=policy(host.packet,{move({10,11})});alternate(value);E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.command_count==2&&host.closures==1&&host.receipt_count==2&&host.consumptions==2);
    assert(host.commands[0].point==P::Point(10,11)&&host.commands[1].point==P::Point(9,10));
    assert(host.receipts[0].outcome==P::Outcome::refused&&host.receipts[0].after_pose==P::Point(10,10));
    assert(host.receipts[0].after_authority.raw_m&&host.receipts[0].before.effect==host.receipts[0].after.effect);
    assert(host.receipts[1].outcome==P::Outcome::accepted&&host.packet.pose==P::Point(9,10));
    assert(outcome.refusals==1&&outcome.successful==1&&outcome.actual_effect&&outcome.closed&&!host.command_seen_with_old_authority);
    assert(outcome.first_failure.failure==P::Failure::refused&&outcome.first_failure_phase==E::ExecutablePhase::command);
}
static void prefix_survives_later_refusal(){
    FakeHost host;host.fail_move_count=1;auto value=policy(host.packet,{basic(),move({10,11})});E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.command_count==2&&host.receipt_count==2&&host.receipts[0].outcome==P::Outcome::accepted&&host.receipts[1].outcome==P::Outcome::refused);
    assert(!host.packet.targets[0].alive&&!host.packet.authority.raw_a&&host.packet.authority.raw_m&&host.packet.pose==P::Point(10,10));
    assert(outcome.successful==1&&outcome.refusals==1&&outcome.actual_effect&&outcome.actual_prefix>=host.receipts[0].serial);
    assert(outcome.last_receipt.before.effect==outcome.last_receipt.after.effect);
}
static void changed_target_revalidates_before_dispatch(){
    FakeHost host;host.change_on_validation=1;auto value=policy(host.packet,{basic()});alternate(value,true,{10,11});E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.validations==2&&host.command_count==1&&host.commands[0].kind==P::ActionKind::move);
    assert(host.packet.targets[0].alive&&!host.packet.targets[0].visible&&host.packet.pose==P::Point(10,11));
    assert(outcome.first_failure.failure==P::Failure::changed_premise&&outcome.first_failure_phase==E::ExecutablePhase::validation);
    assert(outcome.actual_effect&&outcome.refusals==0&&outcome.attempts==1);
}
static void feedback_failure_never_borrows_a_prediction(){
    FakeHost host;host.missing_feedback_at=1;auto value=policy(host.packet,{basic(),move({11,10})});E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.command_count==1&&host.consumptions==1&&host.feedbacks==1&&host.receipt_count==1);
    assert(host.packet.pose==P::Point(10,10)&&!host.packet.targets[0].alive);
    assert(outcome.attempted&&outcome.actual_effect&&outcome.first_failure.failure==P::Failure::feedback_missing);
    assert(outcome.first_failure_phase==E::ExecutablePhase::feedback&&outcome.needs_continuation);
    assert(state.pending_feedback&&outcome.actual_prefix==1&&state.service.completed_words()==0);
    assert(!outcome.last_receipt.actual_body&&!outcome.last_receipt.health_actual&&!outcome.last_receipt.target_life_actual);
}
static void first_failure_remains_latched(){
    FakeHost host;host.change_on_validation=1;host.fail_move_count=1;host.fail_close_at=1;
    auto value=policy(host.packet,{basic()});alternate(value,true,{10,11});E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.command_count==1&&host.closures==1&&host.receipt_count==1&&state.pending_feedback);
    assert(outcome.first_failure.failure==P::Failure::changed_premise&&outcome.first_failure_phase==E::ExecutablePhase::validation);
}
static void lease_queries_and_attempts(){
    FakeHost host;auto old=host.token();P::Snapshot after{};
    assert(host.validate(basic(),after));assert(P::token_matches(old,after)&&old.valid&&after.stamp.observation>old.stamp.observation);
    auto consumed=old;assert(host.consume(consumed)&&!consumed.valid&&!P::token_matches(old,host.snapshot()));
    assert(host.snapshot().stamp.host_attempt==old.stamp.host_attempt+1);
    // A failed physical command consumes authority just as a success does.
    host.fail_move_count=1;const auto attempted=host.attempt(move({10,11}));
    assert(attempted.attempted&&attempted.refused&&!attempted.applied&&!host.authority.valid);
}
static void two_refusals_close_the_bound(){
    FakeHost host;host.fail_move_count=2;auto value=policy(host.packet,{move({10,11})});alternate(value);E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.command_count==2&&host.closures==2&&host.receipt_count==2&&outcome.refusals==2);
    assert(!outcome.actual_effect&&host.packet.authority.raw_m&&outcome.needs_continuation);
    auto retry=policy(host.packet,{move({9,10})});retry.token=host.token();retry.actual_prefix=outcome.actual_prefix;
    const auto again=E::executable_execute(host,retry,budget(),state);
    assert(host.command_count==2&&again.needs_continuation);
}
static void unsupported_wait_cannot_own_an_idle_input(){
    FakeHost host;P::Action wait{};wait.kind=P::ActionKind::wait;wait.wait_reason=P::WaitReason::capture;
    host.packet.capture_actual=host.packet.group_clear=host.packet.reward_available=host.packet.on_region=host.packet.uncontested=true;
    host.packet.group=42;host.packet.finite_capture_inputs=1;host.authority=::token(host.packet);
    auto value=policy(host.packet,{wait});value.purpose=P::Purpose::capture;E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.command_count==0&&host.consumptions==0&&!outcome.finite_wait&&outcome.needs_continuation);
    assert(outcome.status!=E::ExecutableStatus::word_applied);
}
static void whole_branch_funding(){
    FakeHost host;auto value=policy(host.packet,{basic(),move({10,11})});alternate(value);alternate(value,true);
    const auto declared=budget();const auto funding=E::executable_execution_funding(host,value,declared);
    const auto quoted=P::quote_funding(funding,declared.price);assert(quoted.valid);
    // Independent event-cost oracle: 4 validation +5 command +6 feedback
    // +1 receipt commit;
    // each false adds6 closure; max path is Basic then false Move then false
    // alternate. Admission has already completed before this residual quote;
    // final10 remains paid once, not per action.
    const std::uint64_t one=4+5+6+1;
    const auto oracle=one+(one+6)+(one+6)+10;
    assert(quoted.required==oracle);
    for(std::uint64_t available=0;available<=oracle;++available){
        FakeHost low;low.remaining=available;auto candidate=value;candidate.token=low.token();E::ExecutableExecutionState state{};
        const auto outcome=E::executable_execute(low,candidate,declared,state);
        assert(low.command_count==0&&low.validations==0&&outcome.status==E::ExecutableStatus::unfunded);
    }
    FakeHost enough;enough.remaining=oracle+1;auto candidate=value;candidate.token=enough.token();E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(enough,candidate,declared,state);assert(enough.command_count==2&&outcome.actual_effect);
    auto overflow=declared;overflow.price.query_bound=P::invalid_cost;
    assert(!P::quote_funding(E::executable_execution_funding(host,value,overflow),overflow.price).valid);
}
static void input_and_word_caps_are_hard_stops(){
    for(unsigned cap=0;cap<2;++cap){FakeHost host;auto value=policy(host.packet,{basic()});E::ExecutableExecutionState state{};
        state.input_epoch=host.packet.stamp.epoch;
        if(cap==0)state.input_attempts=P::max_attempts;else state.input_successful=P::max_successful;
        const auto outcome=E::executable_execute(host,value,budget(),state);
        assert(host.command_count==0&&host.consumptions==0&&outcome.status==E::ExecutableStatus::unsupported&&outcome.needs_continuation);}
    FakeHost host;auto value=policy(host.packet,{basic()});value.success.count=P::max_attempts+1;E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.command_count==0&&host.validations==0&&outcome.status==E::ExecutableStatus::invalid);
}
static void partial_feedback_records_only_observed_flags(){
    for(bool life_known:{false,true}){
        FakeHost host;host.partial_feedback_at=1;host.partial_life_known=life_known;
        auto value=policy(host.packet,{basic(),move({11,10})});E::ExecutableExecutionState state{};
        const auto outcome=E::executable_execute(host,value,budget(),state);
        assert(host.command_count==1&&host.feedbacks==1&&host.receipt_count==1&&host.packet.pose==P::Point(10,10));
        assert(outcome.actual_effect&&outcome.actual_prefix==1&&state.pending_feedback&&!outcome.feedback_closed);
        assert(outcome.last_receipt.actual_body&&outcome.last_receipt.health_actual&&outcome.last_receipt.after_health==100);
        assert(!outcome.last_receipt.shield_actual&&!outcome.last_receipt.gems_actual);
        assert(outcome.last_receipt.target_life_actual==life_known);
        assert(!outcome.last_receipt.actual_targets&&!outcome.last_receipt.actual_occupancy);
        assert(state.service.completed_words()==0&&outcome.first_failure.failure==P::Failure::feedback_missing);
        assert(outcome.first_failure_phase==E::ExecutablePhase::feedback&&outcome.needs_continuation);
    }
}
static void applied_command_overrun_closes_truthful_feedback(){
    FakeHost host;host.command_cost=6;auto value=policy(host.packet,{basic(),move({11,10})});E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.command_count==1&&host.feedbacks==1&&host.receipt_count==1&&host.packet.pose==P::Point(10,10));
    assert(outcome.status==E::ExecutableStatus::overrun&&outcome.attempted&&outcome.actual_effect&&outcome.successful==1);
    assert(outcome.first_failure.failure==P::Failure::bound_overrun&&outcome.first_failure_phase==E::ExecutablePhase::command);
    assert(outcome.feedback_closed&&!state.pending_feedback&&outcome.actual_prefix==1);
    assert(outcome.last_receipt.actual_body&&outcome.last_receipt.health_actual&&outcome.last_receipt.target_life_actual&&!outcome.last_receipt.target_alive);
    assert(state.service.completed_words()==0&&outcome.needs_continuation);
}
static void refused_fallback_is_not_a_completed_word(){
    FakeHost host;host.fail_move_count=2;auto value=policy(host.packet,{basic(),move({10,11})});alternate(value);E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,budget(),state);
    assert(host.command_count==3&&host.feedbacks==3&&host.closures==2&&host.receipt_count==3);
    assert(outcome.successful==1&&outcome.refusals==2&&outcome.actual_effect&&outcome.actual_prefix==1);
    assert(state.service.refusal_debt()==2&&state.service.completed_words()==0&&!state.pending_feedback);
    assert(state.service.stage()==P::Stage::recover&&outcome.next_required==P::NextRequired::recover);
    assert(!outcome.closed&&outcome.needs_continuation&&host.packet.authority.raw_m);
    assert(host.receipts[0].outcome==P::Outcome::accepted&&host.receipts[1].outcome==P::Outcome::refused&&host.receipts[2].outcome==P::Outcome::refused);
}
struct OracleStep {std::uint64_t id{},cost{};};
struct OraclePath {std::array<OracleStep,24> steps{};unsigned count{};};
static OraclePath oracle_path(std::initializer_list<OracleStep> steps){OraclePath result{};for(const auto& step:steps)result.steps[result.count++]=step;return result;}
static std::uint64_t residual(const OraclePath& path,unsigned from){std::uint64_t value=10;for(unsigned n=from;n<path.count;++n)value+=path.steps[n].cost;return value;}
static std::array<OraclePath,7> physical_paths(){
    return {{
        oracle_path({{1,4},{2,5},{3,6},{5,1},{7,4},{8,5},{9,6},{11,1}}),
        oracle_path({{1,4},{2,5},{3,6},{5,1},{7,4},{8,5},{9,6},{10,6},{12,1},{164,4},{165,5},{166,6},{168,1}}),
        oracle_path({{1,4},{2,5},{3,6},{5,1},{7,4},{8,5},{9,6},{10,6},{12,1},{164,4},{165,5},{166,6},{167,6},{169,1}}),
        oracle_path({{1,4},{1000,4},{1001,5},{1002,6},{1004,1}}),
        oracle_path({{1,4},{1000,4},{1001,5},{1002,6},{1003,6},{1005,1}}),
        oracle_path({{1,4},{2,5},{3,6},{5,1},{7,4},{1064,4},{1065,5},{1066,6},{1068,1}}),
        oracle_path({{1,4},{2,5},{3,6},{5,1},{7,4},{1064,4},{1065,5},{1066,6},{1067,6},{1069,1}})
    }};
}
static void generated_cursor_matches_every_physical_branch(){
    FakeHost host;auto value=policy(host.packet,{basic(),move({10,11})});alternate(value);alternate(value,true);
    const auto declared=budget();const auto plan=E::executable_execution_funding(host,value,declared);const auto strict=P::quote_funding(plan,declared.price);
    assert(strict.valid&&plan.refusal_count==2&&plan.changed_count==4);
    // Explicit physical branches, independently listed: Basic cannot return
    // false. Only Move has the two refusal continuations; both primitives can
    // have their premise change before a command.
    const auto paths=physical_paths();
    std::uint64_t worst=0;for(const auto& path:paths)worst=std::max(worst,residual(path,0));assert(worst==strict.required);
    for(unsigned selected=0;selected<paths.size();++selected){
        E::ExecutableFundingCursor cursor;assert(cursor.start(plan,declared.price,1000)&&cursor.remaining()==strict.required);
        std::array<unsigned,7> positions{};std::uint16_t possible=127;std::uint64_t available=1000;
        for(unsigned n=0;n<paths[selected].count;++n){const auto step=paths[selected].steps[n];
            std::uint64_t bound=0,after=0;assert(cursor.begin(unsigned(step.id),available,bound,after));assert(bound==step.cost);
            std::uint64_t oracle_after=0;std::uint16_t next=0;
            for(unsigned path=0;path<paths.size();++path)if(possible&(1u<<path)){
                if(positions[path]<paths[path].count&&paths[path].steps[positions[path]].id==step.id){
                    next|=std::uint16_t(1u<<path);++positions[path];oracle_after=std::max(oracle_after,residual(paths[path],positions[path]));}}
            possible=next;assert(possible&&after==oracle_after);available-=step.cost;
            assert(cursor.finish(unsigned(step.id),step.cost,available)&&cursor.remaining()==oracle_after);
        }
        assert(cursor.complete(available)&&cursor.remaining()==10);
    }
}
static void token_free_shape_pricing_never_mints_authority(){
    FakeHost host;auto value=policy(host.packet,{basic(),move({10,11})});alternate(value);alternate(value,true);
    value.token={};const auto prior=host.token();const auto stamp=host.packet.stamp;
    assert(P::valid_shape(value)&&!P::valid_policy(value));
    const auto plan=E::executable_execution_funding(host,value,budget());const auto price=P::quote_funding(plan,budget().price);
    assert(price.valid&&host.command_count==0&&host.consumptions==0&&host.packet.stamp==stamp&&host.token().lease==prior.lease);
    E::ExecutableExecutionState state{};const auto outcome=E::executable_execute(host,value,budget(),state);
    assert((outcome.status==E::ExecutableStatus::invalid||outcome.status==E::ExecutableStatus::stale)&&host.command_count==0&&host.validations==0);
    assert(!value.token.valid&&host.token().valid&&host.packet.stamp==stamp);
}
struct RealTableHost {
    E::ExecutableObservationRequest request{};
    E::ExecutableExecutionBudget declared=E::executable_execution_default_budget();
    E::ExecutableActionCost cost(const P::Action& action)const{
        const auto query=E::executable_observe_query_bounds(action,request);E::ExecutableActionCost result{};
        result.validation.sdk_queries=query.validation;result.validation.compute_bound=declared.validation_compute;
        result.feedback.sdk_queries=query.feedback;result.feedback.compute_bound=declared.feedback_compute;
        result.refusal_close.sdk_queries=query.refusal_close;result.refusal_close.compute_bound=declared.refusal_compute;
        result.commit.compute_bound=declared.commit_compute;return result;
    }
};
static void real_producer_table_fits_the_declared_entry(){
    FakeHost source;auto value=policy(source.packet,{basic(),move({10,11})});alternate(value);alternate(value,true);value.token={};
    RealTableHost real;real.request.owner=value.owner;real.request.member=0;real.request.group=4;
    real.request.have_move=true;real.request.move={10,11};
    const auto plan=E::executable_execution_funding(real,value,real.declared);const auto quote=P::quote_funding(plan,real.declared.price);
    assert(quote.valid);const auto admission=E::executable_shape_admission(value,real.declared);
    const auto prepare_queries=E::executable_observe_prepare_query_bound(real.request,true);
    const auto total=E::ExecutableObservationCone::cost(prepare_queries,real.declared.price.query_bound,E::executable_observe_compute_bound,quote.required+admission);
    std::printf("component preflight: queries=%u execution=%llu admission=%llu prepare_compute=6000 total=%llu margin=%llu\n",
        prepare_queries,static_cast<unsigned long long>(quote.required),static_cast<unsigned long long>(admission),
        static_cast<unsigned long long>(total),static_cast<unsigned long long>(total<100000?100000-total:0));
    std::fflush(stdout);
    assert(total<100000); // A failure is a resource blocker, never a floor to lower.
    assert(prepare_queries==9&&quote.required==62384&&admission==9792&&total==96176);
    assert(E::ExecutableObservationCone::admitted(100000,prepare_queries,real.declared.price.query_bound,E::executable_observe_compute_bound,quote.required+admission));
}
static void fresh_readonly_owner(FakeHost& host,P::Point pose,double health,bool preserve_authority=false){
    ++host.packet.stamp.observation;
    E::ExecutableOwnerDraft actual{};actual.self=7;actual.pose=pose;actual.health=float(health);actual.shield=0;actual.attack_range=1;
    actual.authority.dead=false;actual.authority.raw_m=actual.authority.raw_a=actual.authority.raw_c=true;
    if(preserve_authority)actual.authority=host.packet.authority;
    actual.body_read=actual.restrictions_read=actual.buffs_read=true;
    assert(E::ExecutableObservationFacts::publish_owner(host.packet,actual,host.packet.stamp));
    host.packet.move_count=1;host.packet.moves[0]={{pose.x,pose.y+1},true,true,true};
    if(host.readonly_lease.valid()&&P::same_physical(host.readonly_lease.token().stamp,host.packet.stamp))
        assert(host.readonly_lease.observe(host.packet));
    else assert(host.readonly_lease.issue(host.packet,++host.lease));
    host.authority=host.readonly_lease.token();
}
static void pending_reconciliation_requires_new_actual_authority(){
    FakeHost host;host.partial_feedback_at=1;auto old_policy=policy(host.packet,{basic(),move({11,10})});E::ExecutableExecutionState state{};
    const auto failed=E::executable_execute(host,old_policy,budget(),state);assert(state.pending_feedback&&failed.actual_prefix==1);
    const auto original=state.pending_receipt;const auto before_commands=host.command_count;
    assert(E::executable_reconcile_pending(host,budget(),state).status==E::ExecutableStatus::feedback_missing&&state.pending_feedback);
    ++host.packet.stamp.epoch;++host.packet.stamp.frame;++host.packet.stamp.observation;
    assert(E::executable_reconcile_pending(host,budget(),state).status==E::ExecutableStatus::feedback_missing&&state.pending_feedback);
    fresh_readonly_owner(host,{20,20},55);host.packet.body_actual=false;
    assert(E::executable_reconcile_pending(host,budget(),state).status==E::ExecutableStatus::feedback_missing&&state.pending_feedback);
    fresh_readonly_owner(host,{20,20},55);
    // The generic entry reconciles and returns. It cannot dispatch the old
    // policy merely because acquisition has now become possible.
    const auto reconciled=E::executable_execute(host,old_policy,budget(),state);
    assert(reconciled.status==E::ExecutableStatus::reconciled&&reconciled.feedback_closed&&!state.pending_feedback);
    assert(host.command_count==before_commands&&host.consumptions==1&&host.reconciliations==1);
    assert(state.service.completed_words()==0&&state.service.actual_prefix()==1&&state.service.trail_size()==0&&!state.service.pose_known());
    assert(state.pending_receipt.after_pose==original.after_pose&&state.pending_receipt.after_health==original.after_health);
    assert(state.pending_receipt.after==original.after&&state.pending_receipt.after_pose!=host.packet.pose);
    const auto stale=E::executable_execute(host,old_policy,budget(),state);assert(stale.status==E::ExecutableStatus::stale&&host.command_count==before_commands);
    auto next=policy(host.packet,{move({20,21})});
    assert(E::executable_execute(host,next,budget(),state).status==E::ExecutableStatus::stale&&host.command_count==before_commands);
    next.actual_prefix=state.service.actual_prefix();const auto applied=E::executable_execute(host,next,budget(),state);
    assert(applied.status==E::ExecutableStatus::word_applied&&host.command_count==before_commands+1);
    assert(state.service.trail_size()==1&&state.service.trail(0).from==P::Point(20,20)&&state.service.trail(0).to==P::Point(20,21));
}
static void late_missing_body_registers_only_the_old_minimal_receipt(){
    FakeHost host;host.command_cost=9990;auto old_policy=policy(host.packet,{move({10,11})});alternate(old_policy);E::ExecutableExecutionState state{};
    const auto failed=E::executable_execute(host,old_policy,budget(),state);
    assert(failed.status==E::ExecutableStatus::overrun&&failed.actual_effect&&state.pending_feedback&&!state.pending_recorded);
    assert(host.command_count==1&&host.feedbacks==0&&host.receipt_count==0&&state.service.actual_prefix()==0);
    const auto old=state.pending_receipt;host.remaining=10000;host.command_cost=5;
    ++host.packet.stamp.epoch;++host.packet.stamp.frame;fresh_readonly_owner(host,{30,30},41);
    const auto reconciled=E::executable_reconcile_pending(host,budget(),state);
    assert(reconciled.status==E::ExecutableStatus::reconciled&&reconciled.actual_prefix==1&&!state.pending_feedback);
    assert(host.command_count==1&&host.feedbacks==0&&host.receipt_count==1&&host.consumptions==1);
    const auto& recorded=host.receipts[0];
    assert(recorded.before==old.before&&recorded.after==old.after&&recorded.before_pose==old.before_pose);
    assert(!recorded.actual_body&&!recorded.actual_targets&&!recorded.actual_occupancy&&!recorded.health_actual&&!recorded.target_life_actual);
    assert(recorded.after_pose!=host.packet.pose&&recorded.after_health!=host.packet.health);
    assert(!reconciled.last_receipt.health_actual&&!reconciled.last_receipt.actual_body);
    assert(state.service.trail_size()==0&&!state.service.pose_known()&&state.service.completed_words()==0);
    assert(reconciled.first_failure.failure==P::Failure::bound_overrun&&reconciled.first_failure_phase==E::ExecutablePhase::command);
}
static void reconciliation_commit_overrun_blocks_current_input(bool commit_succeeds){
    for(bool later_input:{false,true}){
        FakeHost host;host.fail_commit_at=1;auto value=policy(host.packet,{basic(),move({11,10})});E::ExecutableExecutionState state{};
        const auto initial=E::executable_execute(host,value,budget(),state);
        assert(initial.status==E::ExecutableStatus::feedback_missing&&state.pending_feedback&&!state.pending_hooks);
        assert(host.command_count==1&&state.service.actual_prefix()==1&&state.service.completed_words()==0);
        if(later_input){++host.packet.stamp.epoch;++host.packet.stamp.frame;}
        fresh_readonly_owner(host,{10,10},100,!later_input);
        host.commit_cost=2;host.fail_commit_at=commit_succeeds?0:2;
        const auto previous_overruns=state.overruns;const auto previous_spent=state.phase_spent[unsigned(E::ExecutablePhase::commit)];
        const auto overrun=E::executable_reconcile_pending(host,budget(),state);
        assert(overrun.status==E::ExecutableStatus::overrun&&state.overruns==previous_overruns+1&&state.input_blocked);
        assert(state.phase_spent[unsigned(E::ExecutablePhase::commit)]==previous_spent+2);
        assert(state.phase_max[unsigned(E::ExecutablePhase::commit)]==2&&host.command_count==1&&host.consumptions==1);
        assert(state.input_epoch==host.packet.stamp.epoch&&state.service.actual_prefix()==1&&state.service.completed_words()==0);
        assert(state.pending_feedback!=commit_succeeds&&overrun.feedback_closed==commit_succeeds);
        // The original commit failure is still the first failure. The later
        // declared-bound violation is separately counted and blocks this input.
        assert(overrun.first_failure.failure==P::Failure::feedback_missing&&overrun.first_failure_phase==E::ExecutablePhase::commit);
        if(!commit_succeeds){
            host.commit_cost=1;host.fail_commit_at=0;fresh_readonly_owner(host,{10,10},100,true);
            const auto closed=E::executable_reconcile_pending(host,budget(),state);
            assert(closed.status==E::ExecutableStatus::reconciled&&!state.pending_feedback&&state.input_blocked);
        }
        auto next=policy(host.packet,{move({10,11})});next.token=host.token();next.actual_prefix=state.service.actual_prefix();
        const auto blocked=E::executable_execute(host,next,budget(),state);
        assert(blocked.status==E::ExecutableStatus::overrun&&host.command_count==1&&state.input_blocked);
        ++host.packet.stamp.epoch;++host.packet.stamp.frame;host.commit_cost=1;host.fail_commit_at=0;
        fresh_readonly_owner(host,{10,10},100);next=policy(host.packet,{move({10,11})});next.token=host.token();next.actual_prefix=state.service.actual_prefix();
        const auto subsequent=E::executable_execute(host,next,budget(),state);
        assert(subsequent.status==E::ExecutableStatus::word_applied&&host.command_count==2&&!state.input_blocked);
        assert(state.service.actual_prefix()>1&&state.service.completed_words()==1);
    }
}
template<class Host> static void scalar_matches_strict(Host& host,const P::Policy& shape,const E::ExecutableExecutionBudget& declared){
    const auto scalar=E::executable_scalar_quote(host,shape,declared);
    const auto strict=P::quote_funding(E::executable_execution_funding(host,shape,declared),declared.price);
    assert(scalar.valid==strict.valid);
    if(scalar.valid){assert(scalar.success==strict.success&&scalar.refusal==strict.refusal&&scalar.changed==strict.changed);
        assert(scalar.required==strict.required);}
}
static void scalar_quotes_match_strict_branch_oracles(){
    FakeHost fake;RealTableHost real;real.request.member=0;real.request.have_move=true;real.request.move={10,11};
    auto fake_budget=budget();
    std::array<P::Policy,12> shapes{};
    shapes[0]=policy(fake.packet,{basic()});shapes[1]=policy(fake.packet,{move({10,11})});
    shapes[2]=policy(fake.packet,{basic(),move({10,11})});
    shapes[3]=shapes[2];alternate(shapes[3]);alternate(shapes[3],true);
    shapes[4]=policy(fake.packet,{move({10,11}),basic()});alternate(shapes[4]);alternate(shapes[4],true);
    shapes[5]=policy(fake.packet,{basic(),move({10,11}),basic()});alternate(shapes[5]);alternate(shapes[5],true);
    shapes[6]=policy(fake.packet,{basic(),move({10,11}),basic(),move({9,10})});alternate(shapes[6]);alternate(shapes[6],true);
    P::Action wait{};wait.kind=P::ActionKind::wait;wait.wait_reason=P::WaitReason::capture;
    shapes[7]=policy(fake.packet,{wait});shapes[7].purpose=P::Purpose::capture;
    shapes[8]=shapes[3];shapes[8].token={}; // Pricing is authority-free.
    shapes[9]=shapes[3];shapes[9].on_refusal.declared=false; // Unsupported shape.
    shapes[10]=shapes[3];shapes[10].success.count=P::max_attempts+1;
    auto enemy=basic();enemy.target_kind=P::TargetKind::champion;enemy.target_id=99;
    shapes[11]=policy(fake.packet,{enemy,move({10,11})});alternate(shapes[11]);alternate(shapes[11],true);
    for(const auto& shape:shapes){scalar_matches_strict(fake,shape,fake_budget);scalar_matches_strict(real,shape,real.declared);}
    auto limited=fake_budget;limited.max_primary=1;scalar_matches_strict(fake,shapes[3],limited);
    auto overflow=fake_budget;overflow.price.query_bound=P::invalid_cost;scalar_matches_strict(fake,shapes[3],overflow);
    auto zero=fake_budget;zero.price.query_bound=0;scalar_matches_strict(fake,shapes[3],zero);
    auto undeclared=fake_budget;undeclared.declared=false;scalar_matches_strict(fake,shapes[3],undeclared);
    assert(fake.command_count==0&&fake.consumptions==0&&fake.validations==0&&fake.token().valid);
}
static void actual_caller_lifecycle_fits_without_double_admission(){
    FakeHost source;auto shape=policy(source.packet,{basic(),move({10,11})});alternate(shape);alternate(shape,true);shape.token={};
    RealTableHost real;real.request.owner=shape.owner;real.request.member=0;real.request.group=4;
    real.request.have_move=true;real.request.move={10,11};
    const auto scalar=E::executable_scalar_quote(real,shape,real.declared);assert(scalar.valid);
    const auto admission=E::executable_shape_admission(shape,real.declared);
    const auto future_execute=E::executable_family_max_tail(real,real.declared);
    assert(future_execute==72176&&future_execute==scalar.required+admission);
    assert(scalar.required==62384&&admission==9792); // Admission is reserved only for the future execute.
    E::ExecutableObservationRequest owner_request{};owner_request.owner=shape.owner;owner_request.need_home=true;
    const auto owner_queries=E::executable_observe_prepare_query_bound(owner_request,true);
    const auto later_queries=E::executable_observe_prepare_query_bound(real.request,false);
    assert(owner_queries==4&&later_queries==6&&owner_queries+later_queries==E::executable_observe_prepare_query_bound(real.request,true)+1);
    const auto family_quote=E::executable_family_quote_bound(),shape_quote=E::executable_policy_quote_bound(shape);
    const auto imports=(owner_queries+later_queries)*real.declared.price.query_bound;
    // The shape quote occurs between the preparation stages, so its own local
    // bound is part of the ONE shared clock, not another whole-call charge.
    const auto shared=E::executable_observe_compute_bound;
    assert(real.declared.price.query_bound==E::executable_observe_query_bound);
    const auto caller=family_quote+shared+imports+future_execute;
    std::printf("{\"schema\":\"mosaic-r17-executable-preflight/v1\",\"available\":100000,\"query_bound\":%llu,\"rows\":["
        "{\"phase\":\"family_quote\",\"fuel\":%llu,\"before_shared_clock\":true},"
        "{\"phase\":\"owner_prefix\",\"queries\":%u,\"need_home\":true,\"fuel\":%llu},"
        "{\"phase\":\"later_work_scope\",\"queries\":%u,\"owner_needed\":false,\"need_home\":false,\"fuel\":%llu},"
        "{\"phase\":\"shared_prepare_compute\",\"fuel\":%llu,\"includes_shape_quote\":true},"
        "{\"phase\":\"shape_quote_local_bound\",\"fuel\":%llu,\"charged_separately\":false},"
        "{\"phase\":\"future_execution\",\"fuel\":%llu,\"includes_once_admission\":%llu}],"
        "\"import_queries\":%u,\"funding_compile_count\":1,\"funding_arrays_in_quotes\":false,\"native_fuel_qualified\":false,\"required\":%llu,\"margin\":%llu}\n",
        static_cast<unsigned long long>(real.declared.price.query_bound),static_cast<unsigned long long>(family_quote),
        owner_queries,static_cast<unsigned long long>(owner_queries*real.declared.price.query_bound),
        later_queries,static_cast<unsigned long long>(later_queries*real.declared.price.query_bound),
        static_cast<unsigned long long>(shared),static_cast<unsigned long long>(shape_quote),
        static_cast<unsigned long long>(future_execute),static_cast<unsigned long long>(admission),owner_queries+later_queries,
        static_cast<unsigned long long>(caller),static_cast<unsigned long long>(caller<100000?100000-caller:0));
    std::fflush(stdout);
    assert(family_quote==1024&&shape_quote==1536&&shared==6000&&caller==99200&&caller<100000);
}
static void preparation_compute_is_one_shared_ledger(){
    E::ExecutableObservationPreparation ledger;P::Stamp physical{7,11,3,5,0};ledger.begin(physical,100000,0,E::executable_observe_compute_bound);
    auto after_owner=physical;after_owner.observation+=3;
    assert(ledger.spent(after_owner,93000,6000)==1000&&ledger.remaining(after_owner,93000,6000)==5000);
    auto shape=policy(ready_snapshot(),{basic(),move({10,11})});alternate(shape);alternate(shape,true);
    const auto shape_quote=E::executable_policy_quote_bound(shape);assert(shape_quote==1536);
    assert(ledger.spent(after_owner,93000-shape_quote,6000)==1000+shape_quote);
    assert(ledger.remaining(after_owner,93000-shape_quote,6000)==5000-shape_quote);
    auto after_target_and_move=after_owner;after_target_and_move.observation+=6;
    assert(ledger.spent(after_target_and_move,77500,18000)==4500&&ledger.remaining(after_target_and_move,77500,18000)==1500);
    // A second preparation stage does not mint another6000 of compute. Import
    // fuel is removed once; the same physical lease retains the paid prefix.
    assert(ledger.remaining(after_target_and_move,75999,18000)==P::invalid_cost);
    auto attempted=after_target_and_move;++attempted.host_attempt;
    assert(!ledger.matches(attempted)&&ledger.remaining(attempted,77500,18000)==P::invalid_cost);
    ledger.close();assert(!ledger.matches(after_target_and_move));
}

// R18 compiles only declared resource structure. A cost lookup may consume
// actual fake fuel, but it performs no physical read or SDK command.
struct PlanHost:FakeHost {
    E::ExecutableObservationRequest request{};
    E::ExecutableExecutionBudget declared=E::executable_execution_default_budget();
    mutable unsigned cost_calls{},fuel_polls{};
    unsigned charge_poll{};std::uint64_t lookup_cost{7},poll_cost{};bool real_table{};
    std::uint64_t fuel()const{
        ++fuel_polls;if(charge_poll==fuel_polls)const_cast<PlanHost*>(this)->spend(poll_cost);return remaining;
    }
    E::ExecutableActionCost cost(const P::Action& action)const{
        ++cost_calls;const_cast<PlanHost*>(this)->spend(lookup_cost);
        if(!real_table)return FakeHost::cost(action);
        const auto query=E::executable_observe_query_bounds(action,request);E::ExecutableActionCost result{};
        result.validation.sdk_queries=query.validation;result.validation.compute_bound=declared.validation_compute;
        result.feedback.sdk_queries=query.feedback;result.feedback.compute_bound=declared.feedback_compute;
        result.refusal_close.sdk_queries=query.refusal_close;result.refusal_close.compute_bound=declared.refusal_compute;
        result.commit.compute_bound=declared.commit_compute;return result;
    }
};
static E::ExecutableExecutionBudget plan_budget(){auto value=budget();value.admission_bound=0;return value;}
static P::Policy canonical_plan_policy(const P::Snapshot& snapshot){
    auto value=policy(snapshot,{basic(),move({10,11})});alternate(value);alternate(value,true);return value;
}
static E::ExecutableObservationRequest canonical_plan_request(const P::Policy& value){
    E::ExecutableObservationRequest request{};request.owner=value.owner;request.member=0;request.group=4;
    request.have_move=true;request.move={10,11};return request;
}
static void compiled_plan_matches_strict_and_cached_path_oracles(){
    PlanHost host;host.remaining=100000;const auto value=canonical_plan_policy(host.packet);const auto declared=plan_budget();
    const auto request=canonical_plan_request(value);E::ExecutablePlanStore store{};
    const auto handle=E::executable_compile_plan(host,value,declared,request,store,host.packet.stamp.epoch);
    const auto* compiled=store.get(handle);assert(compiled&&compiled->valid&&compiled->branches==7);
    const auto strict=P::quote_funding(compiled->funding,declared.price);assert(strict.valid&&strict.required==compiled->required);
    assert(compiled->cost_lookups==3&&host.cost_calls==3&&store.compilation.paid==21);
    unsigned used=0;std::uint64_t maximum=0;
    for(unsigned b=0;b<compiled->branches;++b){const auto& branch=E::executable_plan_branch(compiled->funding,b);
        auto independently_summed=declared.price.final_tail;
        for(unsigned p=0;p<branch.count;++p){const auto& phase=branch.phases[p];
            const auto cost=phase.sdk_queries*declared.price.query_bound+phase.commands*declared.price.command_bound+phase.compute_bound;
            assert(compiled->phase_costs[b][p]==cost);independently_summed+=cost;++used;}
        assert(compiled->initial[b]==independently_summed);maximum=std::max(maximum,independently_summed);}
    assert(compiled->used_phases==used&&compiled->required==maximum);
    E::ExecutableFundingCursor boundary;assert(!boundary.start(*compiled,maximum));assert(boundary.start(*compiled,maximum+1));
    const auto paths=physical_paths();
    for(unsigned selected=0;selected<paths.size();++selected){
        E::ExecutableFundingCursor cached,uncached;assert(cached.start(*compiled,1000));
        assert(uncached.start(compiled->funding,declared.price,1000));
        std::array<unsigned,7> positions{};std::uint16_t possible=127;std::uint64_t available=1000;
        for(unsigned n=0;n<paths[selected].count;++n){const auto step=paths[selected].steps[n];
            std::uint64_t bound=0,after=0,other_bound=0,other_after=0;
            assert(cached.begin(unsigned(step.id),available,bound,after));
            assert(uncached.begin(unsigned(step.id),available,other_bound,other_after));
            assert(bound==step.cost&&bound==other_bound&&after==other_after);
            std::uint64_t expected=0;std::uint16_t next=0;
            for(unsigned p=0;p<paths.size();++p)if(possible&(1u<<p)){
                if(positions[p]<paths[p].count&&paths[p].steps[positions[p]].id==step.id){
                    next|=std::uint16_t(1u<<p);++positions[p];expected=std::max(expected,residual(paths[p],positions[p]));}}
            possible=next;assert(possible&&after==expected);available-=step.cost;
            assert(cached.finish(unsigned(step.id),step.cost,available)&&uncached.finish(unsigned(step.id),step.cost,available));
            assert(cached.remaining()==expected&&uncached.remaining()==expected);
        }
        assert(cached.complete(available)&&uncached.complete(available)&&cached.remaining()==10);
        // Starting again must clear both the cached cost pointer and old phase
        // positions. Different first-phase costs make stale reuse observable.
        auto changed=compiled->funding;
        for(unsigned b=0;b<compiled->branches;++b){auto& branch=b==0?changed.success:
            b<=changed.refusal_count?changed.refusals[b-1]:changed.changes[b-changed.refusal_count-1];
            branch.phases[0].compute_bound+=3;}
        assert(cached.start(changed,declared.price,1000));std::uint64_t bound=0,after=0;
        assert(cached.begin(1,1000,bound,after)&&bound==7);
        assert(cached.finish(1,7,993));
        assert(cached.start(*compiled,1000));assert(cached.begin(1,1000,bound,after)&&bound==4);
    }
    assert(host.cost_calls==3&&host.command_count==0&&host.validations==0&&host.consumptions==0);
}
static void immutable_plan_reuse_and_paid_binding(){
    PlanHost host;host.remaining=100000;auto value=canonical_plan_policy(host.packet);const auto declared=plan_budget();
    const auto request=canonical_plan_request(value);E::ExecutablePlanStore store{};
    const auto handle=E::executable_compile_plan(host,value,declared,request,store,host.packet.stamp.epoch);
    const auto* compiled=store.get(handle);assert(compiled);const auto initial_paid=store.compilation.paid;
    const auto required=compiled->required,generation=compiled->generation;const auto lookups=host.cost_calls;
    auto authority_free=value;authority_free.token={};authority_free.actual_prefix=999;
    const auto reused=E::executable_compile_plan(host,authority_free,declared,request,store,host.packet.stamp.epoch);
    assert(reused.valid&&reused.slot==handle.slot&&reused.generation==generation&&store.get(reused)==compiled);
    assert(host.cost_calls==lookups&&store.compilation.paid==initial_paid&&compiled->required==required);
    assert(E::executable_plan_required(*compiled,store.compilation)==required+E::executable_compilation_bound(declared)-initial_paid);
    // Charge actual binding work at its first inner fuel sample. Binding uses
    // stored branch costs; it must not call the cost compiler again.
    host.poll_cost=17;host.charge_poll=host.fuel_polls+2;E::ExecutableExecutionState state{};
    const auto outcome=E::executable_execute(host,value,declared,state,compiled,&store.compilation);
    assert(outcome.status==E::ExecutableStatus::word_applied&&host.command_count==2&&outcome.successful==2);
    assert(store.compilation.binds==1&&store.compilation.paid==initial_paid+17&&store.compilation.last.spent==17);
    assert(host.cost_calls==lookups&&compiled->generation==generation&&compiled->required==required);
    assert(state.phase_spent[unsigned(E::ExecutablePhase::admission)]==17);
}
static void compiled_binding_rejects_shape_and_budget_mutations(){
    PlanHost compiler;compiler.remaining=100000;const auto original=canonical_plan_policy(compiler.packet);const auto declared=plan_budget();
    const auto request=canonical_plan_request(original);E::ExecutablePlanStore store{};
    const auto handle=E::executable_compile_plan(compiler,original,declared,request,store,compiler.packet.stamp.epoch);
    const auto* compiled=store.get(handle);assert(compiled);const auto binds=store.compilation.binds,paid=store.compilation.paid;
    for(unsigned n=0;n<23;++n){auto changed=original;
        switch(n){case 0:changed.success.actions[0].kind=P::ActionKind::move;break;
        case 1:changed.success.actions[0].target_kind=P::TargetKind::champion;break;
        case 2:++changed.success.actions[0].target_id;break;
        case 3:++changed.success.actions[1].point.x;break;case 4:++changed.success.actions[1].point.y;break;
        case 5:++changed.success.actions[0].slot;break;case 6:changed.success.actions[0].wait_reason=P::WaitReason::channel;break;
        case 7:--changed.success.count;break;case 8:++changed.owner.service;break;case 9:++changed.owner.job;break;
        case 10:++changed.owner.episode;break;case 11:changed.purpose=P::Purpose::bank;break;
        case 12:changed.on_refusal.declared=false;break;case 13:changed.on_refusal.guard=P::Guard::always;break;
        case 14:changed.on_refusal.word.count=0;break;case 15:changed.on_refusal.exclude_point=false;break;
        case 16:++changed.on_refusal.excluded.x;break;case 17:++changed.on_refusal.word.actions[0].point.x;break;
        case 18:changed.on_changed.declared=false;break;case 19:changed.on_changed.guard=P::Guard::always;break;
        case 20:changed.on_changed.word.count=0;break;case 21:changed.on_changed.exclude_point=true;break;
        case 22:++changed.on_changed.word.actions[0].point.y;break;}
        FakeHost host;host.remaining=100000;changed.token=host.token();E::ExecutableExecutionState state{};
        const auto outcome=E::executable_execute(host,changed,declared,state,compiled,&store.compilation);
        assert(outcome.status==E::ExecutableStatus::invalid&&host.command_count==0&&host.validations==0&&host.consumptions==0);
    }
    for(unsigned n=0;n<12;++n){auto changed=declared;
        switch(n){case 0:changed.declared=false;break;case 1:++changed.price.query_bound;break;
        case 2:++changed.price.command_bound;break;case 3:++changed.price.final_tail;break;
        case 4:++changed.admission_bound;break;case 5:--changed.branch_limit;break;
        case 6:++changed.validation_compute;break;case 7:++changed.feedback_compute;break;
        case 8:++changed.refusal_compute;break;case 9:++changed.commit_compute;break;
        case 10:--changed.max_primary;break;case 11:--changed.max_fallback;break;}
        FakeHost host;host.remaining=100000;auto value=original;value.token=host.token();E::ExecutableExecutionState state{};
        const auto outcome=E::executable_execute(host,value,changed,state,compiled,&store.compilation);
        assert(outcome.status==E::ExecutableStatus::invalid&&host.command_count==0&&host.validations==0);
    }
    assert(store.compilation.binds==binds+35&&store.compilation.paid==paid&&store.get(handle)==compiled);
}
static void request_changes_recompile_their_own_feedback_cone(){
    PlanHost host;host.remaining=1000000;host.real_table=true;host.lookup_cost=1;
    const auto value=canonical_plan_policy(host.packet);host.request=canonical_plan_request(value);
    const auto original=host.request;const auto declared=host.declared;E::ExecutablePlanStore store{};
    auto previous=E::executable_compile_plan(host,value,declared,original,store,host.packet.stamp.epoch);
    assert(store.get(previous));const auto original_required=store.get(previous)->required;
    for(unsigned n=0;n<22;++n){auto changed=original;
        switch(n){case 0:++changed.owner.service;break;case 1:++changed.owner.job;break;case 2:++changed.owner.episode;break;
        case 3:changed.purpose=P::Purpose::bank;break;case 4:changed.phase=E::ExecutableMacroPhase::bank_channel;break;
        case 5:++changed.member;break;case 6:++changed.group;break;case 7:++changed.group_witness_member;break;
        case 8:++changed.enemy;break;case 9:changed.have_enemy=true;break;case 10:++changed.move.x;break;
        case 11:++changed.altar.y;break;case 12:changed.have_move=false;break;case 13:changed.have_altar=true;break;
        case 14:changed.need_group=true;break;case 15:changed.need_bag=true;break;case 16:changed.need_bank_receipt=true;break;
        case 17:changed.need_home=true;break;case 18:changed.need_target_shield=true;break;
        case 19:++changed.capture_inputs_remaining;break;case 20:++changed.move.y;break;case 21:++changed.altar.x;break;}
        auto matching_shape=value;matching_shape.owner=changed.owner;matching_shape.purpose=changed.purpose;
        host.request=changed;const auto before=host.cost_calls;
        const auto next=E::executable_compile_plan(host,matching_shape,declared,changed,store,host.packet.stamp.epoch);
        const auto* compiled=store.get(next);assert(compiled&&next.generation>previous.generation);
        assert(E::executable_same_request(compiled->request,changed)&&host.cost_calls==before+3);
        const auto strict=P::quote_funding(compiled->funding,declared.price);assert(strict.valid&&compiled->required==strict.required);
        if(n==18)assert(compiled->required==original_required+declared.price.query_bound);
        previous=next;
    }
    assert(!store.compilation.failed&&host.command_count==0&&host.validations==0);
    const auto count=host.cost_calls;auto mismatch=original;++mismatch.owner.job;host.request=mismatch;
    assert(!E::executable_compile_plan(host,value,declared,mismatch,store,host.packet.stamp.epoch).valid&&host.cost_calls==count);
    mismatch=original;mismatch.purpose=P::Purpose::bank;host.request=mismatch;
    assert(!E::executable_compile_plan(host,value,declared,mismatch,store,host.packet.stamp.epoch).valid&&host.cost_calls==count);
}
static void compile_and_bind_strict_funding_boundaries(){
    PlanHost source;source.remaining=100000;const auto value=canonical_plan_policy(source.packet);const auto declared=plan_budget();
    const auto request=canonical_plan_request(value);E::ExecutablePlanStore reference{};
    const auto handle=E::executable_compile_plan(source,value,declared,request,reference,source.packet.stamp.epoch);
    const auto* compiled=reference.get(handle);assert(compiled);const auto required=compiled->required;
    const auto allowance=E::executable_compilation_bound(declared);
    for(bool enough:{false,true}){PlanHost host;host.remaining=required+allowance+unsigned(enough);E::ExecutablePlanStore store{};
        const auto proposed=E::executable_compile_plan(host,value,declared,request,store,host.packet.stamp.epoch);
        assert(proposed.valid==enough&&host.command_count==0&&host.validations==0);
        if(enough)assert(store.get(proposed)&&host.remaining==E::executable_plan_required(*store.get(proposed),store.compilation)+1);
        else assert(!store.get(store.pinned));
    }
    for(bool enough:{false,true}){FakeHost host;host.remaining=E::executable_plan_required(*compiled,reference.compilation)+unsigned(enough);
        auto fresh=value;fresh.token=host.token();E::ExecutableExecutionState state{};
        const auto result=E::executable_execute(host,fresh,declared,state,compiled,&reference.compilation);
        if(enough)assert(host.command_count==2&&result.status==E::ExecutableStatus::word_applied);
        else assert(host.command_count==0&&host.validations==0&&result.status==E::ExecutableStatus::unfunded);
    }
    PlanHost too_low;too_low.remaining=allowance+declared.price.final_tail;E::ExecutablePlanStore low_store{};
    assert(!E::executable_compile_plan(too_low,value,declared,request,low_store,too_low.packet.stamp.epoch).valid);
    assert(too_low.cost_calls==0&&low_store.compilation.failure==P::Failure::unfunded);
}
static void failed_staging_preserves_the_retained_plan_and_cursor(){
    PlanHost host;host.remaining=100000;auto value=canonical_plan_policy(host.packet);const auto declared=plan_budget();
    const auto request=canonical_plan_request(value);E::ExecutablePlanStore store{};
    const auto first=E::executable_compile_plan(host,value,declared,request,store,host.packet.stamp.epoch);
    const auto* kept=store.get(first);assert(kept);const auto generation=kept->generation,required=kept->required;
    E::ExecutableFundingCursor cursor;assert(cursor.start(*kept,1000));std::uint64_t bound=0,after=0;
    assert(cursor.begin(1,1000,bound,after)&&cursor.finish(1,4,996));const auto residual_before=cursor.remaining();
    auto optional=value;optional.success.actions[1].point={9,10};
    const auto optional_handle=E::executable_compile_plan(host,optional,declared,request,store,host.packet.stamp.epoch);
    assert(optional_handle.valid&&optional_handle.slot!=first.slot&&store.get(first)==kept&&store.pinned.generation==first.generation);
    auto invalid=value;invalid.success.count=0;const auto rejected=E::executable_compile_plan(host,invalid,declared,request,store,host.packet.stamp.epoch);
    assert(!rejected.valid&&store.get(first)==kept&&!store.get(optional_handle)&&kept->required==required&&kept->generation==generation);
    value.success.actions[0].target_id=43;host.lookup_cost=4000;
    const auto overbudget=E::executable_compile_plan(host,value,declared,request,store,host.packet.stamp.epoch);
    assert(!overbudget.valid&&store.compilation.failed&&!store.compilation.last.paid&&store.get(first)==kept);
    assert(store.pinned.slot==first.slot&&kept->generation==generation&&kept->required==required&&cursor.remaining()==residual_before);
    assert(cursor.begin(2,996,bound,after)&&bound==5&&cursor.finish(2,5,991));
    // A failed staging attempt cannot be retried with a new allowance in the
    // same input. A genuine later input permits a new compile, not old authority.
    host.lookup_cost=1;const auto blocked=E::executable_compile_plan(host,value,declared,request,store,host.packet.stamp.epoch);
    assert(!blocked.valid&&store.get(first)==kept);
    ++host.packet.stamp.epoch;const auto next=E::executable_compile_plan(host,value,declared,request,store,host.packet.stamp.epoch);
    assert(next.valid&&next.generation>generation&&store.get(first)==kept&&store.get(next));
    assert(store.pin(next));value.success.actions[1].point={9,10};value.success.actions[0].target_id=44;
    const auto third=E::executable_compile_plan(host,value,declared,request,store,host.packet.stamp.epoch);
    assert(third.valid&&third.slot==first.slot&&third.generation>first.generation&&!store.get(first));
}
static void compilation_ledger_is_one_measured_input_allowance(){
    const auto declared=E::executable_execution_default_budget();const auto limit=E::executable_compilation_bound(declared);assert(limit==9792);
    E::ExecutableCompilationLedger ledger;ledger.enter(7,limit);assert(ledger.remaining()==limit);
    assert(ledger.charge(7,100000,98000)&&ledger.paid==2000&&ledger.last.spent==2000&&ledger.last.paid);
    const auto first_serial=ledger.last.serial;ledger.enter(7,limit);assert(ledger.paid==2000&&ledger.remaining()==7792);
    assert(ledger.charge(7,97000,89208)&&ledger.paid==limit&&ledger.last.spent==7792&&ledger.last.serial>first_serial);
    ledger.enter(7,limit);assert(ledger.remaining()==0&&ledger.paid==limit);
    assert(!ledger.charge(7,89208,89207)&&ledger.failed&&!ledger.last.paid);
    assert(ledger.failure==P::Failure::bound_overrun&&ledger.maximum==limit&&ledger.interval_maximum==7792);
    ledger.enter(7,limit);assert(ledger.failed&&ledger.remaining()==P::invalid_cost);
    ledger.enter(7,limit+1);assert(ledger.failure==P::Failure::bound_overrun);
    ledger.enter(8,limit);assert(!ledger.failed&&ledger.paid==0&&ledger.remaining()==limit);
    assert(!ledger.charge(7,80000,79999)&&ledger.failed); // A mismatched input cannot spend the new allowance.
    E::ExecutableCompilationLedger changed;changed.enter(7,limit);changed.enter(7,limit+1);assert(changed.failed);
    E::ExecutableCompilationLedger reversed;reversed.enter(7,limit);assert(!reversed.charge(7,100,101)&&reversed.failed);
}
static void paid_compilation_credit_does_not_renew_preparation(){
    const auto physical=ready_snapshot().stamp;E::ExecutableObservationPreparation preparation;
    const auto limit=E::executable_compilation_bound(E::executable_execution_default_budget());
    E::ExecutableCompilationLedger compilation;compilation.enter(physical.epoch,limit);
    preparation.begin(physical,100000,0,E::executable_observe_compute_bound);
    auto observed=physical;observed.observation+=4;assert(preparation.spent(observed,91000,8000)==1000);
    assert(compilation.charge(physical.epoch,91000,89000));const auto receipt=compilation.last;
    assert(preparation.credit(observed,receipt.before_fuel,receipt.after_fuel,89000,receipt.serial,receipt.paid));
    assert(!preparation.credit(observed,receipt.before_fuel,receipt.after_fuel,89000,receipt.serial,receipt.paid));
    assert(preparation.spent(observed,89000,8000)==1000&&preparation.remaining(observed,89000,8000)==5000);
    observed.observation+=6;assert(preparation.spent(observed,74500,20000)==3500);
    assert(preparation.remaining(observed,74500,20000)==2500&&preparation.compile_paid==2000);
    auto unpaid=receipt;unpaid.serial+=1;assert(!preparation.credit(observed,74000,73900,73900,unpaid.serial,false));
    assert(!preparation.credit(observed,90000,89900,73900,unpaid.serial,true)); // Overlapping the already credited interval.
    assert(preparation.remaining(observed,71999,20000)==P::invalid_cost);
    assert(preparation.finish(observed,74500,20000)&&!preparation.active);
    assert(compilation.charge(physical.epoch,74500,66708)&&compilation.paid==limit&&compilation.remaining()==0);
    std::printf("{\"schema\":\"mosaic-r18-plan-ledger/v1\",\"canonical_compilation_bound\":%llu,\"compile_paid\":2000,\"bind_paid\":7792,\"total_paid\":%llu,\"remaining\":%llu,\"native_fuel_qualified\":false}\n",
        static_cast<unsigned long long>(limit),static_cast<unsigned long long>(compilation.paid),static_cast<unsigned long long>(compilation.remaining()));
}
static void cached_plans_do_not_mint_physical_authority(){
    PlanHost host;host.remaining=100000;auto value=canonical_plan_policy(host.packet);const auto declared=plan_budget();
    const auto request=canonical_plan_request(value);E::ExecutablePlanStore store{};
    const auto handle=E::executable_compile_plan(host,value,declared,request,store,host.packet.stamp.epoch);const auto* compiled=store.get(handle);assert(compiled);
    E::ExecutableExecutionState state{};auto absent=value;absent.token={};
    const auto no_token=E::executable_execute(host,absent,declared,state,compiled,&store.compilation);
    assert(no_token.status==E::ExecutableStatus::invalid&&host.command_count==0&&host.validations==0);
    ++host.packet.stamp.frame;E::ExecutableExecutionState fresh_state{};
    const auto stale=E::executable_execute(host,value,declared,fresh_state,compiled,&store.compilation);
    assert(stale.status==E::ExecutableStatus::stale&&host.command_count==0&&host.validations==0);
    ++host.packet.stamp.epoch;host.authority=::token(host.packet);value.token=host.token();
    const auto wrong_input=E::executable_execute(host,value,declared,fresh_state,compiled,&store.compilation);
    assert(wrong_input.status==E::ExecutableStatus::overrun&&host.command_count==0&&host.validations==0&&store.compilation.failed);
}
static void pending_actual_failure_owns_before_legacy(){
    for(unsigned failure=0;failure<3;++failure){
        FakeHost host;host.fail_move_count=1;
        if(failure==0)host.missing_feedback_at=1;
        if(failure==1)host.fail_close_at=1;
        if(failure==2)host.fail_commit_at=1;
        auto value=policy(host.packet,{move({10,11})});alternate(value);E::ExecutableExecutionState state{};
        const auto refused=E::executable_execute(host,value,budget(),state);
        assert(host.command_count==1&&refused.attempts==1&&refused.refusals==1&&!refused.actual_effect);
        assert(state.pending_feedback&&host.packet.authority.raw_m&&host.packet.pose==P::Point(10,10));
        assert(state.service.refusal_debt()==1&&state.service.completed_words()==0);
        assert(E::executable_owner_selection(state.pending_feedback,true)==E::ExecutableOwnerSelection::pending_feedback);
        assert(E::executable_owner_selection(state.pending_feedback,false)==E::ExecutableOwnerSelection::pending_feedback);
        const auto old=state.pending_receipt;const auto commands=host.command_count;
        host.missing_feedback_at=host.fail_close_at=host.fail_commit_at=0;
        ++host.packet.stamp.epoch;++host.packet.stamp.frame;fresh_readonly_owner(host,{10,10},100,true);
        const auto closed=E::executable_reconcile_pending(host,budget(),state);
        assert(closed.status==E::ExecutableStatus::reconciled&&closed.feedback_closed&&!state.pending_feedback);
        assert(host.command_count==commands&&host.reconciliations==1&&!state.service.pose_known());
        assert(state.service.refusal_debt()==1&&state.service.completed_words()==0);
        assert(state.pending_receipt.after==old.after&&state.pending_receipt.after_pose==old.after_pose);
        assert(E::executable_owner_selection(state.pending_feedback,true)==E::ExecutableOwnerSelection::retained_legacy);
    }
    FakeHost host;host.fail_move_count=1;host.missing_feedback_at=2;
    auto value=policy(host.packet,{basic(),move({11,10})});E::ExecutableExecutionState state{};
    const auto prefix=E::executable_execute(host,value,budget(),state);
    assert(prefix.actual_effect&&prefix.successful==1&&prefix.refusals==1&&host.command_count==2&&state.pending_feedback);
    assert(state.service.actual_prefix()==1&&state.service.completed_words()==0&&host.packet.authority.raw_m);
    // The real retained-return condition is true, but unfinished acquisition
    // remains the owner. The successful Basic is not erased by its false tail.
    const bool retained_return=true;
    assert(E::executable_owner_selection(state.pending_feedback,retained_return)==E::ExecutableOwnerSelection::pending_feedback);
    const auto old=state.pending_receipt;host.missing_feedback_at=0;
    ++host.packet.stamp.epoch;++host.packet.stamp.frame;fresh_readonly_owner(host,{20,20},55,true);
    const auto closed=E::executable_reconcile_pending(host,budget(),state);
    assert(closed.status==E::ExecutableStatus::reconciled&&!state.pending_feedback&&host.command_count==2);
    assert(state.service.actual_prefix()==1&&state.service.refusal_debt()==1&&!state.service.pose_known());
    assert(state.pending_receipt.after==old.after&&state.pending_receipt.after_pose!=host.packet.pose);
    assert(E::executable_owner_selection(state.pending_feedback,retained_return)==E::ExecutableOwnerSelection::retained_legacy);
}
static void acknowledged_commit_overrun_is_not_replayed(){
    struct AckHost:FakeHost {
        P::Receipt acknowledged{};bool have_ack{};unsigned commit_calls{};
        bool commit(const P::Receipt& receipt){
            ++commit_calls;
            if(have_ack&&receipt.serial<=acknowledged.serial)return false;
            const bool result=FakeHost::commit(receipt);acknowledged=receipt;have_ack=true;
            // The producer publishes its causal ACK, then its outer cost check
            // fails. Returning false must not erase the already performed hooks.
            return result&&commit_cost<=1;
        }
        bool pending_commit_known(const P::Receipt& receipt)const{
            return have_ack&&receipt.serial==acknowledged.serial&&receipt.before==acknowledged.before&&
                receipt.owner==acknowledged.owner&&receipt.outcome==acknowledged.outcome&&
                E::executable_same_action(receipt.action,acknowledged.action);
        }
    } host;
    host.commit_cost=2;auto value=policy(host.packet,{basic(),move({11,10})});E::ExecutableExecutionState state{};
    const auto overrun=E::executable_execute(host,value,budget(),state);
    assert(overrun.status==E::ExecutableStatus::feedback_missing&&overrun.actual_effect&&state.pending_feedback&&!state.pending_hooks);
    assert(overrun.first_failure.failure==P::Failure::bound_overrun&&overrun.first_failure_phase==E::ExecutablePhase::commit&&state.input_blocked);
    assert(host.commit_calls==1&&host.have_ack&&state.pending_recorded&&state.service.actual_prefix()==1);
    const auto old=state.pending_receipt;const auto commands=host.command_count;
    assert(E::executable_owner_selection(state.pending_feedback,true)==E::ExecutableOwnerSelection::pending_feedback);
    host.commit_cost=1;host.remaining=10000;++host.packet.stamp.epoch;++host.packet.stamp.frame;
    fresh_readonly_owner(host,{30,30},41);
    const auto closed=E::executable_reconcile_pending(host,budget(),state);
    assert(closed.status==E::ExecutableStatus::reconciled&&closed.feedback_closed&&!state.pending_feedback&&state.pending_hooks);
    assert(host.commit_calls==1&&host.receipt_count==1&&host.command_count==commands&&host.reconciliations==1);
    assert(state.service.actual_prefix()==1&&state.service.completed_words()==0&&!state.service.pose_known());
    assert(state.pending_receipt.after==old.after&&state.pending_receipt.after_pose==old.after_pose&&state.pending_receipt.after_pose!=host.packet.pose);
    assert(E::executable_owner_selection(state.pending_feedback,true)==E::ExecutableOwnerSelection::retained_legacy);
}
static constexpr bool constant_compilation_metric_contract(){
    E::ExecutableExecutionBudget empty{};
    if(E::executable_compilation_bound(empty)!=9792)return false;
    // The generic shape metric must itself remain usable in a C++20 constant
    // expression. An empty shape is not the fixed canonical work family.
    const P::Policy empty_shape{};
    if(E::executable_shape_admission(empty_shape,empty)!=1088)return false;
    for(unsigned bit=0;bit<64;++bit){
        empty.admission_bound=std::uint64_t(1)<<bit;
        if(E::executable_compilation_bound(empty)!=empty.admission_bound)return false;
    }
    empty.admission_bound=P::invalid_cost;
    return E::executable_compilation_bound(empty)==P::invalid_cost;
}
static_assert(constant_compilation_metric_contract());
static void constant_compilation_metric_matches_all_budget_dependencies(){
    FakeHost host;const auto canonical=canonical_plan_policy(host.packet);const auto declared=plan_budget();
    const auto funding=E::executable_execution_funding(host,canonical,declared);
    unsigned phases=funding.success.count;
    for(unsigned n=0;n<funding.refusal_count;++n)phases+=funding.refusals[n].count;
    for(unsigned n=0;n<funding.changed_count;++n)phases+=funding.changes[n].count;
    assert(phases==65&&1+funding.refusal_count+funding.changed_count==7);
    auto equivalent=[&](const E::ExecutableExecutionBudget& candidate){
        const auto old_shape=E::executable_shape_admission(canonical,candidate);
        const auto independent_paths=E::executable_admission_bound(funding,candidate);
        assert(E::executable_compilation_bound(candidate)==old_shape&&old_shape==independent_paths);
        if(candidate.admission_bound)assert(E::executable_compilation_bound(candidate)==candidate.admission_bound);
    };
    E::ExecutableExecutionBudget empty{};equivalent(empty);equivalent(declared);
    // Every other budget field can differ without changing this resource
    // metric. In particular, an empty/undeclared budget still derives the
    // historical metric; this function does not grant execution admission.
    auto unrelated=empty;unrelated.declared=true;unrelated.price={P::invalid_cost,1,0};
    unrelated.validation_compute=P::invalid_cost;unrelated.feedback_compute=1;
    unrelated.refusal_compute=2;unrelated.commit_compute=3;unrelated.max_primary=0;
    unrelated.max_fallback=100;unrelated.branch_limit=0;equivalent(unrelated);
    for(const auto override_value:{std::uint64_t(0),std::uint64_t(1),std::uint64_t(2),std::uint64_t(1024),
            std::uint64_t(9791),std::uint64_t(9792),std::uint64_t(9793),P::invalid_cost-1,P::invalid_cost}){
        for(auto candidate:{empty,declared,unrelated}){candidate.admission_bound=override_value;equivalent(candidate);}
    }
    for(unsigned bit=0;bit<64;++bit){auto candidate=unrelated;candidate.admission_bound=std::uint64_t(1)<<bit;equivalent(candidate);}
    // A custom first caller cannot seed a cached default for later callers.
    auto first=declared;first.admission_bound=1;equivalent(first);equivalent(empty);equivalent(declared);
    std::puts("{\"schema\":\"mosaic-r19-constant-metric/v1\",\"constant_expression\":true,\"canonical_phases\":65,\"canonical_paths\":7,\"default_bound\":9792,\"override_preserved\":true,\"native_fuel_qualified\":false}");
}

int main(){
    actual_kill_then_move();spent_and_restricted_move_are_distinct();false_keeps_actual_move_and_uses_declared_branch();
    prefix_survives_later_refusal();changed_target_revalidates_before_dispatch();feedback_failure_never_borrows_a_prediction();
    first_failure_remains_latched();lease_queries_and_attempts();two_refusals_close_the_bound();
    unsupported_wait_cannot_own_an_idle_input();whole_branch_funding();input_and_word_caps_are_hard_stops();
    partial_feedback_records_only_observed_flags();applied_command_overrun_closes_truthful_feedback();refused_fallback_is_not_a_completed_word();
    generated_cursor_matches_every_physical_branch();token_free_shape_pricing_never_mints_authority();
    real_producer_table_fits_the_declared_entry();pending_reconciliation_requires_new_actual_authority();late_missing_body_registers_only_the_old_minimal_receipt();
    reconciliation_commit_overrun_blocks_current_input(false);reconciliation_commit_overrun_blocks_current_input(true);
    scalar_quotes_match_strict_branch_oracles();actual_caller_lifecycle_fits_without_double_admission();preparation_compute_is_one_shared_ledger();
    compiled_plan_matches_strict_and_cached_path_oracles();immutable_plan_reuse_and_paid_binding();compiled_binding_rejects_shape_and_budget_mutations();
    request_changes_recompile_their_own_feedback_cone();failed_staging_preserves_the_retained_plan_and_cursor();compilation_ledger_is_one_measured_input_allowance();
    compile_and_bind_strict_funding_boundaries();paid_compilation_credit_does_not_renew_preparation();cached_plans_do_not_mint_physical_authority();
    pending_actual_failure_owns_before_legacy();acknowledged_commit_overrun_is_not_replayed();
    constant_compilation_metric_matches_all_budget_dependencies();
    std::puts("executable execution: 37 behavior/funding cases passed");
}
