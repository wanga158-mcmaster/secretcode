#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::causeway;
inline constexpr char name[] = "Causeway";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "varg", "vendetta", "marcella", "ryth", "stellar"};
inline constexpr char role_summary[] =
    "Splash clearer; mobile finisher; durable capture holder; independent crossing workers.";
} // namespace p7::profile

