#pragma once
#include "allocation.h"
#include "trace.h"
#include "../../common/src/policy.h"

namespace p8::banded_cells {
using common::Arm; using common::Rejection; using common::Offer;
using common::Decision; using common::Admission; using common::evaluate;
using common::participant; using common::context_matches;
struct Proposal {
    p6::Observation<int> named_group{};
    p6::Fact<int> actual_route_distance{};
};
struct Inputs:common::Inputs {
    Assignments assignments{};
    p6::Fact<std::uint64_t> control_route{};
    std::array<Proposal,2> proposals{};
};
inline bool crew_legal(const Inputs& in,const Offer& offer) {
    const auto& plan=offer.plan;
    if (!participant(plan,in.frame.clock.self)) return false;
    for (int i=0;i<plan.crew_count;++i) {
        const auto* actor=in.frame.visible(plan.crew[i]); const auto* safe=common::certificate(offer,plan.crew[i]);
        if (!actor || !roster_actor(*actor,in.draft) || !actor->alive || actor->channel!=p6::Channel::none ||
            !safe || safe->group!=plan.group || !safe->valid(in.frame,actor->id,plan.group)) return false;
        if (plan.organization==Organization::H && actor->id!=plan.collector && cell_actor(*actor,in.draft)) return false;
    }
    const auto* collector=in.frame.visible(plan.collector);
    if (!collector) return false;
    if (plan.organization==Organization::U || plan.organization==Organization::G)
        return plan.crew_count==1 && plan.collector==in.frame.clock.self &&
            independent_safety(*common::certificate(offer,in.frame.clock.self));
    if (plan.organization==Organization::H) return plan.crew_count>=2 && cell_actor(*collector,in.draft);
    if (plan.organization!=Organization::J || plan.crew_count!=3 || collector->pick_ordinal!=0 ||
        !public_fixture(in.control_route.source) || !in.control_route.value || in.control_route.value!=joint_route_hash(plan)) return false;
    for (int ordinal=0;ordinal<3;++ordinal) if (!participant(plan,in.draft.actors[ordinal])) return false;
    return true;
}
inline int proposal_distance(const Inputs& in,const Offer& offer) {
    if (offer.plan.organization!=Organization::H || cell_actor(in.frame.own.actor,in.draft)) return -1;
    const auto* collector=in.frame.visible(offer.plan.collector);
    const int index=crew_index(offer.plan,offer.plan.collector);
    if (!collector || index<0 || !(collector->position==offer.plan.pads[index])) return -1;
    for (const auto& proposal:in.proposals)
        if (proposal.named_group.current(in.frame.clock,offer.plan.collector) &&
            proposal.named_group.value==offer.plan.group && public_fixture(proposal.actual_route_distance.source) &&
            proposal.actual_route_distance.value==offer.plan.ingress.count-1 &&
            offer.plan.ingress.tiles[0]==in.frame.own.actor.position) return proposal.actual_route_distance.value;
    return -1;
}
inline bool own_component(const Inputs& in,const EvidenceLedger& ledger) {
    return common::slot_current(in,ledger,in.component_slot,in.component_entry) &&
        ledger.component_qualified(in.component_slot,in.component_entry,in.frame.clock.round);
}
inline bool own_organization(const Inputs& in,const EvidenceLedger& ledger) {
    const auto* slot=ledger.state(in.supported_slot);
    return slot && slot->declaration.require_supported_work &&
        common::slot_current(in,ledger,in.supported_slot,in.supported_entry) &&
        ledger.organization_qualified(in.supported_slot,in.supported_entry,in.frame.clock.round);
}
inline Decision choose(const Inputs& in,const EvidenceLedger& ledger,const ProbeBudget&,Arm arm) {
    Decision out; out.clock=in.frame.clock;
    if (!common::valid_inputs(in)) { out.reason=Rejection::malformed; return out; }
    if (!in.assignments.valid(in.draft)) return out;
    const bool leader=cell_actor(in.frame.own.actor,in.draft);
    const auto* assigned=in.assignments.for_actor(in.frame.clock.self);
    const bool component=own_component(in,ledger), qualified=own_organization(in,ledger);
    int nearest=-1;
    for (int i=0;i<in.offer_count;++i) {
        const auto& offer=in.offers[i]; const auto& plan=offer.plan;
        Rejection reject=common::legal_offer(in,offer);
        if (reject==Rejection::none && !crew_legal(in,offer)) reject=Rejection::unsafe;
        if (reject==Rejection::none && (offer.capital_cost!=0 || !common::package_funded(in,offer))) reject=Rejection::resources;
        // This bounded ordinary-circuit implementation never invents a future
        // H continuation for a diagnostic. Unsupported diagnostics stay out.
        if (reject==Rejection::none && (offer.diagnostic_segment || offer.marked_extra_turns!=0)) reject=Rejection::evidence;
        bool record=false;
        if (reject==Rejection::none && arm==Arm::active) {
            if (plan.organization==Organization::J) reject=Rejection::evidence;
            else if (leader) {
                if (!assigned || !assigned_circuit(*assigned,plan)) reject=Rejection::evidence;
                else if (plan.organization==Organization::H) {
                    const auto* slot=ledger.state(in.supported_slot);
                    if (!component || offer.context_slot!=in.supported_slot || offer.entry!=in.supported_entry ||
                        !slot || slot->declaration.kind!=EpisodeKind::full_organization || !slot->declaration.require_supported_work ||
                        !context_matches(in,offer) || !route_component_matches(in.component_entry,in.supported_entry) ||
                        !common::slot_current(in,ledger,in.supported_slot,in.supported_entry) ||
                        ledger.suspended(in.supported_slot,in.frame.clock.round/project_rounds) ||
                        (!qualified && ledger.campaign_exhausted(in.supported_slot))) reject=Rejection::evidence;
                    else record=true;
                } else if (!component) {
                    record=offer.context_slot==in.component_slot && offer.entry==in.component_entry && context_matches(in,offer) &&
                        !ledger.suspended(in.component_slot,in.frame.clock.round/project_rounds) &&
                        !ledger.campaign_exhausted(in.component_slot) && in.frame.own.actor.bag==0;
                    // The incumbent remains ordinary production after a local
                    // suspension; it neither resets evidence nor commands peers.
                    if (!record && i!=in.independent) reject=Rejection::evidence;
                }
            } else if (plan.organization==Organization::H && proposal_distance(in,offer)<0) reject=Rejection::evidence;
        } else if (reject==Rejection::none && plan.organization==Organization::H) reject=Rejection::evidence;
        if (reject==Rejection::none && plan.organization==Organization::H &&
            p6::compare(evaluate(in,offer.schedule),evaluate(in,in.offers[in.independent].schedule))<=0) reject=Rejection::no_gain;
        if (reject==Rejection::none && record && in.frame.own.actor.health<minimum_trial_health_fraction*in.frame.own.actor.max_health)
            reject=Rejection::unsafe;
        out.rejected[i]=reject;
        if (reject!=Rejection::none) continue;
        const int distance=proposal_distance(in,offer);
        const auto score=evaluate(in,offer.schedule);
        const bool helper_h=!leader && arm==Arm::active && plan.organization==Organization::H;
        if (out.option<0 || (helper_h && (nearest<0 || distance<nearest ||
            (distance==nearest && plan.group<in.offers[out.option].plan.group))) ||
            (nearest<0 && p6::compare(score,out.score)>0)) {
            out.option=i; out.organization=plan.organization; out.reason=Rejection::none; out.score=score;
            out.record_episode=record; nearest=helper_h?distance:-1;
        }
    }
    return out;
}
inline Decision decide(const Inputs& in,EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm) {
    common::refresh_evidence(in,ledger); return choose(in,ledger,probes,arm);
}
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,
                        EvidenceLedger& ledger,ProbeBudget& probes,Arm arm,Work& work) {
    Admission out;
    if (!local.fresh() || !(local.frame.clock==in.frame.clock) || !(proposed.clock==in.frame.clock) || work.active) {
        out.rejection=Rejection::stale; return out;
    }
    const auto now=decide(in,ledger,probes,arm);
    if (now.option<0 || now.option!=proposed.option || now.record_episode!=proposed.record_episode) return out;
    const auto& offer=in.offers[now.option];
    if (!physical_admissible(offer.plan,local)) { out.rejection=Rejection::unsafe; return out; }
    if (now.record_episode) {
        const auto* slot=ledger.state(offer.context_slot); if (!slot) return out;
        Premises premises{offer.entry,in.own_state.value,all_premises};
        if (offer.plan.organization==Organization::H && !slot->campaign_started &&
            !ledger.begin_organization_campaign(in.component_slot,offer.context_slot,in.frame.clock.round,{premises,true,true,true})) return out;
        EpisodeRequest request; request.context=offer.context_slot; request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+std::min(forecast_window,offer.plan.remaining_rounds);
        request.nominal_duration=offer.plan.forecast_rounds; request.action=int(offer.plan.organization); request.premises=premises;
        out.episode=ledger.issue(request); if (!out.episode) return out;
    }
    out.started=work.start(offer.plan,local); out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if (!out.started && out.episode) ledger.invalidate_active(in.frame.clock.round);
    return out;
}
inline Command advance(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,Arm arm,Work& work) {
    if (!common::valid_inputs(in) || !(in.frame.clock==local.frame.clock) ||
        in.frame.own.actor.health!=local.frame.own.actor.health || in.frame.own.actor.bag!=local.frame.own.actor.bag) {
        work.request_stop(); if (ledger.active() && local.fresh()) ledger.invalidate_active(local.frame.clock.round);
        return work.step(local);
    }
    common::refresh_evidence(in,ledger);
    if (ledger.active() && (!local.frame.own.actor.alive || local.fuel_resumed)) ledger.invalidate_active(local.frame.clock.round);
    if (arm==Arm::active && work.active && work.plan.organization==Organization::H && work.plan.collector==in.frame.clock.self) {
        const auto* supported=ledger.state(in.supported_slot);
        const bool continuing=ledger.active() && ledger.active()->context==in.supported_slot;
        if (!own_component(in,ledger) || !supported || ledger.suspended(in.supported_slot,in.frame.clock.round/project_rounds) ||
            (!continuing && ledger.campaign_exhausted(in.supported_slot) && !own_organization(in,ledger))) work.request_stop();
    }
    if (arm==Arm::active && work.active && work.plan.organization==Organization::H && work.plan.collector!=in.frame.clock.self) {
        const auto* holder=local.certificate(work.plan.collector,work.plan.group);
        if (!holder || !local.frame.visible(work.plan.collector)) work.request_stop();
    }
    const auto command=work.step(local);
    if (ledger.active() && !work.active && !work.cycle_completed) ledger.invalidate_active(local.frame.clock.round);
    return command;
}
} // namespace p8::banded_cells
