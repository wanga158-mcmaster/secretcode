#pragma once
#include "../../common/src/profile.h"
namespace p8::underwritten_frontier {
using common::DraftRoster; using common::ProbeBudget; using common::roster_actor;
using common::champion_pool; using common::supported_champion;
using common::max_options; using common::forecast_window; using common::project_rounds;
using common::minimum_trial_health_fraction;
inline constexpr bool allow_denial=false;
inline bool insurer_actor(const p6::Actor& actor,const DraftRoster& draft) {
    return roster_actor(actor,draft) && actor.pick_ordinal==0;
}
inline bool venture_actor(const p6::Actor& actor,const DraftRoster& draft) {
    return roster_actor(actor,draft) && (actor.pick_ordinal==1 || actor.pick_ordinal==2);
}
inline constexpr std::array<p6::Champion,8> insurer_priority{
    p6::Champion::marcella,p6::Champion::vesper_vale,p6::Champion::tavi,p6::Champion::varg,
    p6::Champion::vendetta,p6::Champion::ren,p6::Champion::willow,p6::Champion::prism};
inline constexpr std::array<p6::Champion,8> venture_priority{
    p6::Champion::varg,p6::Champion::tavi,p6::Champion::vendetta,p6::Champion::marcella,
    p6::Champion::ren,p6::Champion::vesper_vale,p6::Champion::willow,p6::Champion::prism};
inline constexpr std::array<p6::Champion,8> collector_priority{
    p6::Champion::prism,p6::Champion::tavi,p6::Champion::vesper_vale,p6::Champion::varg,
    p6::Champion::marcella,p6::Champion::vendetta,p6::Champion::ren,p6::Champion::willow};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal) {
    return ordinal==0?insurer_priority:ordinal==2?collector_priority:ordinal==1?venture_priority:champion_pool;
}
} // namespace p8::underwritten_frontier
