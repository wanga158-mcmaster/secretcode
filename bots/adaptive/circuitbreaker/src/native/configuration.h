#pragma once
#include "../policy.h"
#include "../../../common/src/coalition.h"
namespace p8::circuitbreaker::native {
using namespace p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
inline bool independent_offer(Offer& out,const Solo& own,std::span<const Solo> models,const p6::Frame& frame) {
    out={};out.plan=own.plan;out.schedule.target_id=own.plan.group;int prefix=0;bool self=false;
    for(const auto& model:models) {
        if(!model.feasible||out.safety_count==p6::max_actors)return false;
        auto copy=model;normalize(copy,frame);self|=copy.model.schedule.actors[0].actor_id==frame.clock.self;
        if(!append(out.schedule,copy.model.schedule,0,prefix))return false;
        out.safety[out.safety_count++]=copy.model.certificate;prefix+=64;
    }
    return self&&queue_alternatives(out,models,frame);
}
inline TransitionEnvelope envelope(const NumericState& state,p6::Champion champion,p6::Gems payload) {
    const double maximum_bag=double(std::max<p6::Gems>(payload,state.bag));
    const double focus=state.focus+(champion==p6::Champion::prism?
        double(cambc::constants::passive_abilities::prism_focus_per_gem)*maximum_bag:0.);
    return {{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},
        {state.strength,state.strength},{state.focus,focus},{state.growth,state.growth},{0,maximum_bag},0};
}
inline ContextDeclaration declaration(const Plan& plan,const Inputs& in,bool joint,p6::Gems payload) {
    ContextDeclaration d;auto& k=d.key;const auto& state=in.own_state.value;
    k.settings_id=in.settings_identity.value;k.route_hash=plan_route_hash(plan);k.return_route_hash=plan_return_hash(plan);
    k.actor_id=in.frame.clock.self;k.champion=int(in.frame.own.actor.type);k.role=joint?1:0;k.source_group=plan.group;
    k.route_template=0;k.endpoint=plan.altar;k.threat_regime=in.threat_regime.value;k.participants=plan.crew;k.participant_count=plan.crew_count;
    std::sort(k.participants.begin(),k.participants.begin()+k.participant_count);
    k.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};k.hp_band=health_band(state.health,state.max_health);
    d.envelope=envelope(state,in.frame.own.actor.type,payload);d.kind=joint?EpisodeKind::full_organization:EpisodeKind::return_component;
    d.require_spawn_shop=true;d.require_supported_work=joint;return d;
}
inline bool subsystem_plan(Subsystem& out,const Plan& plan,const Inputs& in,p6::Gems payload) {
    out={};out.plan=plan;
    if(!plan_valid(out.plan,in.frame.clock.self))return false;
    out.declaration=declaration(out.plan,in,true,payload);
    for(int i=0;i<3;++i) {
        const int actor=out.declaration.key.participants[i];const auto* actual=member_state(in,actor);const auto* seen=in.frame.visible(actor);
        if(!actual||!seen||!actual->current(in.frame.clock,actor)||!valid_numeric_state(actual->value))return false;
        out.members[i]={actor,seen->type,{actual->value.equipment_hash,actual->value.max_health,actual->value.strength,actual->value.focus,actual->value.growth},
            envelope(actual->value,seen->type,payload)};
    }
    return subsystem_valid(out,in.draft,in.frame.clock.self);
}
inline bool subsystem(Subsystem& out,const Routes& routes,int site,int bank,const Inputs& in) {
    auto canonical=in.frame;canonical.own.actor.position=routes.sites[site].approach.destination;
    const auto plan=formation_plan(routes,site,bank,canonical,in.draft,Organization::J,true);
    return subsystem_plan(out,plan,in,routes.sites[site].group.gem_value());
}
// A full-payload U comparator on the exact retained J duty route. Additional
// walking is charged, never a discount or a reason to omit the shorter solo.
inline Solo retained_solo(const Routes& routes,int site,int bank,const p6::Frame& frame,const Plan& retained) {
    auto out=solo(routes,site,bank,frame,true);if(!out.feasible||retained.ingress.tiles[0]!=frame.own.actor.position)return {};
    const auto& camps=routes.sites[site].group;const auto pad=retained.ingress.tiles[retained.ingress.count-1];double incoming=out.ambient_incoming;
    for(const auto& camp:camps) {
        if(distance(pad,point(camp.spawn_position()))>frame.own.actor.attack_range||
            !cambc::map::walls().shadow_cast(tile(pad),frame.own.actor.attack_range).is_set(camp.spawn_position()))return {};
        incoming+=camp.is_visible()?double(camp.strength()):double(camp.base_stats().base_strength);
    }
    const int before=std::max(0,walking(retained.ingress)-walking(out.plan.ingress))+std::max(0,walking(retained.capture)-walking(out.plan.capture));
    const int bank_extra=std::max(0,walking(retained.bank)-walking(out.plan.bank));
    const int renew_extra=std::max(0,walking(retained.renewal)-walking(out.plan.renewal));
    for(int i=0;i<out.model.schedule.production_count;++i)out.model.schedule.productions[i].tick+=before;
    auto& actor=out.model.schedule.actors[0];actor.occupied_turns.value+=before+bank_extra+renew_extra;
    for(int i=0;i<actor.event_count;++i) {
        auto& event=actor.events[i];event.tick+=before;
        if(event.kind!=p6::EventKind::production)event.tick+=bank_extra;
        if(event.kind==p6::EventKind::renewal)event.tick+=renew_extra;
    }
    for(auto* exposure:{&out.model.certificate.clear,&out.model.certificate.hold,&out.model.certificate.bank,&out.model.certificate.recovery})
        exposure->incoming_total+=incoming*(before+bank_extra+renew_extra);
    out.plan=retained;out.plan.organization=Organization::U;out.plan.collector=frame.clock.self;
    out.plan.crew={frame.clock.self,-1,-1,-1,-1,-1};out.plan.crew_count=1;out.plan.release={};
    out.plan.forecast_rounds=int(actor.occupied_turns.value);out.plan.remaining_rounds=std::min(120,int(cambc::constants::max_rounds)-frame.clock.round);
    out.feasible=plan_valid(out.plan,frame.clock.self)&&out.model.certificate.valid(frame,frame.clock.self,out.plan.group);return out;
}
} // namespace p8::circuitbreaker::native
