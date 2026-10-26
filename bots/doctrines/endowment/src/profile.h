#pragma once
#include "../../common/src/observe.h"
#include <array>
#include <string_view>
namespace p5::endowment {
enum class Arm { active, bank_first_control };
struct Profile {
    Arm arm = Arm::active;
    static constexpr DraftRequest draft_request() {
        return {"prism", {{{1.1, .010, .04, 2, 2}, {.7, .012, .30, 2, 2},
                          {1.1, .018, .04, 2, 2}, {1.2, .010, .08, 3, 3},
                          {1.1, .012, .08, 3, 3}}}};
    }
    static constexpr std::array<std::string_view, 3> purchase_families{
        "monster_strength", "jungle_health", "monster_focus"};
};
} // namespace p5::endowment
