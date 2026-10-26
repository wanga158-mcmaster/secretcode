#pragma once
#include "../../common/src/organization.h"

namespace p8::earned_center {
// Earned's five-leg circuit retains its bank->spawn->productive return and
// modeled spawn wait. Only one already charged initial ingress slack turn is
// consumed by admission; this predicate grants no physical action or episode.
inline bool spawn_return_wait_covered(const p6::Frame& frame,const Plan& plan,
                                      const p6::forecast::Request& request,
                                      const p6::forecast::Result& model,p6::Point spawn) {
    if(!frame.valid()||plan.organization!=Organization::U||plan.crew_count!=1||
        plan.collector!=frame.clock.self||plan.crew[0]!=frame.clock.self||
        !route_valid(plan.ingress)||plan.ingress.count<2||!route_valid(plan.capture)||
        !route_valid(plan.bank)||!route_valid(plan.renewal)||
        plan.forecast_rounds<1||plan.forecast_rounds>120||
        model.status!=p6::Status::feasible||model.phase_count<1||model.phase_count>int(model.phases.size())||
        model.schedule.actor_count!=1||model.schedule.actors[0].actor_id!=frame.clock.self||
        model.schedule.actors[0].occupied_turns.value!=plan.forecast_rounds||
        !model.certificate.solo||model.certificate.clock!=frame.clock||
        model.certificate.actor!=frame.clock.self||model.certificate.group!=plan.group||
        request.shape!=p6::forecast::Shape::produce_bank||request.project_group!=plan.group||
        request.group_count!=1||!request.groups[0].observed.current(frame.clock,plan.group)||
        request.groups[0].observed.value.id!=plan.group||request.leg_count!=5||
        request.shield.enabled||request.purchase_requested||
        !request.kit.current(frame.clock,frame.clock.self)||request.kit.value.scheduled_single_cast!=0||
        !request.recovery.enabled||request.recovery.requires_health_restore||
        request.recovery.position!=spawn||request.recovery.waypoint<0||
        request.recovery.turns.source!=p6::Source::conservative||request.recovery.turns.value!=1)
        return false;
    const Route* routes[]={&plan.ingress,&plan.capture,&plan.bank};
    for(int i=0;i<3;++i) {
        const auto& route=*routes[i];const auto& leg=request.legs[i];
        if(leg.distance.source!=p6::Source::public_map||leg.distance.value!=route.count-1||
            leg.from!=route.tiles[0]||leg.to!=route.tiles[route.count-1])return false;
    }
    const auto& home=request.legs[3];const auto& back=request.legs[4];
    const int a=home.distance.value,b=back.distance.value;
    if(home.distance.source!=p6::Source::public_map||back.distance.source!=p6::Source::public_map||
        a<0||b<0||a>=max_route_tiles||b>=max_route_tiles||a+b!=plan.renewal.count-1||
        home.from!=plan.bank.tiles[plan.bank.count-1]||home.from!=plan.renewal.tiles[0]||
        home.to!=spawn||back.from!=spawn||back.to!=plan.renewal.tiles[plan.renewal.count-1]||
        plan.renewal.tiles[a]!=spawn||request.bank_position!=home.from||
        request.bank_waypoint!=plan.altar||request.renewal_position!=back.to||
        request.renewal_waypoint!=plan.group)return false;
    const auto& first=model.phases[0];
    if(request.legs[0].from!=frame.own.actor.position||first.kind!=p6::forecast::PhaseKind::travel||
        first.actor!=frame.clock.self||first.group!=plan.group||first.start!=0||
        first.finish!=plan.ingress.count+1||first.source!=p6::Source::scheduled)return false;
    int recoveries=0;
    for(int i=0;i<model.phase_count;++i) {
        const auto& phase=model.phases[i];
        if(phase.kind!=p6::forecast::PhaseKind::recovery)continue;
        if(phase.actor!=frame.clock.self||phase.group!=-1||phase.waypoint!=request.recovery.waypoint||
            phase.start<first.finish||phase.start>=plan.forecast_rounds||phase.finish!=phase.start+1||
            phase.finish>plan.forecast_rounds||phase.source!=p6::Source::scheduled)return false;
        ++recoveries;
    }
    return recoveries==1;
}
} // namespace p8::earned_center
