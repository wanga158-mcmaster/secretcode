#pragma once
#include "profile.h"

namespace p6::trailhead {
struct State {
    ActorState project;
    int last_round=-1, pose_group=-1, pose_turns=0;
    bool lost_contact=false;
};
inline int proposal(const organization::Input& in,const Actor& leader) {
    for (int i=0; i<in.site_count; ++i) {
        const auto& site=in.sites[i];
        if (site.known && leader.position==site.group.approach) return site.group.id;
    }
    return -1;
}
inline organization::Decision choose(State& state,const organization::Input& in,bool control) {
    using namespace organization;
    const bool fresh=state.last_round!=in.frame.clock.round;
    state.last_round=in.frame.clock.round;
    if (!in.frame.valid() || in.ordinal<0 || in.ordinal>=3 ||
        in.frame.own.actor.channel!=Channel::none) return independent(in);
    if (in.actual_bank || in.restored) { state.project.active=false; state.lost_contact=false; }
    if (in.phase==Phase::hold && in.current_group>=0) {
        const auto* held=in.site(in.current_group);
        if (held && held->solo(in.frame,in.frame.clock.self))
            return work(in.current_group,"trailhead-preserve-own-hold");
    }
    if (in.actual_payout || in.phase==Phase::bank || in.phase==Phase::recover) return independent(in);
    if (in.ordinal==0) {
        int group=in.proposed_group>=0?in.proposed_group:in.independent_group;
        const auto* site=in.site(group);
        if (!site) return independent(in);
        auto& history=state.project;
        if (!history.active || history.project.group!=group) {
            history.active=false;
            if (!history.admit(group,in.frame.clock.round,4,
                std::max(1,in.settings.max_rounds-in.frame.clock.round),
                in.frame.own.actor.bag,in.frame.own.gold)) return independent(in);
        }
        if (in.observed_failure || in.hidden_collision) {
            if (!history.repair()) {
                history.retire(Failure::observed_failed_action); return independent(in);
            }
        }
        if (state.pose_group!=group) { state.pose_group=group; state.pose_turns=0; }
        // Both arms have the same Tavi route and two owner-turn public pose.
        if (in.safe_now && state.pose_turns<2) {
            if (fresh && !history.wait_once()) {
                history.retire(Failure::missed_appointment); return independent(in);
            }
            if (in.frame.own.actor.position==site->group.approach && fresh) ++state.pose_turns;
            return waypoint(Intent::approach,group,site->group.approach,"trailhead-two-turn-pose");
        }
        if (site->solo(in.frame,in.frame.clock.self)) return work(group,"trailhead-leader-owned-work");
        bool follower=false;
        for (int ordinal=1; ordinal<3; ++ordinal)
            follower|=useful(in.ordinal_actor(ordinal),group);
        if (joint(in,group) && useful(&in.frame.own.actor,group) && follower)
            return work(group,"trailhead-present-followers");
        return independent(in);
    }
    const auto* leader=in.ordinal_actor(0);
    int group=-1;
    if (control) {
        // Public draft order is geometry, not Tavi's private current frontier.
        if (in.site_count>0) group=in.sites[(in.ordinal-1)%in.site_count].group.id;
    } else if (ready(leader)) group=proposal(in,*leader);
    auto& history=state.project;
    if (history.active) group=history.project.group;
    const auto* site=in.site(group);
    if (!site) return independent(in);
    if (!history.active) {
        if (!control && (!ready(leader) || proposal(in,*leader)!=group)) return independent(in);
        if (!site->solo(in.frame,in.frame.clock.self) && !joint(in,group)) return independent(in);
        if (!history.admit(group,in.frame.clock.round,4,
            std::max(1,in.settings.max_rounds-in.frame.clock.round),
            in.frame.own.actor.bag,in.frame.own.gold)) return independent(in);
    }
    const bool contact=ready(leader) &&
        (proposal(in,*leader)==group || useful(leader,group));
    if (!control && (!contact || in.observed_failure || in.hidden_collision)) {
        if (!state.lost_contact) {
            if (!history.repair()) { history.retire(Failure::not_observed); return independent(in); }
            state.lost_contact=true; history.waits=0;
        }
        if (fresh && ++history.waits>4) {
            history.retire(Failure::missed_appointment); return independent(in);
        }
        return waypoint(Intent::rally,group,site->group.retreat,"trailhead-four-turn-retreat");
    }
    state.lost_contact=false;
    if (site->solo(in.frame,in.frame.clock.self)) return work(group,"trailhead-adopted-solo-work");
    if (joint(in,group) && useful(leader,group) && useful(&in.frame.own.actor,group))
        return work(group,"trailhead-witnessed-joint-work");
    if (in.safe_now) {
        if (fresh && !history.wait_once()) {
            history.retire(Failure::missed_appointment); return independent(in);
        }
        return waypoint(Intent::approach,group,site->group.approach,
            control?"trailhead-draft-appointment":"trailhead-witnessed-proposal");
    }
    return independent(in);
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in,bool control) {
        return trailhead::choose(state,in,control);
    }
};
}
