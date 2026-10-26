#pragma once
namespace gremlins {
constexpr bool qualifies(bool visible, bool alive, bool submitting, unsigned bag, unsigned adjacent_altars, unsigned minimum = 24) {
    return visible && alive && submitting && bag >= minimum && adjacent_altars == 1;
}
constexpr bool precedes(unsigned bag, int distance, unsigned actor, int altar,
                        unsigned old_bag, int old_distance, unsigned old_actor, int old_altar) {
    return bag != old_bag ? bag > old_bag : distance != old_distance ? distance < old_distance
        : actor != old_actor ? actor < old_actor : altar < old_altar;
}
constexpr bool expired(unsigned first, unsigned now, unsigned stalls, unsigned maximum = 8, unsigned stall_limit = 3) {
    return now < first || now - first >= maximum || stalls >= stall_limit;
}
constexpr bool cooldown_complete(unsigned end, unsigned now) { return now >= end; }
constexpr unsigned cooldown_until(unsigned completion_turn, unsigned quiet_turns = 32) { return completion_turn + quiet_turns + 1; }
// A safe current placement has exact distance zero. Any positive-distance
// candidate with no greater bag loses before the remaining tie breakers.
constexpr bool needs_route_field(bool have_zero, unsigned zero_bag, unsigned candidate_bag) {
    return !have_zero || candidate_bag > zero_bag;
}
}
