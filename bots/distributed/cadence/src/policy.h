#pragma once
#include "profile.h"

namespace p6::cadence {
struct State {
    ActorState project;
    int last_round=-1, last_watch=-1;
    bool admitted=false;
};
inline int owner_project(const organization::Input& in,const Actor* owner) {
    if (!organization::ready(owner)) return -1;
    for (int i=0; i<in.site_count; ++i) {
        const auto& site=in.sites[i];
        if (site.known && site.solo(in.frame,owner->id) &&
            (organization::useful(owner,site.group.id) || owner->position==site.group.approach))
            return site.group.id;
    }
    return -1;
}
inline organization::Decision choose(State& state,const organization::Input& in,bool control) {
    using namespace organization;
    state.last_round=in.frame.clock.round;
    if (!in.frame.valid() || in.ordinal<0 || in.ordinal>=3 ||
        in.frame.own.actor.channel!=Channel::none) return independent(in);
    if (in.actual_payout || in.actual_bank || in.restored) {
        state.project.active=false; state.admitted=false; return independent(in);
    }
    const auto* owner=in.ordinal_actor(2);
    int group=in.ordinal==2?(in.proposed_group>=0?in.proposed_group:in.independent_group):
        owner_project(in,owner);
    const auto* site=in.site(group);
    if (!site || !ready(owner) || !site->solo(in.frame,owner->id)) return independent(in);
    if (in.ordinal==2) return work(group,"cadence-independent-productive-owner");
    auto& history=state.project;
    if (in.observed_failure || in.hidden_collision) {
        if (history.active) history.retire(Failure::observed_failed_action);
        state.admitted=false; return independent(in);
    }
    const int watch=(in.frame.clock.round%9)/3;
    state.last_watch=watch;
    // Off-watch departure is licensed by a fresh owner's entire solo cycle,
    // rather than the residue proving a successful handoff or future stun.
    if (!control && watch!=in.ordinal) {
        state.admitted=false; history.active=false;
        return independent(in);
    }
    if (!joint(in,group) || !useful(owner,group) || !useful(&in.frame.own.actor,group))
        return independent(in);
    if (!history.active && !history.admit(group,in.frame.clock.round,3,
        std::max(1,in.settings.max_rounds-in.frame.clock.round),
        in.frame.own.actor.bag,in.frame.own.gold)) return independent(in);
    state.admitted=true;
    // The runtime performs a fresh, legal nonimmune/nonstunned threat test and
    // requires witnessed recent camp damage before a CC override. This intent
    // never awards prevented-damage value or claims that a cast happened.
    Decision out{Intent::control_watch,group,owner->id,{},false,
                 control?"cadence-retained-control-crew":"cadence-current-rotating-watch"};
    return out;
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in,bool control) {
        return cadence::choose(state,in,control);
    }
};
}
