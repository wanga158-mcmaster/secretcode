#pragma once
#include "organization.h"

namespace p8::native {
struct Solo {
    Plan plan{};
    p6::forecast::Result model{};
    int site=-1,bank=-1;
    double ambient_incoming=0;
    bool feasible=false;
};
struct SoloStorage { Solo result{};p6::forecast::Request request{}; };

// Reuse live, initialized objects. Inactive arrays retain their payload behind
// zero counts; none supplies an observation, route, schedule or certificate.
// The caller must completely fill active group0 and each active leg/member,
// including clearing member.current before any optional current-state query.
inline void reset_solo(Solo& out) {
    out.site=out.bank=-1;out.ambient_incoming=0;out.feasible=false;
    auto& plan=out.plan;
    plan.organization=Organization::U;plan.group=plan.collector=plan.altar=-1;
    plan.crew.fill(-1);plan.pads.fill(p6::Point{});plan.crew_count=0;
    for(auto* route:{&plan.ingress,&plan.capture,&plan.bank,&plan.renewal,&plan.release}) {
        route->count=0;route->blocker_assumptions=0;route->verified=false;
    }
    plan.forecast_rounds=plan.remaining_rounds=plan.capture_window=0;
    plan.gem_bearing=plan.complete_verified=plan.rally_legal=false;

    auto& result=out.model;
    result.status=p6::Status::unresolved;result.issue=p6::forecast::Issue::unknown_input;
    result.abort=p6::Failure::none;
    result.schedule.actor_count=result.schedule.production_count=0;
    result.schedule.target_id=-1;result.schedule.immediate_win={};result.schedule.terminal_tiebreak={};
    result.certificate={};result.phase_count=0;result.first={};
    result.recovery_health_unmodeled=false;result.purchases_deferred=true;
}

inline void reset_request(p6::forecast::Request& request) {
    request.shape=p6::forecast::Shape::produce_bank;request.project_group=-1;
    request.timing={};request.group_count=0;request.kit={};request.ambient_incoming={};
    request.enemy_cast_count=0;request.bank_waypoint=request.renewal_waypoint=-1;
    request.bank_position=request.renewal_position=p6::Point{};
    request.bank_available={};request.bank_lock_hits={};request.leg_count=0;
    request.shield={};request.recovery={};request.purchase_requested=false;
    auto& group=request.groups[0];group.observed={};group.membership_complete={};
    group.member_count=0;group.gems={};group.gold={};
    group.ambient_overlap={};group.stationary_basic={};
    // groups[1], member arrays, cast-stress and leg arrays remain inactive.
}
inline void reset_solo(Solo& out,p6::forecast::Request& request) {
    reset_solo(out);reset_request(request);
}
} // namespace p8::native
