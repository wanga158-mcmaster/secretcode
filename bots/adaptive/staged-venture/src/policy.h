#pragma once
#include "package.h"
#include "../../earned-center/src/trace.h"
namespace p8::staged_venture {
using CycleTrace=earned_center::CycleTrace;
inline bool crew_legal(const Inputs& in,const Offer& offer) {
    const auto& p=offer.plan;
    if(!common::participant(p,in.frame.clock.self))return false;
    for(int i=0;i<p.crew_count;++i) {
        const auto* actor=in.frame.visible(p.crew[i]);const auto* safe=common::certificate(offer,p.crew[i]);
        if(!actor||!roster_actor(*actor,in.draft)||!actor->alive||actor->channel!=p6::Channel::none||
            !safe||safe->group!=p.group||!safe->valid(in.frame,actor->id,p.group))return false;
    }
    if(p.organization==Organization::U||p.organization==Organization::G)
        return p.crew_count==1&&p.collector==in.frame.clock.self&&
            independent_safety(*common::certificate(offer,in.frame.clock.self));
    return p.organization==Organization::H&&p.crew_count==2&&p.collector==in.draft.actors[1]&&
        crew_index(p,in.draft.actors[0])>=0&&crew_index(p,in.draft.actors[1])>=0;
}
inline bool visible_adoption(const Inputs& in,const Package& p,const Offer& offer) {
    const int collector=in.draft.actors[1];const auto* actor=in.frame.visible(collector);
    const auto& visible=in.collector_state;const auto& expected=p.invested.key.build;
    return actor&&venture_actor(*actor,in.draft)&&common::helper_accepts(in,offer)&&
        visible.current(in.frame.clock,collector)&&valid_numeric_state(visible.value)&&
        visible.value.equipment_hash==expected.equipment_hash&&visible.value.max_health==expected.max_health&&
        visible.value.strength==expected.strength&&p.invested.envelope.contains(visible.value,expected)&&
        visible.value.growth==expected.growth&&actor->max_health==expected.max_health&&actor->strength==expected.strength;
}
inline Decision choose(const Inputs& in,const Venture& venture,const EvidenceLedger& ledger,Arm arm) {
    Decision out;out.clock=in.frame.clock;
    if(!common::valid_inputs(in)){out.reason=Rejection::malformed;return out;}
    const bool owner=venture_actor(in.frame.own.actor,in.draft);
    const bool package=package_valid(venture.package,in.draft);
    for(int i=0;i<in.offer_count;++i) {
        const auto& o=in.offers[i];auto reject=common::legal_offer(in,o);bool record=false;
        if(reject==Rejection::none&&!crew_legal(in,o))reject=Rejection::unsafe;
        if(reject==Rejection::none&&(o.capital_cost!=0||!common::package_funded(in,o)))reject=Rejection::resources;
        // This kernel uses ordinary productive trials. Diagnostics without a
        // represented value comparison cannot acquire a hidden extra turn.
        if(reject==Rejection::none&&(o.diagnostic_segment||o.marked_extra_turns!=0))reject=Rejection::evidence;
        if(reject==Rejection::none&&owner&&venture.phase==VenturePhase::purchase_pending)reject=Rejection::evidence;
        if(reject==Rejection::none&&o.plan.organization==Organization::H) {
            if(!package||o.plan.group!=venture.package.supported.group||
                (owner&&plan_return_hash(o.plan)!=venture.package.invested.key.return_route_hash)||
                p6::compare(common::evaluate(in,o.schedule),common::evaluate(in,in.offers[in.independent].schedule))<=0)
                reject=Rejection::no_gain;
            else if(owner) {
                if(!venture.ready(in,ledger,arm)||o.context_slot!=venture.organization||
                    o.entry!=venture.package.invested.key||!common::context_matches(in,o))reject=Rejection::evidence;
                else if(arm==Arm::active) {
                    const auto* slot=ledger.state(venture.organization);
                    record=slot&&!ledger.active()&&slot->latest_terminal<in.frame.clock.round&&
                        (!ledger.campaign_exhausted(venture.organization)||
                         ledger.organization_qualified(venture.organization,o.entry,in.frame.clock.round));
                    if(!record&&!ledger.organization_qualified(venture.organization,o.entry,in.frame.clock.round))
                        reject=Rejection::evidence;
                }
            } else if(!helper_actor(in.frame.own.actor,in.draft)||!visible_adoption(in,venture.package,o))reject=Rejection::evidence;
        }
        if(reject==Rejection::none&&owner&&arm==Arm::active&&!venture.paid&&package&&
            o.plan.organization==Organization::U&&o.context_slot==venture.component&&
            o.entry==venture.package.ordinary.key&&common::context_matches(in,o)) {
            const auto* slot=ledger.state(venture.component);
            record=slot&&!ledger.active()&&slot->latest_terminal<in.frame.clock.round&&in.frame.own.actor.bag==0&&
                !ledger.campaign_exhausted(venture.component)&&
                !ledger.suspended(venture.component,in.frame.clock.round/common::project_rounds);
        }
        if(reject==Rejection::none&&record&&in.frame.own.actor.health<
            common::minimum_trial_health_fraction*in.frame.own.actor.max_health)reject=Rejection::unsafe;
        out.rejected[i]=reject;if(reject!=Rejection::none)continue;
        const auto rank=common::evaluate(in,o.schedule);
        if(out.option<0||p6::compare(rank,out.score)>0) {
            out.option=i;out.organization=o.plan.organization;out.score=rank;out.reason=Rejection::none;
            out.record_episode=record;out.capital_allowed=owner&&venture.phase==VenturePhase::preliminary&&
                (arm==Arm::control||venture.component_current(in,ledger));
        }
    }
    return out;
}
inline Decision decide(Inputs& in,const Venture& venture,EvidenceLedger& ledger,Arm arm) {
    bind(in,venture);common::refresh_evidence(in,ledger);return choose(in,venture,ledger,arm);
}
inline Admission commit(Inputs& in,const LocalFrame& local,const Decision& proposed,Venture& venture,
                        EvidenceLedger& ledger,Arm arm,Work& work,CycleTrace& trace) {
    Admission out;
    if(!local.fresh()||local.frame.clock!=in.frame.clock||proposed.clock!=in.frame.clock||work.active) {
        out.rejection=Rejection::stale;return out;
    }
    const auto selected=decide(in,venture,ledger,arm);
    if(selected.option<0||selected.option!=proposed.option||selected.record_episode!=proposed.record_episode)return out;
    const auto& o=in.offers[selected.option];
    if(!physical_admissible(o.plan,local)){out.rejection=Rejection::unsafe;return out;}
    if(selected.record_episode) {
        EpisodeRequest request;request.context=o.context_slot;request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+std::min(forecast_window,o.plan.remaining_rounds);
        request.nominal_duration=o.plan.forecast_rounds;request.action=int(o.plan.organization);
        request.premises={o.entry,in.own_state.value,all_premises};out.episode=ledger.issue(request);
        if(!out.episode)return out;
    }
    out.started=work.start(o.plan,local);out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if(out.episode&&(!out.started||!trace.begin(ledger,work))) {
        ledger.invalidate_active(in.frame.clock.round);work.request_stop();out.started=false;
    }
    return out;
}
inline Command advance(Inputs& in,const LocalFrame& local,const Venture& venture,EvidenceLedger& ledger,Arm arm,Work& work) {
    bind(in,venture);
    if(!common::valid_inputs(in)||in.frame.clock!=local.frame.clock||local.fuel_resumed||
        in.frame.own.actor.health!=local.frame.own.actor.health||in.frame.own.actor.bag!=local.frame.own.actor.bag) {
        work.request_stop();if(ledger.active()&&local.fresh())ledger.invalidate_active(local.frame.clock.round);
        return work.step(local);
    }
    common::refresh_evidence(in,ledger);
    if(work.active&&work.plan.organization==Organization::H&&work.plan.collector==in.frame.clock.self&&
        !venture.ready(in,ledger,arm,true))work.request_stop();
    const auto command=work.step(local);
    if(ledger.active()&&!work.active&&!work.cycle_completed)ledger.invalidate_active(local.frame.clock.round);
    return command;
}
inline Resolution observe_cycle(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,
                                const Work& work,CycleTrace& trace) {
    trace.observe(local,work,in.spawn_shop_legal);
    if(!trace.active||(work.active&&ledger.active()))return {};
    const auto* episode=ledger.active();if(!episode)return trace.finish(ledger,in.frame.clock.round,{});
    Premises terminal{episode->premises.key,in.own_state.value,all_premises};
    for(int i=0;i<terminal.key.participant_count;++i)
        if(!in.frame.visible(terminal.key.participants[i]))terminal.personally_observed&=~membership_known;
    if(!work.route_witness_active)terminal.personally_observed&=~route_known;
    return trace.finish(ledger,in.frame.clock.round,terminal);
}
} // namespace p8::staged_venture
