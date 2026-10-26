#pragma once
#include "../../common/src/profile.h"
namespace p8::abstaining_specialists {
using common::DraftRoster;using common::roster_actor;using common::champion_pool;
inline constexpr std::array<p6::Champion,8> carry_priority{p6::Champion::prism,p6::Champion::varg,p6::Champion::tavi,
    p6::Champion::marcella,p6::Champion::vendetta,p6::Champion::vesper_vale,p6::Champion::willow,p6::Champion::ren};
inline constexpr std::array<p6::Champion,8> partner_priority{p6::Champion::varg,p6::Champion::marcella,p6::Champion::tavi,
    p6::Champion::willow,p6::Champion::vesper_vale,p6::Champion::vendetta,p6::Champion::ren,p6::Champion::prism};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal){return ordinal==0?carry_priority:partner_priority;}
inline bool carry_actor(const p6::Actor& actor,const DraftRoster& draft){return roster_actor(actor,draft)&&actor.pick_ordinal==0&&actor.type==p6::Champion::prism;}
}
