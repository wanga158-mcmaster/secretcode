#pragma once
#include <cambc.h>
#include <array>

// Suite indexes are append-only and distinct from SDK draft/physical IDs.
// Completed executors keep their original five-entry lookup unchanged.
namespace heuristics_draft {
enum Index {
    tavi=0, varg=1, vendetta=2, vesper_vale=3, marcella=4,
    prism=5, heart_of_ruin=6, willow=7, stellar=8, ryth=9,
    ren=10, cassi_and_issac=11
};
inline constexpr std::array choices{
    cambc::champion_draft::tavi, cambc::champion_draft::varg,
    cambc::champion_draft::vendetta, cambc::champion_draft::vesper_vale,
    cambc::champion_draft::marcella, cambc::champion_draft::prism,
    cambc::champion_draft::heart_of_ruin, cambc::champion_draft::willow,
    cambc::champion_draft::stellar, cambc::champion_draft::ryth,
    cambc::champion_draft::ren, cambc::champion_draft::cassi_and_issac
};
} // namespace heuristics_draft
