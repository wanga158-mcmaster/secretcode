#pragma once
// Autoregressive command interface. The controller chooses a command family, then just that family's
// argument head. Execution and engineering admission belong to the caller; there is no phase priority.
#include <cstdint>
#include <span>
#include "cand.h"

namespace zero {

// Stable data/SDK vocabulary. END preserves the current engine channel. Recall is an ordinary command:
// recall -> END starts it, END on the next turn continues it, CANCEL explicitly abandons it.
enum ActionType : std::uint8_t
{
    a_end, a_move, a_attack, a_cast0, a_cast1, a_buy, a_recall, a_tribute, a_cancel, a_replan,
    a_buy_consumable, a_use_consumable, a_ward, a_sell, a_priority, a_count
};
inline constexpr std::uint32_t kAllActions = (1u << a_count) - 1u;
static_assert(a_count <= kMaxCands && a_count < 32);
constexpr std::uint32_t action_bit(ActionType type) { return 1u << static_cast<unsigned>(type); }

enum class ActionBuildStatus : std::uint8_t
{
    ok, unavailable, unsupported, truncated, mismatch, overflow
};
struct ActionBuild
{
    ActionBuildStatus status{ ActionBuildStatus::ok };
    int total{};  // cheap legal/represented-source count before the fixed candidate cap
    bool usable() const { return status == ActionBuildStatus::ok || status == ActionBuildStatus::truncated; }
};

// Cheap family mask/facts only: never calls goal/step/attack/cast/shop candidate builders or paths.
// allowed_mask is solely the caller's engineering admission mask (fuel, internal-operation cap).
// END is always offered. h_action facts: type one-hot[0..14], count/24[15], allowance bits[16..18],
// channel code + remaining/(remaining+1)[19], issued commands/8[20], range/20[21],
// nearest argument distance/20[22], direct rule power/own max hp[23].
// DASH count[15] is a 0/1 availability witness divided by 24, never landing cardinality, in both full and
// deferred observations. MOVE/DASH/END override [22] with signed current straight-line goal distance/20.
// AOE/DMG/CC override [23] with the sum of clipped rule damage to alive permitted visible targets within
// range+radius, divided by own max hp: a potential upper bound, never a score or a legality condition.
// REPLAN overrides [21] with current-goal-valid and [22] with current-goal age/40 (zero if unknown).
// PRIORITY overrides [23] with the encoded current next-turn priority. Own command history distinguishes
// repeated free commands from their first issue; no family is forbidden because it was already used.
void action_candidates(const Context& ctx, CandSet& out, std::uint32_t allowed_mask = kAllActions);
// The identical supported-family membership without descriptors or visible-damage potential work.
// Intended for direct-logit policies; no selected argument arrays, Paths or forecasts are constructed.
std::uint32_t action_rule_mask(const Context& ctx, std::uint32_t allowed_mask = kAllActions);

// Builds only a chosen family. Old target heads retain their existing Cand conventions; h_aux uses
// kind=ActionType, target=catalog item id (SELL), SDK consumable enum 0/1 (BUY_CONSUMABLE/USE_CONSUMABLE),
// altar index or signed priority, at=ground argument. REPLAN uses h_goal.
// None/stay/stop are removed. Utility arguments are supplied by the exact SDK/replay adapter through
// Obs::utility_actions; its utility_known_mask distinguishes unsupported from currently unavailable.
// A truncated set is usable at runtime, but a missing expert argument is a coverage failure, never END.
// PRIORITY is a scalar head, not classification: h_priority, n=1, phi={1,0,0,0}, with no source value
// in its facts. The caller must evaluate its logit even though n=1 and decode that scalar as below.
ActionBuild argument_candidates(const Context& ctx, ActionType type, CandSet& out);

// Complete ground arguments factor into (payload, X), then legal Y values in that chosen column.
// No X candidate is an executable command. Both decisions use the same immutable pre-input observation.
// Payload keeps coincident but different commands distinct (e.g. lock vs crystal consumable enum).
struct SpatialTile { Tile at{}; std::int32_t target{ -1 }; };
struct SpatialColumn { std::int32_t target{ -1 }; std::int16_t x{}; };
enum class SpatialStage : std::uint8_t { column = 1, row = 2 };
inline constexpr int kMaxSpatialTiles = kMaxCands * kMaxCands;

bool spatial_action(const Context& ctx, ActionType type);
// Cheap legal positions only: range/map-bounded AOE centres, exact observed dash landings, or supplied
// SDK utility arguments. The caller retains this list unchanged through both stages. Duplicate entries
// are permitted. An input/axis beyond the bounded factorization returns overflow, never a partial grid.
// A selected dash uses the exact slot range: unknown or differently ranged cached lists are generated
// through the shared const movement helper without mutating/copying Obs. Root masks only seek a witness.
ActionBuild spatial_argument_tiles(const Context& ctx, ActionType type, std::vector<SpatialTile>& out);
ActionBuild spatial_argument_candidates(const Context& ctx, ActionType type, std::span<const SpatialTile> legal,
                                       SpatialStage stage, SpatialColumn selected, CandSet& out);
// Spatial facts override phi[0]=stage, phi[14]=dx/20 and phi[13]=dy/20 (row only, zero for column).
// X facts are means over all its legal rows; Y facts describe exactly the selected destination. For dash,
// X goal progress uses straight-line goal distance (cheap); Y uses the same Paths contract as old casts.
// Other argument modes retain their prior facts. Existing cast or h_aux head scores both spatial stages.

float encode_priority(std::int32_t priority);  // asinh(priority) / 10, finite for every int32
// Rounded sinh(10*scalar), saturated before evaluating sinh outside the int32 range. +/-infinity
// saturates; NaN returns zero to avoid undefined conversion. Callers should reject/log nonfinite logits.
std::int32_t decode_priority(float scalar);

// BUY quantity is one learned scalar evaluated after the catalogue item choice, at the same pre-input
// state. A zero maximum means invalid/unavailable/zero-price item; the builder distinguishes unsupported
// zero prices explicitly. Quantities are bounded by current gold/cost and signed-int32 representability.
std::int32_t max_affordable_quantity(const Context& ctx, int item_id);
ActionBuild quantity_candidates(const Context& ctx, int item_id, CandSet& out);
ActionBuild quantity_argument_candidates(const Context& ctx, const Cand& item, CandSet& out);
std::int32_t decode_quantity(float scalar, std::int32_t maximum);  // zero only when maximum <= 0

const char* action_name(ActionType type);

}  // namespace zero
