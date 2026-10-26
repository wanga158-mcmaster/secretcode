#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::encirclement;
inline constexpr char name[] = "Encirclement";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "ren", "ryth", "marcella", "varg", "stellar"};
inline constexpr char role_summary[] =
    "Mobile targeted-stun pressure; area-stun pressure; durable home producer; independent extensions.";
} // namespace p7::profile

