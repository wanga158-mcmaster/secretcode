#pragma once
#include "../../common/src/observe.h"
#include <array>

namespace p6::firebrigade {
inline constexpr std::array<Champion, 5> roster{
    Champion::marcella, Champion::tavi, Champion::ren, Champion::willow, Champion::ryth};
// Ren's dash and targeted stun fit bounded local response; Willow/Ryth add
// optional healing/control while the first two picks keep independent income.
inline constexpr int max_options = 12, intervention_limit = 8;
inline bool roster_actor(const Actor& actor, int picks) {
    return picks >= 3 && picks <= 5 && actor.team == Team::ally &&
        actor.pick_ordinal >= 0 && actor.pick_ordinal < picks &&
        actor.type == roster[actor.pick_ordinal];
}
inline bool responder(const Actor& actor) {
    return actor.team == Team::ally && actor.pick_ordinal == 2 && actor.type == roster[2];
}
} // namespace p6::firebrigade
