#pragma once
#include "../../common/src/profile.h"
namespace p8::staged_venture {
using common::DraftRoster;using common::ProbeBudget;using common::roster_actor;
using common::champion_pool;using common::supported_champion;
inline constexpr bool allow_denial=false;
inline constexpr int max_options=8,max_contexts=8,forecast_window=120;
inline bool venture_actor(const p6::Actor& actor,const DraftRoster& draft) {
    return roster_actor(actor,draft)&&actor.pick_ordinal==1;
}
inline bool helper_actor(const p6::Actor& actor,const DraftRoster& draft) {
    return roster_actor(actor,draft)&&actor.pick_ordinal==0;
}
inline constexpr std::array<p6::Champion,8> venture_priority{
    p6::Champion::tavi,p6::Champion::marcella,p6::Champion::varg,p6::Champion::vendetta,
    p6::Champion::vesper_vale,p6::Champion::ren,p6::Champion::willow,p6::Champion::prism};
inline constexpr std::array<p6::Champion,8> helper_priority{
    p6::Champion::varg,p6::Champion::willow,p6::Champion::marcella,p6::Champion::ren,
    p6::Champion::vendetta,p6::Champion::tavi,p6::Champion::prism,p6::Champion::vesper_vale};
inline constexpr std::array<p6::Champion,8> producer_priority{
    p6::Champion::marcella,p6::Champion::vesper_vale,p6::Champion::vendetta,p6::Champion::tavi,
    p6::Champion::varg,p6::Champion::prism,p6::Champion::ren,p6::Champion::willow};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal) {
    return ordinal==0?helper_priority:ordinal==1?venture_priority:producer_priority;
}
} // namespace p8::staged_venture
