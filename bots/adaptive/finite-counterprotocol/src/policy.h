#pragma once
#include "convoy.h"
namespace p8::finite_counterprotocol {
inline bool layout_current(const Inputs& in,const Protocol& protocol) {
    const auto& frozen=protocol.layout(); const auto& site=in.layout.site; const auto& fixed=frozen.site;
    return protocol.declared()&&in.layout.valid(in.draft)&&
        plan_route_hash(in.layout.collector)==plan_route_hash(frozen.collector)&&
        plan_route_hash(in.layout.helper)==plan_route_hash(frozen.helper)&&
        plan_route_hash(in.layout.short_solo)==plan_route_hash(frozen.short_solo)&&
        plan_route_hash(in.layout.helper_after)==plan_route_hash(frozen.helper_after)&&
        in.layout.escort_pad==frozen.escort_pad&&in.layout.escort_wait_bound==frozen.escort_wait_bound&&
        site.opportunity==fixed.opportunity&&site.altar==fixed.altar&&site.width==fixed.width&&site.height==fixed.height&&
        site.altar_tile==fixed.altar_tile&&site.target==fixed.target&&site.own_approaches==fixed.own_approaches&&
        site.approach_count==fixed.approach_count&&site.map_identity.value==fixed.map_identity.value&&site.map_identity.source==fixed.map_identity.source;
}
inline void refresh(const Inputs& in,Protocol& protocol,EvidenceLedger& ledger) {
    if (!common::valid_inputs(in)||!protocol.declared()||!collector_actor(in.frame.own.actor,in.draft)) return;
    const auto& key=protocol.organization().key; Premises now{key,in.own_state.value,all_premises};
    now.key.settings_id=in.settings_identity.value; now.key.actor_id=in.frame.clock.self;
    now.key.champion=int(in.frame.own.actor.type); now.key.threat_regime=in.threat_regime.value;
    const bool executing=ledger.active()&&ledger.active()->context==protocol.slot();
    if (!executing) { now.key.build={now.state.equipment_hash,now.state.max_health,now.state.strength,now.state.focus,now.state.growth};
        now.key.hp_band=health_band(now.state.health,now.state.max_health); }
    for (int i=0;i<key.participant_count;++i) if (!in.frame.visible(key.participants[i])) now.personally_observed&=~membership_known;
    const auto app=ledger.observe_context(protocol.slot(),now,in.frame.clock.round);
    if (app==Applicability::changed) protocol.responses.interrupted(in.frame.clock.round);
}
inline Rejection base(const Inputs& in,const Offer& offer) {
    auto reject=common::legal_offer(in,offer); if (reject!=Rejection::none) return reject;
    if (!common::crew_legal(in,offer,false)) return Rejection::unsafe;
    if (offer.capital_cost||!common::package_funded(in,offer)) return Rejection::resources;
    if (offer.diagnostic_segment||offer.marked_extra_turns!=0) return Rejection::probe_value;
    if (offer.plan.organization!=Organization::U&&offer.plan.organization!=Organization::G&&offer.plan.organization!=Organization::H) return Rejection::evidence;
    return Rejection::none;
}
inline Decision choose(const Inputs& in,const Protocol& protocol,const EvidenceLedger& ledger,const ProbeBudget&,Arm arm) {
    Decision out; out.clock=in.frame.clock;
    if (!common::valid_inputs(in)||!in.layout.valid(in.draft)) { out.reason=Rejection::malformed; return out; }
    const bool owner=collector_actor(in.frame.own.actor,in.draft),helper=in.frame.own.actor.id==in.draft.actors[1];
    bool own_after_available=false;
    if (helper&&protocol.helper.phase==HelperPhase::own_income) for (int i=0;i<in.offer_count;++i)
        own_after_available|=in.offers[i].plan.organization==Organization::U&&plan_route_hash(in.offers[i].plan)==plan_route_hash(in.layout.helper_after)&&base(in,in.offers[i])==Rejection::none;
    for (int i=0;i<in.offer_count;++i) {
        const auto& o=in.offers[i]; auto reject=base(in,o); bool record=false;
        if (reject==Rejection::none&&o.plan.organization==Organization::H) {
            if (arm==Arm::control||(!owner&&!helper)||o.plan.collector!=in.draft.actors[0]||o.plan.crew!=in.layout.collector.crew||
                in.frame.own.actor.bag!=0) reject=Rejection::evidence;
            else if (owner) {
                const auto* slot=ledger.state(protocol.slot()); const bool full=slot&&ledger.organization_qualified(protocol.slot(),o.entry,in.frame.clock.round);
                if (!layout_current(in,protocol)||!protocol.responses.approach_current(in.frame.clock.self,in.frame.clock.round)||
                    o.context_slot!=protocol.slot()||o.entry!=protocol.organization().key||!common::context_matches(in,o)||
                    !common::slot_current(in,ledger,protocol.slot(),o.entry)||ledger.active()||!slot||slot->latest_terminal>=in.frame.clock.round||
                    ledger.suspended(protocol.slot(),in.frame.clock.round/common::project_rounds)||(ledger.campaign_exhausted(protocol.slot())&&!full)) reject=Rejection::evidence;
                else {
                    const auto end=common::probe_terminal_round(in,o,ledger);
                    if (end<0||(slot->latest_positive>=0&&end-slot->latest_positive>=qualification_rounds)) reject=Rejection::evidence;
                    else record=true;
                }
            } else if (protocol.helper.phase!=HelperPhase::independent||plan_route_hash(o.plan)!=plan_route_hash(in.layout.helper)||!common::helper_accepts(in,o)) reject=Rejection::evidence;
            if (reject==Rejection::none&&p6::compare(common::evaluate(in,o.schedule),common::evaluate(in,in.offers[in.independent].schedule))<=0) reject=Rejection::no_gain;
        } else if (reject==Rejection::none&&own_after_available&&plan_route_hash(o.plan)!=plan_route_hash(in.layout.helper_after)) reject=Rejection::evidence;
        if (reject==Rejection::none&&record&&in.frame.own.actor.health<common::minimum_trial_health_fraction*in.frame.own.actor.max_health) reject=Rejection::unsafe;
        out.rejected[i]=reject; if (reject!=Rejection::none) continue;
        const auto score=common::evaluate(in,o.schedule);
        if (out.option<0||p6::compare(score,out.score)>0) { out.option=i; out.organization=o.plan.organization; out.score=score; out.reason=Rejection::none; out.record_episode=record; }
    }
    return out;
}
inline Decision decide(const Inputs& in,Protocol& protocol,EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm) {
    refresh(in,protocol,ledger); return finite_counterprotocol::choose(in,protocol,ledger,probes,arm);
}
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,Protocol& protocol,EvidenceLedger& ledger,
                        const ProbeBudget& probes,Arm arm,Work& work,ConvoyTrace& trace) {
    Admission out;
    if (!local.fresh()||!(local.frame.clock==in.frame.clock)||!(proposed.clock==in.frame.clock)||work.active) { out.rejection=Rejection::stale; return out; }
    const auto now=decide(in,protocol,ledger,probes,arm);
    if (now.option<0||now.option!=proposed.option||now.record_episode!=proposed.record_episode) return out;
    const auto& o=in.offers[now.option]; if (!physical_admissible(o.plan,local)) { out.rejection=Rejection::unsafe; return out; }
    if (now.record_episode) {
        const Premises entry{o.entry,in.own_state.value,all_premises}; const auto* slot=ledger.state(protocol.slot()); if (!slot) return out;
        if (!slot->campaign_started&&!ledger.begin_initial_campaign(protocol.slot(),in.frame.clock.round,{entry,true,true,true})) return out;
        EpisodeRequest request; request.context=protocol.slot(); request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+std::min(common::forecast_window,o.plan.remaining_rounds); request.nominal_duration=o.plan.forecast_rounds;
        request.action=int(Organization::H); request.premises=entry; out.episode=ledger.issue(request); if (!out.episode) return out;
    }
    out.started=work.start(o.plan,local); out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if (out.episode&&(!out.started||!trace.begin(ledger,work,in.layout))) { ledger.invalidate_active(in.frame.clock.round); work.request_stop(); out.started=false; }
    if (out.started) protocol.helper.begin(work,in.layout,in.frame.clock.self);
    return out;
}
inline Command advance(const Inputs& in,const LocalFrame& local,Protocol& protocol,EvidenceLedger& ledger,Arm arm,Work& work) {
    if (!common::valid_inputs(in)||!local.fresh()||!(local.frame.clock==in.frame.clock)||local.fuel_resumed) {
        work.request_stop(); protocol.responses.interrupted(local.frame.clock.round);
        if (ledger.active()&&local.fresh()) ledger.invalidate_active(local.frame.clock.round);
    } else {
        refresh(in,protocol,ledger);
        if (arm==Arm::active&&work.active&&work.plan.organization==Organization::H&&collector_actor(in.frame.own.actor,in.draft)&&
            (!protocol.responses.approach_current(in.frame.clock.self,in.frame.clock.round)||ledger.suspended(protocol.slot(),in.frame.clock.round/common::project_rounds))) work.request_stop();
    }
    const auto prepared=protocol.helper.prepare(in,local,work); const auto command=work.step(prepared);
    if (ledger.active()&&!local.legal.value.safe_now) {
        Observation unsafe; unsafe.kind=ObservationKind::safety_failure; unsafe.round=local.frame.clock.round;
        unsafe.premises={ledger.active()->premises.key,in.own_state.value,all_premises}; ledger.resolve(ledger.active()->serial,unsafe);
    } else if (ledger.active()&&!work.active&&!work.cycle_completed) ledger.invalidate_active(local.frame.clock.round);
    return command;
}
inline Resolution observe(const Inputs& in,const LocalFrame& local,Protocol& protocol,EvidenceLedger& ledger,const Work& work,ConvoyTrace& trace) {
    protocol.observe_passive(in,local,work); protocol.helper.observe(in,local,work); trace.observe(local,work);
    if (!trace.active||(work.active&&ledger.active())) return {};
    const auto* e=ledger.active(); Premises terminal;
    if (e) { terminal={e->premises.key,in.own_state.value,all_premises};
        for (int i=0;i<terminal.key.participant_count;++i) if (!in.frame.visible(terminal.key.participants[i])) terminal.personally_observed&=~membership_known;
        if (!work.route_witness_active) terminal.personally_observed&=~route_known; }
    return trace.finish(ledger,in.frame.clock.round,terminal);
}
} // namespace p8::finite_counterprotocol
