#pragma once
#include "../policy.h"
#include "../../../common/src/coalition.h"

namespace p8::underwritten_frontier::native {
namespace paths=p8::native;
#ifndef P8_CONTROL
#define P8_CONTROL 0
#endif
inline constexpr Arm arm=P8_CONTROL?Arm::control:Arm::active;
// Actual roster remains in Inputs. Only the shared geometry's local roles are
// rebased: collector2, optional control insurer0, active helper1.
inline DraftRoster geometry_roster(DraftRoster draft) {
    if(draft.picks>=3) {
        std::swap(draft.actors[0],draft.actors[2]);std::swap(draft.champions[0],draft.champions[2]);
        std::swap(draft.actors[1],draft.actors[2]);std::swap(draft.champions[1],draft.champions[2]);
    }
    return draft;
}
inline bool independent_offer(Offer& offer,const paths::Solo& own,std::span<const paths::Solo> alternatives,const p6::Frame& frame) {
    offer={};offer.plan=own.plan;offer.schedule.target_id=own.plan.group;int prefix=0;bool self=false;
    for(const auto& baseline:alternatives) {
        if(!baseline.feasible||offer.safety_count==p6::max_actors)return false;
        auto current=baseline;paths::normalize(current,frame);
        self|=current.model.schedule.actors[0].actor_id==frame.clock.self;
        if(!paths::append(offer.schedule,current.model.schedule,0,prefix))return false;
        offer.safety[offer.safety_count++]=current.model.certificate;prefix+=64;
    }
    return self&&paths::queue_alternatives(offer,alternatives,frame);
}
inline ContextDeclaration declaration(const Plan& plan,const sdk::Snapshot& snapshot,const Inputs& in,p6::Gems maximum_bag) {
    ContextDeclaration d;auto& k=d.key;const auto& state=snapshot.own;
    k.settings_id=in.settings_identity.value;k.route_hash=plan_route_hash(plan);k.return_route_hash=plan_return_hash(plan);
    k.actor_id=snapshot.frame.clock.self;k.champion=int(snapshot.frame.own.actor.type);k.role=1;
    k.source_group=plan.group;k.route_template=0;k.endpoint=plan.altar;k.threat_regime=in.threat_regime.value;
    k.participants=plan.crew;k.participant_count=plan.crew_count;
    std::sort(k.participants.begin(),k.participants.begin()+k.participant_count);
    k.build={state.equipment_hash,state.max_health,state.strength,state.focus,state.growth};
    k.hp_band=health_band(state.health,state.max_health);
    const double focus_bound=state.focus+(snapshot.frame.own.actor.type==p6::Champion::prism?
        double(cambc::constants::passive_abilities::prism_focus_per_gem)*double(maximum_bag):0.);
    d.envelope={{std::nextafter(0.,1.),state.max_health},{state.max_health,state.max_health},
        {state.strength,state.strength},{state.focus,focus_bound},{state.growth,state.growth},{0,double(maximum_bag)},0};
    d.kind=EpisodeKind::full_organization;d.require_supported_work=true;return d;
}
inline p6::Observation<LoadedReturn> loaded(const sdk::Snapshot& snapshot,const ContextDeclaration& d,
                                          const p6::CycleCertificate& certificate) {
    LoadedReturn value;value.context=d.key;
    value.focus_per_gem={double(cambc::constants::passive_abilities::prism_focus_per_gem),p6::Source::queried_setting};
    if(!std::isfinite(d.envelope.bag.upper)||d.envelope.bag.upper<1||d.envelope.bag.upper>=9223372036854775808.)return {};
    const auto maximum=std::int64_t(d.envelope.bag.upper);
    value.points={FocusPoint{0,d.key.build.focus,certificate.hold},
        FocusPoint{maximum,d.key.build.focus+double(maximum)*value.focus_per_gem.value,certificate.bank},
        FocusPoint{0,d.key.build.focus,certificate.recovery}};
    // These are current own cumulative survival forecasts at empty, fully
    // loaded, and post-bank return states. They are never cycle receipts.
    for(auto& point:value.points) {
        point.cumulative.health=snapshot.own.health;point.cumulative.max_health=snapshot.own.max_health;
    }
    return {value,snapshot.frame.clock,snapshot.frame.clock.self,p6::Knowledge::exact};
}
} // namespace p8::underwritten_frontier::native
