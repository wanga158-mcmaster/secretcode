#pragma once
#include "reserve.h"
#include "../../earned-center/src/trace.h"
namespace p8::requalification_reserve {
using CycleTrace=earned_center::CycleTrace;
inline int represented(const Reserve& reserve,const Offer& offer) {
    if (!reserve.declared()) return -1;
    for (int i=0;i<2;++i) if (offer.context_slot==reserve.slot(i)&&offer.entry==reserve.package().systems[i].context.key&&
        plan_route_hash(offer.plan)==plan_route_hash(reserve.package().systems[i].plan)) return i;
    return -1;
}
inline void refresh(const Inputs& in,Reserve& reserve,EvidenceLedger& ledger) {
    if (!reserve.declared()||!collector_actor(in.frame.own.actor,in.draft)||!common::valid_inputs(in)) return;
    reserve.synchronize(ledger,in.frame.clock.round);
    const int selected=reserve.selected(); const auto& key=reserve.package().systems[selected].context.key;
    Premises p{key,in.own_state.value,all_premises}; p.key.settings_id=in.settings_identity.value;
    p.key.actor_id=in.frame.clock.self; p.key.champion=int(in.frame.own.actor.type); p.key.threat_regime=in.threat_regime.value;
    const auto* active=ledger.active();
    if (!active||active->context!=reserve.slot(selected)) {
        p.key.build={p.state.equipment_hash,p.state.max_health,p.state.strength,p.state.focus,p.state.growth};
        p.key.hp_band=health_band(p.state.health,p.state.max_health);
    }
    for (int i=0;i<key.participant_count;++i) if (!in.frame.visible(key.participants[i])) p.personally_observed&=~membership_known;
    ledger.observe_context(reserve.slot(selected),p,in.frame.clock.round);
    reserve.synchronize(ledger,in.frame.clock.round);
}
inline Decision choose(const Inputs& in,const Reserve& reserve,const EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm) {
    Decision out; out.clock=in.frame.clock;
    if (!common::valid_inputs(in)) { out.reason=Rejection::malformed; return out; }
    const bool owner=collector_actor(in.frame.own.actor,in.draft);
    for (int i=0;i<in.offer_count;++i) {
        const auto& o=in.offers[i]; const auto& p=o.plan; auto reject=common::legal_offer(in,o); bool record=false;
        if (reject==Rejection::none&&!common::crew_legal(in,o,false)) reject=Rejection::unsafe;
        if (reject==Rejection::none&&(o.capital_cost||!common::package_funded(in,o))) reject=Rejection::resources;
        // Only ordinary productive trials are represented. This card does not
        // create a diagnostic detour or replenish the shared lifetime budget.
        if (reject==Rejection::none&&(o.diagnostic_segment||o.marked_extra_turns!=0))
            reject=probes.available(in.frame.clock.round/common::project_rounds)?Rejection::probe_value:Rejection::probe_limit;
        if (reject==Rejection::none&&p.organization==Organization::H) {
            if (!allowed_members(p,in.draft)) reject=Rejection::evidence;
            else if (p6::compare(common::evaluate(in,o.schedule),common::evaluate(in,in.offers[in.independent].schedule))<=0) reject=Rejection::no_gain;
            else if (!owner) {
                // An actor's visible adoption never reads the collector's
                // ledger. The two insulated actors cannot enter this crew.
                if (insulated_actor(in.frame.own.actor,in.draft)||!common::helper_accepts(in,o)) reject=Rejection::evidence;
            } else {
                const int which=represented(reserve,o); const auto* slot=which<0?nullptr:ledger.state(reserve.slot(which));
                if (!reserve.declared()||which<0||!slot||!common::context_matches(in,o)||!common::slot_current(in,ledger,reserve.slot(which),o.entry)) reject=Rejection::evidence;
                else if (arm==Arm::control) {
                    // This control retires the original coupled route for the
                    // match. A later clock or changed candidate cannot reset it.
                    if (reserve.retired()||which!=0) reject=Rejection::evidence;
                }
                if (reject==Rejection::none) {
                    const bool full=ledger.organization_qualified(reserve.slot(which),o.entry,in.frame.clock.round);
                    if ((arm==Arm::active&&(which!=reserve.selected()||(reserve.retired()&&!reserve.used())))||ledger.active()||
                        ledger.suspended(reserve.slot(which),in.frame.clock.round/common::project_rounds)||
                        (ledger.campaign_exhausted(reserve.slot(which))&&!full)||slot->latest_terminal>=in.frame.clock.round||
                        (!full&&in.frame.own.actor.bag!=0)) reject=Rejection::evidence;
                    else {
                        const auto terminal=common::probe_terminal_round(in,o,ledger);
                        if (slot->latest_positive>=0&&(terminal<0||terminal-slot->latest_positive>=qualification_rounds)) reject=Rejection::evidence;
                        else record=true;
                    }
                }
            }
        } else if (reject==Rejection::none&&p.organization!=Organization::U&&p.organization!=Organization::G) reject=Rejection::evidence;
        if (reject==Rejection::none&&record&&in.frame.own.actor.health<common::minimum_trial_health_fraction*in.frame.own.actor.max_health) reject=Rejection::unsafe;
        out.rejected[i]=reject; if (reject!=Rejection::none) continue;
        const auto rank=common::evaluate(in,o.schedule);
        if (out.option<0||p6::compare(rank,out.score)>0) {
            out.option=i; out.organization=p.organization; out.score=rank; out.reason=Rejection::none; out.record_episode=record;
        }
    }
    return out;
}
inline Decision decide(const Inputs& in,Reserve& reserve,EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm) {
    refresh(in,reserve,ledger); return requalification_reserve::choose(in,reserve,ledger,probes,arm);
}
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,Reserve& reserve,
                        EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm,Work& work,CycleTrace& trace) {
    Admission out;
    if (!local.fresh()||!(local.frame.clock==in.frame.clock)||!(proposed.clock==in.frame.clock)||work.active) { out.rejection=Rejection::stale; return out; }
    const auto now=requalification_reserve::decide(in,reserve,ledger,probes,arm);
    if (now.option<0||now.option!=proposed.option||now.record_episode!=proposed.record_episode) return out;
    const auto& o=in.offers[now.option];
    if (!physical_admissible(o.plan,local)) { out.rejection=Rejection::unsafe; return out; }
    if (now.record_episode) {
        const auto* slot=ledger.state(o.context_slot); if (!slot) return out;
        const Premises p{o.entry,in.own_state.value,all_premises};
        if (!slot->campaign_started&&!ledger.begin_initial_campaign(o.context_slot,in.frame.clock.round,{p,true,true,true})) return out;
        EpisodeRequest request; request.context=o.context_slot; request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+std::min(common::forecast_window,o.plan.remaining_rounds);
        request.nominal_duration=o.plan.forecast_rounds; request.action=int(o.plan.organization); request.premises=p;
        out.episode=ledger.issue(request); if (!out.episode) return out;
    }
    out.started=work.start(o.plan,local); out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if (out.episode&&(!out.started||!trace.begin(ledger,work))) {
        ledger.invalidate_active(in.frame.clock.round); work.request_stop(); out.started=false; reserve.synchronize(ledger,in.frame.clock.round);
    }
    return out;
}
inline Premises terminal(const Inputs& in,const Work& work,const Episode& episode) {
    Premises p{episode.premises.key,in.own_state.value,all_premises};
    for (int i=0;i<p.key.participant_count;++i) if (!in.frame.visible(p.key.participants[i])) p.personally_observed&=~membership_known;
    if (!work.route_witness_active) p.personally_observed&=~route_known;
    return p;
}
inline Command advance(const Inputs& in,const LocalFrame& local,Reserve& reserve,EvidenceLedger& ledger,Arm arm,Work& work) {
    if (!common::valid_inputs(in)||!(local.frame.clock==in.frame.clock)||!local.fresh()||local.fuel_resumed) {
        work.request_stop(); if (ledger.active()&&local.fresh()) ledger.invalidate_active(local.frame.clock.round);
    } else {
        refresh(in,reserve,ledger);
        if (arm==Arm::active&&work.active&&work.plan.organization==Organization::H&&collector_actor(in.frame.own.actor,in.draft)) {
            const int slot=reserve.slot(reserve.selected());
            if ((reserve.retired()&&!reserve.used())||ledger.suspended(slot,in.frame.clock.round/common::project_rounds)) work.request_stop();
        }
    }
    const auto command=work.step(local);
    if (ledger.active()&&!local.legal.value.safe_now) {
        Observation unsafe; unsafe.kind=ObservationKind::safety_failure; unsafe.round=local.frame.clock.round;
        unsafe.premises=terminal(in,work,*ledger.active()); ledger.resolve(ledger.active()->serial,unsafe);
    } else if (ledger.active()&&!work.active&&!work.cycle_completed) ledger.invalidate_active(local.frame.clock.round);
    reserve.synchronize(ledger,local.frame.clock.round); return command;
}
inline Resolution observe_cycle(const Inputs& in,const LocalFrame& local,Reserve& reserve,EvidenceLedger& ledger,const Work& work,CycleTrace& trace) {
    reserve.observe_route(local,work); trace.observe(local,work,in.market.spawn_shop_legal);
    if (!trace.active||(work.active&&ledger.active())) return {};
    const auto* e=ledger.active(); const auto p=e?terminal(in,work,*e):Premises{};
    const auto result=trace.finish(ledger,in.frame.clock.round,p); reserve.synchronize(ledger,in.frame.clock.round); return result;
}
} // namespace p8::requalification_reserve
