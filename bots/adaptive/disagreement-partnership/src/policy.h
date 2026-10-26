#pragma once
#include "models.h"
namespace p8::disagreement_partnership {
struct Admission:common::Admission { bool stage_issued=false; };
inline Rejection base(const Inputs& in,const Offer& offer) {
    auto reject=common::legal_offer(in,offer); if (reject!=Rejection::none) return reject;
    if (!crew_current(in,offer)) return Rejection::unsafe;
    if (offer.capital_cost||!common::package_funded(in,offer)) return Rejection::resources;
    if (offer.plan.organization!=Organization::U&&offer.plan.organization!=Organization::G&&offer.plan.organization!=Organization::H) return Rejection::evidence;
    return Rejection::none;
}
inline int assay_offer(const Inputs& in,const Partnership& p) {
    if (!p.declared()) return -1;
    for (int i=0;i<in.offer_count;++i) if (in.offers[i].plan.organization==Organization::U&&
        plan_route_hash(in.offers[i].plan)==p.spec().route.key.route_hash) return i;
    return -1;
}
inline bool stage_entry(const Inputs& in,const Partnership& p) {
    return p.declared()&&in.frame.own.actor.bag==0&&in.frame.own.actor.health>=common::minimum_trial_health_fraction*in.frame.own.actor.max_health&&
        common::guarded_key_current(in,p.spec().route.key)&&p.spec().route.envelope.contains(in.own_state.value,p.spec().route.key.build);
}
inline bool useful_probe(const Inputs& in,const Partnership& p,const EvidenceLedger& ledger,const ProbeBudget& probes,int option,int alternative) {
    if (!matching_models(in,p)||!disagreement(in)||option<0||alternative<0||in.probe.offer!=option||!in.probe.endpoint_distinguishes||
        !probes.available(in.frame.clock.round/common::project_rounds)||in.frame.own.actor.bag!=0||
        in.frame.own.actor.health<common::minimum_trial_health_fraction*in.frame.own.actor.max_health||
        p.spec().paid_bank_turn_bound<=0||in.offers[option].marked_extra_turns!=p.spec().paid_bank_turn_bound||
        in.probe.named_continuations[0]!=Organization::U||in.probe.named_continuations[1]!=Organization::H) return false;
    if (!common::slot_current(in,ledger,p.slot(),p.spec().organization.key)||ledger.active()||
        ledger.suspended(p.slot(),in.frame.clock.round/common::project_rounds)||ledger.campaign_exhausted(p.slot())) return false;
    const auto ordinary=common::evaluate(in,in.offers[alternative].schedule),prefix=common::evaluate(in,in.offers[option].schedule);
    if (!ordinary.eligible()||!prefix.eligible()) return false;
    for (int b=0;b<3;++b) {
        const auto& plan=in.probe.continuation_plans[b]; const auto& schedule=in.probe.branches[b];
        const auto score=common::evaluate(in,schedule);
        if (!p6::same_actors(schedule,in.offers[option].schedule)||!score.eligible()||score.productive_arrival>common::forecast_window||
            score.occupancy<prefix.occupancy||p6::compare(score,ordinary)<0||schedule.target_id!=plan.group||
            !plan_valid(plan,in.frame.clock.self)||(b==1?plan.organization!=Organization::H:
                (plan.organization!=Organization::U&&plan.organization!=Organization::G))) return false;
        Offer next=in.offers[option]; next.plan=plan; next.schedule=schedule; next.safety=in.probe.safety[b]; next.safety_count=in.probe.safety_count[b];
        if (base(in,next)!=Rejection::none) return false;
        if (b==1&&(plan_route_hash(plan)!=p.spec().organization.key.route_hash||plan.crew!=p.spec().supported.crew)) return false;
    }
    return true;
}
inline void refresh(const Inputs& in,Partnership& p,EvidenceLedger& ledger) {
    if (!common::valid_inputs(in)||!p.declared()||!investigator(in.frame.own.actor,in.draft)) return;
    const auto& key=p.spec().organization.key; Premises current{key,in.own_state.value,all_premises};
    current.key.settings_id=in.settings_identity.value; current.key.actor_id=in.frame.clock.self;
    current.key.champion=int(in.frame.own.actor.type); current.key.threat_regime=in.threat_regime.value;
    if (!ledger.active()) { current.key.build={current.state.equipment_hash,current.state.max_health,current.state.strength,current.state.focus,current.state.growth};
        current.key.hp_band=health_band(current.state.health,current.state.max_health); }
    for (int i=0;i<key.participant_count;++i) if (!in.frame.visible(key.participants[i])) current.personally_observed&=~membership_known;
    ledger.observe_context(p.slot(),current,in.frame.clock.round);
}
inline Decision choose(const Inputs& in,const Partnership& p,const EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm) {
    Decision out; out.clock=in.frame.clock;
    if (!common::valid_inputs(in)) { out.reason=Rejection::malformed; return out; }
    const bool owner=investigator(in.frame.own.actor,in.draft);
    const auto desired=organization(in,p,arm); const int stage=assay_offer(in,p);
    for (int i=0;i<in.offer_count;++i) {
        const auto& o=in.offers[i]; auto reject=base(in,o); bool record=false;
        if (reject==Rejection::none&&(o.diagnostic_segment||o.marked_extra_turns!=0)) reject=Rejection::probe_value;
        if (reject==Rejection::none&&o.plan.organization==Organization::H) {
            if (!owner) { if (!common::helper_accepts(in,o)) reject=Rejection::evidence; }
            else if (!p.declared()||desired!=Organization::H||o.context_slot!=p.slot()||o.entry!=p.spec().organization.key||
                !common::context_matches(in,o)||!common::slot_current(in,ledger,p.slot(),o.entry)||ledger.active()) reject=Rejection::evidence;
            else {
                const auto* slot=ledger.state(p.slot()); const bool full=ledger.organization_qualified(p.slot(),o.entry,in.frame.clock.round);
                if (!slot||slot->latest_terminal>=in.frame.clock.round||ledger.suspended(p.slot(),in.frame.clock.round/common::project_rounds)||
                    (ledger.campaign_exhausted(p.slot())&&!full)||(!full&&in.frame.own.actor.bag!=0)) reject=Rejection::evidence;
                else {
                    const auto terminal=common::probe_terminal_round(in,o,ledger);
                    if (terminal<0||(slot->latest_positive>=0&&terminal-slot->latest_positive>=qualification_rounds)||
                        (arm==Arm::active&&terminal-p.assay.completed_round>=qualification_rounds)) reject=Rejection::evidence;
                    else record=true;
                }
            }
            const int model=arm==Arm::control&&p.declared()?p.spec().nominal_model:p.assay.consistent[0]?0:1;
            const auto own=common::evaluate(in,o.schedule);
            if (reject==Rejection::none&&p.declared()&&(model<0||p6::compare(own,common::evaluate(in,in.models[model].independent))<=0)) reject=Rejection::no_gain;
        }
        out.rejected[i]=reject; if (reject!=Rejection::none) continue;
        const auto score=common::evaluate(in,o.schedule);
        if (out.option<0||p6::compare(score,out.score)>0) { out.option=i; out.organization=o.plan.organization; out.score=score; out.reason=Rejection::none; out.record_episode=record; }
    }
    if (arm==Arm::active&&owner&&p.declared()&&!p.assay.attempted&&stage>=0&&matching_models(in,p)&&disagreement(in)&&
        stage_entry(in,p)&&!ledger.active()&&base(in,in.offers[stage])==Rejection::none) {
        const bool ordinary=out.option==stage&&!in.offers[stage].diagnostic_segment&&in.offers[stage].marked_extra_turns==0;
        if (ordinary||useful_probe(in,p,ledger,probes,stage,out.option)) {
            out.option=stage; out.organization=Organization::U; out.score=common::evaluate(in,in.offers[stage].schedule); out.reason=Rejection::none;
            out.deliberate_probe=!ordinary; out.record_episode=false; out.rejected[stage]=Rejection::none;
        }
    }
    return out;
}
inline Decision decide(const Inputs& in,Partnership& p,EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm) {
    refresh(in,p,ledger); return disagreement_partnership::choose(in,p,ledger,probes,arm);
}
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,Partnership& p,EvidenceLedger& ledger,
                        ProbeBudget& probes,Arm arm,Work& work,CycleTrace& trace) {
    Admission out;
    if (!local.fresh()||!(local.frame.clock==in.frame.clock)||!(proposed.clock==in.frame.clock)||work.active) { out.rejection=Rejection::stale; return out; }
    const auto now=decide(in,p,ledger,probes,arm);
    if (now.option<0||now.option!=proposed.option||now.deliberate_probe!=proposed.deliberate_probe||now.record_episode!=proposed.record_episode) return out;
    const auto& o=in.offers[now.option]; if (!physical_admissible(o.plan,local)) { out.rejection=Rejection::unsafe; return out; }
    if (now.deliberate_probe&&!probes.spend(in.frame.clock.round/common::project_rounds)) { out.rejection=Rejection::probe_limit; return out; }
    if (arm==Arm::active&&investigator(in.frame.own.actor,in.draft)&&!p.assay.attempted&&stage_entry(in,p)&&!ledger.active()&&
        now.option==assay_offer(in,p)&&matching_models(in,p)&&disagreement(in)) {
        out.stage_issued=p.assay.issue(p.spec(),in,now.deliberate_probe); if (!out.stage_issued) return out;
    }
    // The control may passively observe the identical bank-route stage of its
    // immediate organization. It keeps the result but uses the nominal model.
    if (arm==Arm::control&&investigator(in.frame.own.actor,in.draft)&&!p.assay.attempted&&stage_entry(in,p)&&!ledger.active()&&
        plan_route_hash(o.plan)==p.spec().route.key.route_hash&&matching_models(in,p)&&disagreement(in)) {
        out.stage_issued=p.assay.issue(p.spec(),in,false); if (!out.stage_issued) return out;
    }
    if (now.record_episode) {
        const auto* slot=ledger.state(p.slot()); if (!slot) return out; const Premises entry{o.entry,in.own_state.value,all_premises};
        if (!slot->campaign_started&&!ledger.begin_initial_campaign(p.slot(),in.frame.clock.round,{entry,true,true,true})) {
            if (out.stage_issued) p.assay.fail();
            return out;
        }
        EpisodeRequest request; request.context=p.slot(); request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+std::min(common::forecast_window,o.plan.remaining_rounds); request.nominal_duration=o.plan.forecast_rounds;
        request.action=int(Organization::H); request.premises=entry; out.episode=ledger.issue(request);
        if (!out.episode) { if (out.stage_issued) p.assay.fail(); return out; }
    }
    out.started=work.start(o.plan,local); out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if (!out.started||(out.stage_issued&&!p.assay.attach(work))||(out.episode&&!trace.begin(ledger,work))) {
        if (out.stage_issued) p.assay.fail();
        if (out.episode) ledger.invalidate_active(in.frame.clock.round);
        work.request_stop(); out.started=false;
    }
    return out;
}
inline Command advance(const Inputs& in,const LocalFrame& local,Partnership& p,EvidenceLedger& ledger,Arm arm,Work& work) {
    if (!common::valid_inputs(in)||!(local.frame.clock==in.frame.clock)||!local.fresh()||local.fuel_resumed) {
        work.request_stop(); p.assay.fail(); if (ledger.active()&&local.fresh()) ledger.invalidate_active(local.frame.clock.round);
    } else {
        refresh(in,p,ledger);
        if (p.assay.deliberate&&p.assay.unavailable&&work.active&&plan_route_hash(work.plan)==p.spec().route.key.route_hash) work.request_stop();
        if (work.active&&work.plan.organization==Organization::H&&investigator(in.frame.own.actor,in.draft)&&
            (ledger.suspended(p.slot(),in.frame.clock.round/common::project_rounds)||
             (arm==Arm::active&&p.assay.completed_round>=0&&in.frame.clock.round-p.assay.completed_round>=qualification_rounds))) work.request_stop();
    }
    const auto command=work.step(local);
    if (ledger.active()&&!local.legal.value.safe_now) {
        Observation unsafe; unsafe.kind=ObservationKind::safety_failure; unsafe.round=local.frame.clock.round;
        unsafe.premises={ledger.active()->premises.key,in.own_state.value,all_premises}; ledger.resolve(ledger.active()->serial,unsafe);
    } else if (ledger.active()&&!work.active&&!work.cycle_completed) ledger.invalidate_active(local.frame.clock.round);
    return command;
}
inline Resolution observe(const Inputs& in,const LocalFrame& local,const Command& command,Partnership& p,EvidenceLedger& ledger,
                          ProbeBudget& probes,const Work& work,CycleTrace& trace) {
    p.assay.observe(in,local,work,command,probes); trace.observe(local,work,{});
    if (!trace.active||(work.active&&ledger.active())) return {};
    const auto* e=ledger.active(); Premises terminal;
    if (e) { terminal={e->premises.key,in.own_state.value,all_premises};
        for (int i=0;i<terminal.key.participant_count;++i) if (!in.frame.visible(terminal.key.participants[i])) terminal.personally_observed&=~membership_known;
        if (!work.route_witness_active) terminal.personally_observed&=~route_known; }
    return trace.finish(ledger,in.frame.clock.round,terminal);
}
} // namespace p8::disagreement_partnership
