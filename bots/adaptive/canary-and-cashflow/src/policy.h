#pragma once
#include "profile.h"
#include "../../common/src/policy.h"
#include <utility>
namespace p8::canary {
using namespace p8::common;
struct OptionAdmission {
    Rejection rejection = Rejection::none;
    bool trial = false, record_episode = false;
    p6::ScoreEvaluation score{};
};
inline OptionAdmission base_admission(const Inputs& in, const EvidenceLedger& ledger,
                                     Arm arm, int option, bool component, bool supported) {
    OptionAdmission out;
    const auto& offer = in.offers[option];
    const int now = in.frame.clock.round;
    const bool self_canary = canary_actor(in.frame.own.actor,in.picks);
    const bool frontier = offer.plan.group == in.target_group;
    const bool joint = offer.plan.organization == Organization::H || offer.plan.organization == Organization::J;
    auto& rejection = out.rejection;
    rejection = legal_offer(in,offer,&out.score);
    if (rejection == Rejection::none && !crew_legal(in,offer)) rejection = Rejection::unsafe;
    if (rejection == Rejection::none && offer.marked_extra_turns < 0) rejection = Rejection::malformed;
    if (rejection == Rejection::none && joint && (!frontier ||
        (arm == Arm::active && offer.plan.organization != Organization::H) ||
        (arm == Arm::control && offer.plan.organization != Organization::J))) rejection = Rejection::evidence;
    if (rejection == Rejection::none && frontier && !package_funded(in,offer)) rejection = Rejection::resources;
    if (rejection == Rejection::none && joint) {
        const auto baseline = evaluate(in,in.offers[in.independent].schedule);
        if (p6::compare(out.score,baseline) <= 0) rejection = Rejection::no_gain;
        if (!package_funded(in,offer)) rejection = Rejection::resources;
        if (!self_canary && !helper_accepts(in,offer)) rejection = Rejection::evidence;
    }
#if P8_ORDINARY_COMPLETE_CYCLE
    // A long ordinary circuit earns its own payout but never an adaptation
    // episode, diagnostic permission or a commissioning attempt.
    if(ordinary_long_cycle(offer.plan)) {
        out.trial=false;out.record_episode=false;return out;
    }
#endif
    if (rejection == Rejection::none && arm == Arm::active && frontier) {
        if (self_canary) {
            if (offer.plan.organization == Organization::H) {
                if (!component || !context_matches(in,offer) ||
                    offer.context_slot != in.supported_slot || offer.entry != in.supported_entry ||
                    ledger.suspended(in.supported_slot,now / project_rounds)) rejection = Rejection::evidence;
                else {
                    const auto* slot = ledger.state(in.supported_slot);
                    const bool can_issue = slot && !ledger.active() && slot->latest_terminal < now;
                    out.trial = !supported;
                    out.record_episode = can_issue && (supported || !ledger.campaign_exhausted(in.supported_slot));
                    if (out.trial && !out.record_episode) rejection = Rejection::evidence;
                }
            } else if (offer.plan.organization == Organization::J) rejection = Rejection::evidence;
            else {
                const auto* slot = ledger.state(in.component_slot);
                out.record_episode = slot && !ledger.active() && slot->latest_terminal < now &&
                    context_matches(in,offer) && offer.context_slot == in.component_slot &&
                    offer.entry == in.component_entry && !ledger.suspended(in.component_slot,now / project_rounds) &&
                    !ledger.campaign_exhausted(in.component_slot) && in.frame.own.actor.bag == 0;
                out.trial = out.record_episode;
                // Exhausted or inapplicable evidence never forbids safe productive U.
                // It does forbid disguising a new diagnostic as that fallback.
                if (!out.record_episode && (offer.diagnostic_segment || offer.marked_extra_turns > 0))
                    rejection = Rejection::evidence;
            }
            if (out.trial && in.frame.own.actor.health <
                    minimum_trial_health_fraction * in.frame.own.actor.max_health) rejection = Rejection::unsafe;
            if (!component && offer.capital_cost != 0) rejection = Rejection::evidence;
        } else if (joint && (offer.plan.organization != Organization::H || !helper_accepts(in,offer)))
            rejection = Rejection::evidence;
    }
    if (rejection == Rejection::none && arm == Arm::active &&
        offer.capital_cost > 0 && (!self_canary || !component)) rejection = Rejection::evidence;
    return out;
}
inline Decision choose(const Inputs& in, const EvidenceLedger& ledger,
                       const ProbeBudget& budget, Arm arm) {
    Decision out; out.clock = in.frame.clock;
    if (!valid_inputs(in)) { out.reason = Rejection::malformed; return out; }
    const bool self_canary = canary_actor(in.frame.own.actor, in.picks);
    const int now = in.frame.clock.round;
    const bool component = slot_current(in,ledger,in.component_slot,in.component_entry) &&
        ledger.component_qualified(in.component_slot, in.component_entry, now);
    const bool supported = slot_current(in,ledger,in.supported_slot,in.supported_entry) &&
        ledger.organization_qualified(in.supported_slot, in.supported_entry, now);
    std::array<OptionAdmission,max_options> admissions{};
    for (int i = 0; i < in.offer_count; ++i) {
        admissions[i] = base_admission(in,ledger,arm,i,component,supported);
        out.rejected[i] = admissions[i].rejection;
    }
    // Only these freshly admitted, choose-local scores may supply a rank.
    int ordinary=-1;
    for(int i=0;i<in.offer_count;++i) {
        const auto& offer=in.offers[i];
        if(offer.diagnostic_segment||offer.marked_extra_turns!=0||out.rejected[i]!=Rejection::none)continue;
        if(ordinary<0||p6::compare(admissions[i].score,admissions[ordinary].score)>0)ordinary=i;
    }
    for (int i = 0; i < in.offer_count; ++i) {
        const auto& offer = in.offers[i];
        auto rejection = admissions[i].rejection;
        const bool deliberate = offer.diagnostic_segment || offer.marked_extra_turns != 0 ||
            (admissions[i].trial && i != ordinary);
        if (rejection == Rejection::none && deliberate &&
            (!admissions[i].trial || !admissions[i].record_episode)) rejection = Rejection::evidence;
        if (rejection == Rejection::none && deliberate && !probe_useful(in,offer,ledger,budget,i,ordinary))
            rejection = budget.available(now / project_rounds) ? Rejection::probe_value : Rejection::probe_limit;
        out.rejected[i] = rejection;
        if (rejection != Rejection::none) continue;
        const auto& rank=admissions[i].score;
        if (out.option < 0 || p6::compare(rank, out.score) > 0) {
            out.option = i; out.score = rank; out.reason = Rejection::none;
            out.organization = offer.plan.organization; out.deliberate_probe = deliberate;
            out.capital_allowed = self_canary && (arm == Arm::control || component);
            out.record_episode = admissions[i].record_episode;
        }
    }
    return out;
}

inline Decision decide(const Inputs& in, EvidenceLedger& ledger,
                       const ProbeBudget& probes, Arm arm) {
    refresh_evidence(in,ledger);
    return choose(in,ledger,probes,arm);
}
// Issue a private episode BEFORE any proposed action can execute, then spend
// an admitted probe permanently. Revalidate after every round/revision change.
inline Admission commit(const Inputs& in, const LocalFrame& local, const Decision& proposal,
                        EvidenceLedger& ledger, ProbeBudget& probes, Arm arm, Work& work) {
    Admission out;
    if (!local.fresh() || !(local.frame.clock == in.frame.clock) ||
        !(proposal.clock == in.frame.clock) || work.active) { out.rejection = Rejection::stale; return out; }
    const auto current = decide(in,ledger,probes,arm);
    if (current.option < 0 || proposal.option != current.option ||
        proposal.deliberate_probe != current.deliberate_probe ||
        proposal.record_episode != current.record_episode) return out;
    const auto& offer = in.offers[current.option];
    if (!physical_admissible(offer.plan,local)) { out.rejection = Rejection::unsafe; return out; }
    if (current.record_episode) {
        Premises premises{offer.entry,in.own_state.value,all_premises};
        if (offer.plan.organization == Organization::H) {
            const auto* slot = ledger.state(offer.context_slot);
            if (!slot) return out;
            if (!slot->campaign_started && !ledger.begin_organization_campaign(in.component_slot,
                    offer.context_slot,in.frame.clock.round,{premises,true,true,true})) return out;
        }
        EpisodeRequest request;
        request.context = offer.context_slot; request.issue_round = in.frame.clock.round;
        request.deadline = request.issue_round + std::min(forecast_window,offer.plan.remaining_rounds);
        request.nominal_duration = offer.plan.forecast_rounds;
        request.action = static_cast<int>(offer.plan.organization); request.premises = premises;
        out.episode = ledger.issue(request);
        if (!out.episode) return out;
    }
    if (current.deliberate_probe && !probes.spend(in.frame.clock.round / project_rounds)) {
        if (out.episode) ledger.invalidate_active(in.frame.clock.round);
        out.rejection = Rejection::probe_limit; return out;
    }
    out.started = work.start(offer.plan,local);
    out.rejection = out.started ? Rejection::none : Rejection::unsafe;
    if (!out.started && out.episode) ledger.invalidate_active(in.frame.clock.round);
    return out;
}
// Read-only private admission: choose once, then obtain an actual current
// admission-only observation. No external proposal supplies policy authority.
// Only a sole unmarked own U with positive ingress can use this entry point.
// The callback observes only; Work consumption/execution belongs to the caller.
struct CurrentAdmission {
template<class ObserveLocal>
static Admission run(const Inputs& in,EvidenceLedger& ledger,ProbeBudget& probes,
                     Arm arm,Work& work,ObserveLocal&& observe_local) {
    Admission out;
    if(work.active){out.rejection=Rejection::stale;return out;}
    if(in.offer_count!=1||in.independent!=0){out.rejection=Rejection::malformed;return out;}
    const auto current=decide(in,ledger,probes,arm);
    if(current.option<0)return out;
    if(current.option!=0){out.rejection=Rejection::malformed;return out;}
    const auto& offer=in.offers[current.option];
    if(offer.diagnostic_segment||offer.marked_extra_turns!=0||current.deliberate_probe) {
        out.rejection=Rejection::evidence;return out;
    }
    if(offer.plan.organization!=Organization::U||offer.plan.crew_count!=1||
        offer.plan.collector!=in.frame.clock.self||offer.plan.crew[0]!=in.frame.clock.self||
        offer.plan.ingress.count<2) {
        out.rejection=Rejection::malformed;return out;
    }
    const auto& local=std::forward<ObserveLocal>(observe_local)(current);
    if(!local.admission_only||!local.fresh()||!(local.frame.clock==in.frame.clock)||
        !(current.clock==in.frame.clock)||work.active) {
        out.rejection=Rejection::stale;return out;
    }
    if(!physical_admissible(offer.plan,local)){out.rejection=Rejection::unsafe;return out;}
    if(current.record_episode) {
        Premises premises{offer.entry,in.own_state.value,all_premises};
        if(offer.plan.organization==Organization::H) {
            const auto* slot=ledger.state(offer.context_slot);
            if(!slot)return out;
            if(!slot->campaign_started&&!ledger.begin_organization_campaign(in.component_slot,
                    offer.context_slot,in.frame.clock.round,{premises,true,true,true}))return out;
        }
        EpisodeRequest request;
        request.context=offer.context_slot;request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+std::min(forecast_window,offer.plan.remaining_rounds);
        request.nominal_duration=offer.plan.forecast_rounds;
        request.action=static_cast<int>(offer.plan.organization);request.premises=premises;
        out.episode=ledger.issue(request);
        if(!out.episode)return out;
    }
    if(current.deliberate_probe&&!probes.spend(in.frame.clock.round/project_rounds)) {
        if(out.episode)ledger.invalidate_active(in.frame.clock.round);
        out.rejection=Rejection::probe_limit;return out;
    }
    out.started=work.start_after_physical(offer.plan,local);
    out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if(!out.started&&out.episode)ledger.invalidate_active(in.frame.clock.round);
    return out;
}
};
template<class ObserveLocal>
Admission admit_current(const Inputs& in,EvidenceLedger& ledger,ProbeBudget& probes,
                        Arm arm,Work& work,ObserveLocal&& observe_local) {
    return CurrentAdmission::run(in,ledger,probes,arm,work,std::forward<ObserveLocal>(observe_local));
}
inline Command advance(const Inputs& in, const LocalFrame& local, EvidenceLedger& ledger,
                       Arm arm, Work& work) {
    if (!valid_inputs(in) || !(in.frame.clock == local.frame.clock) ||
        in.frame.own.actor.health != local.frame.own.actor.health ||
        in.frame.own.actor.bag != local.frame.own.actor.bag) {
        work.request_stop();
        if (ledger.active() && local.fresh()) ledger.invalidate_active(local.frame.clock.round);
        return work.step(local);
    }
    refresh_evidence(in,ledger);
    if (ledger.active() && (!local.frame.own.actor.alive || local.fuel_resumed))
        ledger.invalidate_active(local.frame.clock.round);
    if (arm == Arm::active && work.active && work.plan.organization == Organization::H &&
        work.plan.collector == in.frame.clock.self) {
        const auto* slot = ledger.state(in.supported_slot);
        const bool current_component =
            ledger.component_qualified(in.component_slot,in.component_entry,in.frame.clock.round);
        const bool continuing_trial = ledger.active() && ledger.active()->context == in.supported_slot;
        const bool commissioning = slot && slot->campaign_started &&
            (!ledger.campaign_exhausted(in.supported_slot) || continuing_trial) &&
            !ledger.suspended(in.supported_slot,in.frame.clock.round / project_rounds);
        const bool qualified = ledger.organization_qualified(in.supported_slot,
            in.supported_entry,in.frame.clock.round);
        if (!current_component || (!commissioning && !qualified)) work.request_stop();
    }
    const auto command = work.step(local);
    if (ledger.active() && !work.active && !work.cycle_completed)
        ledger.invalidate_active(local.frame.clock.round);
    return command;
}
} // namespace p8::canary
