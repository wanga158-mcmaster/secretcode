#pragma once
#include "../../common/src/observe.h"
#include <array>

namespace p6::owner_builder {
inline constexpr std::array<Champion, 5> roster{
    Champion::marcella, Champion::varg, Champion::vendetta,
    Champion::tavi, Champion::prism};
// User-directed draft choice: independent income/conversion, own shielding,
// and kill-reset clearance suit owners which cannot depend on future helpers.
inline constexpr int core_picks = 3;
inline constexpr int max_options = 12;

inline bool roster_actor(const Actor& actor, int picks) {
    return actor.team == Team::ally && picks >= 3 && picks <= 5 && actor.pick_ordinal >= 0 &&
           actor.pick_ordinal < picks && actor.type == roster[actor.pick_ordinal];
}
// Public group identity, never a private count of captures, fixes the owner.
// Extension picks retain their independent circuits and may visibly help.
inline int owner_pick(int group) { return group < 0 ? -1 : group % core_picks; }
inline bool fixed_owner(const Actor& actor, int group, int picks) {
    return roster_actor(actor, picks) && actor.pick_ordinal == owner_pick(group);
}
} // namespace p6::owner_builder
