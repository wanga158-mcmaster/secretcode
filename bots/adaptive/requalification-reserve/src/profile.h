#pragma once
#include "../../common/src/profile.h"
namespace p8::requalification_reserve {
using common::DraftRoster; using common::ProbeBudget; using common::champion_pool;
inline bool collector_actor(const p6::Actor& a,const DraftRoster& d) { return common::roster_actor(a,d)&&a.pick_ordinal==0; }
inline bool insulated_actor(const p6::Actor& a,const DraftRoster& d) { return common::roster_actor(a,d)&&(a.pick_ordinal==1||a.pick_ordinal==2); }
inline constexpr std::array<p6::Champion,8> collector_priority{p6::Champion::varg,p6::Champion::vendetta,p6::Champion::marcella,p6::Champion::tavi,
    p6::Champion::ren,p6::Champion::vesper_vale,p6::Champion::willow,p6::Champion::prism};
inline constexpr std::array<p6::Champion,8> producer_priority{p6::Champion::tavi,p6::Champion::varg,p6::Champion::vendetta,p6::Champion::vesper_vale,
    p6::Champion::marcella,p6::Champion::prism,p6::Champion::willow,p6::Champion::ren};
inline constexpr std::array<p6::Champion,8> conversion_priority{p6::Champion::marcella,p6::Champion::varg,p6::Champion::vendetta,p6::Champion::tavi,
    p6::Champion::vesper_vale,p6::Champion::willow,p6::Champion::prism,p6::Champion::ren};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal) { return ordinal==0?collector_priority:ordinal==1?producer_priority:ordinal==2?conversion_priority:champion_pool; }
} // namespace p8::requalification_reserve
