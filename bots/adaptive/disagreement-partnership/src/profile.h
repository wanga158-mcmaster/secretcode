#pragma once
#include "../../common/src/profile.h"
namespace p8::disagreement_partnership {
using common::DraftRoster; using common::ProbeBudget;
inline bool investigator(const p6::Actor& a,const DraftRoster& d) { return common::roster_actor(a,d)&&a.pick_ordinal==2; }
inline constexpr std::array<p6::Champion,8> producer_priority{p6::Champion::vesper_vale,p6::Champion::tavi,p6::Champion::vendetta,p6::Champion::varg,
    p6::Champion::marcella,p6::Champion::willow,p6::Champion::ren,p6::Champion::prism};
inline constexpr std::array<p6::Champion,8> converter_priority{p6::Champion::marcella,p6::Champion::willow,p6::Champion::varg,p6::Champion::tavi,
    p6::Champion::vesper_vale,p6::Champion::vendetta,p6::Champion::ren,p6::Champion::prism};
inline constexpr std::array<p6::Champion,8> investigator_priority{p6::Champion::ren,p6::Champion::varg,p6::Champion::vendetta,p6::Champion::marcella,
    p6::Champion::tavi,p6::Champion::vesper_vale,p6::Champion::willow,p6::Champion::prism};
inline const std::array<p6::Champion,8>& priority(int ordinal) { return ordinal==0?producer_priority:ordinal==1?converter_priority:ordinal==2?investigator_priority:common::champion_pool; }
} // namespace p8::disagreement_partnership
