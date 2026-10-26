#pragma once
#include "meeting.h"
#include "../../earned-center/src/trace.h"
namespace p8::custodial_authority {
using common::Arm;using common::Rejection;using common::Admission;using CycleTrace=earned_center::CycleTrace;
struct Decision:common::Decision{bool private_income=false;};
struct Execution{Work work{};CycleTrace trace{};ContextKey issued{};
    bool confirm(const Command& command,const ActionReceipt& receipt){return work.confirm(command,receipt);}};
inline bool no_extra(const Offer& offer){
    if(offer.capital_cost!=0||offer.diagnostic_segment||offer.marked_extra_turns!=0)return false;
    for(int a=0;a<offer.schedule.actor_count;++a)for(int e=0;e<offer.schedule.actors[a].event_count;++e)
        if(offer.schedule.actors[a].events[e].kind==p6::EventKind::purchase)return false;
    return true;
}
inline bool independent(const Inputs& in,const Offer& offer){
    const auto* safe=common::certificate(offer,in.frame.clock.self);
    return offer.plan.organization==Organization::U&&offer.plan.crew_count==1&&offer.plan.collector==in.frame.clock.self&&
        safe&&safe->group==offer.plan.group&&independent_safety(*safe);
}
inline bool private_valid(const Inputs& in){
    if(!in.private_income.current(in.frame.clock,in.frame.clock.self))return false;
    const auto& o=in.private_income.value;
    return o.safety_count==1&&plan_valid(o.plan,in.frame.clock.self)&&o.schedule.actor_count==1&&o.schedule.actors[0].actor_id==in.frame.clock.self&&
        o.schedule.target_id==o.plan.group&&o.plan.remaining_rounds<=in.settings.max_rounds-in.frame.clock.round&&independent(in,o)&&
        common::certificate(o,in.frame.clock.self)->valid(in.frame,in.frame.clock.self,o.plan.group)&&common::evaluate(in,o.schedule).eligible()&&no_extra(o);
}
inline bool current_organization(const Inputs& in,const EvidenceLedger& ledger){return common::slot_current(in,ledger,in.supported_slot,in.supported_entry)&&
    ledger.organization_qualified(in.supported_slot,in.supported_entry,in.frame.clock.round);}
inline bool crew_now(const Inputs& in,const Offer& offer){
    if(offer.plan.crew_count!=2||offer.plan.organization!=Organization::H)return false;
    for(int i=0;i<offer.plan.crew_count;++i){const auto* actor=in.frame.visible(offer.plan.crew[i]);const auto* safe=common::certificate(offer,offer.plan.crew[i]);
        if(!actor||!roster_actor(*actor,in.draft)||!actor->alive||actor->bag!=0||actor->channel!=p6::Channel::none||
            !safe||safe->group!=offer.plan.group||!safe->valid(in.frame,actor->id,offer.plan.group))return false;}
    return common::participant(offer.plan,in.meeting.helper);
}
inline bool visible_proposal(const Inputs& in,const Offer& offer){
    const int collector=offer.plan.collector;const auto* actor=in.frame.visible(collector);const int pad=crew_index(offer.plan,collector);
    return actor&&pad>=0&&actor->position==offer.plan.pads[pad]&&in.collector_proposal.current(in.frame.clock,collector)&&in.collector_proposal.value==offer.plan.group;
}
inline bool may_record_component(const Inputs& in,const EvidenceLedger& ledger,const Offer& offer){
    return candidate(in.frame.own.actor,in.draft)&&offer.context_slot==in.component_slot&&offer.entry==in.component_entry&&
        common::context_matches(in,offer)&&!ledger.suspended(in.component_slot,in.frame.clock.round/common::project_rounds)&&!ledger.campaign_exhausted(in.component_slot);
}
inline Decision choose(const Inputs& in,const Authority& authority,const EvidenceLedger& ledger,Arm arm){
    Decision out;out.clock=in.frame.clock;
    if(!common::valid_inputs(in)||authority.owner!=in.frame.clock.self||!spec_valid(in.meeting,in.draft)||!ledger_local(in,ledger)){out.reason=Rejection::malformed;return out;}
    if(in.frame.own.actor.bag>0)return out;
    for(int i=0;i<in.offer_count;++i){const auto& o=in.offers[i];auto reject=common::legal_offer(in,o);
        if(reject==Rejection::none&&(!independent(in,o)||!no_extra(o)))reject=Rejection::evidence;
        if(reject==Rejection::none&&authority.transferred&&in.frame.clock.self==in.meeting.original.collector&&
            (o.plan.group!=in.meeting.former_u.group||plan_route_hash(o.plan)!=plan_route_hash(in.meeting.former_u)))reject=Rejection::evidence;
        out.rejected[i]=reject;if(reject!=Rejection::none)continue;
        const auto score=common::evaluate(in,o.schedule);
        if(out.option<0||p6::compare(score,out.score)>0){out.option=i;out.organization=Organization::U;out.score=score;out.reason=Rejection::none;
            out.record_episode=!own_component(in,ledger)&&may_record_component(in,ledger,o);}}
    if(out.option<0&&private_valid(in)){
        const auto& o=in.private_income.value;
        if(!authority.transferred||in.frame.clock.self!=in.meeting.original.collector||
            (o.plan.group==in.meeting.former_u.group&&plan_route_hash(o.plan)==plan_route_hash(in.meeting.former_u))){out.private_income=true;out.organization=Organization::U;
            out.reason=Rejection::none;out.score=common::evaluate(in,o.schedule);out.record_episode=!own_component(in,ledger)&&may_record_component(in,ledger,o);}}
    if(out.option<0||in.frame.own.actor.pick_ordinal>=3)return out;
    const auto ordinary_score=out.score;
    const int collector=arm==Arm::active&&authority.transferred?authority.collector:in.meeting.original.collector;
    const bool replacement=collector!=in.meeting.original.collector;
    if(replacement&&authority.accepted_meeting!=meeting_hash(in.meeting))return out;
    for(int i=0;i<in.offer_count;++i){const auto& o=in.offers[i];if(o.plan.organization!=Organization::H)continue;
        auto reject=common::legal_offer(in,o);bool record=false;
        if(reject==Rejection::none&&(!crew_now(in,o)||!no_extra(o)||o.plan.collector!=collector||o.plan.group!=in.meeting.original.group))reject=Rejection::evidence;
        if(reject==Rejection::none&&in.frame.clock.self==collector){
            const auto* state=ledger.state(in.supported_slot);const auto& expected=replacement?in.meeting.successor:in.meeting.original;
            const bool retained_control=arm==Arm::control&&!replacement;
            const bool exact_plan=plan_route_hash(o.plan)==plan_route_hash(expected)&&o.plan.altar==expected.altar&&
                o.plan.crew==expected.crew&&o.plan.crew_count==expected.crew_count&&o.plan.pads==expected.pads&&
                o.plan.forecast_rounds==expected.forecast_rounds&&o.plan.capture_window==expected.capture_window;
            if(retained_control){
                // A restored actual premise permits the control's fixed future
                // duty. It does not restore or record its retired private proof.
                if(!access_available(in)||!exact_plan)reject=Rejection::evidence;
            }else if((!replacement&&(!access_available(in)||authority.lost))||
                (replacement&&!relevant_component(in,ledger))||
                !state||state->declaration.kind!=EpisodeKind::full_organization||!state->declaration.require_supported_work||
                o.context_slot!=in.supported_slot||o.entry!=in.supported_entry||!common::context_matches(in,o)||
                !exact_plan||ledger.suspended(in.supported_slot,in.frame.clock.round/common::project_rounds)||
                (ledger.campaign_exhausted(in.supported_slot)&&!current_organization(in,ledger)))reject=Rejection::evidence;
            else record=true;
        }else if(reject==Rejection::none&&(!visible_proposal(in,o)||in.frame.clock.self!=in.meeting.helper))reject=Rejection::evidence;
        if(reject==Rejection::none&&p6::compare(common::evaluate(in,o.schedule),ordinary_score)<=0)reject=Rejection::no_gain;
        out.rejected[i]=reject;if(reject!=Rejection::none)continue;
        out.option=i;out.organization=Organization::H;out.score=common::evaluate(in,o.schedule);out.reason=Rejection::none;out.record_episode=record;
    }
    return out;
}
inline Decision decide(const Inputs& in,Authority& authority,EvidenceLedger& ledger,Arm arm){refresh(in,authority,ledger);return choose(in,authority,ledger,arm);}
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,Authority& authority,EvidenceLedger& ledger,Arm arm,Execution& execution){
    Admission out;if(!local.fresh()||local.frame.clock!=in.frame.clock||proposed.clock!=in.frame.clock||execution.work.active)return out;
    const auto selected=decide(in,authority,ledger,arm);
    if((selected.option<0&&!selected.private_income)||selected.option!=proposed.option||selected.private_income!=proposed.private_income||selected.record_episode!=proposed.record_episode)return out;
    const auto& offer=selected.private_income?in.private_income.value:in.offers[selected.option];if(!physical_admissible(offer.plan,local))return out;
    if(selected.record_episode){const auto* state=ledger.state(offer.context_slot);if(!state)return out;Premises premises{offer.entry,in.own_state.value,all_premises};
        if(offer.plan.organization==Organization::H&&!state->campaign_started){
            const bool allowed=offer.plan.collector==in.meeting.original.collector?
                ledger.begin_initial_campaign(offer.context_slot,in.frame.clock.round,{premises,true,true,true}):
                ledger.begin_organization_campaign(in.component_slot,offer.context_slot,in.frame.clock.round,{premises,true,true,true});
            if(!allowed)return out;}
        EpisodeRequest request;request.context=offer.context_slot;request.issue_round=in.frame.clock.round;
        request.deadline=request.issue_round+std::min(common::forecast_window,offer.plan.remaining_rounds);request.nominal_duration=offer.plan.forecast_rounds;
        request.action=int(offer.plan.organization);request.premises=premises;out.episode=ledger.issue(request);if(!out.episode)return out;execution.issued=offer.entry;}
    out.started=execution.work.start(offer.plan,local);out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if(!out.started&&out.episode)ledger.invalidate_active(in.frame.clock.round);
    if(out.started&&out.episode&&!execution.trace.begin(ledger,execution.work)){ledger.invalidate_active(in.frame.clock.round);execution.work.request_stop();}
    return out;
}
inline Command advance(const Inputs& in,const LocalFrame& local,Authority& authority,EvidenceLedger& ledger,Execution& execution){
    refresh(in,authority,ledger);
    if(!common::valid_inputs(in)||local.frame.clock!=in.frame.clock){execution.work.request_stop();if(ledger.active()&&local.fresh())ledger.invalidate_active(local.frame.clock.round);}
    // Authority changes stop future proposals. Physical Work retains its own
    // safe hold, existing owned bag, valid channel and verified return first.
    return execution.work.step(local);
}
inline Resolution observe(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,Execution& execution){
    execution.trace.observe(local,execution.work,{});
    if(!execution.work.active||!ledger.active())return execution.trace.finish(ledger,in.frame.clock.round,{execution.issued,in.own_state.value,all_premises});
    return {};
}
}
