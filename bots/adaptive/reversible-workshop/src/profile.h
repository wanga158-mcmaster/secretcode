#pragma once
#include "../../common/src/profile.h"

// P8-04 uses the selected own roster. Preferences break equal complete-cycle
// forecasts only; every pick still needs a certified solo first income. The
// current pinned rules give reversible equipment sales only to actual Tavi.
namespace p8::reversible_workshop {
using common::champion_pool;
using common::supported_champion;
using common::DraftRoster;
using common::roster_actor;
using common::ProbeBudget;
using common::allow_denial;
using common::max_options;
using common::max_contexts;
using common::qualification_rounds;
using common::project_rounds;
using common::max_probe_turns;
using common::max_probes_match;
using common::forecast_window;
using common::minimum_trial_health_fraction;

inline bool collector_actor(const p6::Actor& actor, int picks) {
    return roster_actor(actor,picks) && actor.team == p6::Team::ally && actor.pick_ordinal == 0;
}
inline bool collector_actor(const p6::Actor& actor, const DraftRoster& draft) {
    return roster_actor(actor,draft) && actor.pick_ordinal == 0;
}

inline constexpr std::array<p6::Champion,8> collector_priority{
    p6::Champion::tavi, p6::Champion::marcella, p6::Champion::varg,
    p6::Champion::vendetta, p6::Champion::ren, p6::Champion::vesper_vale,
    p6::Champion::willow, p6::Champion::prism};
inline bool reversible_collector(p6::Champion champion) { return champion==p6::Champion::tavi; }
// Tavi's actual full spawn refund makes a reversible workshop possible. This
// preference does not assert sale legality or count a refund as earned gold;
// transaction admission must inspect the selected actor and current shop state.
inline constexpr std::array<p6::Champion,8> producer_priority{
    p6::Champion::varg, p6::Champion::vendetta, p6::Champion::vesper_vale,
    p6::Champion::tavi, p6::Champion::marcella, p6::Champion::prism,
    p6::Champion::ren, p6::Champion::willow};
inline constexpr std::array<p6::Champion,8> helper_priority{
    p6::Champion::willow, p6::Champion::varg, p6::Champion::marcella,
    p6::Champion::vendetta, p6::Champion::ren, p6::Champion::tavi,
    p6::Champion::vesper_vale, p6::Champion::prism};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal) {
    if (ordinal == 0) return collector_priority;
    if (ordinal == 1) return producer_priority;
    if (ordinal == 2) return helper_priority;
    return champion_pool;
}
// Producers begin independent. A helper preference reserves no future labor,
// and each actual actor keeps its own bag until its own legal conversion.
} // namespace p8::reversible_workshop
