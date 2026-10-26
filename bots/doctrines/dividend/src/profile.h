#pragma once
#include <array>
#include <string_view>
#include "../../common/src/observe.h"

namespace p5::dividend {
enum class Arm { active, route_first_control };
struct Profile {
    Arm arm = Arm::active;
    // Marcella is the financing mechanism. Companion picks are chosen from the
    // full public pool using the map's independent work and these role weights.
    // These are bounded engineering hypotheses, not a claim of an optimal draft.
    static DraftRequest draft_request() {
        DraftRequest request;
        request.required_anchor = "marcella";
        request.roles = {{{1, 0.01, 0, 0.5, 1},
                          {1, 0.012, 0, 0.5, 0.4},
                          {1.5, 0.004, 0, 1, 2},
                          {0.8, 0.005, 0.2, 0.2, 1},
                          {1.4, 0.003, 0.05, 3, 2}}};
        return request;
    }
    static constexpr std::array<std::string_view, 3> purchase_families{
        "monster_strength", "jungle_health", "monster_focus"};
    bool valid_team_size(int picks) const { return picks >= 3 && picks <= 5; }
};
} // namespace p5::dividend
