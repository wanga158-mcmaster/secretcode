#pragma once
#include "../../common/src/profile.h"
namespace p8::common_cause_firewall {
using common::DraftRoster; using common::ProbeBudget;
using common::champion_pool; using common::roster_actor;
inline bool field_actor(const p6::Actor& a,const DraftRoster& d) { return roster_actor(a,d)&&a.pick_ordinal==0; }
inline bool spawn_collector(const p6::Actor& a,const DraftRoster& d) { return roster_actor(a,d)&&a.pick_ordinal==1; }
inline bool producer(const p6::Actor& a,const DraftRoster& d) { return field_actor(a,d)||spawn_collector(a,d); }
inline constexpr std::array<p6::Champion,8> field_priority{p6::Champion::tavi,p6::Champion::marcella,p6::Champion::varg,
    p6::Champion::vendetta,p6::Champion::vesper_vale,p6::Champion::willow,p6::Champion::prism,p6::Champion::ren};
inline constexpr std::array<p6::Champion,8> spawn_priority{p6::Champion::varg,p6::Champion::vendetta,p6::Champion::marcella,
    p6::Champion::vesper_vale,p6::Champion::ren,p6::Champion::tavi,p6::Champion::prism,p6::Champion::willow};
inline constexpr std::array<p6::Champion,8> helper_priority{p6::Champion::marcella,p6::Champion::willow,p6::Champion::varg,
    p6::Champion::vendetta,p6::Champion::ren,p6::Champion::tavi,p6::Champion::vesper_vale,p6::Champion::prism};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal) {
    return ordinal==0?field_priority:ordinal==1?spawn_priority:ordinal==2?helper_priority:champion_pool;
}
} // namespace p8::common_cause_firewall
