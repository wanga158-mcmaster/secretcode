#pragma once
#include "../../common/src/profile.h"
namespace p8::assay_guild {
using common::DraftRoster; using common::ProbeBudget; using common::roster_actor;
using common::champion_pool; using common::supported_champion;
inline constexpr std::array<p6::Champion,8> assay_priority{p6::Champion::tavi,p6::Champion::varg,
    p6::Champion::vendetta,p6::Champion::marcella,p6::Champion::vesper_vale,p6::Champion::ren,p6::Champion::prism,p6::Champion::willow};
inline constexpr std::array<p6::Champion,8> partner_priority{p6::Champion::varg,p6::Champion::marcella,
    p6::Champion::vendetta,p6::Champion::tavi,p6::Champion::ren,p6::Champion::vesper_vale,p6::Champion::willow,p6::Champion::prism};
inline constexpr std::array<p6::Champion,8> producer_priority{p6::Champion::marcella,p6::Champion::tavi,
    p6::Champion::vesper_vale,p6::Champion::varg,p6::Champion::vendetta,p6::Champion::ren,p6::Champion::prism,p6::Champion::willow};
inline const std::array<p6::Champion,8>& draft_priority(int ordinal) {
    return ordinal==0?assay_priority:ordinal==1?partner_priority:ordinal==2?producer_priority:champion_pool;
}
enum class Configuration { independent, joint_clear, serial };
inline Configuration division(int clear,int logistics) {
    if (clear<0 || logistics<0) return Configuration::independent;
    if (std::int64_t(clear)>=std::int64_t(logistics)+4) return Configuration::joint_clear;
    if (std::int64_t(logistics)>=std::int64_t(clear)+4) return Configuration::serial;
    return Configuration::independent;
}
}
