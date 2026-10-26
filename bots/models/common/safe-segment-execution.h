#pragma once
#include "safe-segment.h"
#include <cstdint>

namespace plan4::safe_segment {
// A provider supplies CURRENT legal observations. This protocol never fills
// absent scene/lifecycle/kit evidence, and a prediction cannot authorize an
// SDK action without the executor's fresh action/movement/channel checks.
struct ExecutionProposal {
    const AttackContext* actor=nullptr;
    const EscapeScene* scene=nullptr;
    const Settings* settings=nullptr;
    int group_id=-1;
    std::uint64_t kit_version=0;
    std::uint32_t stat_epoch=0,lifecycle_version=0;
    bool lifecycle_supported=false;
};
struct ExecutionBody {
    int round=-1,actor_id=-1,group_id=-1,target_id=-1;
    Point position{};
    float health=0,maximum_health=0;
    std::uint64_t kit_version=0;
    std::uint32_t stat_epoch=0,lifecycle_version=0;
    bool alive=false,can_attack=false,can_move=false,on_spawn=false,recalling=false,other_channel=false;
    bool group_cleared=false,new_visible_rival=false,emergency=false;
};
enum class ExecutionKind { none,attack,escape_move,recall,wait,complete,abort };
struct ExecutionCommand { ExecutionKind kind=ExecutionKind::none;Point destination{};Failure failure=Failure::none; };
enum class ExecutionStage { idle,attacking,reevaluating,escaping,recalling,healing };
struct RecoveryPermission {
    int observed_round=-1,current_round=-1;
    std::uint64_t fuel=0;
    bool alive=false,channel_active=false,can_start_recall=false;
};
template<class Action>
inline bool checked_recovery(const RecoveryPermission& permit,Action action) {
    if (permit.observed_round<0 || permit.observed_round!=permit.current_round || permit.fuel<35000 ||
        !permit.alive || permit.channel_active || !permit.can_start_recall) return false;
    action();return true;
}

class SegmentExecutor {
    SegmentToken token_{};
    EscapeBuild escape_{};
    ExecutionStage stage_=ExecutionStage::idle;
    ExecutionCommand pending_{};
    ExecutionBody pending_body_{};
    int group_=-1,last_round_=-1,issued_round_=-1,deadline_=-1;
    std::uint64_t kit_=0;
    std::uint32_t epoch_=0,lifecycle_=0;
    bool pending_action_=false;
    ExecutionCommand abort(Failure reason) {
        stage_=ExecutionStage::idle;token_.active=false;pending_action_=false;
        return {ExecutionKind::abort,{},reason};
    }
    ExecutionCommand issue(ExecutionKind kind,const ExecutionBody& body,Point destination={}) {
        pending_={kind,destination,Failure::none};pending_body_=body;pending_action_=true;issued_round_=body.round;return pending_;
    }
    bool source(const ExecutionProposal& p,const ExecutionBody& body) const {
        if (!p.actor || !p.scene || !p.settings || p.settings->max_rounds<=body.round || !p.lifecycle_supported || p.group_id!=body.group_id ||
            p.kit_version!=body.kit_version || p.stat_epoch!=body.stat_epoch || p.lifecycle_version!=body.lifecycle_version) return false;
        const auto& a=*p.actor;
        return a.observed_round==body.round && a.actor_id==body.actor_id && a.target_id==body.target_id &&
            a.position==body.position && a.health==body.health && a.maximum_health==body.maximum_health && a.alive==body.alive &&
            p.scene->observed_round==body.round && p.scene->actor_id==body.actor_id;
    }
public:
    bool active() const {return stage_!=ExecutionStage::idle;}
    ExecutionStage stage() const {return stage_;}
    int graph_vertices() const {return escape_.vertices;}
    int group_id() const {return group_;}
    int target_id() const {return token_.target_id;}
    void reset() {stage_=ExecutionStage::idle;token_.active=false;pending_action_=false;}
    ExecutionCommand next(const ExecutionProposal& p,const ExecutionBody& body,int graph_quota,WorkBudget work={}) {
        escape_.vertices=0;
        if (body.round<0 || body.round>std::numeric_limits<int>::max()-max_horizon ||
            body.actor_id<0 || body.group_id<0 || !body.alive || !valid_loss(body.health) ||
            body.health<=0 || !valid_loss(body.maximum_health) || body.maximum_health<=0 || body.health>body.maximum_health)
            return abort(Failure::unsupported);
        if (body.new_visible_rival || body.emergency) return abort(Failure::emergency);
        if (active() && body.round>=deadline_) return abort(Failure::horizon);
        if (active() && last_round_>=0 && (body.round<last_round_ || std::int64_t(body.round)>std::int64_t(last_round_)+1))
            return abort(Failure::stale_observation);
        last_round_=body.round;
        if (body.round==issued_round_) return {ExecutionKind::wait};
        if (pending_action_) return abort(Failure::changed_segment); // Caller must acknowledge each actual attempt.
        if (active() && (body.group_id!=group_ || body.target_id!=token_.target_id || body.kit_version!=kit_ ||
            body.stat_epoch!=epoch_ || ((stage_==ExecutionStage::attacking || stage_==ExecutionStage::reevaluating || stage_==ExecutionStage::escaping) &&
                body.lifecycle_version!=lifecycle_))) return abort(Failure::changed_segment);
        if (body.group_cleared) {reset();return {ExecutionKind::complete};}
        if (body.other_channel) return abort(Failure::changed_segment);
        if (stage_==ExecutionStage::recalling || stage_==ExecutionStage::healing) {
            if (body.recalling) return {ExecutionKind::wait};
            if (!body.on_spawn) return abort(Failure::unsafe_recall);
            stage_=ExecutionStage::healing;
            if (body.health<body.maximum_health) return {ExecutionKind::wait};
            reset();return {ExecutionKind::complete};
        }
        if (body.recalling || !source(p,body) || graph_quota<0 || graph_quota>256) return abort(Failure::unsupported);
        auto actor=*p.actor;
        struct Budget {WorkBudget outer,original;const EscapeBuild* escape;int quota;};
        Budget budget{work,actor.work_budget,&escape_,graph_quota};
        actor.work_budget={[](const void* raw) {
            const auto& b=*static_cast<const Budget*>(raw);
            return (!b.outer.can_refine || b.outer.can_refine(b.outer.context)) &&
                (!b.original.can_refine || b.original.can_refine(b.original.context)) && b.escape->vertices<b.quota;
        },&budget};
        bool escape_ready=false;
        const bool admitting=stage_==ExecutionStage::idle;
        if (admitting) {
            if (!body.can_attack) return abort(Failure::not_attackable);
            build_escape_into(actor,*p.settings,*p.scene,escape_);
            const auto quote=safe_segment_quote(actor,escape_.proof,*p.settings);
            if (escape_.failure!=BuildFailure::none || quote.failure!=Failure::none)
                return abort(escape_.failure==BuildFailure::budget?Failure::budget:quote.failure);
            token_=begin_segment(actor,quote);group_=body.group_id;kit_=body.kit_version;epoch_=body.stat_epoch;lifecycle_=body.lifecycle_version;
            stage_=ExecutionStage::attacking;
            deadline_=std::min(body.round+max_horizon,p.settings->max_rounds);
        }
        if (stage_==ExecutionStage::reevaluating) {
            if (body.position!=token_.position || !actor.stationary || actor.target_changed || actor.ability_timing_changed)
                return abort(Failure::changed_segment);
            build_escape_into(actor,*p.settings,*p.scene,escape_);escape_ready=true;
            if (escape_.failure!=BuildFailure::none || validate_escape(actor,escape_.proof,*p.settings)!=Failure::none)
                return abort(escape_.failure==BuildFailure::budget?Failure::budget:Failure::unsafe_escape);
            if (!body.can_attack) return {ExecutionKind::wait};
            const auto quote=safe_segment_quote(actor,escape_.proof,*p.settings);
            if (escape_.failure!=BuildFailure::none) return abort(escape_.failure==BuildFailure::budget?Failure::budget:Failure::unsafe_escape);
            if (quote.failure==Failure::none) {token_=begin_segment(actor,quote);stage_=ExecutionStage::attacking;}
            else if (quote.failure==Failure::unsafe_segment || quote.failure==Failure::unsafe_escape) stage_=ExecutionStage::escaping;
            else return abort(quote.failure);
        }
        if (stage_==ExecutionStage::attacking) {
            if (!segment_still_valid(token_,actor)) return abort(Failure::changed_segment);
            if (!admitting && !escape_ready) {
                build_escape_into(actor,*p.settings,*p.scene,escape_);
                const auto renewed=quote_segment(actor,escape_.proof,*p.settings,token_.remaining_attacks);
                if (escape_.failure!=BuildFailure::none || renewed.failure!=Failure::none ||
                    renewed.health_after_extra_exposure-renewed.health_at_recall_arrival>token_.escape_loss)
                    return abort(escape_.failure==BuildFailure::budget?Failure::budget:Failure::unsafe_escape);
            }
            if (!body.can_attack) return {ExecutionKind::wait};
            return issue(ExecutionKind::attack,body);
        }
        if (!escape_ready) build_escape_into(actor,*p.settings,*p.scene,escape_);
        if (escape_.failure!=BuildFailure::none || validate_escape(actor,escape_.proof,*p.settings)!=Failure::none)
            return abort(escape_.failure==BuildFailure::budget?Failure::budget:Failure::unsafe_escape);
        float loss=actor.predicted_incoming;
        for (int n=0;n<escape_.proof.escape_count;++n) loss+=escape_.proof.escape[n].incoming;
        if (body.health<=loss) return abort(Failure::unsafe_escape);
        if (escape_.path_count) {
            if (!body.can_move) return {ExecutionKind::wait};
            return issue(ExecutionKind::escape_move,body,escape_.path[0]);
        }
        return issue(ExecutionKind::recall,body);
    }
    bool acknowledge(const ExecutionCommand& command,const ExecutionBody& body,bool success) {
        if (!pending_action_ || body.round!=issued_round_ || body.actor_id!=pending_body_.actor_id ||
            body.group_id!=pending_body_.group_id || body.target_id!=pending_body_.target_id || body.position!=pending_body_.position ||
            body.health!=pending_body_.health || body.kit_version!=pending_body_.kit_version || body.stat_epoch!=pending_body_.stat_epoch ||
            command.kind!=pending_.kind || command.destination!=pending_.destination)
            return false;
        pending_action_=false;
        if (!success) {reset();return false;}
        if (command.kind==ExecutionKind::attack) {consumed_attack(token_);if (!token_.active) stage_=ExecutionStage::reevaluating;}
        else if (command.kind==ExecutionKind::recall) stage_=ExecutionStage::recalling;
        return true;
    }
};
} // namespace plan4::safe_segment
