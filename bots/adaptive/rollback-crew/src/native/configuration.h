#pragma once
#include "../policy.h"
#include "../../../common/src/coalition.h"
namespace p8::rollback_crew::native {
namespace paths=p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
struct Duties {
    std::array<int,3> site{-1,-1,-1},bank{-1,-1,-1};bool valid=false;
};
inline DraftRoster geometry_roster(DraftRoster d) {
    if(d.picks>=3){std::swap(d.actors[0],d.actors[2]);std::swap(d.champions[0],d.champions[2]);
        std::swap(d.actors[1],d.actors[2]);std::swap(d.champions[1],d.champions[2]);}return d;
}
inline bool independent_offer(Offer& out,const paths::Solo& own,std::span<const paths::Solo> models,const p6::Frame& frame) {
    out={};out.plan=own.plan;out.schedule.target_id=own.plan.group;int prefix=0;bool self=false;
    for(const auto& baseline:models) {
        if(!baseline.feasible||out.safety_count==p6::max_actors)return false;
        auto current=baseline;paths::normalize(current,frame);self|=current.model.schedule.actors[0].actor_id==frame.clock.self;
        if(!paths::append(out.schedule,current.model.schedule,0,prefix))return false;
        out.safety[out.safety_count++]=current.model.certificate;prefix+=64;
    }
    return self&&paths::queue_alternatives(out,models,frame);
}
inline double ambient() {
    double value=0;
    for(const auto& camp:cambc::vision::camps())if(camp.is_alive()&&camp.is_awake())value+=camp.strength();
    for(const auto& enemy:cambc::vision::champions(cambc::team::enemy)) {
        double cast=0;for(const auto& ability:enemy.abilities())if(ability.info()&&(ability.is_damage()||ability.is_aoe_damage()||ability.is_cc()))cast=std::max(cast,double(ability.damage_amount()));
        value+=enemy.strength()+cast;
    }
    return value;
}
// Preserve source production, bank service and queue ordering. Each actual
// member owns a different retained productive destination after its own bank.
// This is charged restoration travel, never extra incumbent production income.
inline paths::Coalition replacement(const paths::Routes& routes,const Duties& duties,int site,int bank,
                                   const p6::Frame& frame,const DraftRoster& draft,std::span<const paths::Solo> alternatives) {
    auto out=paths::coalition(routes,site,bank,frame,geometry_roster(draft),alternatives,Organization::J,true);
    const double healing=cambc::constants::spawn_healing;
    if(!out.feasible||!duties.valid||!std::isfinite(healing)||healing<=0){out.feasible=false;return out;}
    const int recovery=int(std::ceil(1./std::min(1.,healing)))+1;
    if(recovery>9){out.feasible=false;return out;}
    const double incoming=ambient();
    for(int ordinal=0;ordinal<3;++ordinal) {
        const int id=draft.actors[ordinal];const auto* observed=frame.visible(id);
        if(!observed){out.feasible=false;return out;}
        auto plan=std::make_unique<Plan>(paths::formation_plan(routes,site,bank,paths::subject(frame,*observed),
            geometry_roster(draft),Organization::J,true));
        const auto renewal=paths::Routes::join(routes.route(routes.banks[bank].approach.destination,routes.spawn),
            routes.route(routes.spawn.destination,routes.sites[duties.site[ordinal]].approach));
        if(!plan_valid(*plan,id)||!route_valid(renewal)){out.feasible=false;return out;}
        const int extra_walk=std::max(0,paths::walking(renewal)-paths::walking(plan->renewal));
        const int extra=extra_walk+recovery;
        const int index=p6::actor_index(out.offer.schedule,id);if(index<0){out.feasible=false;return out;}
        auto& actor=out.offer.schedule.actors[index];actor.occupied_turns.value+=extra;
        for(int e=0;e<actor.event_count;++e) {
            if(actor.events[e].kind==p6::EventKind::recovery)actor.events[e].tick+=recovery;
            if(actor.events[e].kind==p6::EventKind::renewal)actor.events[e].tick+=extra;
        }
        for(int c=0;c<out.offer.safety_count;++c)if(out.offer.safety[c].actor==id) {
            out.offer.safety[c].recovery.incoming_total+=incoming*extra;
            if(!out.offer.safety[c].valid(frame,id,out.offer.plan.group)){out.feasible=false;return out;}
        }
        if(id==frame.clock.self) {
            out.offer.plan.renewal=renewal;out.offer.plan.forecast_rounds=int(actor.occupied_turns.value);
        }
        if(actor.occupied_turns.value>std::min(common::forecast_window,int(cambc::constants::max_rounds)-frame.clock.round)) {
            out.feasible=false;return out;
        }
    }
    out.feasible=plan_valid(out.offer.plan,frame.clock.self);return out;
}
inline ContextDeclaration declaration(const Plan& plan,const sdk::Snapshot& snapshot,const Inputs& in,bool joint,p6::Gems maximum_bag) {
    ContextDeclaration d;auto& k=d.key;const auto& state=snapshot.own;
    k.settings_id=in.settings_identity.value;k.route_hash=plan_route_hash(plan);k.return_route_hash=plan_return_hash(plan);
    k.actor_id=snapshot.frame.clock.self;k.champion=int(snapshot.frame.own.actor.type);k.role=joint?1:0;
    k.source_group=plan.group;k.route_template=0;k.endpoint=plan.altar;k.threat_regime=in.threat_regime.value;
    k.participants=plan.crew;k.participant_count=plan.crew_count;std::sort(k.participants.begin(),k.participants.begin()+k.participant_count);
    k.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};k.hp_band=health_band(state.health,state.max_health);
    const double focus=state.focus+(snapshot.frame.own.actor.type==p6::Champion::prism?
        double(cambc::constants::passive_abilities::prism_focus_per_gem)*double(maximum_bag):0.);
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},{state.strength,state.strength},
        {state.focus,focus},{state.growth,state.growth},{0,double(maximum_bag)},0};
    d.kind=joint?EpisodeKind::full_organization:EpisodeKind::return_component;d.require_supported_work=joint;return d;
}
} // namespace p8::rollback_crew::native
