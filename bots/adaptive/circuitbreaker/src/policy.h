#pragma once
#include "circuit.h"
#include "trace.h"
namespace p8::circuitbreaker {
inline int represented_system(const Circuit& c,const Offer& offer) {
    for(int i=0;i<2;++i)if(offer.context_slot==c.systems[i].slot&&
        offer.entry==c.package.systems[i].declaration.key&&
        plan_route_hash(offer.plan)==plan_route_hash(c.package.systems[i].plan))return i;
    return -1;
}
inline Rejection base_rejection(const Inputs& in,const Circuit& c,const Offer& o) {
    auto reject=common::legal_offer(in,o);if(reject!=Rejection::none)return reject;
    if(!common::crew_legal(in,o,false))return Rejection::unsafe;
    if(o.capital_cost!=0||!common::package_funded(in,o))return Rejection::resources;
    // This bounded kernel represents ordinary trials only. A marked detour
    // needs a complete probe/continuation builder, which this card does not add.
    if(o.diagnostic_segment||o.marked_extra_turns!=0)return Rejection::probe_value;
    if(o.plan.organization==Organization::U||o.plan.organization==Organization::G)return Rejection::none;
    if(o.plan.organization!=Organization::J)return Rejection::opportunity_unavailable;
    const int which=represented_system(c,o);
    if(which<0||!common::context_matches(in,o)||!members_current(in,c.package.systems[which]))return Rejection::evidence;
    if(o.plan.collector!=in.frame.clock.self&&!common::helper_accepts(in,o))return Rejection::evidence;
    return Rejection::none;
}
inline void refresh(Inputs& in,Circuit& c,EvidenceLedger& ledger) {
    bind(in,c);common::refresh_evidence(in,ledger);c.synchronize(ledger);
    if(!common::valid_inputs(in))return;
    for(int i=0;i<2;++i) {
        const auto* slot=ledger.state(c.systems[i].slot);if(!slot||!slot->campaign_started)continue;
        for(const auto& m:c.package.systems[i].members) {
            const auto* actor=in.frame.visible(m.actor);const auto* actual=member_state(in,m.actor);
            // Missing visibility is unknown. An actual different gear/base
            // regime permanently invalidates this private subsystem's proof.
            if(actor&&actual&&actual->current(in.frame.clock,m.actor)&&valid_numeric_state(actual->value)&&
                (actual->value.equipment_hash!=m.build.equipment_hash||
                 !m.envelope.contains(actual->value,m.build)))c.systems[i].material_changed=true;
        }
    }
}
inline Decision choose(const Inputs& in,const Circuit& c,const EvidenceLedger& ledger,Arm arm) {
    Decision out;out.clock=in.frame.clock;
    if(!common::valid_inputs(in)||!c.declared||!package_valid(c.package,in.draft)) {
        out.reason=Rejection::malformed;return out;
    }
    for(int i=0;i<in.offer_count;++i) {
        const auto& o=in.offers[i];auto reject=base_rejection(in,c,o);bool record=false;
        if(reject==Rejection::none&&o.plan.organization==Organization::J) {
            const int which=represented_system(c,o);const auto* slot=ledger.state(c.systems[which].slot);
            if(!c.bootstrapped)reject=Rejection::evidence;
            else if(arm==Arm::active) {
                if(which!=c.selected||!c.may_trial(ledger,which,in.frame.clock.round)||ledger.active()||
                    !slot||slot->latest_terminal>=in.frame.clock.round)reject=Rejection::evidence;
                else {
                    record=true;
                    // A new campaign always enters with no at-risk inventory.
                    if(!c.fresh_full(ledger,which,in.frame.clock.round)&&in.frame.own.actor.bag!=0)
                        reject=Rejection::resources;
                    const auto terminal=common::probe_terminal_round(in,o,ledger);
                    if(slot->latest_positive>=0&&(terminal<0||
                        terminal-slot->latest_positive>=qualification_rounds))reject=Rejection::evidence;
                }
            }
        }
        if(reject==Rejection::none&&o.plan.organization==Organization::U&&arm==Arm::active&&!c.bootstrapped&&
            o.context_slot==c.ordinary&&o.entry==c.package.ordinary.key&&common::context_matches(in,o)) {
            const auto* slot=ledger.state(c.ordinary);
            record=slot&&!ledger.active()&&slot->latest_terminal<in.frame.clock.round&&
                in.frame.own.actor.bag==0&&!ledger.campaign_exhausted(c.ordinary)&&
                !ledger.suspended(c.ordinary,in.frame.clock.round/common::project_rounds);
        }
        if(reject==Rejection::none&&record&&in.frame.own.actor.health<
            common::minimum_trial_health_fraction*in.frame.own.actor.max_health)reject=Rejection::unsafe;
        out.rejected[i]=reject;if(reject!=Rejection::none)continue;
        const auto rank=common::evaluate(in,o.schedule);
        if(out.option<0||p6::compare(rank,out.score)>0) {
            out.option=i;out.organization=o.plan.organization;out.score=rank;out.reason=Rejection::none;
            out.record_episode=record;
        }
    }
    return out;
}
inline Decision decide(Inputs& in,Circuit& c,EvidenceLedger& ledger,Arm arm) {
    refresh(in,c,ledger);return circuitbreaker::choose(in,c,ledger,arm);
}
inline bool authorize_requalification(Inputs& in,const LocalFrame& local,const Work& work,
                                     Circuit& c,EvidenceLedger& ledger,Arm arm) {
    if(arm!=Arm::active||!c.declared||c.selected!=0||c.requalification_used||!c.bootstrapped||
        work.active||ledger.active()||!local.fresh()||local.frame.clock!=in.frame.clock||
        !local.legal.value.safe_now||in.frame.own.actor.bag!=0||
        !in.actual_change.current(in.frame.clock,in.frame.clock.self))return false;
    refresh(in,c,ledger);
    const auto* old=ledger.state(c.systems[0].slot);
    if(!old||!old->campaign_started)return false;
    const auto& change=in.actual_change.value;const auto& before=c.package.systems[0];const auto& after=c.package.systems[1];
    if(change.previous!=before.declaration.key||change.current!=after.declaration.key||
        !members_current(in,after))return false;
    bool real=false;
    if(change.kind==ChangeKind::route)real=change.previous.route_hash!=change.current.route_hash&&
        change.inspected_route==change.current.route_hash;
    if(change.kind==ChangeKind::membership)real=change.previous.participants!=change.current.participants;
    if(change.kind==ChangeKind::equipment) {
        const auto* was=member(before,change.member);const auto* now=member(after,change.member);
        real=was&&now&&was->build.equipment_hash!=now->build.equipment_hash;
    }
    if(!real)return false;
    std::array<Rejection,common::max_options> admissible{};
    for(int i=0;i<in.offer_count;++i)admissible[i]=base_rejection(in,c,in.offers[i]);
    const int ordinary=common::ordinary_choice(in,admissible);
    if(ordinary<0||represented_system(c,in.offers[ordinary])!=1)return false;
    // Consumes the one lifetime permission even if subsequent physical start
    // becomes unavailable. It never clears the old failure ring or probe cap.
    c.selected=1;c.requalification_used=true;bind(in,c);return true;
}
inline Admission commit(Inputs& in,const LocalFrame& local,const Decision& proposed,Circuit& c,
                        EvidenceLedger& ledger,Arm arm,Work& work,JTrace& trace) {
    Admission out;
    if(!local.fresh()||local.frame.clock!=in.frame.clock||proposed.clock!=in.frame.clock||work.active) {
        out.rejection=Rejection::stale;return out;
    }
    const auto selected=circuitbreaker::decide(in,c,ledger,arm);
    if(selected.option<0||selected.option!=proposed.option||selected.record_episode!=proposed.record_episode)return out;
    const auto& o=in.offers[selected.option];
    if(!physical_admissible(o.plan,local)){out.rejection=Rejection::unsafe;return out;}
    if(selected.record_episode) {
        const int which=represented_system(c,o);
        if(o.plan.organization==Organization::J) {
            const auto* slot=ledger.state(o.context_slot);if(!slot)return out;
            if(!slot->campaign_started&&!ledger.begin_initial_campaign(o.context_slot,in.frame.clock.round,
                {{o.entry,in.own_state.value,all_premises},true,true,true}))return out;
        }
        EpisodeRequest request;request.context=o.context_slot;request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+std::min(forecast_window,o.plan.remaining_rounds);
        request.nominal_duration=o.plan.forecast_rounds;request.action=int(o.plan.organization);
        request.premises={o.entry,in.own_state.value,all_premises};
        const bool qualified=which>=0&&c.fresh_full(ledger,which,in.frame.clock.round);
        out.episode=ledger.issue(request);if(!out.episode)return out;
        if(which>=0&&!qualified)saturating_increment(c.systems[which].trials);
    }
    out.started=work.start(o.plan,local);out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if(out.episode&&(!out.started||!trace.begin(ledger,work))) {
        ledger.invalidate_active(in.frame.clock.round);work.request_stop();out.started=false;c.synchronize(ledger);
    }
    if(out.started)c.track_bootstrap(work);
    return out;
}
inline Premises terminal_premises(const Inputs& in,const Work& work,const Episode& episode) {
    Premises p{episode.premises.key,in.own_state.value,all_premises};
    for(int i=0;i<p.key.participant_count;++i)if(!in.frame.visible(p.key.participants[i]))p.personally_observed&=~membership_known;
    if(!work.route_witness_active)p.personally_observed&=~route_known;
    return p;
}
inline Command advance(Inputs& in,const LocalFrame& local,Circuit& c,EvidenceLedger& ledger,Arm arm,Work& work) {
    if(!common::valid_inputs(in)||in.frame.clock!=local.frame.clock||!local.fresh()||local.fuel_resumed||
        in.frame.own.actor.health!=local.frame.own.actor.health||in.frame.own.actor.bag!=local.frame.own.actor.bag) {
        work.request_stop();if(ledger.active()&&local.fresh())ledger.invalidate_active(local.frame.clock.round);
        c.synchronize(ledger);return work.step(local);
    }
    refresh(in,c,ledger);
    if(work.active&&work.plan.organization==Organization::J) {
        int which=-1;for(int i=0;i<2;++i)if(plan_route_hash(work.plan)==plan_route_hash(c.package.systems[i].plan))which=i;
        if(which<0)work.request_stop();
        else {
            const auto* slot=ledger.state(c.systems[which].slot);
            const bool expired=slot&&slot->previous_positive>=0&&
                !ledger.organization_qualified(c.systems[which].slot,c.package.systems[which].declaration.key,in.frame.clock.round);
            if((arm==Arm::active&&(c.systems[which].retired||c.systems[which].material_changed||expired))||
                !members_current(in,c.package.systems[which],true))work.request_stop();
        }
    }
    const auto command=work.step(local);
    if(ledger.active()&&!local.legal.value.safe_now) {
        Observation unsafe;unsafe.kind=ObservationKind::safety_failure;unsafe.round=local.frame.clock.round;
        unsafe.premises=terminal_premises(in,work,*ledger.active());ledger.resolve(ledger.active()->serial,unsafe);
    } else if(ledger.active()&&!work.active&&!work.cycle_completed)ledger.invalidate_active(local.frame.clock.round);
    c.synchronize(ledger);return command;
}
inline Resolution observe_cycle(const Inputs& in,const LocalFrame& local,Circuit& c,EvidenceLedger& ledger,
                                const Work& work,JTrace& trace) {
    c.observe_bootstrap(local,work,in.spawn_shop_legal);c.observe_independent(local,work);
    trace.observe(local,work,in.spawn_shop_legal);
    if(!trace.active||(work.active&&ledger.active()))return {};
    const auto* e=ledger.active();const auto p=e?terminal_premises(in,work,*e):Premises{};
    const auto resolution=trace.finish(ledger,in.frame.clock.round,p);c.synchronize(ledger);return resolution;
}
} // namespace p8::circuitbreaker
