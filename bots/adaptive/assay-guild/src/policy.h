#pragma once
#include "trace.h"
namespace p8::assay_guild {
using common::Arm;using common::Offer;using common::Rejection;using common::Admission;
struct Decision:common::Decision {int role=-1;bool assay=false,private_income=false;Configuration configuration=Configuration::independent;};
struct Execution {
    Work work{};RoleSpec frozen{};RoleTrace trace{};
    bool specialized=false,next=false,awaiting_next=false;
    int slot=-1;
    const Plan& selected_plan() const {return awaiting_next?frozen.next:work.plan;}
    bool confirm(const Command& command,const ActionReceipt& receipt) {
        const bool accepted=work.confirm(command,receipt);
        if(accepted && specialized && command.action==PhysicalAction::attack && receipt.success)
            trace.attack(work,next,command.clock.round);
        return accepted;
    }
};
inline bool no_extra(const Offer& offer) {
    if(offer.capital_cost!=0 || offer.diagnostic_segment || offer.marked_extra_turns!=0)return false;
    for(int a=0;a<offer.schedule.actor_count;++a)for(int e=0;e<offer.schedule.actors[a].event_count;++e)
        if(offer.schedule.actors[a].events[e].kind==p6::EventKind::purchase)return false;
    return true;
}
inline bool crew_now(const Inputs& in,const Offer& offer) {
    for(int i=0;i<offer.plan.crew_count;++i) {
        const auto* actor=in.frame.visible(offer.plan.crew[i]);const auto* safe=common::certificate(offer,offer.plan.crew[i]);
        if(!actor || !actor->alive || !roster_actor(*actor,in.draft) || actor->channel!=p6::Channel::none || actor->bag!=0 ||
            !safe || safe->group!=offer.plan.group || !safe->valid(in.frame,actor->id,offer.plan.group))return false;
    }
    return true;
}
inline bool named(const Inputs& in,const RoleSpec& role) {
    const int proposer=in.draft.actors[0];const auto* actor=in.frame.visible(proposer);const int pad=crew_index(role.primary,proposer);
    return actor && pad>=0 && in.proposal.current(in.frame.clock,proposer) &&
        in.proposal.value.configuration==role.configuration && in.proposal.value.group==role.primary.group &&
        actor->position==role.primary.pads[pad];
}
inline bool private_income_valid(const Inputs& in) {
    if(!in.private_income.current(in.frame.clock,in.frame.clock.self) || in.frame.own.actor.bag!=0)return false;
    const auto& o=in.private_income.value;if(o.safety_count!=1)return false;
    const auto* safe=common::certificate(o,in.frame.clock.self);
    return plan_valid(o.plan,in.frame.clock.self) && o.plan.organization==Organization::U && o.plan.crew_count==1 &&
        o.schedule.actor_count==1 && o.schedule.actors[0].actor_id==in.frame.clock.self && o.schedule.target_id==o.plan.group &&
        o.plan.remaining_rounds<=in.settings.max_rounds-in.frame.clock.round && o.safety_count==1 && safe &&
        safe->group==o.plan.group && safe->valid(in.frame,in.frame.clock.self,o.plan.group) && independent_safety(*safe) &&
        common::evaluate(in,o.schedule).eligible() && no_extra(o);
}
inline bool qualified(const Inputs& in,const Guild& guild,const EvidenceLedger& ledger) {
    const auto* state=ledger.state(guild.locked_slot);
    return state && common::slot_current(in,ledger,guild.locked_slot,state->declaration.key) &&
        ledger.organization_qualified(guild.locked_slot,state->declaration.key,in.frame.clock.round);
}
inline void refresh(const Inputs& in,EvidenceLedger& ledger) {
    if(!common::valid_inputs(in))return;
    for(const auto& candidate:in.roles) {
        if(candidate.offer<0 || candidate.offer>=in.offer_count)continue;
        const int id=in.offers[candidate.offer].context_slot;const auto* state=ledger.state(id);if(!state)continue;
        Premises observed{state->declaration.key,in.own_state.value,all_premises};auto& key=observed.key;
        key.actor_id=in.frame.clock.self;key.champion=int(in.frame.own.actor.type);
        key.settings_id=in.settings_identity.value;key.threat_regime=in.threat_regime.value;
        const auto* active=ledger.active();
        if(!active || active->context!=id || !state->declaration.envelope.contains(observed.state,state->declaration.key.build)) {
            key.build={observed.state.equipment_hash,observed.state.max_health,observed.state.strength,observed.state.focus,observed.state.growth};
            key.hp_band=health_band(observed.state.health,observed.state.max_health);
        }
        for(int i=0;i<key.participant_count;++i)if(!in.frame.visible(key.participants[i]))observed.personally_observed&=~membership_known;
        ledger.observe_context(id,observed,in.frame.clock.round);
    }
}
inline Decision choose(const Inputs& in,const Guild& guild,const EvidenceLedger& ledger,Arm arm) {
    Decision out;out.clock=in.frame.clock;
    if(!common::valid_inputs(in) || guild.owner!=in.frame.clock.self){out.reason=Rejection::malformed;return out;}
    // The same unchanged complete-schedule comparator first chooses ordinary
    // production. Naming an assay cannot select a lower-ranked route.
    for(int i=0;i<in.offer_count;++i) {
        const auto& offer=in.offers[i];auto reject=common::legal_offer(in,offer);
        if(reject==Rejection::none && (!no_extra(offer) || !crew_now(in,offer)))reject=Rejection::unsafe;
        if(reject==Rejection::none && (offer.plan.organization!=Organization::U && offer.plan.organization!=Organization::G))reject=Rejection::evidence;
        if(reject==Rejection::none && !independent_safety(*common::certificate(offer,in.frame.clock.self)))reject=Rejection::unsafe;
        out.rejected[i]=reject;if(reject!=Rejection::none)continue;
        const auto score=common::evaluate(in,offer.schedule);
        if(out.option<0 || p6::compare(score,out.score)>0){out.option=i;out.organization=offer.plan.organization;out.score=score;out.reason=Rejection::none;}
    }
    if((out.option<0 || in.frame.own.actor.pick_ordinal>=2) && private_income_valid(in)) {
        out.option=-1;out.private_income=true;out.organization=Organization::U;out.reason=Rejection::none;
        out.score=common::evaluate(in,in.private_income.value.schedule);return out;
    }
    if(out.option<0 || in.frame.own.actor.bag>0 || in.frame.own.actor.pick_ordinal>=2)return out;
    const auto ordinary_score=out.score;
    if(in.frame.own.actor.pick_ordinal==0 && !guild.assay.attempted) {
        out.assay=out.option>=0 && in.offers[out.option].plan.organization==Organization::U &&
            plan_route_hash(in.offers[out.option].plan)==plan_route_hash(in.assay.ordinary);
        return out;
    }
    const auto desired=in.frame.own.actor.pick_ordinal==0?(guild.assay.current(in)?guild.assay.desired(arm):Configuration::independent):
        in.proposal.current(in.frame.clock,in.draft.actors[0])?in.proposal.value.configuration:Configuration::independent;
    if(desired==Configuration::independent || (guild.locked!=Configuration::independent && guild.locked!=desired))return out;
    for(int r=0;r<int(in.roles.size());++r) {
        const auto& candidate=in.roles[r];const int i=candidate.offer;
        if(i<0 || i>=in.offer_count || candidate.role.configuration!=desired)continue;
        const auto& offer=in.offers[i];const auto* state=ledger.state(offer.context_slot);
        auto reject=common::legal_offer(in,offer);
        if(reject==Rejection::none && (!no_extra(offer) || !crew_now(in,offer) || !role_valid(candidate.role,in) ||
            plan_route_hash(offer.plan)!=plan_route_hash(candidate.role.primary) || offer.entry!=candidate.role.context.key ||
            !state || state->declaration.key!=offer.entry || state->declaration.kind!=EpisodeKind::full_organization ||
            !state->declaration.require_supported_work || ledger.suspended(offer.context_slot,in.frame.clock.round/common::project_rounds) ||
            (guild.locked_slot>=0 && guild.locked_slot!=offer.context_slot) ||
            (ledger.campaign_exhausted(offer.context_slot) && !ledger.organization_qualified(offer.context_slot,offer.entry,in.frame.clock.round))))reject=Rejection::evidence;
        if(reject==Rejection::none && in.frame.own.actor.pick_ordinal==1 && !named(in,candidate.role))reject=Rejection::evidence;
        if(reject==Rejection::none) {
            const auto* collector=common::certificate(offer,candidate.role.primary.collector);
            const auto* clearer=common::certificate(offer,in.draft.actors[0]);
            if(!collector || !independent_safety(*collector) || (desired==Configuration::serial && (!clearer || !independent_safety(*clearer))))reject=Rejection::unsafe;
        }
        if(reject==Rejection::none && in.frame.own.actor.pick_ordinal==0 && candidate.role.primary.group!=guild.assay.spec.ordinary.group)reject=Rejection::evidence;
        if(reject==Rejection::none && p6::compare(common::evaluate(in,offer.schedule),ordinary_score)<=0)reject=Rejection::no_gain;
        out.rejected[i]=reject;if(reject!=Rejection::none)continue;
        out.option=i;out.role=r;out.organization=Organization::H;out.configuration=desired;out.record_episode=true;out.assay=false;
        out.score=common::evaluate(in,offer.schedule);out.reason=Rejection::none;
    }
    return out;
}
inline Decision decide(const Inputs& in,const Guild& guild,EvidenceLedger& ledger,Arm arm) {
    refresh(in,ledger);return choose(in,guild,ledger,arm);
}
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,Guild& guild,EvidenceLedger& ledger,
                        Arm arm,Execution& execution) {
    Admission out;
    if(!local.fresh() || local.frame.clock!=in.frame.clock || proposed.clock!=in.frame.clock || execution.work.active || execution.awaiting_next)return out;
    const auto now=decide(in,guild,ledger,arm);
    if((now.option<0&&!now.private_income) || now.option!=proposed.option || now.role!=proposed.role ||
        now.assay!=proposed.assay || now.private_income!=proposed.private_income)return out;
    const auto& offer=now.private_income?in.private_income.value:in.offers[now.option];if(!physical_admissible(offer.plan,local))return out;
    if(now.role>=0) {
        const auto& role=in.roles[now.role].role;const auto* state=ledger.state(offer.context_slot);if(!state)return out;
        const Premises premises{role.context.key,in.own_state.value,all_premises};
        if(!state->campaign_started && !ledger.begin_initial_campaign(offer.context_slot,in.frame.clock.round,{premises,true,true,true}))return out;
        EpisodeRequest request;request.context=offer.context_slot;request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+std::min(common::forecast_window,offer.plan.remaining_rounds);
        request.nominal_duration=role.nominal_duration;request.action=int(role.configuration);request.premises=premises;
        out.episode=ledger.issue(request);if(!out.episode)return out;
        execution.frozen=role;execution.frozen.context=state->declaration;
        execution.specialized=true;execution.slot=offer.context_slot;
        guild.locked=role.configuration;guild.locked_slot=offer.context_slot;
    } else {execution.specialized=false;execution.slot=-1;}
    execution.next=execution.awaiting_next=false;out.started=execution.work.start(offer.plan,local);
    out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if(!out.started){if(out.episode)ledger.invalidate_active(in.frame.clock.round);return out;}
    if(now.role>=0 && !execution.trace.begin(ledger,execution.frozen,execution.work)) {
        ledger.invalidate_active(in.frame.clock.round);execution.work.request_stop();
    }
    if(in.frame.own.actor.pick_ordinal==0 && !guild.assay.attempted) {
        if(!now.assay || !guild.assay.begin(in.assay,in,execution.work))guild.assay.unavailable();
    }
    return out;
}
inline Command advance(const Inputs& in,const LocalFrame& local,Guild&,EvidenceLedger& ledger,Execution& execution) {
    auto& work=execution.work;
    if(!common::valid_inputs(in) || !local.fresh() || local.frame.clock!=in.frame.clock) {
        work.request_stop();if(ledger.active() && local.fresh())ledger.invalidate_active(local.frame.clock.round);return work.step(local);
    }
    refresh(in,ledger);
    if(execution.specialized) {
        const auto* state=ledger.state(execution.slot);const auto& key=execution.frozen.context.key;
        if(!state || ledger.suspended(execution.slot,in.frame.clock.round/common::project_rounds) ||
            in.settings_identity.value!=key.settings_id || in.threat_regime.value!=key.threat_regime ||
            int(in.frame.own.actor.type)!=key.champion ||
            !execution.frozen.context.envelope.contains(in.own_state.value,key.build)) {
            work.request_stop();if(ledger.active())ledger.invalidate_active(local.frame.clock.round);
            if(execution.awaiting_next){execution.awaiting_next=false;execution.trace.failed=true;return work.fallback(local);}
        }
    }
    if(execution.awaiting_next) {
        execution.awaiting_next=false;execution.next=true;
        if(!physical_admissible(execution.frozen.next,local) || !work.start(execution.frozen.next,local) || !execution.trace.start_next(work)) {
            if(ledger.active())ledger.invalidate_active(local.frame.clock.round);execution.trace.failed=true;return work.fallback(local);
        }
    }
    auto conservative=local;
    if(execution.specialized && !execution.next && execution.frozen.configuration==Configuration::serial &&
        collecting(execution.frozen,in.frame.clock.self))conservative.legal.value.attack_legal=false;
    const auto command=work.step(conservative);
    // confirm() precedes observation for productive actions. Call observe()
    // after the actual command receipt, including hold/move owner turns.
    return command;
}
inline Resolution observe(const Inputs& in,const LocalFrame& local,Guild& guild,EvidenceLedger& ledger,Execution& execution) {
    if(!execution.specialized){guild.assay.observe(in,local,execution.work);return {};}
    execution.trace.observe(local,execution.work,execution.next);
    if(!execution.work.active) {
        if(!execution.next && !collecting(execution.frozen,in.frame.clock.self) && execution.work.helper_released &&
            execution.work.renewal_seen && execution.trace.released && !execution.trace.failed)execution.awaiting_next=true;
        else return execution.trace.finish(ledger,in.own_state.value,in.frame.clock.round);
    }
    return {};
}
}
