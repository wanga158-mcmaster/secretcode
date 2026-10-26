#pragma once
#include "../../common/src/profile.h"
namespace p8::finite_counterprotocol {
using common::DraftRoster; using common::ProbeBudget;
inline bool collector_actor(const p6::Actor& a,const DraftRoster& d) { return common::roster_actor(a,d)&&a.pick_ordinal==0; }
inline constexpr std::array<p6::Champion,8> collector_priority{p6::Champion::vendetta,p6::Champion::varg,p6::Champion::ren,p6::Champion::marcella,
    p6::Champion::tavi,p6::Champion::vesper_vale,p6::Champion::willow,p6::Champion::prism};
inline constexpr std::array<p6::Champion,8> escort_priority{p6::Champion::willow,p6::Champion::marcella,p6::Champion::varg,p6::Champion::vendetta,
    p6::Champion::ren,p6::Champion::tavi,p6::Champion::vesper_vale,p6::Champion::prism};
inline constexpr std::array<p6::Champion,8> producer_priority{p6::Champion::vesper_vale,p6::Champion::tavi,p6::Champion::marcella,p6::Champion::vendetta,
    p6::Champion::varg,p6::Champion::willow,p6::Champion::ren,p6::Champion::prism};
inline const std::array<p6::Champion,8>& priority(int ordinal) { return ordinal==0?collector_priority:ordinal==1?escort_priority:ordinal==2?producer_priority:common::champion_pool; }
}
