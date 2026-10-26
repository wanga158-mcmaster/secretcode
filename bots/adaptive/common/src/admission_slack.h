#pragma once
#include "organization.h"

namespace p8::native {
// The native solo builder already charges two turns beyond every positive
// public leg. A single private admission wait consumes one of those turns;
// it never adds a free delay or applies to zero-distance work at the source.
// Current target-range safety is checked separately by the SDK adapter.
inline bool admission_wait_covered(const p6::Frame& frame,const Plan& plan,
                                  const p6::forecast::Request& request,
                                  const p6::forecast::Result& model) {
    if(!frame.valid()||plan.organization!=Organization::U||plan.crew_count!=1||
        plan.collector!=frame.clock.self||!route_valid(plan.ingress)||plan.ingress.count<2||
        model.status!=p6::Status::feasible||model.phase_count<1||model.phase_count>int(model.phases.size())||
        model.schedule.actor_count!=1||model.schedule.actors[0].actor_id!=frame.clock.self||
        !model.certificate.solo||model.certificate.clock!=frame.clock||
        model.certificate.actor!=frame.clock.self||model.certificate.group!=plan.group||
        request.shape!=p6::forecast::Shape::produce_bank||request.project_group!=plan.group||
        request.group_count!=1||!request.groups[0].observed.current(frame.clock,plan.group)||
        request.groups[0].observed.value.id!=plan.group||request.leg_count!=4||
        request.shield.enabled||request.recovery.enabled||request.purchase_requested||
        !request.kit.current(frame.clock,frame.clock.self)||request.kit.value.scheduled_single_cast!=0)
        return false;
    const auto& leg=request.legs[0];const auto& phase=model.phases[0];
    const int distance=plan.ingress.count-1;
    return leg.distance.source==p6::Source::public_map&&leg.distance.value==distance&&
        leg.from==frame.own.actor.position&&leg.from==plan.ingress.tiles[0]&&
        leg.to==plan.ingress.tiles[distance]&&
        phase.kind==p6::forecast::PhaseKind::travel&&phase.actor==frame.clock.self&&
        phase.group==plan.group&&phase.start==0&&phase.finish==distance+2&&
        phase.source==p6::Source::scheduled;
}
} // namespace p8::native
