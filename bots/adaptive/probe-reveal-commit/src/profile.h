#pragma once
#include "../../common/src/profile.h"
namespace p8::probe_reveal_commit {
using common::DraftRoster;using common::roster_actor;using common::champion_pool;
inline constexpr std::array<p6::Champion,8> approach_priority{p6::Champion::tavi,p6::Champion::varg,p6::Champion::marcella,
    p6::Champion::vendetta,p6::Champion::vesper_vale,p6::Champion::ren,p6::Champion::prism,p6::Champion::willow};
inline constexpr std::array<p6::Champion,8> income_priority{p6::Champion::marcella,p6::Champion::varg,p6::Champion::tavi,
    p6::Champion::willow,p6::Champion::vesper_vale,p6::Champion::vendetta,p6::Champion::ren,p6::Champion::prism};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal){return ordinal==1?income_priority:approach_priority;}
}
