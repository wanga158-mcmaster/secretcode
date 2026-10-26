#pragma once
#include "context.h"
#include "../../earned-center/src/trace.h"
namespace p8::underwritten_frontier {
using common::Arm; using common::Offer; using common::Rejection; using common::Admission;
using common::evaluate; using common::participant; using CycleTrace=earned_center::CycleTrace;
struct Decision:common::Decision { bool secure_inventory=false,generalist=false; };
struct Inputs:common::Inputs {
    int venture_alternative=-1,control_alternative=-1;
    p6::Observation<LoadedReturn> loaded_return{};
    // A current own bank/return plan starts at current position with singleton
    // ingress/capture legs. It creates no production or qualification episode.
    p6::Observation<Offer> existing_inventory{};
};
inline bool no_purchases(const Offer& offer) {
    if (offer.capital_cost!=0 || offer.schedule.actor_count<1 || offer.schedule.actor_count>p6::max_actors) return false;
    for (int a=0;a<offer.schedule.actor_count;++a) {
        if (offer.schedule.actors[a].event_count<1 || offer.schedule.actors[a].event_count>p6::max_actor_events) return false;
        for (int e=0;e<offer.schedule.actors[a].event_count;++e)
            if (offer.schedule.actors[a].events[e].kind==p6::EventKind::purchase) return false;
    }
    return true;
}
inline bool without_insurer(const Inputs& in,const Offer& offer) {
    if (offer.entry.participant_count<0 || offer.entry.participant_count>max_participants) return false;
    const int insurer=in.draft.actors[0];
    if (participant(offer.plan,insurer)) return false;
    for (int a=0;a<offer.schedule.actor_count;++a) if (offer.schedule.actors[a].actor_id==insurer) return false;
    for (int p=0;p<offer.schedule.production_count;++p)
        for (int h=0;h<offer.schedule.productions[p].holder_count;++h)
            if (offer.schedule.productions[p].holders[h]==insurer) return false;
    for (int c=0;c<offer.safety_count;++c) if (offer.safety[c].actor==insurer) return false;
    for (int m=0;m<offer.entry.participant_count;++m) if (offer.entry.participants[m]==insurer) return false;
    return true;
}
inline Rejection legal(const Inputs& in,const Offer& offer,int alternative=-1) {
    if (!plan_valid(offer.plan,in.frame.clock.self) || offer.schedule.target_id!=offer.plan.group ||
        offer.safety_count<1 || offer.safety_count>p6::max_actors ||
        offer.plan.remaining_rounds>in.settings.max_rounds-in.frame.clock.round) return Rejection::malformed;
    if (!no_purchases(offer)) return Rejection::resources;
    if (offer.diagnostic_segment || offer.marked_extra_turns!=0) return Rejection::evidence;
    if (alternative>=0 && (alternative>=in.offer_count || !p6::same_actors(offer.schedule,in.offers[alternative].schedule))) return Rejection::malformed;
    if (!common::resources_match(in,offer.schedule)) return Rejection::resources;
    for (int a=0;a<offer.schedule.actor_count;++a) {
        const int id=offer.schedule.actors[a].actor_id; const auto* safe=common::certificate(offer,id);
        if (!safe || !safe->valid(in.frame,id,safe->group)) return Rejection::unsafe;
    }
    return evaluate(in,offer.schedule).eligible()?Rejection::none:Rejection::malformed;
}
inline bool independent(const Inputs& in,const Offer& offer) {
    const auto* safe=common::certificate(offer,in.frame.clock.self);
    return (offer.plan.organization==Organization::U || offer.plan.organization==Organization::G) &&
        offer.plan.collector==in.frame.clock.self && offer.plan.crew_count==1 && offer.schedule.actor_count==1 &&
        offer.schedule.actors[0].actor_id==in.frame.clock.self && safe && safe->group==offer.plan.group && independent_safety(*safe);
}
inline bool crew(const Inputs& in,const Offer& offer,Arm arm) {
    const auto& plan=offer.plan;
    if (offer.schedule.actor_count!=plan.crew_count) return false;
    for (int a=0;a<offer.schedule.actor_count;++a)
        if (!participant(plan,offer.schedule.actors[a].actor_id)) return false;
    const auto* collector=in.frame.visible(plan.collector);
    if (!collector || !venture_actor(*collector,in.draft)) return false;
    if (!participant(plan,in.draft.actors[1]) || !participant(plan,in.draft.actors[2])) return false;
    if (plan.organization==Organization::H) {
        if (arm!=Arm::active || plan.crew_count<2 || plan.crew_count>3 || !without_insurer(in,offer)) return false;
        for (int i=0;i<plan.crew_count;++i) {
            const auto* actor=in.frame.visible(plan.crew[i]);
            if (!actor || !(venture_actor(*actor,in.draft) || actor->pick_ordinal==3)) return false;
        }
    } else if (plan.organization!=Organization::J || arm!=Arm::control || plan.crew_count!=3 ||
               !participant(plan,in.draft.actors[0])) return false;
    for (int i=0;i<plan.crew_count;++i) {
        const auto* actor=in.frame.visible(plan.crew[i]); const auto* safe=common::certificate(offer,plan.crew[i]);
        if (!actor || !roster_actor(*actor,in.draft) || !actor->alive || actor->channel!=p6::Channel::none ||
            actor->bag!=0 || !safe || safe->group!=plan.group || !safe->valid(in.frame,actor->id,plan.group)) return false;
    }
    return true;
}
inline bool component(const Inputs& in,const EvidenceLedger& ledger) {
    return common::slot_current(in,ledger,in.component_slot,in.component_entry) &&
        ledger.component_qualified(in.component_slot,in.component_entry,in.frame.clock.round);
}
inline bool organization(const Inputs& in,const EvidenceLedger& ledger) {
    const auto* slot=ledger.state(in.supported_slot);
    return slot && slot->declaration.require_supported_work && common::slot_current(in,ledger,in.supported_slot,in.supported_entry) &&
        ledger.organization_qualified(in.supported_slot,in.supported_entry,in.frame.clock.round);
}
inline bool own_loaded(const Inputs& in,const EvidenceLedger& ledger,const Plan& plan) {
    const auto* slot=ledger.state(in.supported_slot);
    return slot && loaded_return_valid(in,in.loaded_return,slot->declaration,plan);
}
inline bool bank_return_valid(const Inputs& in) {
    if (!in.existing_inventory.current(in.frame.clock,in.frame.clock.self)) return false;
    const auto& offer=in.existing_inventory.value; const auto& plan=offer.plan;
    return legal(in,offer)==Rejection::none && independent(in,offer) && plan.ingress.count==1 && plan.capture.count==1 &&
        plan.ingress.tiles[0]==in.frame.own.actor.position && plan.capture.tiles[0]==in.frame.own.actor.position;
}
inline Decision choose(const Inputs& in,const EvidenceLedger& ledger,const ProbeBudget&,Arm arm) {
    Decision out; out.clock=in.frame.clock;
    if (!common::valid_inputs(in)) { out.reason=Rejection::malformed; return out; }
    if (in.frame.own.actor.bag>0) {
        out.secure_inventory=bank_return_valid(in); out.reason=out.secure_inventory?Rejection::none:Rejection::resources; return out;
    }
    const auto* own=ledger.state(in.component_slot); const auto* supported=ledger.state(in.supported_slot);
    const bool retreat=arm==Arm::active && venture_actor(in.frame.own.actor,in.draft) &&
        ((own && ledger.suspended(in.component_slot,in.frame.clock.round/project_rounds)) ||
         (supported && ledger.suspended(in.supported_slot,in.frame.clock.round/project_rounds)));
    for (int i=0;i<in.offer_count;++i) {
        const auto& offer=in.offers[i]; const auto& plan=offer.plan; auto rejected=legal(in,offer);
        if (rejected==Rejection::none && !independent(in,offer)) rejected=Rejection::evidence;
        bool record=false;
        if (rejected==Rejection::none && !retreat && arm==Arm::active && venture_actor(in.frame.own.actor,in.draft) &&
            plan.group==in.target_group && !component(in,ledger)) {
            record=offer.context_slot==in.component_slot && offer.entry==in.component_entry && common::context_matches(in,offer) &&
                !ledger.suspended(in.component_slot,in.frame.clock.round/project_rounds) && !ledger.campaign_exhausted(in.component_slot);
        }
        out.rejected[i]=rejected;
        if (rejected!=Rejection::none) continue;
        const auto score=evaluate(in,offer.schedule);
        if (out.option<0 || p6::compare(score,out.score)>0) {
            out.option=i; out.organization=plan.organization; out.score=score; out.reason=Rejection::none; out.record_episode=record;
            out.generalist=retreat;
            if (out.generalist) out.organization=Organization::G;
        }
    }
    // The insured actor and fifth pick never enter the active venture menu.
    if (arm==Arm::active && (insurer_actor(in.frame.own.actor,in.draft) || in.frame.own.actor.pick_ordinal==4)) return out;
    int best_coalition=-1; p6::ScoreEvaluation coalition_rank;
    for (int i=0;i<in.offer_count;++i) {
        const auto& offer=in.offers[i]; const auto& plan=offer.plan;
        if (plan.organization!=(arm==Arm::active?Organization::H:Organization::J)) continue;
        const int alternative=arm==Arm::active?in.venture_alternative:in.control_alternative;
        Rejection rejected=alternative<0?Rejection::malformed:legal(in,offer,alternative);
        if (rejected==Rejection::none && (!crew(in,offer,arm) || legal(in,in.offers[alternative])!=Rejection::none)) rejected=Rejection::unsafe;
        if (rejected==Rejection::none && arm==Arm::active && !without_insurer(in,in.offers[alternative])) rejected=Rejection::evidence;
        if (rejected==Rejection::none && p6::compare(evaluate(in,offer.schedule),evaluate(in,in.offers[alternative].schedule))<=0) rejected=Rejection::no_gain;
        bool record=false;
        if (rejected==Rejection::none && arm==Arm::active) {
            if (plan.collector!=in.frame.clock.self) {
                if (!common::helper_accepts(in,offer)) rejected=Rejection::evidence;
            } else {
                const auto* slot=ledger.state(in.supported_slot);
                if (offer.context_slot!=in.supported_slot || offer.entry!=in.supported_entry ||
                    !slot || slot->declaration.kind!=EpisodeKind::full_organization || !slot->declaration.require_supported_work ||
                    !common::context_matches(in,offer) || !common::slot_current(in,ledger,in.supported_slot,in.supported_entry) ||
                    !own_loaded(in,ledger,plan) ||
                    ledger.suspended(in.supported_slot,in.frame.clock.round/project_rounds) ||
                    (!organization(in,ledger) && ledger.campaign_exhausted(in.supported_slot))) rejected=Rejection::evidence;
                else record=true;
            }
        }
        out.rejected[i]=rejected;
        if (rejected!=Rejection::none) continue;
        const auto score=evaluate(in,offer.schedule);
        if (best_coalition>=0 && p6::compare(score,coalition_rank)<=0) continue;
        best_coalition=i; coalition_rank=score;
        out.option=i; out.organization=plan.organization; out.score=score; out.reason=Rejection::none; out.record_episode=record; out.generalist=false;
    }
    return out;
}
inline Decision decide(const Inputs& in,EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm) {
    common::refresh_evidence(in,ledger); return choose(in,ledger,probes,arm);
}
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,EvidenceLedger& ledger,
                        ProbeBudget& probes,Arm arm,Work& work) {
    Admission result;
    if (!local.fresh() || !(local.frame.clock==in.frame.clock) || !(proposed.clock==in.frame.clock) || work.active) {
        result.rejection=Rejection::stale; return result;
    }
    const auto selected=decide(in,ledger,probes,arm);
    if (selected.option!=proposed.option || selected.secure_inventory!=proposed.secure_inventory || selected.generalist!=proposed.generalist) return result;
    const auto* offer=selected.secure_inventory?&in.existing_inventory.value:selected.option>=0?&in.offers[selected.option]:nullptr;
    if (!offer || !physical_admissible(offer->plan,local)) return result;
    if (selected.record_episode) {
        const auto* slot=ledger.state(offer->context_slot); if (!slot) return result;
        Premises premises{offer->entry,in.own_state.value,all_premises};
        if (offer->plan.organization==Organization::H && !slot->campaign_started &&
            !ledger.begin_initial_campaign(offer->context_slot,in.frame.clock.round,{premises,true,true,true})) return result;
        EpisodeRequest request; request.context=offer->context_slot; request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+std::min(forecast_window,offer->plan.remaining_rounds);
        request.nominal_duration=offer->plan.forecast_rounds; request.action=int(offer->plan.organization); request.premises=premises;
        result.episode=ledger.issue(request); if (!result.episode) return result;
    }
    Plan selected_plan=offer->plan;
    if (selected.generalist) selected_plan.organization=Organization::G;
    result.started=work.start(selected_plan,local); result.rejection=result.started?Rejection::none:Rejection::unsafe;
    if (result.started && selected.secure_inventory) work.enter(PhysicalStage::bank);
    if (!result.started && result.episode) ledger.invalidate_active(in.frame.clock.round);
    return result;
}
inline Command advance(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,Arm arm,Work& work) {
    if (!common::valid_inputs(in) || !(in.frame.clock==local.frame.clock) ||
        in.frame.own.actor.health!=local.frame.own.actor.health || in.frame.own.actor.bag!=local.frame.own.actor.bag) {
        work.request_stop(); if (ledger.active() && local.fresh()) ledger.invalidate_active(local.frame.clock.round); return work.step(local);
    }
    common::refresh_evidence(in,ledger);
    if (arm==Arm::active && work.active && work.plan.organization==Organization::H && work.plan.collector==in.frame.clock.self) {
        const auto* slot=ledger.state(in.supported_slot);
        const bool ongoing=ledger.active() && ledger.active()->context==in.supported_slot;
        // This card permits initial commissioning from current feasibility.
        // Its own full H campaign supplies status; U history is never a gate.
        // Expected carried Focus remains within that frozen H envelope.
        if (!slot || ledger.suspended(in.supported_slot,in.frame.clock.round/project_rounds) ||
            (!ongoing && ledger.campaign_exhausted(in.supported_slot) && !organization(in,ledger)) || !own_loaded(in,ledger,work.plan)) {
            work.request_stop();
            if (ledger.active()) ledger.invalidate_active(local.frame.clock.round);
        }
    }
    if (ledger.active() && (!local.frame.own.actor.alive || local.fuel_resumed)) ledger.invalidate_active(local.frame.clock.round);
    const auto command=work.step(local);
    if (ledger.active() && !work.active && !work.cycle_completed) ledger.invalidate_active(local.frame.clock.round);
    return command;
}
} // namespace p8::underwritten_frontier
