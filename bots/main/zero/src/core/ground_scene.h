#pragma once
// Optional computation-only ground-utility scene. Map and Obs stay immutable
// for this borrow; retire it before a primitive or mutation. Identity/revision
// checks reject reuse after the SDK observes or refreshes the borrowed Obs.
#include "utility.h"
#include "action.h"

namespace zero {

struct GroundUtilityLock
{
    Tile at{};
    bool enemy{};
};

struct GroundUtilityScene
{
    const Map* source_map{};
    const Obs* source_obs{};
    std::uint64_t source_revision{};
    UtilityRules rules{};
    UtilityBuildContract contract{};
    std::uint32_t known_mask{};  // WARD/USE bits only; unsupported differs from empty.
    std::vector<Tile> ward_tiles;
    std::vector<GroundUtilityLock> lock_rows;

    bool fresh_for(const Map& map, const Obs& obs) const noexcept
    {
        return source_map == &map && source_obs == &obs && source_revision == obs.revision;
    }
};

// Exact append_ground_utilities predicates/order/coverage, without raw row writes.
// Reuses only the output vectors' capacity. The scene is bound only after completion.
void build_ground_utility_scene(const Map& map, const Obs& obs, const UtilityRules& rules,
                                GroundUtilityScene& scene);
// Same non-ground preservation and SDK contract fields as eager observation.
// Stale identity/revision returns false without mutating obs.
bool apply_ground_utility_contract(Obs& obs, const GroundUtilityScene& scene);
// Complete original 24-float UtilityAction representation for one ground family.
// Clears output; false means stale borrow or a non-ground family, never a partial list.
bool materialize_ground_utility_scene(const Map& map, const Obs& obs,
                                     const GroundUtilityScene& scene, ActionType type,
                                     std::vector<UtilityAction>& out);

namespace student {
// Direct-root membership only; no descriptors or unselected raw arrays.
// A stale borrow returns END only. It is not a valid parity/coverage observation.
std::uint32_t rule_mask_ground_scene(const Context& ctx, const GroundUtilityScene& scene,
                                     std::uint32_t allowed = kAllActions);
// Ground families use scene facts; every other family delegates unchanged.
// A stale ground borrow returns mismatch with no rows.
ActionBuild argument_candidates_ground_scene(const Context& ctx, const GroundUtilityScene& scene,
                                             ActionType type, std::vector<Cand>& out);
}  // namespace student

}  // namespace zero
