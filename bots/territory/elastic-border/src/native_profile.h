#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::elastic_border;
inline constexpr char name[] = "Elastic Border";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "tavi", "prism", "heart_of_ruin", "ren", "willow"};
inline constexpr char role_summary[] =
    "Field-shopping forward producer; spellcasting forward producer; shielded inner reserve; independent extensions.";
} // namespace p7::profile

