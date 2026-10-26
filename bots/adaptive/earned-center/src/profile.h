#pragma once
#include "../../common/src/profile.h"

// Offline P8-02 profile; actor roles follow the actual selected own roster.
// These preferences resolve identical complete-cycle ranks only. Every pick
// needs a public-data solo first-income certificate before it may be selected.
namespace p8::earned_center {
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
    p6::Champion::marcella, p6::Champion::varg, p6::Champion::vendetta,
    p6::Champion::tavi, p6::Champion::ren, p6::Champion::vesper_vale,
    p6::Champion::willow, p6::Champion::prism};
// Marcella is a conversion-capable collector, with stun immunity and a safe
// melee kit. Her tribute income, when supported by queried settings, belongs
// in her complete schedule at the actual own tribute. This table adds neither
// income nor a gold-to-score bonus; unreconciled mechanics remain unavailable.
inline constexpr std::array<p6::Champion,8> producer_priority{
    p6::Champion::tavi, p6::Champion::varg, p6::Champion::vendetta,
    p6::Champion::vesper_vale, p6::Champion::marcella, p6::Champion::prism,
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
// Helper preference never promises future labor: all noncollectors start U,
// retain their own bags, and help only at a visible, individually safe proposal.
// Fourth/fifth picks retain independent production until such a proposal exists.
} // namespace p8::earned_center
