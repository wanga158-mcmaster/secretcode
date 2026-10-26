#pragma once
#include "../../common/src/profile.h"
namespace p8::rollback_crew {
using common::DraftRoster; using common::ProbeBudget; using common::champion_pool;
inline bool incumbent_actor(const p6::Actor& a,const DraftRoster& d) { return common::roster_actor(a,d)&&a.pick_ordinal<2; }
inline bool proposer_actor(const p6::Actor& a,const DraftRoster& d) { return common::roster_actor(a,d)&&a.pick_ordinal==2; }
inline constexpr std::array<p6::Champion,8> first_priority{p6::Champion::marcella,p6::Champion::varg,p6::Champion::vendetta,p6::Champion::tavi,
    p6::Champion::vesper_vale,p6::Champion::willow,p6::Champion::prism,p6::Champion::ren};
inline constexpr std::array<p6::Champion,8> second_priority{p6::Champion::varg,p6::Champion::vendetta,p6::Champion::marcella,p6::Champion::tavi,
    p6::Champion::vesper_vale,p6::Champion::ren,p6::Champion::prism,p6::Champion::willow};
inline constexpr std::array<p6::Champion,8> proposer_priority{p6::Champion::tavi,p6::Champion::varg,p6::Champion::vendetta,p6::Champion::vesper_vale,
    p6::Champion::marcella,p6::Champion::willow,p6::Champion::prism,p6::Champion::ren};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal) { return ordinal==0?first_priority:ordinal==1?second_priority:ordinal==2?proposer_priority:champion_pool; }
} // namespace p8::rollback_crew
