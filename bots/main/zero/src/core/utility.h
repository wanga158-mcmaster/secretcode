#pragma once
// Pure ground-utility facts shared by the live SDK and exact replay adapter.
#include <cstdint>
#include "map.h"
#include "obs.h"

namespace zero {

inline constexpr std::uint32_t kConsumeTp = 1u << 0;    // typed SDK enum 0, native raw ID 0
inline constexpr std::uint32_t kConsumeLock = 1u << 1;  // typed SDK enum 1, native raw ID 2

struct UtilityRules
{
    float ward_radius{};  // queried game settings; not a movement mask or policy radius
    float lock_radius{};  // caller passes pinned rules value 2; current native settings have no query
    std::uint32_t lock_price{};
    std::uint32_t known_consumable_mask{};  // only kConsumeLock is implemented here
};

enum UtilityLimitation : std::uint32_t
{
    ul_ward_holes = 1u << 0,          // excluded conservatively; no positive hole canary yet
    ul_tp_use = 1u << 1,              // TP arguments are not implemented
    ul_reservations = 1u << 2,        // reserved targets unproven, or the reservation mask is unknown
    ul_command_facts = 1u << 3,       // required exact command facts are absent
    ul_ally_ward_facts = 1u << 4,      // own ward positions do not replace this known occupancy mask
    ul_rules = 1u << 5,
    ul_lock_facts = 1u << 6
};

struct UtilityBuildContract
{
    std::uint32_t known_consume_mask{};
    // Capability limits remain explicit even in observations where they omit no possible arguments.
    std::uint32_t limitations{ ul_ward_holes | ul_tp_use | ul_reservations };
    std::uint32_t affected_limits{};
    bool ward_complete{}, consume_complete{};  // complete legal space for this observation only
    int ward_count{}, lock_count{};
};

// Rebuilds WARD and USE_CONSUMABLE rows; preserves shopping/selling rows and their known bits.
// Ward payload target=-1; lock payload target=typed consumable enum 1. Coordinates are row-major,
// with no 24-candidate cap. Family bits admit represented rows; the returned contract must accompany
// coverage claims, since a lock can still be offered while TP arguments remain unsupported.
// phi[15..18] use append_utility's payload/dx/dy/distance facts. [19] charges/inventory, [20] price/1000,
// [21] radius/20, [22] ward recharge/40 or enemy-lock replacement, [23] reserved zero.
UtilityBuildContract append_ground_utilities(const Map& map, Obs& obs, const UtilityRules& rules);

}  // namespace zero
