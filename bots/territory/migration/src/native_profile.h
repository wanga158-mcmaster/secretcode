#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::migration;
inline constexpr char name[] = "Migration";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "varg", "willow", "marcella", "vendetta", "stellar"};
inline constexpr char role_summary[] =
    "Shielded crew worker; visible healing support; durable crew collector; independent extensions.";
} // namespace p7::profile

