#pragma once
#include "protocol.h"
namespace p8::probe_reveal_commit {
using CycleTrace=earned_center::CycleTrace;
struct Decision:common::Decision {bool intervention=false,standalone=false,secure_inventory=false;};
struct Execution {Work work{};CycleTrace trace{};ContextKey issued{};bool ordinary=false;int response_deadline=-1;
    bool confirm(const Inputs& in,Protocol& protocol,const Command& command,const ActionReceipt& receipt){
        if(!work.confirm(command,receipt))return false;protocol.confirm_intervention(in,work,command,receipt);
        protocol.begin_passive(in,work,command,receipt,ordinary);return true;}
};
inline bool no_purchase(const Offer& offer){
    if(offer.capital_cost||offer.diagnostic_segment||offer.marked_extra_turns)return false;
    for(int a=0;a<offer.schedule.actor_count;++a)for(int e=0;e<offer.schedule.actors[a].event_count;++e)
        if(offer.schedule.actors[a].events[e].kind==p6::EventKind::purchase)return false;
    return true;
}
inline bool independent(const Inputs& in,const Offer& offer){const auto* safe=common::certificate(offer,in.frame.clock.self);
    return (offer.plan.organization==Organization::U||offer.plan.organization==Organization::G)&&offer.plan.crew_count==1&&offer.plan.collector==in.frame.clock.self&&
        safe&&safe->group==offer.plan.group&&independent_safety(*safe);
}
inline bool own_offer(const Inputs& in,const Offer& offer){const auto* safe=common::certificate(offer,in.frame.clock.self);
    return no_purchase(offer)&&independent(in,offer)&&plan_valid(offer.plan,in.frame.clock.self)&&offer.schedule.actor_count==1&&
        offer.schedule.actors[0].actor_id==in.frame.clock.self&&offer.schedule.target_id==offer.plan.group&&offer.safety_count==1&&safe&&safe->valid(in.frame,in.frame.clock.self,offer.plan.group)&&
        common::evaluate(in,offer.schedule).eligible();
}
inline Decision ordinary(const Inputs& in){Decision out;out.clock=in.frame.clock;
    for(int i=0;i<in.offer_count;++i){const auto& o=in.offers[i];auto rejection=common::legal_offer(in,o);
        if(rejection==Rejection::none&&(!independent(in,o)||!no_purchase(o)))rejection=Rejection::evidence;
        out.rejected[i]=rejection;if(rejection!=Rejection::none)continue;const auto score=common::evaluate(in,o.schedule);
        if(out.option<0||p6::compare(score,out.score)>0){out.option=i;out.organization=o.plan.organization;out.score=score;out.reason=Rejection::none;}}
    if(out.option<0&&in.own_income.current(in.frame.clock,in.frame.clock.self)&&own_offer(in,in.own_income.value)){
        out.standalone=true;out.organization=in.own_income.value.plan.organization;out.score=common::evaluate(in,in.own_income.value.schedule);out.reason=Rejection::none;}
    return out;
}
inline bool crew(const Inputs& in,const Offer& offer){
    if(offer.plan.organization!=Organization::H||offer.plan.collector!=in.draft.actors[0]||offer.plan.crew_count!=2||
        !common::participant(offer.plan,in.draft.actors[0])||!common::participant(offer.plan,in.draft.actors[2])||common::participant(offer.plan,in.draft.actors[1]))return false;
    for(int i=0;i<2;++i){const auto* actor=in.frame.visible(offer.plan.crew[i]);const auto* safe=common::certificate(offer,offer.plan.crew[i]);
        if(!actor||!roster_actor(*actor,in.draft)||!actor->alive||actor->bag||actor->channel!=p6::Channel::none||!safe||
            safe->group!=offer.plan.group||!safe->valid(in.frame,actor->id,offer.plan.group)||!independent_safety(*safe))return false;}
    return true;
}
inline bool protected_same(const p6::Schedule& a,const p6::Schedule& b,int protected_actor){
    const int x=p6::actor_index(a,protected_actor),y=p6::actor_index(b,protected_actor);if(x<0||y<0)return false;
    const auto& left=a.actors[x];const auto& right=b.actors[y];
    if(left.event_count!=right.event_count||left.occupied_turns.value!=right.occupied_turns.value||left.initial_bag.value!=right.initial_bag.value||left.initial_gold.value!=right.initial_gold.value)return false;
    for(int e=0;e<left.event_count;++e){const auto& l=left.events[e];const auto& r=right.events[e];
        if(l.event_id!=r.event_id||l.tick!=r.tick||l.kind!=r.kind||l.gold_cost.value!=r.gold_cost.value||l.waypoint_id!=r.waypoint_id)return false;
        if(l.kind==p6::EventKind::production){const auto& lp=a.productions[l.production_index];const auto& rp=b.productions[r.production_index];
            if(lp.event_id!=rp.event_id||lp.tick!=rp.tick||lp.gems.value!=rp.gems.value||lp.gold.value!=rp.gold.value||lp.holder_count!=rp.holder_count||lp.holders!=rp.holders)return false;}}
    return true;
}
template<class T> inline bool public_payload(const p6::Fact<T>& value){return value.source==p6::Source::public_map||
    value.source==p6::Source::queried_setting||value.source==p6::Source::synthetic_fixture;}
inline bool forecast_valid(const Inputs& in,const Protocol& protocol,const EvidenceLedger& ledger,const Decision& unchanged,bool& diagnostic){
    if(in.intervention<0||in.intervention>=in.offer_count||unchanged.option<0||in.frame.own.actor.id!=in.draft.actors[0]||
        in.frame.own.actor.bag||in.frame.own.actor.channel!=p6::Channel::none||!protocol.local(in,ledger)||protocol.issued)return false;
    const auto& low=in.offers[in.intervention];const auto& f=in.forecast;
    if(!same_plan(low.plan,in.layout.intervention)||common::legal_offer(in,low)!=Rejection::none||!independent(in,low)||!no_purchase(low)||
        !f.endpoint_distinguishes||f.marked_extra_turns<0||f.marked_extra_turns>common::max_probe_turns||
        f.lowload_event<0||!public_payload(f.lowload_gems)||f.lowload_gems.value<=0||!public_payload(f.lowload_gold)||f.lowload_gold.value<0)return false;
    diagnostic=unchanged.option!=in.intervention||f.marked_extra_turns>0;
    if(diagnostic&&!protocol.budget.available(in.frame.clock.round/common::project_rounds))return false;
    p6::ScoreEvaluation worst;bool first=true;
    for(int b=0;b<3;++b){const auto& schedule=f.branches[b];
        const auto score=common::evaluate(in,schedule);if(!score.eligible())return false;
        if(!p6::same_actors(schedule,in.offers[unchanged.option].schedule)||!protected_same(schedule,in.offers[unchanged.option].schedule,in.draft.actors[1])||
            f.safety_count[b]<1||f.safety_count[b]>p6::max_actors)return false;
        for(int a=0;a<schedule.actor_count;++a){bool safe=false;for(int c=0;c<f.safety_count[b];++c){const auto& candidate=f.safety[b][c];
                safe|=candidate.actor==schedule.actors[a].actor_id&&candidate.valid(in.frame,candidate.actor,candidate.group)&&independent_safety(candidate);}
            if(!safe)return false;
            for(int e=0;e<schedule.actors[a].event_count;++e)if(schedule.actors[a].events[e].tick>common::forecast_window||schedule.actors[a].events[e].kind==p6::EventKind::purchase)return false;}
        const auto& continuation=b==0?in.layout.supported:b==1?in.layout.independent:in.layout.generalist;
        const int own=p6::actor_index(schedule,in.frame.clock.self);int prefix=0,next=0,productions=0,prefix_tick=-1,next_tick=-1,prefix_bank=-1,renewal=-1;
        if(own<0||f.continuation_event[b]<0||f.continuation_event[b]==f.lowload_event||!public_payload(f.continuation_gems[b])||f.continuation_gems[b].value<=0||
            !public_payload(f.continuation_gold[b])||f.continuation_gold[b].value<0||f.lowload_end[b]<in.layout.intervention.forecast_rounds||
            f.continuation_start[b]<f.lowload_end[b]||schedule.actors[own].occupied_turns.value<f.continuation_start[b]+continuation.forecast_rounds+f.marked_extra_turns)return false;
        for(int e=0;e<schedule.actors[own].event_count;++e){const auto& event=schedule.actors[own].events[e];if(event.kind!=p6::EventKind::production)continue;
            ++productions;const auto& p=schedule.productions[event.production_index];if(p.holder_count!=1||p.holders[0]!=in.frame.clock.self)return false;
            if(p.event_id==f.lowload_event&&p.gems.value==f.lowload_gems.value&&p.gold.value==f.lowload_gold.value){++prefix;prefix_tick=p.tick;}
            if(p.event_id==f.continuation_event[b]&&p.gems.value==f.continuation_gems[b].value&&p.gold.value==f.continuation_gold[b].value){++next;next_tick=p.tick;}}
        for(int e=0;e<schedule.actors[own].event_count;++e){const auto& event=schedule.actors[own].events[e];
            if(event.kind==p6::EventKind::tribute&&event.tick>=prefix_tick&&event.tick<f.lowload_end[b])prefix_bank=event.tick;
            if(event.kind==p6::EventKind::renewal)renewal=event.tick;}
        if(prefix!=1||next!=1||productions!=2||prefix_bank<0||next_tick<f.continuation_start[b]||renewal<f.continuation_start[b]+continuation.forecast_rounds)return false;
        if(first||p6::compare(score,worst)<0){worst=score;first=false;}}
    // A paid intervention must beat the complete unchanged productive menu in
    // its least useful branch. Ordinary elicitation adds no diagnostic delay.
    return !diagnostic||p6::compare(worst,unchanged.score)>0;
}
inline bool full_current(const Inputs& in,const Protocol& protocol,const EvidenceLedger& ledger){return common::guarded_key_current(in,protocol.organization.key)&&
    ledger.organization_qualified(protocol.slot,protocol.organization.key,in.frame.clock.round);}
inline bool collector_allowed(const Inputs& in,const Protocol& protocol,const EvidenceLedger& ledger,const Offer& offer){
    const auto* slot=ledger.state(protocol.slot);if(!slot||!protocol.intervention_complete||protocol.intervention_failed||!protocol.initial_approach||
        !protocol.response_current(in.frame.clock.round)||protocol.responses.window.record.response!=Response::approach||
        offer.context_slot!=protocol.slot||offer.entry!=protocol.organization.key||!common::context_matches(in,offer)||
        ledger.suspended(protocol.slot,in.frame.clock.round/common::project_rounds)||
        finite_counterprotocol::sample_failure(in.frame,in.layout.site,in.rival,in.distance)!=finite_counterprotocol::ResponseFailure::none)return false;
    int duration=offer.plan.forecast_rounds;for(int h=0;h<slot->declaration.hypothesis_count;++h)
        if(slot->consistent[h])duration=std::max(duration,offer.plan.forecast_rounds+slot->declaration.hypotheses[h].offset.upper);
    if(slot->campaign_episodes<2&&!slot->campaign_failed)return protocol.initial_current(in.frame.clock.round,duration);
    return full_current(in,protocol,ledger)&&protocol.responses.approach_current(in.frame.clock.self,in.frame.clock.round)&&
        in.frame.clock.round+duration-protocol.responses.latest()<64;
}
inline Decision choose(const Inputs& in,const Protocol& protocol,const EvidenceLedger& ledger,Arm arm){
    Decision out;out.clock=in.frame.clock;if(!common::valid_inputs(in)||!protocol.local(in,ledger)){out.reason=Rejection::malformed;return out;}
    if(in.frame.own.actor.bag>0){
        if(in.existing_inventory.current(in.frame.clock,in.frame.clock.self)&&own_offer(in,in.existing_inventory.value)){
            const auto& p=in.existing_inventory.value.plan;if(p.ingress.count==1&&p.capture.count==1&&p.ingress.tiles[0]==in.frame.own.actor.position&&p.capture.tiles[0]==in.frame.own.actor.position){out.secure_inventory=true;out.reason=Rejection::none;}}
        return out;}
    out=ordinary(in);const auto ordinary_score=out.score;const int ordinal=in.frame.own.actor.pick_ordinal;
    if(ordinal==1||ordinal>=3)return out;
    if(ordinal==0&&arm==Arm::active&&!protocol.issued){bool diagnostic=false;
        if(forecast_valid(in,protocol,ledger,out,diagnostic)){out.option=in.intervention;out.organization=Organization::U;out.intervention=true;out.deliberate_probe=diagnostic;out.standalone=false;out.reason=Rejection::none;}
        return out;}
    if(ordinal==0&&arm==Arm::active){
        const Plan* fallback=&in.layout.independent;
        if(!protocol.intervention_complete||protocol.intervention_failed||!protocol.initial_observed||protocol.initial==Response::unknown||
            (protocol.response_current(in.frame.clock.round)&&protocol.responses.window.record.response==Response::unknown))fallback=&in.layout.generalist;
        out.option=-1;out.standalone=false;out.reason=Rejection::opportunity_unavailable;
        for(int i=0;i<in.offer_count;++i)if(same_plan(in.offers[i].plan,*fallback)&&common::legal_offer(in,in.offers[i])==Rejection::none&&no_purchase(in.offers[i])&&independent(in,in.offers[i])){
            out.option=i;out.organization=fallback->organization;out.standalone=false;out.score=common::evaluate(in,in.offers[i].schedule);out.reason=Rejection::none;break;}
        if(out.option<0&&in.own_income.current(in.frame.clock,in.frame.clock.self)&&same_plan(in.own_income.value.plan,*fallback)&&own_offer(in,in.own_income.value)){
            out.standalone=true;out.organization=fallback->organization;out.score=common::evaluate(in,in.own_income.value.schedule);out.reason=Rejection::none;}
    }
    for(int i=0;i<in.offer_count;++i){const auto& offer=in.offers[i];if(offer.plan.organization!=Organization::H)continue;
        auto rejection=common::legal_offer(in,offer);bool record=false;
        if(rejection==Rejection::none&&(!crew(in,offer)||!no_purchase(offer)||!same_plan(offer.plan,ordinal==0?in.layout.supported:in.layout.helper)))rejection=Rejection::unsafe;
        if(rejection==Rejection::none&&ordinal==0&&arm==Arm::active){if(!collector_allowed(in,protocol,ledger,offer))rejection=Rejection::evidence;else record=true;}
        if(rejection==Rejection::none&&ordinal==2){const auto* collector=in.frame.visible(in.draft.actors[0]);
            if(!collector||collector->position!=offer.plan.pads[crew_index(offer.plan,collector->id)]||!in.collector_proposal.current(in.frame.clock,collector->id)||
                in.collector_proposal.value!=offer.plan.group)rejection=Rejection::evidence;}
        if(rejection==Rejection::none&&arm==Arm::active&&p6::compare(common::evaluate(in,offer.schedule),ordinary_score)<=0)rejection=Rejection::no_gain;
        out.rejected[i]=rejection;if(rejection!=Rejection::none)continue;
        if(out.organization==Organization::H&&p6::compare(common::evaluate(in,offer.schedule),out.score)<=0)continue;
        out.option=i;out.organization=Organization::H;out.standalone=false;out.score=common::evaluate(in,offer.schedule);out.record_episode=record;out.reason=Rejection::none;}
    return out;
}
inline Decision decide(const Inputs& in,Protocol& protocol,EvidenceLedger& ledger,Arm arm){
    if(protocol.local(in,ledger)&&in.frame.clock.self==in.draft.actors[0])common::refresh_evidence(in,ledger);
    return choose(in,protocol,ledger,arm);
}
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,Protocol& protocol,EvidenceLedger& ledger,Arm arm,Execution& execution){
    Admission out;if(!local.fresh()||local.frame.clock!=in.frame.clock||proposed.clock!=in.frame.clock||execution.work.active)return out;
    const auto selected=decide(in,protocol,ledger,arm);if(selected.option!=proposed.option||selected.intervention!=proposed.intervention||selected.deliberate_probe!=proposed.deliberate_probe||
        selected.standalone!=proposed.standalone||selected.secure_inventory!=proposed.secure_inventory||selected.record_episode!=proposed.record_episode)return out;
    const auto* offer=selected.secure_inventory?&in.existing_inventory.value:selected.standalone?&in.own_income.value:selected.option>=0?&in.offers[selected.option]:nullptr;
    if(!offer||!physical_admissible(offer->plan,local))return out;
    if(selected.intervention){if(protocol.issued)return out;if(selected.deliberate_probe&&!protocol.budget.spend(in.frame.clock.round/common::project_rounds))return out;
        protocol.issued=true;protocol.intervention_failed=true;}
    execution.response_deadline=-1;
    if(selected.record_episode){const auto* slot=ledger.state(protocol.slot);if(!slot)return out;Premises premises{offer->entry,in.own_state.value,all_premises};
        execution.response_deadline=(full_current(in,protocol,ledger)?protocol.responses.latest():protocol.initial_round)+64;
        if(!slot->campaign_started&&!ledger.begin_initial_campaign(protocol.slot,in.frame.clock.round,{premises,true,true,true}))return out;
        EpisodeRequest request;request.context=protocol.slot;request.issue_round=in.frame.clock.round;request.deadline=request.issue_round+std::min(common::forecast_window,offer->plan.remaining_rounds);
        request.nominal_duration=offer->plan.forecast_rounds;request.action=int(offer->plan.organization);request.premises=premises;out.episode=ledger.issue(request);if(!out.episode)return out;execution.issued=offer->entry;}
    out.started=execution.work.start(offer->plan,local);out.rejection=out.started?Rejection::none:Rejection::unsafe;
    execution.ordinary=!selected.intervention&&((independent(in,*offer)&&selected.option==ordinary(in).option)||
        (offer->plan.organization==Organization::H&&!selected.deliberate_probe));
    if(out.started&&selected.intervention){protocol.bind(execution.work,selected.deliberate_probe);protocol.intervention_failed=false;}
    if(out.started&&selected.secure_inventory)execution.work.enter(PhysicalStage::bank);
    if(!out.started&&out.episode)ledger.invalidate_active(in.frame.clock.round);
    if(out.started&&out.episode&&!execution.trace.begin(ledger,execution.work)){ledger.invalidate_active(in.frame.clock.round);execution.work.request_stop();}
    return out;
}
inline Command advance(const Inputs& in,const LocalFrame& local,Protocol& protocol,EvidenceLedger& ledger,Arm arm,Execution& execution){
    if(protocol.local(in,ledger)&&in.frame.clock.self==in.draft.actors[0])common::refresh_evidence(in,ledger);
    if(!common::valid_inputs(in)||local.frame.clock!=in.frame.clock||!protocol.local(in,ledger)){execution.work.request_stop();if(ledger.active()&&local.fresh())ledger.invalidate_active(local.frame.clock.round);}
    protocol.limit_before_step(local,execution.work);
    if(arm==Arm::active&&execution.work.active&&execution.work.plan.organization==Organization::H&&execution.work.plan.collector==in.frame.clock.self&&
        (!protocol.ongoing_response_current(in.frame.clock.round)||(execution.response_deadline>=0&&in.frame.clock.round>=execution.response_deadline))){
        execution.work.request_stop();if(ledger.active())ledger.invalidate_active(in.frame.clock.round);}
    return execution.work.step(local);
}
inline Resolution observe(const Inputs& in,const LocalFrame& local,Protocol& protocol,EvidenceLedger& ledger,Execution& execution){
    protocol.observe(in,local,execution.work);execution.trace.observe(local,execution.work,{});
    if(!execution.work.active||!ledger.active())return execution.trace.finish(ledger,in.frame.clock.round,{execution.issued,in.own_state.value,all_premises});return {};
}
}
