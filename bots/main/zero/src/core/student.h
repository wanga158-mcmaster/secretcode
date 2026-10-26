#pragma once
// Compact command policy facts. This is a distinct feature ABI from the diagnostic legacy heads.
// Every descriptor is a raw personal/public fact. Geometry is Chebyshev geometry, never a Paths
// distance or an implicit cache fallback. Obstacle-path and allied-claim facts are unavailable in v1.
#include <cstdint>
#include <vector>
#include "action.h"

namespace zero::student {

inline constexpr int kState = 48, kPhi = 24;
inline constexpr int kFeatureSchema = 3;

// Shared by native source records and production. Runtime fuel belongs only to mechanical admission,
// never a fabricated replay feature. Goal/control history must be declared as actual learner history
// or explicit goal-conditioned hindsight augmentation, not recovered expert-private Memory.
// Construction performs no SDK calls, Paths queries, forecasts or neural inference.
void state_vector(const Context& ctx, float* out);
int geometry_goal_distance(const Context& ctx, Tile from, bool* known = nullptr);

// Rule legality only. Internal caps are supplied by the caller; END is always present. No argument
// descriptors, damage forecasts or unselected candidate arrays are built to obtain this mask.
std::uint32_t rule_mask(const Context& ctx, std::uint32_t allowed = kAllActions);

// Full stable intent/argument lists, with no fixed nearest-target cap. Rows use Cand's existing
// executable payload conventions, but their 24 raw features belong only to this student schema.
// Goal rows carry GoalKind/target. Primitive rows are passed unchanged to sdk::primitive_result.
void goal_candidates(const Context& ctx, std::vector<Cand>& out);
ActionBuild argument_candidates(const Context& ctx, ActionType type, std::vector<Cand>& out);
Head argument_head(ActionType type);
void quantity_candidate(const Context& ctx, const Cand& item, Cand& out);

}  // namespace zero::student
