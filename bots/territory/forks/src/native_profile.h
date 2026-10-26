#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::forks;
inline constexpr char name[] = "Forks";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "marcella", "tavi", "varg", "stellar", "prism"};
inline constexpr char role_summary[] =
    "Durable collector; field-shopping collector; shielded route support; independent extensions.";
} // namespace p7::profile

