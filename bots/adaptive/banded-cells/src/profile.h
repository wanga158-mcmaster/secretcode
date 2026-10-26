#pragma once
#include "../../common/src/profile.h"

// P8-03 keeps two independent cell leaders and a short-circuit producer.
// Preferences break equal certified complete-cycle ranks only; they neither
// prescribe a roster nor promise that a champion can complete a local cycle.
namespace p8::banded_cells {
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

inline bool cell_actor(const p6::Actor& actor, const DraftRoster& draft) {
    return roster_actor(actor,draft) &&
        (actor.pick_ordinal == 0 || actor.pick_ordinal == 1);
}

inline constexpr std::array<p6::Champion,8> cell_priority{
    p6::Champion::varg, p6::Champion::tavi, p6::Champion::vendetta,
    p6::Champion::marcella, p6::Champion::ren, p6::Champion::vesper_vale,
    p6::Champion::prism, p6::Champion::willow};
inline constexpr std::array<p6::Champion,8> producer_priority{
    p6::Champion::tavi, p6::Champion::vesper_vale, p6::Champion::vendetta,
    p6::Champion::varg, p6::Champion::marcella, p6::Champion::prism,
    p6::Champion::ren, p6::Champion::willow};
inline constexpr std::array<p6::Champion,8> helper_priority{
    p6::Champion::willow, p6::Champion::varg, p6::Champion::marcella,
    p6::Champion::ren, p6::Champion::vendetta, p6::Champion::tavi,
    p6::Champion::vesper_vale, p6::Champion::prism};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal) {
    if (ordinal == 0 || ordinal == 1) return cell_priority;
    if (ordinal == 2) return producer_priority;
    if (ordinal == 3 || ordinal == 4) return helper_priority;
    return champion_pool;
}
// Each pick still needs its own public-data solo first-income certificate.
// Helpers retain independent production and their bags until a visible, safe
// proposal admits them; drafting adds no cross-cell qualification or labor.
} // namespace p8::banded_cells
