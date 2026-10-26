#pragma once
// Raw observed design-range coverage at each supplied MOVE destination; no forecast or policy.
#include <cstdint>
#include <span>
#include <vector>
#include "cand.h"

namespace zero::student {

struct MoveObservedDesignCoverage
{
    Cand move;  // Exact copy: preserves the complete supplied row, payload and order.
    std::uint64_t living_visible_enemy_champions_in_design_range{};
    double observed_enemy_strength_sum{};
    std::uint64_t living_visible_awake_camps_in_design_range{};
    double queried_awake_camp_base_strength_sum{};
};

// Preconditions: a finished immutable Map, complete canonical current Obs collections (one view per
// visible entity), and the complete unchanged canonical MOVE rows from student::argument_candidates.
// The input span must not alias out. The caller supplies observation completeness; this function does
// not infer unseen entities or reconstruct engine legality, reachability or private history.
//
// Each output row copies its input Cand and covers all supplied living (hp > 0) visible enemies and
// living awake visible camps at inclusive Chebyshev design basic-attack range of that destination.
// Enemy Strength is observed; camp Strength is the queried design base value. Zero-Strength entities
// still count. Stun, allowances, target choice, visibility after moving, spells, buffs, mitigations and
// timing are not simulated. Zero coverage does not assert safety, and sums do not predict damage.
//
// Map shape, consumed observation facts (including dead/asleep sources), and legal-looking MOVE
// payloads/finite descriptors are validated. Invalid inputs throw std::invalid_argument; nonfinite
// accumulated sums throw std::overflow_error. out is cleared at entry and remains empty on failure.
// No sorting, caps, SDK, Paths, Memory, fuel or network calls. Work is linear in supplied observations
// plus O(M * (E + C)) range tests, with all sums in canonical observation order.
void move_observed_design_coverage(const Context& ctx, std::span<const Cand> moves,
                                   std::vector<MoveObservedDesignCoverage>& out);

}  // namespace zero::student
