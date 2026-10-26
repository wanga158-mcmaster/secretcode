#pragma once
#include "profile.h"
#include "trace.h"
#include "../../common/src/policy.h"
#include <utility>

namespace p8::earned_center {
using common::Arm; using common::Rejection; using common::Offer; using common::Probe;
using common::Decision; using common::Admission; using common::DiagnosticSegment;
using common::resources_match; using common::evaluate; using common::certificate;
using common::participant; using common::context_matches; using common::guarded_key_current;
struct Inputs : common::Inputs { p6::Observation<bool> spawn_shop_legal{}; };
inline bool center_component(const Inputs& in, const EvidenceLedger& ledger) {
    const auto* slot = ledger.state(in.component_slot);
    return slot && slot->declaration.require_spawn_shop &&
        common::slot_current(in,ledger,in.component_slot,in.component_entry) &&
        ledger.component_qualified(in.component_slot,in.component_entry,in.frame.clock.round);
}
inline bool center_qualified(const Inputs& in, const EvidenceLedger& ledger) {
    const auto* slot = ledger.state(in.supported_slot);
    return slot && slot->declaration.require_spawn_shop && slot->declaration.require_supported_work &&
        common::slot_current(in,ledger,in.supported_slot,in.supported_entry) &&
        ledger.organization_qualified(in.supported_slot,in.supported_entry,in.frame.clock.round);
}
inline bool purchase_gate(const Inputs& in, const EvidenceLedger& ledger, Arm arm) {
    if (!common::valid_inputs(in) || !collector_actor(in.frame.own.actor,in.draft) ||
        !in.frame.own.at_spawn || in.frame.own.actor.bag != 0 ||
        in.frame.own.actor.channel != p6::Channel::none ||
        !in.spawn_shop_legal.current(in.frame.clock,in.frame.clock.self) ||
        !in.spawn_shop_legal.value || ledger.active()) return false;
    if (arm == Arm::control) return true;
    const auto* h = ledger.state(in.supported_slot);
    // Default entry builds are exact: do not silently change gear between two
    // starts of the one unqualified commissioning campaign.
    return center_component(in,ledger) && (!h || !h->campaign_started || center_qualified(in,ledger));
}
struct OptionAdmission {
    Rejection rejection=Rejection::none;
    bool trial=false, observe=false;
};
inline OptionAdmission base_admission(const Inputs& in,const EvidenceLedger& ledger,Arm arm,int i) {
    const bool collector=collector_actor(in.frame.own.actor,in.draft);
    const bool component=center_component(in,ledger), qualified=center_qualified(in,ledger);
    const int epoch=in.frame.clock.round/project_rounds;
    const auto& o=in.offers[i]; Rejection reject=common::legal_offer(in,o);
    if (reject==Rejection::none && !common::crew_legal(in,o,false)) reject=Rejection::unsafe;
    const bool supported=o.plan.organization==Organization::H;
    const bool center=o.plan.group==in.target_group;
    bool trial=false;
    if (reject==Rejection::none && (o.plan.organization==Organization::J || (supported&&!center)))
        reject=Rejection::evidence;
    if (reject==Rejection::none && supported) {
        if (p6::compare(evaluate(in,o.schedule),evaluate(in,in.offers[in.independent].schedule))<=0)
            reject=Rejection::no_gain;
        if (!common::package_funded(in,o)) reject=Rejection::resources;
        if (!collector && !common::helper_accepts(in,o)) reject=Rejection::evidence;
    }
    if (reject==Rejection::none && arm==Arm::active && center && collector) {
        const auto* slot=ledger.state(o.context_slot);
        if (!slot || !slot->declaration.require_spawn_shop || !context_matches(in,o)) reject=Rejection::evidence;
        else if (supported) {
            if (!component || !slot->declaration.require_supported_work ||
                o.context_slot!=in.supported_slot || o.entry!=in.supported_entry ||
                !common::slot_current(in,ledger,in.supported_slot,o.entry) ||
                ledger.suspended(in.supported_slot,epoch)) reject=Rejection::evidence;
            else if (!qualified) {
                trial=true;
                if (ledger.campaign_exhausted(in.supported_slot)) reject=Rejection::evidence;
            }
        } else if (!component) {
            trial=true;
            if (o.context_slot!=in.component_slot || o.entry!=in.component_entry ||
                ledger.suspended(in.component_slot,epoch) || ledger.campaign_exhausted(in.component_slot) ||
                in.frame.own.actor.bag!=0) reject=Rejection::evidence;
        }
        if (!component && o.capital_cost>0) reject=Rejection::evidence;
    }
    if (reject==Rejection::none && trial && in.frame.own.actor.health <
        minimum_trial_health_fraction*in.frame.own.actor.max_health) reject=Rejection::unsafe;
    if (reject==Rejection::none && o.capital_cost>0 && !purchase_gate(in,ledger,arm)) reject=Rejection::evidence;
    bool observe=arm==Arm::active && collector && center && (supported || trial);
    // An exhausted/mismatched experiment cannot suppress ordinary incumbent
    // production. No episode or probe budget is restored by this fallback.
    if (i==in.independent && o.plan.organization==Organization::U && o.capital_cost==0 &&
        !o.diagnostic_segment && o.marked_extra_turns==0 && reject==Rejection::evidence &&
        common::legal_offer(in,o)==Rejection::none && common::crew_legal(in,o,false) &&
        common::package_funded(in,o)) {
        reject=Rejection::none; observe=trial=false;
    }
    return {reject,trial,observe};
}
inline Decision choose(const Inputs& in, const EvidenceLedger& ledger,
                       const ProbeBudget& probes, Arm arm) {
    Decision out; out.clock=in.frame.clock;
    if (!common::valid_inputs(in)) { out.reason=Rejection::malformed; return out; }
    std::array<OptionAdmission,max_options> admitted{};
    std::array<Rejection,max_options> legal{};
    for (int i=0;i<in.offer_count;++i) {
        admitted[i]=base_admission(in,ledger,arm,i); legal[i]=admitted[i].rejection;
    }
    const int ordinary=common::ordinary_choice(in,legal);
    const int epoch=in.frame.clock.round/project_rounds;
    for (int i=0;i<in.offer_count;++i) {
        const auto& o=in.offers[i]; auto reject=admitted[i].rejection;
        const bool deliberate=o.diagnostic_segment || o.marked_extra_turns!=0 ||
            (admitted[i].trial && i!=ordinary);
        if (reject==Rejection::none && deliberate && (!admitted[i].trial || !admitted[i].observe))
            reject=Rejection::evidence;
        if (reject==Rejection::none && deliberate &&
            !common::probe_useful(in,o,ledger,probes,i,ordinary,false))
            reject=probes.available(epoch)?Rejection::probe_value:Rejection::probe_limit;
        out.rejected[i]=reject;
        if (reject!=Rejection::none) continue;
        const auto score=evaluate(in,o.schedule);
        if (out.option<0 || p6::compare(score,out.score)>0) {
            out.option=i; out.organization=o.plan.organization; out.score=score; out.reason=Rejection::none;
            out.deliberate_probe=deliberate; out.record_episode=admitted[i].observe;
            out.capital_allowed=purchase_gate(in,ledger,arm);
        }
    }
    return out;
}
inline Decision decide(const Inputs& in, EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm) {
    common::refresh_evidence(in,ledger); return choose(in,ledger,probes,arm);
}
namespace detail {
inline Admission commit_tail(const Inputs& in,const LocalFrame& local,const Decision& now,
                             EvidenceLedger& ledger,ProbeBudget& probes,Arm arm,Work& work) {
    Admission out;
    const auto& o=in.offers[now.option];
    if (!physical_admissible(o.plan,local)) { out.rejection=Rejection::unsafe; return out; }
    if (arm==Arm::active && now.record_episode && collector_actor(in.frame.own.actor,in.draft) &&
        o.plan.group==in.target_group) {
        Premises p{o.entry,in.own_state.value,all_premises};
        const auto* slot=ledger.state(o.context_slot);
        if (!slot) return out;
        if (o.plan.organization==Organization::H && !slot->campaign_started &&
            !ledger.begin_organization_campaign(in.component_slot,o.context_slot,in.frame.clock.round,{p,true,true,true})) return out;
        EpisodeRequest r; r.context=o.context_slot; r.issue_round=in.frame.clock.round;
        r.deadline=r.issue_round+std::min(forecast_window,o.plan.remaining_rounds);
        r.nominal_duration=o.plan.forecast_rounds; r.action=int(o.plan.organization); r.premises=p;
        out.episode=ledger.issue(r); if (!out.episode) return out;
    }
    if (now.deliberate_probe && !probes.spend(in.frame.clock.round/project_rounds)) {
        if (out.episode) ledger.invalidate_active(in.frame.clock.round);
        out.rejection=Rejection::probe_limit; return out;
    }
    out.started=work.start(o.plan,local); out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if (!out.started && out.episode) ledger.invalidate_active(in.frame.clock.round);
    return out;
}
} // namespace detail
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,
                        EvidenceLedger& ledger,ProbeBudget& probes,Arm arm,Work& work) {
    Admission out;
    if (!local.fresh() || !(local.frame.clock==in.frame.clock) || !(proposed.clock==in.frame.clock) || work.active) {
        out.rejection=Rejection::stale; return out;
    }
    const auto now=decide(in,ledger,probes,arm);
    if (now.option<0 || now.option!=proposed.option || now.deliberate_probe!=proposed.deliberate_probe) return out;
    return detail::commit_tail(in,local,now,ledger,probes,arm,work);
}
template<class ObserveLocal>
inline Admission admit_current(const Inputs& in,EvidenceLedger& ledger,ProbeBudget& probes,
                               Arm arm,Work& work,ObserveLocal&& observe_local) {
    Admission out;
    if(work.active||ledger.active()) {out.rejection=Rejection::stale;return out;}
    if(in.offer_count!=1||in.independent!=0) {out.rejection=Rejection::malformed;return out;}
    const auto& offer=in.offers[0];
    if(offer.diagnostic_segment||offer.marked_extra_turns!=0) {
        out.rejection=Rejection::evidence;return out;
    }
    if(offer.plan.organization!=Organization::U||offer.plan.crew_count!=1||
        offer.plan.collector!=in.frame.clock.self||offer.plan.crew[0]!=in.frame.clock.self||
        offer.plan.ingress.count<2||offer.safety_count!=1||offer.schedule.actor_count!=1||
        offer.schedule.actors[0].actor_id!=in.frame.clock.self) {
        out.rejection=Rejection::malformed;return out;
    }
    const auto now=decide(in,ledger,probes,arm);
    if(now.option<0) {out.rejection=now.reason;return out;}
    if(now.option!=0||now.organization!=Organization::U) {out.rejection=Rejection::malformed;return out;}
    if(now.deliberate_probe) {out.rejection=Rejection::evidence;return out;}
    const auto& local=std::forward<ObserveLocal>(observe_local)(now);
    if(!local.admission_only||!local.fresh()||!(local.frame.clock==in.frame.clock)||
        !(now.clock==in.frame.clock)||work.active||ledger.active()) {
        out.rejection=Rejection::stale;return out;
    }
    return detail::commit_tail(in,local,now,ledger,probes,arm,work);
}
inline Command advance(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,Arm arm,Work& work) {
    if (!common::valid_inputs(in) || !(in.frame.clock==local.frame.clock) ||
        in.frame.own.actor.health!=local.frame.own.actor.health || in.frame.own.actor.bag!=local.frame.own.actor.bag) {
        work.request_stop(); if (ledger.active()&&local.fresh()) ledger.invalidate_active(local.frame.clock.round);
        return work.step(local);
    }
    common::refresh_evidence(in,ledger);
    if (ledger.active() && (!local.frame.own.actor.alive || local.fuel_resumed)) ledger.invalidate_active(local.frame.clock.round);
    if (arm==Arm::active && work.active && work.plan.organization==Organization::H &&
        work.plan.collector==in.frame.clock.self) {
        const auto* h=ledger.state(in.supported_slot);
        const bool continuing=ledger.active()&&ledger.active()->context==in.supported_slot;
        const bool allowed=ledger.component_qualified(in.component_slot,in.component_entry,in.frame.clock.round) && h &&
            !ledger.suspended(in.supported_slot,in.frame.clock.round/project_rounds) &&
            (continuing || !ledger.campaign_exhausted(in.supported_slot) ||
             ledger.organization_qualified(in.supported_slot,in.supported_entry,in.frame.clock.round));
        if (!allowed) work.request_stop();
    }
    const auto command=work.step(local);
    if (ledger.active()&&!work.active&&!work.cycle_completed) ledger.invalidate_active(local.frame.clock.round);
    return command;
}
} // namespace p8::earned_center
