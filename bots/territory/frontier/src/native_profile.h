#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::frontier;
inline constexpr char name[] = "Frontier";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "varg", "vendetta", "willow", "ryth", "heart_of_ruin"};
inline constexpr char role_summary[] =
    "Shielded opener; mobile resident; visible healing support; independent extensions.";
} // namespace p7::profile

