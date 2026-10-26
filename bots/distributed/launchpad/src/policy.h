#pragma once
#include "profile.h"

namespace p6::launchpad {
struct State {
    ActorState project;
    int last_round=-1, forecast_arrival=-1;
    bool admitted=false, retrying=false;
};
inline bool observed_barrier(const organization::Input& in,int group) {
    if (!organization::joint(in,group)) return false;
    for (int ordinal=0; ordinal<3; ++ordinal) {
        const auto* actor=in.ordinal_actor(ordinal);
        if (!organization::useful(actor,group) || actor->health<=0) return false;
    }
    return true;
}
inline organization::Decision choose(State& state,const organization::Input& in,bool control) {
    using namespace organization;
    const bool fresh=state.last_round!=in.frame.clock.round;
    state.last_round=in.frame.clock.round;
    if (!in.frame.valid() || in.ordinal<0 || in.ordinal>=3 ||
        in.frame.own.actor.channel!=Channel::none) return independent(in);
    auto& history=state.project;
    if (in.actual_payout || in.actual_bank || in.restored) {
        history.active=false; state.admitted=false; state.retrying=false;
        return independent(in);
    }
    int group=history.active?history.project.group:in.proposed_group;
    if (group<0) group=in.independent_group;
    const auto* site=in.site(group);
    if (!site) return independent(in);
    if (!history.active) {
        if (!history.admit(group,in.frame.clock.round,4,
            std::max(1,in.settings.max_rounds-in.frame.clock.round),
            in.frame.own.actor.bag,in.frame.own.gold)) return independent(in);
        state.forecast_arrival=in.frame.clock.round+2;
        state.admitted=false; state.retrying=false;
    }
    if (in.observed_failure || in.hidden_collision) {
        if (!state.retrying) {
            if (!history.repair()) {
                history.retire(Failure::observed_failed_action); return independent(in);
            }
            state.retrying=true; state.admitted=false;
        }
    } else state.retrying=false;
    // A receipt of actual clearance plus a presently safe occupied holder is
    // required before helpers leave. Forecast time never establishes clearance.
    const auto* live=in.frame.live_group(group);
    if (live && live->lifecycle==Lifecycle::reward_available && site->holder>=0) {
        const auto* certificate=site->cycle(site->holder);
        if (certificate && witnessed_release(in.frame,site->holder,group,*certificate)) {
            if (site->holder==in.frame.clock.self) return work(group,"launchpad-safe-fixed-holder");
            history.active=false; return independent(in);
        }
    }
    if (observed_barrier(in,group)) {
        state.admitted=true; history.waits=0;
        return work(group,control?"launchpad-control-present-crew":"launchpad-personal-barrier-passed");
    }
    if (control && in.frame.clock.round>=state.forecast_arrival) {
        // The scheduled arm can start early only under its own exact solo
        // certificate. Missing actors supply no speculative combat credit.
        if (site->solo(in.frame,in.frame.clock.self))
            return work(group,"launchpad-scheduled-safe-start");
        return independent(in);
    }
    if (state.admitted) {
        if (site->solo(in.frame,in.frame.clock.self)) return work(group,"launchpad-loss-safe-continuation");
        if (!state.retrying) {
            if (!history.repair()) { history.retire(Failure::not_observed); return independent(in); }
            state.retrying=true;
        }
        state.admitted=false;
    }
    if (fresh && !history.wait_once()) {
        history.retire(Failure::missed_appointment); return independent(in);
    }
    if (!in.safe_now) return independent(in);
    // Stage on the public pad; extensions and timed-out core actors continue
    // independent production, rather than treating absence as failed death.
    return waypoint(Intent::approach,group,site->group.approach,"launchpad-eight-turn-staging");
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in,bool control) {
        return launchpad::choose(state,in,control);
    }
};
}
