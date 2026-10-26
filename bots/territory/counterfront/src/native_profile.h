#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::counterfront;
inline constexpr char name[] = "Counterfront";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "vesper_vale", "marcella", "ren", "tavi", "varg"};
inline constexpr char role_summary[] =
    "Mobile first producer; durable opposite producer; mobile cross-front reserve; independent extensions.";
} // namespace p7::profile

