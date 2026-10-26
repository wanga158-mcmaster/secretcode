#pragma once
// Draft inputs shared by the live SDK adapter and exact native record conversion. No preference or choice.
#include <cstdint>
#include <span>
#include "cand.h"

namespace zero {

// The SDK can collect these aggregates without loading terrain or path fields. Sums follow the original
// group/member iteration order in float; a sum of per-group strength totals need not have the same bits.
struct DraftMapFacts
{
    std::uint32_t area{}, group_count{}, altar_count{};
    float gems{}, gold{}, members{}, strength{};
};

DraftMapFacts draft_map_facts(const Map& map);

// Pick IDs are champion_draft IDs (0..11), not physical champion types. The candidate order is ascending,
// excluding our completed picks only; the opponent may choose the same champion. All kState entries are set.
void draft_facts(const DraftMapFacts& map, int team_size, std::span<const int> ours, std::span<const int> theirs,
                 float* state, CandSet& candidates);
void draft_facts(const Map& map, int team_size, std::span<const int> ours, std::span<const int> theirs,
                 float* state, CandSet& candidates);

}  // namespace zero
