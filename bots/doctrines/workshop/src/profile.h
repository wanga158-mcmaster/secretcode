#pragma once
#include "../../common/src/observe.h"
#include <array>
#include <string_view>

namespace p5::workshop {
enum class Arm { active, route_first_control };
struct Profile {
    Arm arm = Arm::active;
    static constexpr std::array<std::string_view, 3> purchase_families{
        "monster_strength", "jungle_health", "monster_focus"};
    // The user replaces prescribed pick prefixes with full-pool role drafting.
    // Tavi anchors the field-finance mechanism; the other four roles cover
    // independent production, difficult clearance, recovery and distant work.
    static constexpr DraftRequest draft_request() {
        // Traits use raw DPT, health and healing units, so health weights are
        // deliberately smaller; a thousand health is not a thousand DPT.
        return {"tavi", {{{1.2, .008, 0, 1.5, 1}, {1, .015, 0, 1, 1},
                           {1.2, .006, .05, 2, 2}, {.4, .008, .30, 1.5, 1},
                           {1, .006, .05, 4, 3}}}};
    }
    bool valid_team_size(int picks) const { return picks >= 3 && picks <= 5; }
};
} // namespace p5::workshop
