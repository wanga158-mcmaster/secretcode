#pragma once
#include "layout.h"
#include "../../common/src/policy.h"
#include "../../reversible-workshop/src/transactions.h"
#include "../../earned-center/src/trace.h"
namespace p8::common_cause_firewall {
using common::Arm; using common::Offer; using common::Decision; using common::Rejection; using common::Admission;
using Inventory=reversible_workshop::Inventory;
using Market=reversible_workshop::Market;
using CycleTrace=earned_center::CycleTrace;
struct Inputs:reversible_workshop::Inputs {
    Layout layout{};
    p6::Observation<bool> field_shop_legal{};
};
inline bool own_component(const Inputs& in,const EvidenceLedger& ledger) {
    return common::slot_current(in,ledger,in.component_slot,in.component_entry) &&
        ledger.component_qualified(in.component_slot,in.component_entry,in.frame.clock.round);
}
inline bool own_organization(const Inputs& in,const EvidenceLedger& ledger) {
    return common::slot_current(in,ledger,in.supported_slot,in.supported_entry) &&
        ledger.organization_qualified(in.supported_slot,in.supported_entry,in.frame.clock.round);
}
inline bool crew_legal(const Inputs& in,const Offer& offer) {
    const auto& p=offer.plan;
    if (!common::participant(p,in.frame.clock.self)) return false;
    for (int i=0;i<p.crew_count;++i) {
        const auto* a=in.frame.visible(p.crew[i]); const auto* c=common::certificate(offer,p.crew[i]);
        if (!a||!roster_actor(*a,in.draft)||!a->alive||a->channel!=p6::Channel::none||!c||!c->valid(in.frame,a->id,p.group)) return false;
        if (p.organization==Organization::H&&field_actor(*a,in.draft)) return false;
    }
    if (p.organization==Organization::U||p.organization==Organization::G)
        return p.crew_count==1&&p.collector==in.frame.clock.self&&independent_safety(*common::certificate(offer,in.frame.clock.self));
    const auto* collector=in.frame.visible(p.collector);
    if (p.organization==Organization::H)
        return collector&&spawn_collector(*collector,in.draft)&&p.group==in.layout.spawn_supported.group&&
            common::participant(p,in.draft.actors[2])&&visits(p,in.layout.spawn_shop);
    return p.organization==Organization::J&&p.crew_count==3&&p.collector==in.draft.actors[0]&&
        uniform_dependency_hash(p)==uniform_dependency_hash(in.layout.uniform)&&p.crew==in.layout.uniform.crew&&p.pads==in.layout.uniform.pads;
}
inline Decision choose(const Inputs& in,const EvidenceLedger& ledger,const ProbeBudget&,Arm arm) {
    Decision out; out.clock=in.frame.clock;
    if (!reversible_workshop::own_market(in)||!in.layout.valid(in.draft)) { out.reason=Rejection::malformed; return out; }
    const bool field=field_actor(in.frame.own.actor,in.draft),collector=spawn_collector(in.frame.own.actor,in.draft);
    for (int i=0;i<in.offer_count;++i) {
        const auto& o=in.offers[i]; const auto& p=o.plan;
        auto reject=common::legal_offer(in,o); bool record=false;
        if (reject==Rejection::none&&!crew_legal(in,o)) reject=Rejection::unsafe;
        if (reject==Rejection::none&&(o.diagnostic_segment||o.marked_extra_turns!=0)) reject=Rejection::evidence;
        if (reject==Rejection::none&&(o.capital_cost!=0||!common::package_funded(in,o))) reject=Rejection::resources;
        if (reject==Rejection::none&&arm==Arm::active) {
            if (p.organization==Organization::J) reject=Rejection::evidence;
            else if (field) {
                if (p.organization!=Organization::U||p.group!=in.layout.field.group||plan_route_hash(p)!=plan_route_hash(in.layout.field)) reject=Rejection::evidence;
            } else if (collector) {
                if (p.group!=in.layout.spawn_component.group||plan_return_hash(p)!=plan_return_hash(in.layout.spawn_component)) reject=Rejection::evidence;
                else if (p.organization==Organization::H) {
                    const auto* s=ledger.state(in.supported_slot);
                    if (!own_component(in,ledger)||!s||!s->declaration.require_spawn_shop||!s->declaration.require_supported_work||
                        o.context_slot!=in.supported_slot||o.entry!=in.supported_entry||!common::context_matches(in,o)||
                        !route_component_matches(in.component_entry,in.supported_entry)||
                        !common::slot_current(in,ledger,in.supported_slot,in.supported_entry)||
                        ledger.suspended(in.supported_slot,in.frame.clock.round/common::project_rounds)||
                        (!own_organization(in,ledger)&&ledger.campaign_exhausted(in.supported_slot))) reject=Rejection::evidence;
                    else record=true;
                }
            } else if (p.organization==Organization::H&&!common::helper_accepts(in,o)) reject=Rejection::evidence;
            if (reject==Rejection::none&&producer(in.frame.own.actor,in.draft)&&p.organization==Organization::U&&
                !own_component(in,ledger)&&o.context_slot==in.component_slot&&o.entry==in.component_entry&&common::context_matches(in,o)&&
                !ledger.suspended(in.component_slot,in.frame.clock.round/common::project_rounds)&&!ledger.campaign_exhausted(in.component_slot)&&
                in.frame.own.actor.bag==0) record=true;
        } else if (reject==Rejection::none&&p.organization==Organization::H) reject=Rejection::evidence;
        if (reject==Rejection::none&&p.organization==Organization::H&&
            p6::compare(common::evaluate(in,o.schedule),common::evaluate(in,in.offers[in.independent].schedule))<=0) reject=Rejection::no_gain;
        if (reject==Rejection::none&&record&&in.frame.own.actor.health<common::minimum_trial_health_fraction*in.frame.own.actor.max_health) reject=Rejection::unsafe;
        out.rejected[i]=reject; if (reject!=Rejection::none) continue;
        const auto score=common::evaluate(in,o.schedule);
        if (out.option<0||p6::compare(score,out.score)>0) {
            out.option=i; out.organization=p.organization; out.reason=Rejection::none; out.record_episode=record; out.score=score;
        }
    }
    return out;
}
inline Decision decide(const Inputs& in,EvidenceLedger& ledger,const ProbeBudget& probes,Arm arm) {
    common::refresh_evidence(in,ledger); return choose(in,ledger,probes,arm);
}
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,EvidenceLedger& ledger,
                        const ProbeBudget& probes,Arm arm,Work& work) {
    Admission out;
    if (!local.fresh()||!(local.frame.clock==in.frame.clock)||!(proposed.clock==in.frame.clock)||work.active) { out.rejection=Rejection::stale; return out; }
    const auto now=decide(in,ledger,probes,arm);
    if (now.option<0||now.option!=proposed.option||now.record_episode!=proposed.record_episode) return out;
    const auto& o=in.offers[now.option]; if (!physical_admissible(o.plan,local)) { out.rejection=Rejection::unsafe; return out; }
    if (now.record_episode) {
        const auto* s=ledger.state(o.context_slot); if (!s) return out;
        Premises premises{o.entry,in.own_state.value,all_premises};
        if (o.plan.organization==Organization::H&&!s->campaign_started&&
            !ledger.begin_organization_campaign(in.component_slot,o.context_slot,in.frame.clock.round,{premises,true,true,true})) return out;
        EpisodeRequest r; r.context=o.context_slot; r.issue_round=in.frame.clock.round;
        r.deadline=r.issue_round+std::min(common::forecast_window,o.plan.remaining_rounds); r.action=int(o.plan.organization);
        r.nominal_duration=o.plan.forecast_rounds; r.premises=premises;
        out.episode=ledger.issue(r); if (!out.episode) return out;
    }
    out.started=work.start(o.plan,local); out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if (!out.started&&out.episode) ledger.invalidate_active(in.frame.clock.round);
    return out;
}
inline Command advance(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,Arm arm,Work& work) {
    if (!reversible_workshop::own_market(in)||!(local.frame.clock==in.frame.clock)) {
        work.request_stop(); if (ledger.active()&&local.fresh()) ledger.invalidate_active(local.frame.clock.round); return work.step(local);
    }
    common::refresh_evidence(in,ledger);
    if (ledger.active()&&(!local.frame.own.actor.alive||local.fuel_resumed)) ledger.invalidate_active(local.frame.clock.round);
    if (arm==Arm::active&&work.active&&work.plan.organization==Organization::H) {
        if (work.plan.collector==in.frame.clock.self) {
            if (!own_component(in,ledger)||ledger.suspended(in.supported_slot,local.frame.clock.round/common::project_rounds)) work.request_stop();
        } else if (!local.frame.visible(work.plan.collector)||!local.certificate(work.plan.collector,work.plan.group)) work.request_stop();
    }
    const auto c=work.step(local);
    if (ledger.active()&&!work.active&&!work.cycle_completed) ledger.invalidate_active(local.frame.clock.round);
    return c;
}
} // namespace p8::common_cause_firewall
