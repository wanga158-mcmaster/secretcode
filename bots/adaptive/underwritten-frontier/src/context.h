#pragma once
#include "profile.h"
#include "../../common/src/policy.h"
namespace p8::underwritten_frontier {
struct FocusPoint {
    std::int64_t bag=0;
    double focus=0;
    p6::Exposure cumulative{};
};
// Own-only forecast boundary: empty entry, maximum loaded state, and the full
// post-tribute productive return. It is not a partner's shared qualification.
struct LoadedReturn {
    ContextKey context{};
    p6::Fact<double> focus_per_gem{};
    std::array<FocusPoint,3> points{};
};
inline bool loaded_return_valid(const common::Inputs& in,const p6::Observation<LoadedReturn>& observed,
                                const ContextDeclaration& declaration,const Plan& plan) {
    if (in.frame.own.actor.type!=p6::Champion::prism) return true;
    if (!observed.current(in.frame.clock,in.frame.clock.self) || observed.value.context!=declaration.key ||
        observed.value.context.actor_id!=in.frame.clock.self || plan.collector!=in.frame.clock.self ||
        observed.value.context.route_hash!=plan_route_hash(plan) ||
        observed.value.focus_per_gem.source!=p6::Source::queried_setting ||
        !std::isfinite(observed.value.focus_per_gem.value) || observed.value.focus_per_gem.value<=0) return false;
    const auto& points=observed.value.points; const auto& build=declaration.key.build;
    if (points[0].bag!=0 || points[1].bag<=0 || points[2].bag!=0 ||
        points[0].focus!=build.focus || points[2].focus!=build.focus) return false;
    // The loaded checkpoint covers the largest integer bag admitted by the
    // frozen envelope. A safe one-gem sample cannot license a twelve-gem job.
    if (!declaration.envelope.bag.contains_bag(points[1].bag) ||
        (points[1].bag<std::numeric_limits<std::int64_t>::max() &&
         declaration.envelope.bag.contains_bag(points[1].bag+1))) return false;
    double previous=0;
    for (const auto& point:points) {
        const double expected=build.focus+double(point.bag)*observed.value.focus_per_gem.value;
        const auto& exposure=point.cumulative;
        if (!std::isfinite(expected) || point.focus!=expected ||
            !declaration.envelope.bag.contains_bag(point.bag) || !declaration.envelope.focus.contains(point.focus) ||
            exposure.health!=in.frame.own.actor.health || exposure.max_health!=in.frame.own.actor.max_health ||
            exposure.assigned_timed_heal!=0 || exposure.incoming_total<previous || !p6::survives(exposure)) return false;
        previous=exposure.incoming_total;
    }
    const auto& state=in.own_state.value;
    return declaration.envelope.contains(state,build) &&
        state.focus==build.focus+double(state.bag)*observed.value.focus_per_gem.value;
}
} // namespace p8::underwritten_frontier
