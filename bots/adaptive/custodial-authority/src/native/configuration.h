#pragma once
#include "../policy.h"
#include "../../../common/src/coalition.h"
#include <memory>
namespace p8::custodial_authority::native {
namespace paths=p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
struct Geometry {
    int custody=-1,bank=-1,former=-1,former_bank=-1,helper=-1,helper_bank=-1,backup=-1,backup_bank=-1;
    int successor_ordinal=1,helper_ordinal=2;
    paths::Field candidate_home{};MeetingSpec meeting{};bool ready=false,home_chosen=false;
};
inline DraftRoster organization_roster(const DraftRoster& draft,const Geometry& g,int collector) {
    auto out=draft;const int index=collector==draft.actors[0]?0:g.successor_ordinal;
    out.actors[0]=collector;out.champions[0]=draft.champions[index];
    out.actors[2]=draft.actors[g.helper_ordinal];out.champions[2]=draft.champions[g.helper_ordinal];
    out.actors[1]=draft.actors[index==0?g.successor_ordinal:0];out.champions[1]=draft.champions[index==0?g.successor_ordinal:0];return out;
}
inline std::unique_ptr<paths::Routes> organization_routes(const paths::Routes& routes,const Geometry& g,bool successor) {
    auto out=std::make_unique<paths::Routes>(routes);auto& site=out->sites[g.custody];
    site.rally[0]=routes.sites[g.custody].rally[successor?1:0];site.rally[1]=routes.sites[g.custody].rally[2];return out;
}
inline double ambient() {
    double value=0;for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake())value+=camp.strength();
    for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
        double cast=0;for(const auto& ability:enemy.abilities())if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))cast=std::max(cast,double(ability.damage_amount()));
        value+=enemy.strength()+cast;
    }return value;
}
// Retain the original bank exit before adding productive return travel. This
// keeps the common altar queue's represented first exit and source payout.
inline bool extend(paths::Solo& model,const Route& tail,const p6::Frame& frame) {
    if(!model.feasible||!route_valid(tail))return false;
    const auto joined=paths::Routes::join(model.plan.renewal,tail);if(!route_valid(joined))return false;
    const int extra=std::max(0,paths::walking(joined)-paths::walking(model.plan.renewal));
    auto& actor=model.model.schedule.actors[0];actor.occupied_turns.value+=extra;
    for(int e=0;e<actor.event_count;++e)if(actor.events[e].kind==p6::EventKind::renewal)actor.events[e].tick+=extra;
    for(auto* exposure:{&model.model.certificate.clear,&model.model.certificate.hold,&model.model.certificate.bank,&model.model.certificate.recovery})exposure->incoming_total+=model.ambient_incoming*extra;
    model.plan.renewal=joined;model.plan.forecast_rounds=int(actor.occupied_turns.value);
    return model.plan.forecast_rounds<=std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round)&&
        model.model.certificate.valid(frame,actor.actor_id,model.plan.group);
}
inline bool independent_offer(Offer& out,const paths::Solo& own,std::span<const paths::Solo> models,const p6::Frame& frame) {
    out={};out.plan=own.plan;out.schedule.target_id=own.plan.group;int prefix=0;bool self=false;
    for(const auto& baseline:models) {
        if(!baseline.feasible||out.safety_count==p6::max_actors)return false;
        auto copy=baseline;paths::normalize(copy,frame);self|=copy.model.schedule.actors[0].actor_id==frame.clock.self;
        if(!paths::append(out.schedule,copy.model.schedule,0,prefix))return false;
        out.safety[out.safety_count++]=copy.model.certificate;prefix+=64;
    }
    return self&&paths::queue_alternatives(out,models,frame);
}
inline Plan fixed_h(const paths::Routes& routes,const Geometry& g,const DraftRoster& draft,bool successor) {
    auto adjusted=organization_routes(routes,g,successor);p6::Frame frame;frame.clock.self=draft.actors[successor?g.successor_ordinal:0];
    frame.own.actor.id=frame.clock.self;frame.own.actor.position=successor?g.meeting.successor_pad:routes.sites[g.custody].approach.destination;
    auto plan=paths::formation_plan(*adjusted,g.custody,g.bank,frame,organization_roster(draft,g,frame.clock.self),Organization::H,false);
    if(successor)plan.renewal=paths::Routes::join(plan.renewal,routes.route(routes.sites[g.custody].approach.destination,g.candidate_home));
    plan.forecast_rounds=plan.remaining_rounds=common::forecast_window;return plan;
}
inline std::unique_ptr<paths::Coalition> supported(const paths::Routes& routes,const Geometry& g,const p6::Frame& frame,const DraftRoster& draft,
                                  std::span<const paths::Solo> alternatives,bool successor) {
    auto adjusted=organization_routes(routes,g,successor);const int collector=draft.actors[successor?g.successor_ordinal:0];
    auto out=std::unique_ptr<paths::Coalition>(new paths::Coalition(paths::coalition(*adjusted,g.custody,g.bank,frame,organization_roster(draft,g,collector),alternatives,Organization::H,false)));
    if(!out->feasible)return out;
    const auto& frozen=successor?g.meeting.successor:g.meeting.original;
    const int index=p6::actor_index(out->offer.schedule,collector);if(index<0){out->feasible=false;return out;}
    if(successor) {
        const auto tail=routes.route(routes.sites[g.custody].approach.destination,g.candidate_home);
        const auto normal=routes.route(routes.banks[g.bank].approach.destination,routes.sites[g.custody].approach);
        const auto retained=paths::Routes::join(normal,tail);
        if(!route_valid(retained)){out->feasible=false;return out;}
        const int extra=std::max(0,paths::walking(retained)-paths::walking(normal));auto& actor=out->offer.schedule.actors[index];
        actor.occupied_turns.value+=extra;for(int e=0;e<actor.event_count;++e)if(actor.events[e].kind==p6::EventKind::renewal)actor.events[e].tick+=extra;
        const double stress=ambient()*extra;
        for(int c=0;c<out->offer.safety_count;++c)if(out->offer.safety[c].actor==collector)
            for(auto* exposure:{&out->offer.safety[c].clear,&out->offer.safety[c].hold,&out->offer.safety[c].bank,&out->offer.safety[c].recovery})exposure->incoming_total+=stress;
        if(frame.clock.self==collector)out->offer.plan.renewal=retained;
    }
    for(int c=0;c<out->offer.safety_count;++c)if(!out->offer.safety[c].valid(frame,out->offer.safety[c].actor,out->offer.safety[c].group))out->feasible=false;
    for(int a=0;a<out->offer.schedule.actor_count;++a)if(out->offer.schedule.actors[a].occupied_turns.value>std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round))out->feasible=false;
    if(frame.clock.self==collector) {
        // A frozen complete physical upper bound is independent of which
        // observer first prepares the public meeting. Schedule time stays exact.
        out->offer.plan.forecast_rounds=frozen.forecast_rounds;
        if(plan_route_hash(out->offer.plan)!=plan_route_hash(frozen))out->feasible=false;
    }
    return out;
}
inline ContextDeclaration declaration(const Plan& plan,const sdk::Snapshot& snapshot,const Inputs& in,bool full,p6::Gems maximum_bag) {
    ContextDeclaration d;auto& key=d.key;const auto& state=snapshot.own;
    key.settings_id=in.settings_identity.value;key.route_hash=plan_route_hash(plan);key.return_route_hash=plan_return_hash(plan);
    key.actor_id=snapshot.frame.clock.self;key.champion=int(snapshot.frame.own.actor.type);key.role=full?1:0;key.source_group=plan.group;
    key.route_template=0;key.endpoint=plan.altar;key.threat_regime=in.threat_regime.value;key.participants=plan.crew;key.participant_count=plan.crew_count;
    std::sort(key.participants.begin(),key.participants.begin()+key.participant_count);
    key.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};key.hp_band=health_band(state.health,state.max_health);
    const double focus=state.focus+(snapshot.frame.own.actor.type==p6::Champion::prism?double(cambc::constants::passive_abilities::prism_focus_per_gem)*double(maximum_bag):0.);
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},{state.strength,state.strength},{state.focus,focus},{state.growth,state.growth},{0,double(maximum_bag)},0};
    d.kind=full?EpisodeKind::full_organization:EpisodeKind::return_component;d.require_supported_work=full;return d;
}
} // namespace p8::custodial_authority::native
