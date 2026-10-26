#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::veil;
inline constexpr char name[] = "Veil";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "vesper_vale", "vendetta", "prism", "ren", "tavi"};
inline constexpr char role_summary[] =
    "Mobile ranged producer; mobile melee producer; spellcasting producer; each owns its own observations.";
} // namespace p7::profile

