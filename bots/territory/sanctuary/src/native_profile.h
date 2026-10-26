#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::sanctuary;
inline constexpr char name[] = "Sanctuary";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "varg", "willow", "tavi", "ryth", "prism"};
inline constexpr char role_summary[] =
    "Heavy combat worker; actual ally healer; independent field-shopping producer; independent extensions.";
} // namespace p7::profile

