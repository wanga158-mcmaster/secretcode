#pragma once
#include "../policy.h"
#include "../../../common/src/coalition.h"
#include "../../../common/src/sdk_shop.h"
namespace p8::requalification_reserve::native {
using namespace p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
inline DraftRoster geometry_roster(DraftRoster draft,int helper) {
    if(helper>=0&&helper<draft.picks){std::swap(draft.actors[2],draft.actors[helper]);std::swap(draft.champions[2],draft.champions[helper]);}
    return draft;
}
inline bool independent_offer(Offer& out,const Solo& own,std::span<const Solo> models,const p6::Frame& frame) {
    out={};out.plan=own.plan;out.schedule.target_id=own.plan.group;int prefix=0;bool self=false;
    for(const auto& baseline:models) {
        if(!baseline.feasible||out.safety_count==p6::max_actors)return false;
        auto copy=baseline;normalize(copy,frame);self|=copy.model.schedule.actors[0].actor_id==frame.clock.self;
        if(!append(out.schedule,copy.model.schedule,0,prefix))return false;
        out.safety[out.safety_count++]=copy.model.certificate;prefix+=64;
    }
    return self&&queue_alternatives(out,models,frame);
}
inline Plan owner_plan(const Routes& routes,int site,int bank,const p6::Frame& frame,const DraftRoster& draft,int helper) {
    auto plan=formation_plan(routes,site,bank,frame,geometry_roster(draft,helper),Organization::H,true);
    if(!plan_valid(plan,frame.clock.self)||frame.clock.self!=draft.actors[0])return {};
    plan.renewal=Routes::join(routes.route(routes.banks[bank].approach.destination,routes.spawn),
        routes.route(routes.spawn.destination,routes.sites[site].rally[0]));
    return plan_valid(plan,frame.clock.self)?plan:Plan{};
}
// Ordinary U and H have the same real productive entry. Keeping a potentially
// longer old charge is conservative; no shorter route becomes invented income.
inline Solo retained_solo(const Routes& routes,int site,int bank,const p6::Frame& frame,const DraftRoster& draft) {
    auto out=solo(routes,site,bank,frame,true);if(!out.feasible)return {};
    auto retained=owner_plan(routes,site,bank,frame,draft,3);if(!plan_valid(retained,frame.clock.self))return {};
    const auto pad=retained.ingress.tiles[retained.ingress.count-1];double incoming=out.ambient_incoming;
    for(const auto& camp:routes.sites[site].group) {
        if(distance(pad,point(camp.spawn_position()))>frame.own.actor.attack_range||
            !cambc::map::walls().shadow_cast(tile(pad),frame.own.actor.attack_range).is_set(camp.spawn_position()))return {};
        incoming+=camp.is_visible()?double(camp.strength()):double(camp.base_stats().base_strength);
    }
    const int before=std::max(0,walking(retained.ingress)-walking(out.plan.ingress))+std::max(0,walking(retained.capture)-walking(out.plan.capture));
    const int bank_extra=std::max(0,walking(retained.bank)-walking(out.plan.bank));
    const int renew_extra=std::max(0,walking(retained.renewal)-walking(out.plan.renewal));
    const int extra=before+bank_extra+renew_extra;
    for(int i=0;i<out.model.schedule.production_count;++i)out.model.schedule.productions[i].tick+=before;
    auto& actor=out.model.schedule.actors[0];actor.occupied_turns.value+=extra;
    for(int i=0;i<actor.event_count;++i) {
        auto& event=actor.events[i];event.tick+=before;
        if(event.kind!=p6::EventKind::production)event.tick+=bank_extra;
        if(event.kind==p6::EventKind::renewal)event.tick+=renew_extra;
    }
    for(auto* exposure:{&out.model.certificate.clear,&out.model.certificate.hold,&out.model.certificate.bank,&out.model.certificate.recovery})
        exposure->incoming_total+=incoming*extra;
    retained.organization=Organization::U;retained.crew={frame.clock.self,-1,-1,-1,-1,-1};retained.crew_count=1;retained.release={};
    retained.forecast_rounds=int(actor.occupied_turns.value);out.plan=retained;
    out.feasible=actor.occupied_turns.value<=std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round)&&
        plan_valid(out.plan,frame.clock.self)&&out.model.certificate.valid(frame,frame.clock.self,out.plan.group);return out;
}
inline Coalition supported(const Routes& routes,int site,int bank,const p6::Frame& frame,const DraftRoster& draft,
                           std::span<const Solo> models,int helper) {
    auto out=coalition(routes,site,bank,frame,geometry_roster(draft,helper),models,Organization::H,true);
    if(!out.feasible)return out;
    const auto* owner=frame.visible(draft.actors[0]);if(!owner){out.feasible=false;return out;}
    const auto retained=owner_plan(routes,site,bank,subject(frame,*owner),draft,helper);
    const auto ordinary=formation_plan(routes,site,bank,subject(frame,*owner),geometry_roster(draft,helper),Organization::H,true);
    if(!plan_valid(retained,owner->id)||!plan_valid(ordinary,owner->id)){out.feasible=false;return out;}
    const int extra=std::max(0,walking(retained.renewal)-walking(ordinary.renewal));
    const int index=p6::actor_index(out.offer.schedule,owner->id);if(index<0){out.feasible=false;return out;}
    auto& actor=out.offer.schedule.actors[index];actor.occupied_turns.value+=extra;
    for(int e=0;e<actor.event_count;++e)if(actor.events[e].kind==p6::EventKind::renewal)actor.events[e].tick+=extra;
    double ambient=0;for(const auto& model:models)if(model.model.schedule.actors[0].actor_id==owner->id)ambient=model.ambient_incoming;
    for(const auto& camp:routes.sites[site].group)
        ambient+=camp.is_visible()?double(camp.strength()):double(camp.base_stats().base_strength);
    for(int c=0;c<out.offer.safety_count;++c)if(out.offer.safety[c].actor==owner->id) {
        out.offer.safety[c].recovery.incoming_total+=ambient*extra;
        if(!out.offer.safety[c].valid(frame,owner->id,out.offer.plan.group)){out.feasible=false;return out;}
    }
    if(frame.clock.self==owner->id){out.offer.plan.renewal=retained.renewal;out.offer.plan.forecast_rounds=int(actor.occupied_turns.value);}
    out.feasible=actor.occupied_turns.value<=std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round)&&
        plan_valid(out.offer.plan,frame.clock.self);return out;
}
inline ContextDeclaration declaration(const Plan& plan,const sdk::Snapshot& snapshot,const Inputs& in,p6::Gems maximum_bag) {
    ContextDeclaration d;auto& k=d.key;const auto& state=snapshot.own;
    k.settings_id=in.settings_identity.value;k.route_hash=plan_route_hash(plan);k.return_route_hash=plan_return_hash(plan);
    k.actor_id=snapshot.frame.clock.self;k.champion=int(snapshot.frame.own.actor.type);k.role=1;k.source_group=plan.group;
    k.route_template=0;k.endpoint=plan.altar;k.threat_regime=in.threat_regime.value;k.participants=plan.crew;k.participant_count=plan.crew_count;
    std::sort(k.participants.begin(),k.participants.begin()+k.participant_count);
    k.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};k.hp_band=health_band(state.health,state.max_health);
    const double focus=state.focus+(snapshot.frame.own.actor.type==p6::Champion::prism?
        double(cambc::constants::passive_abilities::prism_focus_per_gem)*double(maximum_bag):0.);
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},{state.strength,state.strength},
        {state.focus,focus},{state.growth,state.growth},{0,double(maximum_bag)},0};
    d.kind=EpisodeKind::full_organization;d.require_supported_work=true;return d;
}
} // namespace p8::requalification_reserve::native
