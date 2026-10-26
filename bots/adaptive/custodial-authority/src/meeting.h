#pragma once
#include "profile.h"
#include "../../common/src/policy.h"
namespace p8::custodial_authority {
using common::Offer;
struct MeetingSpec {
    Plan original{},successor{},former_u{};
    int helper=-1;
    p6::Point original_pad{},successor_pad{},helper_pad{};
};
inline std::uint64_t meeting_hash(const MeetingSpec& spec){
    if(!plan_valid(spec.original,spec.original.collector)||!plan_valid(spec.successor,spec.successor.collector)||
        !plan_valid(spec.former_u,spec.original.collector))return 0;
    auto h=hash_word(1469598103934665603ULL,plan_route_hash(spec.original));h=hash_word(h,plan_route_hash(spec.successor));
    h=hash_word(h,plan_route_hash(spec.former_u));
    for(const Plan* plan:{&spec.original,&spec.successor,&spec.former_u}){
        for(int value:{int(plan->organization),plan->group,plan->collector,plan->altar,plan->crew_count,
                plan->forecast_rounds,plan->remaining_rounds,plan->capture_window})h=hash_word(h,value);
        for(int i=0;i<plan->crew_count;++i){h=hash_word(h,plan->crew[i]);h=hash_word(h,plan->pads[i].x);h=hash_word(h,plan->pads[i].y);}
    }
    for(int id:{spec.original.collector,spec.successor.collector,spec.helper})h=hash_word(h,id);
    for(auto pad:{spec.original_pad,spec.successor_pad,spec.helper_pad}){h=hash_word(h,pad.x);h=hash_word(h,pad.y);}
    return h;
}
inline bool spec_valid(const MeetingSpec& spec,const DraftRoster& draft){
    if(!draft.valid()||!meeting_hash(spec)||spec.original.organization!=Organization::H||spec.successor.organization!=Organization::H||
        spec.original.collector!=draft.actors[0]||spec.original.group!=spec.successor.group||
        (spec.successor.collector!=draft.actors[1]&&spec.successor.collector!=draft.actors[2])||
        spec.helper!=(spec.successor.collector==draft.actors[1]?draft.actors[2]:draft.actors[1])||
        spec.original.crew_count!=2||spec.successor.crew_count!=2||
        !common::participant(spec.original,spec.helper)||!common::participant(spec.successor,spec.helper)||
        spec.former_u.organization!=Organization::U||spec.former_u.group==spec.original.group||
        spec.former_u.ingress.tiles[0]!=spec.original_pad||
        spec.original.pads[crew_index(spec.original,spec.original.collector)]!=spec.original_pad||
        spec.successor.pads[crew_index(spec.successor,spec.successor.collector)]!=spec.successor_pad||
        spec.successor.pads[crew_index(spec.successor,spec.helper)]!=spec.helper_pad)return false;
    return spec.original_pad!=spec.successor_pad&&spec.original_pad!=spec.helper_pad&&spec.successor_pad!=spec.helper_pad;
}
struct Access {
    int group=-1;
    std::uint64_t retained_return=0;
    p6::Fact<bool> region_visible{},return_executable{},blocker_visible{};
    p6::Point blocked_tile{};
};
inline bool personal(p6::Fact<bool> fact){return fact.source==p6::Source::self_observed;}
inline bool on_return(const Plan& plan,p6::Point tile){
    for(Leg leg:{Leg::bank,Leg::renewal})for(int i=0;i<route_for(plan,leg).count;++i)if(route_for(plan,leg).tiles[i]==tile)return true;
    return false;
}
struct Signal {std::uint64_t meeting=0;int from=-1,to=-1,group=-1;};
struct Inputs:common::Inputs {
    MeetingSpec meeting{};
    p6::Observation<Access> own_access{};
    p6::Observation<Signal> original_offer{},successor_offer{};
    std::array<p6::CycleCertificate,3> meeting_safety{};
    p6::Observation<Offer> private_income{};
    p6::Observation<int> collector_proposal{};
};
struct Authority {
    int owner=-1,collector=-1;
    bool seen_applicable=false,lost=false,transferred=false;
    std::uint64_t accepted_meeting=0;
    int transfer_round=-1;
    explicit Authority(int actor):owner(actor){}
};
inline bool access_current(const Inputs& in){
    const auto& observed=in.own_access;const auto& access=observed.value;
    return in.frame.clock.self==in.draft.actors[0]&&observed.current(in.frame.clock,in.frame.clock.self)&&
        access.group==in.meeting.original.group&&access.retained_return==plan_return_hash(in.meeting.original);
}
inline bool access_available(const Inputs& in){return access_current(in)&&personal(in.own_access.value.region_visible)&&personal(in.own_access.value.return_executable)&&
    in.own_access.value.region_visible.value&&in.own_access.value.return_executable.value;}
inline bool personal_loss(const Inputs& in){
    if(!access_current(in))return false;
    const auto& a=in.own_access.value;
    return (personal(a.region_visible)&&!a.region_visible.value)||(personal(a.return_executable)&&!a.return_executable.value&&
        personal(a.blocker_visible)&&a.blocker_visible.value&&on_return(in.meeting.original,a.blocked_tile));
}
inline bool ledger_local(const Inputs& in,const EvidenceLedger& ledger){
    for(int id:{in.component_slot,in.supported_slot})if(id>=0){const auto* state=ledger.state(id);if(!state||state->declaration.key.actor_id!=in.frame.clock.self)return false;}
    return true;
}
inline void refresh(const Inputs& in,Authority& authority,EvidenceLedger& ledger){
    if(!common::valid_inputs(in)||authority.owner!=in.frame.clock.self||!spec_valid(in.meeting,in.draft)||!ledger_local(in,ledger))return;
    if(authority.collector<0)authority.collector=in.meeting.original.collector;
    common::refresh_evidence(in,ledger);
    if(in.frame.clock.self==in.draft.actors[0]){
        if(access_available(in))authority.seen_applicable=true;
        if(authority.seen_applicable&&personal_loss(in))authority.lost=true;
        if(authority.lost){const auto* state=ledger.state(in.supported_slot);
            if(state){auto key=state->declaration.key;key.return_route_hash=0;
                ledger.observe_context(in.supported_slot,{key,in.own_state.value,all_premises},in.frame.clock.round);}}
    }
}
inline bool own_component(const Inputs& in,const EvidenceLedger& ledger){return common::slot_current(in,ledger,in.component_slot,in.component_entry)&&
    ledger.component_qualified(in.component_slot,in.component_entry,in.frame.clock.round);}
inline bool relevant_component(const Inputs& in,const EvidenceLedger& ledger){
    const auto* state=ledger.state(in.supported_slot);
    return own_component(in,ledger)&&in.component_entry.source_group==in.meeting.successor.group&&
        in.component_entry.return_route_hash!=0&&in.component_entry.return_route_hash==plan_return_hash(in.meeting.successor)&&
        in.supported_entry.actor_id==in.frame.clock.self&&in.supported_entry.route_hash==plan_route_hash(in.meeting.successor)&&
        route_component_matches(in.component_entry,in.supported_entry)&&state&&state->declaration.key==in.supported_entry&&
        state->declaration.kind==EpisodeKind::full_organization&&state->declaration.require_supported_work;
}
inline bool signal_current(const Inputs& in,const p6::Observation<Signal>& signal,int actor){
    const auto& s=signal.value;return signal.current(in.frame.clock,actor)&&s.meeting==meeting_hash(in.meeting)&&
        s.from==in.meeting.original.collector&&s.to==in.meeting.successor.collector&&s.group==in.meeting.original.group;
}
inline const p6::CycleCertificate* meeting_certificate(const Inputs& in,int actor){
    for(const auto& safe:in.meeting_safety)if(safe.actor==actor)return &safe;return nullptr;
}
inline bool meeting_visible(const Inputs& in,const Work& work){
    if(!common::valid_inputs(in)||!spec_valid(in.meeting,in.draft)||work.active||in.frame.own.actor.bag!=0||in.frame.own.actor.channel!=p6::Channel::none)return false;
    for(int actor:{in.meeting.original.collector,in.meeting.successor.collector}){
        const auto* observed=in.frame.visible(actor);const auto* safe=meeting_certificate(in,actor);
        const int region=actor==in.meeting.original.collector?in.meeting.former_u.group:in.meeting.successor.group;
        if(!observed||!roster_actor(*observed,in.draft)||!observed->alive||observed->bag!=0||observed->channel!=p6::Channel::none||
            !safe||safe->group!=region||!safe->valid(in.frame,actor,region)||!independent_safety(*safe)||
            observed->position!=(actor==in.meeting.original.collector?in.meeting.original_pad:in.meeting.successor_pad))return false;
    }
    if(in.frame.clock.self==in.meeting.helper&&in.frame.own.actor.position!=in.meeting.helper_pad)return false;
    return signal_current(in,in.original_offer,in.meeting.original.collector)&&signal_current(in,in.successor_offer,in.meeting.successor.collector);
}
inline bool may_offer(const Inputs& in,const Authority& authority,const EvidenceLedger& ledger,const Work& work){
    if(!common::valid_inputs(in)||authority.owner!=in.frame.clock.self||authority.transferred||work.active||
        in.frame.own.actor.bag>0||in.frame.own.actor.channel!=p6::Channel::none||!spec_valid(in.meeting,in.draft))return false;
    if(in.frame.clock.self==in.meeting.original.collector)return authority.lost&&in.frame.own.actor.position==in.meeting.original_pad;
    return in.frame.clock.self==in.meeting.successor.collector&&relevant_component(in,ledger)&&in.frame.own.actor.position==in.meeting.successor_pad;
}
inline bool commit_meeting(const Inputs& in,Authority& authority,EvidenceLedger& ledger,common::Arm arm,const Work& work){
    refresh(in,authority,ledger);
    if(arm!=common::Arm::active||authority.owner!=in.frame.clock.self||!ledger_local(in,ledger)||authority.transferred||!meeting_visible(in,work))return false;
    if(in.frame.clock.self==in.meeting.original.collector){if(!authority.lost)return false;}
    else if(in.frame.clock.self==in.meeting.successor.collector){if(!relevant_component(in,ledger))return false;}
    else if(in.frame.clock.self!=in.meeting.helper)return false;
    authority.transferred=true;authority.collector=in.meeting.successor.collector;authority.accepted_meeting=meeting_hash(in.meeting);
    authority.transfer_round=in.frame.clock.round;return true;
}
}
