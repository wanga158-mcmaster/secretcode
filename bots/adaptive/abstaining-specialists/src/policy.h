#pragma once
#include "context.h"
#include "../../earned-center/src/trace.h"
namespace p8::abstaining_specialists {
using CycleTrace=earned_center::CycleTrace;
enum class Purchase { none=-1,specialist=0,generalist=1,retaining_generalist=2 };
struct Decision:common::Decision {bool secure_inventory=false,private_income=false;};
struct Execution {Work work{};CycleTrace trace{};ContextKey issued{};
    bool confirm(const Command& c,const ActionReceipt& r){return work.confirm(c,r);}};
inline bool no_extra(const Offer& o){
    if(o.capital_cost||o.diagnostic_segment||o.marked_extra_turns)return false;
    for(int a=0;a<o.schedule.actor_count;++a)for(int e=0;e<o.schedule.actors[a].event_count;++e)
        if(o.schedule.actors[a].events[e].kind==p6::EventKind::purchase)return false;
    return true;
}
inline bool independent(const Inputs& in,const Offer& o){const auto* safe=common::certificate(o,in.frame.clock.self);
    return (o.plan.organization==Organization::G||o.plan.organization==Organization::U)&&o.plan.crew_count==1&&
        o.plan.collector==in.frame.clock.self&&safe&&safe->group==o.plan.group&&independent_safety(*safe);
}
inline bool standalone(const Inputs& in,const Offer& o){const auto* safe=common::certificate(o,in.frame.clock.self);
    return plan_valid(o.plan,in.frame.clock.self)&&independent(in,o)&&no_extra(o)&&o.safety_count==1&&safe&&safe->valid(in.frame,in.frame.clock.self,o.plan.group)&&
        o.schedule.actor_count==1&&o.schedule.actors[0].actor_id==in.frame.clock.self&&o.schedule.target_id==o.plan.group&&
        o.plan.remaining_rounds<=in.settings.max_rounds-in.frame.clock.round&&common::evaluate(in,o.schedule).eligible();
}
inline bool bank_available(const Inputs& in,const Specialist& state){
    if(!in.existing_inventory.current(in.frame.clock,in.frame.clock.self))return false;
    const auto& o=in.existing_inventory.value;return independent(in,o)&&no_extra(o)&&generalist_now(in,state,o.plan)&&(common::legal_offer(in,o)==Rejection::none||standalone(in,o))&&
        o.plan.ingress.count==1&&o.plan.capture.count==1&&o.plan.ingress.tiles[0]==in.frame.own.actor.position&&o.plan.capture.tiles[0]==in.frame.own.actor.position;
}
inline bool crew_current(const Inputs& in,const Offer& o){
    const auto* collector=in.frame.visible(o.plan.collector);
    if(o.plan.organization!=Organization::H||!collector||!carry_actor(*collector,in.draft)||o.plan.crew_count!=2)return false;
    for(int i=0;i<o.plan.crew_count;++i){const auto* a=in.frame.visible(o.plan.crew[i]);const auto* safe=common::certificate(o,o.plan.crew[i]);
        if(!a||!roster_actor(*a,in.draft)||!a->alive||a->bag!=0||a->channel!=p6::Channel::none||a->pick_ordinal>=3||
            !safe||safe->group!=o.plan.group||!safe->valid(in.frame,a->id,o.plan.group)||!independent_safety(*safe))return false;}
    return true;
}
inline bool future_crew(const Inputs& in,const Configuration& configuration){
    for(int i=0;i<in.offer_count;++i)if(exact(in.offers[i],configuration)&&common::legal_offer(in,in.offers[i])==Rejection::none&&crew_current(in,in.offers[i]))return true;
    return false;
}
inline void refresh(const Inputs& in,Specialist& state,EvidenceLedger& ledger){
    if(!common::valid_inputs(in)||!carry_actor(in.frame.own.actor,in.draft)||!local_ledger(in,state,ledger))return;
    for(const auto& c:state.supported){const auto* slot=ledger.state(c.context);if(!slot)continue;
        auto key=c.entry;key.settings_id=in.settings_identity.value;key.threat_regime=in.threat_regime.value;
        if(!ledger.active()||ledger.active()->context!=c.context){const auto& s=in.own_state.value;
            key.build={s.equipment_hash,s.max_health,s.strength,s.focus,s.growth};key.hp_band=health_band(s.health,s.max_health);}
        PremiseMask mask=all_premises;for(int i=0;i<key.participant_count;++i)if(!in.frame.visible(key.participants[i]))mask&=~membership_known;
        ledger.observe_context(c.context,{key,in.own_state.value,mask},in.frame.clock.round);}
    for(int i=0;i<2;++i){const auto* slot=ledger.state(state.supported[i].context);if(!slot||(!slot->campaign_started&&!(i==1&&state.specialist_bought)))continue;
        const bool ongoing=ledger.active()&&ledger.active()->context==state.supported[i].context;
        const bool actual_build=in.own_state.value.equipment_hash==state.supported[i].entry.build.equipment_hash;
        const bool attribution=in.return_attribution_available.current(in.frame.clock,in.frame.clock.self)&&in.return_attribution_available.value;
        if((ongoing||actual_build)&&(ledger.suspended(state.supported[i].context,in.frame.clock.round/common::project_rounds)||
            !loaded_now(in,in.loaded[i],slot->declaration,state.supported[i].plan)||!attribution))state.abstaining=true;
    }
}
inline bool compare_purchase(const Inputs& in,const PurchaseComparison& comparison,p6::Gems cost,int physical_duration){
    if(!p6::same_actors(comparison.changed,comparison.unchanged)||cost<=0)return false;
    const auto yes=common::evaluate(in,comparison.changed),no=common::evaluate(in,comparison.unchanged);
    if(!yes.eligible()||!no.eligible()||p6::compare(yes,no)<=0)return false;
    int count=0;bool occupied=false;
    for(int a=0;a<comparison.changed.actor_count;++a){const auto& actor=comparison.changed.actors[a];
        if(actor.actor_id==in.frame.clock.self)occupied=actor.occupied_turns.known()&&actor.occupied_turns.value>=physical_duration+1;
        for(int e=0;e<actor.event_count;++e){const auto& event=actor.events[e];if(event.kind!=p6::EventKind::purchase)continue;
            if(actor.actor_id!=in.frame.clock.self||!event.gold_cost.known()||event.gold_cost.value!=cost)return false;
            ++count;}}
    return count==1&&occupied;
}
inline bool prepare_specialist(const Inputs& in,const LocalFrame& local,Specialist& state,const EvidenceLedger& ledger,const Work& work){
    if(!common::valid_inputs(in)||!carry_actor(in.frame.own.actor,in.draft)||!local_ledger(in,state,ledger)||state.permit.frozen||
        state.abstaining||!reversible_workshop::safe_shop(in,local,work)||in.market.inventory.value!=state.original||
        !qualified(in,ledger,state.supported[0])||!future_crew(in,state.supported[1]))return false;
    const auto* source=ledger.state(state.supported[0].context);const auto* target=ledger.state(state.supported[1].context);
    if(!source||!target||!loaded_forecast(in,in.loaded[1],target->declaration,state.supported[1].plan))return false;
    const auto funding=reversible_workshop::funded(in,state.original,state.supported[1].inventory);
    if(!funding.valid||funding.refunds||!compare_purchase(in,in.purchases[0],funding.purchases,state.supported[1].plan.forecast_rounds))return false;
    state.permit={true,false,false,source->latest_positive,state.supported[0].entry,state.supported[1].entry,
        state.original,state.supported[1].inventory,in.frame.own.gold,funding.purchases};return true;
}
inline Purchase purchase_choice(const Inputs& in,const Specialist& state,const EvidenceLedger& ledger,Arm arm){
    if(!common::valid_inputs(in)||!carry_actor(in.frame.own.actor,in.draft)||!local_ledger(in,state,ledger)||
        !reversible_workshop::own_market(in)||state.rebuild.active||state.rebuild.pending)return Purchase::none;
    const auto& inventory=in.market.inventory.value;
    if(arm==Arm::active&&state.abstaining){
        const int index=inventory==state.original?0:inventory==state.supported[1].inventory?1:-1;
        if(index<0)return Purchase::none;
        const auto funds=reversible_workshop::funded(in,inventory,state.generalist[index].inventory);
        if(funds.valid&&!funds.refunds&&compare_purchase(in,in.purchases[index+1],funds.purchases,state.generalist[index].plan.forecast_rounds))
            return index?Purchase::retaining_generalist:Purchase::generalist;
        return Purchase::none;
    }
    // Control retains the pre-frozen purchase permission across a model-only
    // contradiction. Its original expiry, own exact build and present funding
    // remain binding; it never fabricates qualification for the new build.
    if(!permit_current(in,state)||inventory!=state.permit.from||in.frame.own.gold<state.permit.cost||!future_crew(in,state.supported[1]))return Purchase::none;
    const auto* target=ledger.state(state.supported[1].context);
    if(!target||!loaded_forecast(in,in.loaded[1],target->declaration,state.supported[1].plan))return Purchase::none;
    if(in.own_state.value.equipment_hash!=state.permit.source.build.equipment_hash||
        in.own_state.value.max_health!=state.permit.source.build.max_health||in.own_state.value.strength!=state.permit.source.build.strength||
        in.own_state.value.focus!=state.permit.source.build.focus||in.own_state.value.growth!=state.permit.source.build.growth)return Purchase::none;
    const auto funds=reversible_workshop::funded(in,inventory,state.permit.to);
    return funds.valid&&!funds.refunds&&funds.purchases==state.permit.cost&&compare_purchase(in,in.purchases[0],funds.purchases,state.supported[1].plan.forecast_rounds)?Purchase::specialist:Purchase::none;
}
inline bool begin_purchase(const Inputs& in,const LocalFrame& local,Specialist& state,EvidenceLedger& ledger,Arm arm,const Work& work){
    refresh(in,state,ledger);const auto choice=purchase_choice(in,state,ledger,arm);if(choice==Purchase::none||ledger.active())return false;
    const auto& target=choice==Purchase::specialist?state.supported[1]:state.generalist[choice==Purchase::generalist?0:1];
    if(!state.rebuild.begin(in,local,work,target.inventory))return false;
    state.pending_purchase=int(choice);state.transaction_target=target.entry.build;return true;
}
inline TransactionCommand purchase_step(const Inputs& in,const LocalFrame& local,Specialist& state,const Work& work){return state.rebuild.next(in,local,work);}
inline bool confirm_purchase(const TransactionCommand& c,const TransactionReceipt& receipt,const Inputs& after,Specialist& state){
    if(state.pending_purchase<0||!state.rebuild.confirm(c,receipt,after))return false;
    if(!state.rebuild.completed)return true;
    const auto& actual=after.own_state.value;const EntryBuild build{actual.equipment_hash,actual.max_health,actual.strength,actual.focus,actual.growth};
    if(build!=state.transaction_target){state.rebuild.interrupt();state.permit.abandoned=true;state.abstaining=true;return false;}
    if(state.pending_purchase==int(Purchase::specialist)){state.permit.used=true;state.specialist_bought=true;}
    else state.permit.abandoned=true;
    state.pending_purchase=-1;return true;
}
inline bool collector_gate(const Inputs& in,const Specialist& state,const EvidenceLedger& ledger,const Offer& o,Arm arm,bool& record){
    if(!local_ledger(in,state,ledger))return false;
    for(int i=0;i<2;++i){const auto& c=state.supported[i];if(!exact(o,c))continue;
        const auto* slot=ledger.state(c.context);
        if(!slot||!common::context_matches(in,o)||!loaded_now(in,in.loaded[i],slot->declaration,o.plan))return false;
        if(arm==Arm::active&&(!in.return_attribution_available.current(in.frame.clock,in.frame.clock.self)||!in.return_attribution_available.value))return false;
        if(arm==Arm::active&&state.abstaining)return false;
        if(i==1&&!state.specialist_bought)return false;
        if(arm==Arm::control){record=false;return true;}
        if(i==1&&!qualified(in,ledger,c)){
            int duration=o.plan.forecast_rounds;for(int h=0;h<slot->declaration.hypothesis_count;++h)
                if(slot->consistent[h])duration=std::max(duration,o.plan.forecast_rounds+slot->declaration.hypotheses[h].offset.upper);
            if(!state.permit.used||state.permit.target!=c.entry||state.permit.proof_round<0||
                in.frame.clock.round<state.permit.proof_round||std::int64_t(in.frame.clock.round)+duration>=std::int64_t(state.permit.proof_round)+qualification_rounds)return false;
        }
        if(ledger.suspended(c.context,in.frame.clock.round/common::project_rounds)||
            (ledger.campaign_exhausted(c.context)&&!qualified(in,ledger,c)))return false;
        record=true;return true;
    }
    return false;
}
inline Decision choose(const Inputs& in,const Specialist& state,const EvidenceLedger& ledger,Arm arm){
    Decision out;out.clock=in.frame.clock;if(!common::valid_inputs(in)){out.reason=Rejection::malformed;return out;}
    if(in.frame.own.actor.bag>0){out.secure_inventory=bank_available(in,state);out.reason=out.secure_inventory?Rejection::none:Rejection::resources;return out;}
    for(int i=0;i<in.offer_count;++i){const auto& o=in.offers[i];auto rejection=common::legal_offer(in,o);
        if(rejection==Rejection::none&&(!independent(in,o)||!no_extra(o)||!generalist_now(in,state,o.plan)))rejection=Rejection::evidence;
        if(rejection==Rejection::none&&arm==Arm::active&&carry_actor(in.frame.own.actor,in.draft)&&state.abstaining&&
            (!state.declared||!same_plan(o.plan,state.generalist[state.specialist_bought?1:0].plan)))rejection=Rejection::evidence;
        out.rejected[i]=rejection;if(rejection!=Rejection::none)continue;
        const auto score=common::evaluate(in,o.schedule);if(out.option<0||p6::compare(score,out.score)>0){out.option=i;out.organization=o.plan.organization;out.score=score;out.reason=Rejection::none;}}
    if(out.option<0&&in.private_income.current(in.frame.clock,in.frame.clock.self)&&standalone(in,in.private_income.value)&&
        generalist_now(in,state,in.private_income.value.plan)&&
        (!carry_actor(in.frame.own.actor,in.draft)||arm==Arm::control||!state.abstaining||
            (state.declared&&same_plan(in.private_income.value.plan,state.generalist[state.specialist_bought?1:0].plan)))){
        out.private_income=true;out.organization=in.private_income.value.plan.organization;out.reason=Rejection::none;out.score=common::evaluate(in,in.private_income.value.schedule);}
    if(out.option<0||in.frame.own.actor.pick_ordinal>=3)return out;
    const auto ordinary=out.score;
    for(int i=0;i<in.offer_count;++i){const auto& o=in.offers[i];if(o.plan.organization!=Organization::H)continue;
        auto rejection=common::legal_offer(in,o);bool record=false;
        if(rejection==Rejection::none&&(!crew_current(in,o)||!no_extra(o)))rejection=Rejection::unsafe;
        if(rejection==Rejection::none&&o.plan.collector==in.frame.clock.self){if(!collector_gate(in,state,ledger,o,arm,record))rejection=Rejection::evidence;}
        else if(rejection==Rejection::none){const auto* leader=in.frame.visible(o.plan.collector);const int pad=crew_index(o.plan,o.plan.collector);
            if(!leader||pad<0||leader->position!=o.plan.pads[pad]||!in.collector_proposal.current(in.frame.clock,leader->id)||
                in.collector_proposal.value!=o.plan.group)rejection=Rejection::evidence;}
        if(rejection==Rejection::none&&p6::compare(common::evaluate(in,o.schedule),ordinary)<=0)rejection=Rejection::no_gain;
        out.rejected[i]=rejection;if(rejection!=Rejection::none)continue;
        out.option=i;out.organization=Organization::H;out.score=common::evaluate(in,o.schedule);out.reason=Rejection::none;out.record_episode=record;}
    return out;
}
inline Decision decide(const Inputs& in,Specialist& state,EvidenceLedger& ledger,Arm arm){refresh(in,state,ledger);return choose(in,state,ledger,arm);}
inline Admission commit(const Inputs& in,const LocalFrame& local,const Decision& proposed,Specialist& state,EvidenceLedger& ledger,Arm arm,Execution& execution){
    Admission out;if(!local.fresh()||local.frame.clock!=in.frame.clock||proposed.clock!=in.frame.clock||execution.work.active||state.rebuild.active)return out;
    const auto selected=decide(in,state,ledger,arm);if(selected.option!=proposed.option||selected.secure_inventory!=proposed.secure_inventory||selected.private_income!=proposed.private_income||selected.record_episode!=proposed.record_episode)return out;
    const auto* offer=selected.secure_inventory?&in.existing_inventory.value:selected.private_income?&in.private_income.value:selected.option>=0?&in.offers[selected.option]:nullptr;
    if(!offer||!physical_admissible(offer->plan,local))return out;
    if(selected.record_episode){const auto* slot=ledger.state(offer->context_slot);if(!slot)return out;
        Premises premises{offer->entry,in.own_state.value,all_premises};
        if(!slot->campaign_started&&!ledger.begin_initial_campaign(offer->context_slot,in.frame.clock.round,{premises,true,true,true}))return out;
        EpisodeRequest request;request.context=offer->context_slot;request.issue_round=in.frame.clock.round;request.deadline=request.issue_round+std::min(common::forecast_window,offer->plan.remaining_rounds);
        request.action=int(offer->plan.organization);request.nominal_duration=offer->plan.forecast_rounds;request.premises=premises;
        out.episode=ledger.issue(request);if(!out.episode)return out;execution.issued=offer->entry;state.trial_seen=true;}
    out.started=execution.work.start(offer->plan,local);out.rejection=out.started?Rejection::none:Rejection::unsafe;
    if(out.started&&selected.secure_inventory)execution.work.enter(PhysicalStage::bank);
    if(!out.started&&out.episode)ledger.invalidate_active(in.frame.clock.round);
    if(out.started&&out.episode&&!execution.trace.begin(ledger,execution.work)){ledger.invalidate_active(in.frame.clock.round);execution.work.request_stop();}
    return out;
}
inline Command advance(const Inputs& in,const LocalFrame& local,Specialist& state,EvidenceLedger& ledger,Arm arm,Execution& execution){
    refresh(in,state,ledger);
    if(!common::valid_inputs(in)||local.frame.clock!=in.frame.clock){execution.work.request_stop();if(ledger.active()&&local.fresh())ledger.invalidate_active(local.frame.clock.round);}
    if(execution.work.active&&(execution.work.plan.organization==Organization::G||execution.work.plan.organization==Organization::U)&&
        !generalist_now(in,state,execution.work.plan))execution.work.request_stop();
    if(arm==Arm::active&&state.abstaining&&execution.work.active&&execution.work.plan.organization==Organization::H&&execution.work.plan.collector==in.frame.clock.self){
        execution.work.request_stop();if(ledger.active())ledger.invalidate_active(in.frame.clock.round);}
    return execution.work.step(local);
}
inline Resolution observe(const Inputs& in,const LocalFrame& local,EvidenceLedger& ledger,Execution& execution){
    execution.trace.observe(local,execution.work,{});
    if(!execution.work.active||!ledger.active())return execution.trace.finish(ledger,in.frame.clock.round,{execution.issued,in.own_state.value,all_premises});return {};
}
}
