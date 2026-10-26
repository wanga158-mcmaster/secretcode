#pragma once
#include "profile.h"

namespace p6::bridgecrew {
using namespace organization;
struct State {
    int group = -1, epoch = -1;
    bool alternate_tried = false, retired = false;
};
inline Decision choose(State& state, const Input& in, bool control) {
    const auto& own = in.frame.own.actor;
    if (!in.frame.valid() || !in.safe_now ||
        (own.channel != Channel::none && own.channel_valid)) return independent(in);
    if (in.phase == Phase::hold && own.spawn_group == in.current_group &&
        in.current_group >= 0 && !in.actual_payout)
        return work(in.current_group, "bridge-preserve-capture");
    const int group = in.current_group >= 0 ? in.current_group : in.independent_group;
    if (state.group != group || in.actual_bank || in.restored) {
        state = {}; state.group = group; state.epoch = in.frame.clock.round / 64;
    }
    const auto& passage = in.passage;
    if (!passage.available || passage.length < 1 || passage.length > 3)
        return independent(in); // No invented choke or reservation.
    if (control) {
        if (passage.exit_safe)
            return waypoint(Intent::approach, group, passage.exit, "bridge-independent-route");
        return independent(in);
    }
    if (state.retired) return independent(in);
    bool inside = false;
    for (int i = 0; i < passage.length; ++i) inside |= own.position == passage.tiles[i];
    const bool window = (in.frame.clock.round / 4) % 2 == passage.direction;
    const bool precedence = own.bag > 0 || !passage.loaded_ally_returning;
    if (passage.personally_clear && passage.exit_safe && (inside || (window && precedence)))
        return waypoint(Intent::approach, group, passage.exit, "bridge-clear-direction-window");
    if (!state.alternate_tried && passage.alternate != own.position) {
        state.alternate_tried = true;
        return waypoint(Intent::alternate, group, passage.alternate, "bridge-one-alternate-route");
    }
    state.retired = true;
    return independent(in);
}
struct Policy {
    State state;
    static const organization::Profile& profile() { return profile_value; }
    organization::Decision choose(const organization::Input& in, bool control) {
        return bridgecrew::choose(state, in, control);
    }
};
} // namespace p6::bridgecrew
