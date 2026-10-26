#pragma once
#include "../../common/src/profile.h"
namespace p8::circuitbreaker {
using common::DraftRoster;using common::ProbeBudget;using common::roster_actor;
using common::champion_pool;using common::supported_champion;
inline constexpr int forecast_window=120;
inline constexpr bool allow_denial=false;
// These are ties AFTER the queried complete-cycle comparison. Roles follow
// actual own draft ordinals, and every supported champion remains a candidate.
inline constexpr std::array<p6::Champion,8> leader_priority{
    p6::Champion::varg,p6::Champion::ren,p6::Champion::tavi,p6::Champion::vendetta,
    p6::Champion::willow,p6::Champion::marcella,p6::Champion::prism,p6::Champion::vesper_vale};
inline constexpr std::array<p6::Champion,8> support_priority{
    p6::Champion::willow,p6::Champion::varg,p6::Champion::ren,p6::Champion::tavi,
    p6::Champion::marcella,p6::Champion::vendetta,p6::Champion::prism,p6::Champion::vesper_vale};
inline constexpr std::array<p6::Champion,8> producer_priority{
    p6::Champion::marcella,p6::Champion::tavi,p6::Champion::vesper_vale,p6::Champion::vendetta,
    p6::Champion::varg,p6::Champion::ren,p6::Champion::prism,p6::Champion::willow};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal) {
    return ordinal==0?leader_priority:ordinal==1?support_priority:producer_priority;
}
} // namespace p8::circuitbreaker
