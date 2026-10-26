#pragma once
#include "../../common/src/execute.h"

namespace p6::reference {
// Ranking and inspection share the executor's retirement history. An incumbent
// remains selectable; another group is only a possible replacement, not an
// admission or a reason to erase the incumbent.
inline bool replacement_locked(const ActorState& state) {
    return state.active && (state.phase == Phase::hold || state.phase == Phase::bank ||
        state.tribute_started || state.payout_seen || state.bank_seen || state.empty_liquidation);
}
inline bool admission_eligible(const ActorState& state, int group, int round) {
    if (group < 0 || round < 0) return false;
    if (state.active && state.project.group == group) return true;
    if (replacement_locked(state)) return false;
    return state.epoch_eligible(group, round);
}
// The adapter supplies its real available quote/inspection alternatives. A
// rejected organization proposal must not hide a usable independent choice.
template<class Available>
inline int resolve_work_group(const ActorState& state, int proposed, int independent,
                              int round, Available available) {
    const auto usable = [&](int group) {
        return admission_eligible(state, group, round) && available(group);
    };
    if (usable(proposed)) return proposed;
    if (usable(independent)) return independent;
    if (state.active && usable(state.project.group)) return state.project.group;
    return -1;
}
// The fallback policy has no membership dependency. A future SDK adapter must
// construct fresh observations/certificates and confirm each proposed action.
inline StepProposal choose(ActorState& state, const ReferenceInput& input) {
    return reference_step(state, input);
}
// An owned conversion must reach its physical renewal before the caller can
// dispatch another policy decision. Closing that stage spends no action. Its
// generic idle recall is not a new obligation after the state becomes free.
inline bool renewed_without_action(bool was_pending, const ActorState& state,
    const ReferenceInput& in, const StepProposal& proposal) {
    const auto& actor=in.frame.own.actor;
    return was_pending && in.frame.valid() && in.safe_now && in.at_renewal &&
        actor.alive && actor.health>0 && actor.bag==0 && actor.channel==Channel::none &&
        !state.active && !state.bank_seen && !state.payout_seen && !state.empty_liquidation &&
        !state.tribute_started && state.last_round==in.frame.clock.round &&
        state.phase==Phase::bootstrap && state.stage==Stage::produce_or_approach &&
        proposal.action==Action::recall;
}
// A fully supplied independent actor may yield after closing owned renewal.
// This grants no source, certificate or next action; the next owner turn must
// observe and rank work again. Real shop/recovery and organization orders keep
// their existing paths, and renewed_without_action still proves the closure.
inline bool idle_renewal_may_yield(const ActorState& state, const Frame& frame,
    bool independent, bool safe_now, bool recovering, bool shop_pending) {
    const auto& actor=frame.own.actor;
    return independent && safe_now && !recovering && !shop_pending && frame.valid() &&
        state.active && (state.bank_seen || state.empty_liquidation) &&
        actor.alive && actor.max_health>0 && actor.health>=actor.max_health &&
        actor.bag==0 && frame.own.gold==0 && actor.channel==Channel::none;
}
template<class Forecast> inline std::optional<StepProposal> choose_forecast_checked(ActorState& state,
    const ReferenceInput& observed, const Forecast& request) {
    if (observed.frame.valid() && observed.safe_now &&
        observed.frame.own.actor.channel != Channel::none &&
        observed.frame.own.actor.channel_valid) return reference_step(state, observed);
    if (auto bound = bind_cycle_forecast(state, observed, request))
        return reference_step(state, *bound);
    const bool actual_empty_payout = observed.frame.own.actor.bag == 0 && observed_payout(state, observed);
    if (observed_bank(state, observed) || actual_empty_payout ||
        (state.active && (state.bank_seen || state.empty_liquidation))) {
        // Actual conversion/renewal progresses independently of whether another
        // source can already be forecast. Never admit new work from an empty quote.
        auto progress = observed;
        progress.solo = {}; progress.crew = nullptr; progress.assigned_group = -1;
        return reference_step(state, progress);
    }
    return std::nullopt;
}
inline std::optional<StepProposal> choose_forecast(ActorState& state,
    const ReferenceInput& observed,const forecast::Request& request) {
    return choose_forecast_checked(state,observed,request);
}
inline std::optional<StepProposal> choose_forecast(ActorState& state,
    const ReferenceInput& observed,const forecast::EvaluatedQuote& evaluated) {
    return choose_forecast_checked(state,observed,evaluated);
}
inline std::optional<StepProposal> choose_forecast_into(ActorState& state,
    ReferenceInput& observed,const forecast::EvaluatedQuote& evaluated) {
    if (observed.frame.valid() && observed.safe_now &&
        observed.frame.own.actor.channel != Channel::none &&
        observed.frame.own.actor.channel_valid) return reference_step(state,observed);
    if(bind_cycle_forecast_into(state,observed,evaluated))return reference_step(state,observed);
    const bool actual_empty_payout=observed.frame.own.actor.bag==0 && observed_payout(state,observed);
    if(observed_bank(state,observed) || actual_empty_payout ||
       (state.active && (state.bank_seen || state.empty_liquidation))) {
        observed.solo={};observed.crew=nullptr;observed.assigned_group=-1;return reference_step(state,observed);
    }
    return std::nullopt;
}
// Replacing travel/clear work is transactional. In particular, a missing quote,
// stale inputs, unsafe forecast or retired destination cannot charge retirement
// to the old project. Only a bound quote which actually admits the new project
// commits both states. The original input also survives every failed trial.
inline std::optional<StepProposal> choose_forecast_replacing_into(ActorState& state,
    ReferenceInput& observed, const forecast::EvaluatedQuote* evaluated) {
    if (observed.frame.valid() && observed.safe_now &&
        observed.frame.own.actor.channel != Channel::none &&
        observed.frame.own.actor.channel_valid) return reference_step(state, observed);
    if (!evaluated || observed.assigned_group < 0 ||
        evaluated->result().certificate.group != observed.assigned_group) return std::nullopt;
    if (!state.active || state.project.group == observed.assigned_group)
        return choose_forecast_into(state, observed, *evaluated);
    if (!admission_eligible(state, observed.assigned_group, observed.frame.clock.round))
        return std::nullopt;
    auto trial = state;
    auto bound = observed;
    trial.retire(Failure::none);
    if (!bind_cycle_forecast_into(trial, bound, *evaluated)) return std::nullopt;
    const auto action = reference_step(trial, bound);
    if (!trial.active || trial.project.group != observed.assigned_group) return std::nullopt;
    state = trial;
    observed = bound;
    return action;
}
inline std::optional<StepProposal> choose_crew_into(ActorState& state,
    ReferenceInput& observed, const forecast::EvaluatedCrewQuote& evaluated) {
    if (observed.frame.valid() && observed.safe_now &&
        observed.frame.own.actor.channel != Channel::none &&
        observed.frame.own.actor.channel_valid) return reference_step(state, observed);
    if (bind_crew_forecast_into(state, observed, evaluated)) return reference_step(state, observed);
    const bool actual_empty_payout = observed.frame.own.actor.bag == 0 && observed_payout(state, observed);
    if (observed_bank(state, observed) || actual_empty_payout ||
        (state.active && (state.bank_seen || state.empty_liquidation))) {
        observed.solo = {}; observed.crew = nullptr; observed.assigned_group = -1;
        return reference_step(state, observed);
    }
    return std::nullopt;
}
// A crew replacement has the same transactional ownership boundary as a solo
// quote. Failure never retires the incumbent or publishes partial authority.
inline std::optional<StepProposal> choose_crew_replacing_into(ActorState& state,
    ReferenceInput& observed, const forecast::EvaluatedCrewQuote* evaluated) {
    if (observed.frame.valid() && observed.safe_now &&
        observed.frame.own.actor.channel != Channel::none &&
        observed.frame.own.actor.channel_valid) return reference_step(state, observed);
    if (!evaluated || observed.assigned_group < 0 ||
        evaluated->result().schedule.target_id != observed.assigned_group) return std::nullopt;
    if (!state.active || state.project.group == observed.assigned_group)
        return choose_crew_into(state, observed, *evaluated);
    if (!admission_eligible(state, observed.assigned_group, observed.frame.clock.round))
        return std::nullopt;
    auto trial = state;
    auto bound = observed;
    trial.retire(Failure::none);
    if (!bind_crew_forecast_into(trial, bound, *evaluated)) return std::nullopt;
    const auto action = reference_step(trial, bound);
    if (!trial.active || trial.project.group != observed.assigned_group) return std::nullopt;
    state = trial;
    observed = bound;
    return action;
}
} // namespace p6::reference
