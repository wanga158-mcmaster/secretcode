#pragma once
#include "profile.h"

namespace p6::recovery_relay {
using namespace organization;
struct State {
    int group = -1, epoch = -1, rejoin_turns = 0;
    bool servicing = false, rejoining = false;
};
inline bool needs_service(const Actor& actor) {
    return actor.bag > 0 || (actor.max_health > 0 && actor.health < .7 * actor.max_health) ||
        actor.channel == Channel::recall || actor.channel == Channel::tribute;
}
inline bool feasible_remainder(const Input& in, const Site& site, int departing) {
    if (!site.known || site.blocked || !joint(in, site.group.id)) return false;
    int producers = 0;
    for (int ordinal = 0; ordinal < std::min(3, in.picks); ++ordinal) {
        if (ordinal == departing) continue;
        const auto* a = in.ordinal_actor(ordinal);
        if (!ready(a) || !site.solo(in.frame, a->id) || !useful(a, site.group.id)) return false;
        ++producers;
    }
    return producers > 0;
}
inline Decision service(const Input& in, const char* reason) {
    return {in.frame.own.actor.bag > 0 ? Intent::bank : Intent::recover,
            in.current_group, -1, {}, false, reason};
}
inline Decision choose(State& state, const Input& in, bool control) {
    const auto& own = in.frame.own.actor;
    if (!in.frame.valid() || in.ordinal < 0 || in.ordinal >= 3) return independent(in);
    if (own.channel != Channel::none && own.channel_valid) return independent(in);
    if (!in.safe_now) { state.servicing = true; return service(in, "relay-immediate-survival"); }
    // Never abandon the sole productive capture for a scheduled service slot.
    if (in.phase == Phase::hold && own.spawn_group == in.current_group &&
        in.current_group >= 0 && !in.actual_payout)
        return work(in.current_group, "relay-preserve-valid-capture");
    if (state.servicing && (in.restored || (in.actual_bank && !needs_service(own)))) {
        state.servicing = false; state.rejoining = true; state.rejoin_turns = 0;
    }
    if (state.servicing) return service(in, "relay-complete-own-service");
    const int candidate = in.current_group >= 0 ? in.current_group : in.proposed_group;
    if (state.rejoining) {
        const auto* target = in.site(candidate);
        bool contact = target && target->known && !target->blocked && target->solo(in.frame, own.id);
        for (int ordinal = 0; ordinal < std::min(3, in.picks); ++ordinal) if (ordinal != in.ordinal) {
            const auto* a = in.ordinal_actor(ordinal);
            contact &= ready(a) && target && target->solo(in.frame, a ? a->id : -1);
        }
        if (contact && (in.at_rally || own.position == target->group.approach)) {
            state.rejoining = false; state.group = candidate;
            return work(candidate, "relay-personally-rejoin-visible-crew");
        }
        if (contact && state.rejoin_turns++ < 8)
            return waypoint(Intent::rally, candidate, target->group.approach, "relay-visible-rejoin-approach");
        // Keep the contact requirement latched after releasing to independent
        // production; a timeout never silently rejoins the former crew.
        return independent(in); // Old project knowledge never substitutes for contact.
    }
    const auto* site = in.site(candidate);
    if (!site || !site->known || !site->solo(in.frame, own.id)) return independent(in);
    state.group = candidate; state.epoch = in.frame.clock.round / 24;
    const int proposed_returner = state.epoch % std::min(3, in.picks);
    const auto* returning = in.ordinal_actor(proposed_returner);
    bool visible_need = false;
    for (int ordinal = 0; ordinal < std::min(3, in.picks); ++ordinal) {
        const auto* a = in.ordinal_actor(ordinal);
        visible_need |= a && needs_service(*a);
    }
    if (control && visible_need) {
        state.servicing = true;
        return service(in, "relay-control-synchronized-service");
    }
    const bool remainder = feasible_remainder(in, *site, proposed_returner);
    if (visible_need && !remainder) {
        state.servicing = true;
        return service(in, "relay-no-feasible-subgroup-synchronize");
    }
    if (returning && needs_service(*returning) && proposed_returner == in.ordinal && remainder) {
        state.servicing = true;
        return service(in, "relay-witnessed-feasible-subgroup-departure");
    }
    if (joint(in, candidate)) return work(candidate, "relay-remaining-visible-producers");
    return independent(in);
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in, bool control) {
        return recovery_relay::choose(state, in, control);
    }
};
} // namespace p6::recovery_relay
