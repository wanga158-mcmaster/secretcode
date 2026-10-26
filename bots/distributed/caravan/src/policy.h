#pragma once
#include "profile.h"

namespace p6::caravan {
struct State {
    ActorState project;
    int last_round = -1;
    bool repairing = false, logistics = false;
};
inline bool crew(const organization::Input& in,int group,bool productive) {
    for (int ordinal=0; ordinal<3; ++ordinal) {
        const auto* a=in.ordinal_actor(ordinal);
        if (!organization::ready(a) || (productive && !organization::useful(a,group))) return false;
    }
    return true;
}
inline organization::Decision choose(State& state,const organization::Input& in,bool control) {
    using namespace organization;
    const bool fresh=state.last_round!=in.frame.clock.round;
    state.last_round=in.frame.clock.round;
    if (!in.frame.valid() || in.ordinal<0 || in.ordinal>=3) return independent(in);
    if (in.frame.own.actor.channel!=Channel::none) return independent(in);
    auto& history=state.project;
    if (in.actual_payout) state.logistics=true;
    if (in.actual_bank || in.restored) {
        history.active=false; state.logistics=false; state.repairing=false;
    }
    // Control ends its common circuit at the observed capture receipt. Active
    // keeps the same visible membership through each actor's own bank/recovery.
    if (control && (state.logistics || in.phase==Phase::bank || in.phase==Phase::recover)) {
        history.active=false; return independent(in);
    }
    if (state.logistics || in.phase==Phase::bank || in.phase==Phase::recover) {
        if (in.frame.own.actor.bag>0)
            return waypoint(Intent::bank,history.project.group,in.bank_approach,"caravan-own-bank");
        if (in.phase==Phase::recover)
            return {Intent::recover,history.project.group,-1,{},false,"caravan-common-recovery"};
        const auto* leader=in.ordinal_actor(0);
        if (ready(leader) && leader->id!=in.frame.clock.self && in.safe_now)
            return {Intent::escort,history.project.group,leader->id,leader->position,true,
                    "caravan-bank-contact"};
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
        state.repairing=false;
    }
    if (!crew(in,group,false) || in.observed_failure || in.hidden_collision) {
        if (!state.repairing) {
            if (!history.repair()) {
                history.retire(Failure::not_observed); return independent(in);
            }
            state.repairing=true;
        }
        if (fresh && !history.wait_once()) {
            history.retire(Failure::missed_appointment); return independent(in);
        }
        // The durable leader can keep earning at the rally's solo project while
        // followers restore contact; all three are never forced to idle.
        if (in.ordinal==0 && site->solo(in.frame,in.frame.clock.self))
            return work(group,"caravan-rally-production");
        return waypoint(Intent::rally,group,in.rally,"caravan-eight-turn-rally");
    }
    state.repairing=false;
    if (site->crew_current && in.safe_now) return work(group,"caravan-crew-cycle");
    if (joint(in,group) && crew(in,group,true)) return work(group,"caravan-visible-core");
    if (site->solo(in.frame,in.frame.clock.self)) return work(group,"caravan-safe-solo-stage");
    if (in.safe_now) {
        if (fresh && !history.wait_once()) {
            history.retire(Failure::missed_appointment); return independent(in);
        }
        const auto* leader=in.ordinal_actor(0);
        return waypoint(Intent::approach,group,in.ordinal==0?site->group.approach:leader->position,
                        "caravan-compact-approach");
    }
    return independent(in);
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in,bool control) {
        return caravan::choose(state,in,control);
    }
};
}
