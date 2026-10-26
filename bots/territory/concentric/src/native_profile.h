#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::concentric;
inline constexpr char name[] = "Concentric";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "tavi", "marcella", "varg", "vendetta", "stellar"};
inline constexpr char role_summary[] =
    "Field-shopping opener; durable inner producer; shielded outer worker; independent extensions.";
} // namespace p7::profile

