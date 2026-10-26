#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::ports;
inline constexpr char name[] = "Ports";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "marcella", "vesper_vale", "tavi", "ren", "prism"};
inline constexpr char role_summary[] =
    "Durable first collector; mobile second collector; independent field-shopping producer; independent extensions.";
} // namespace p7::profile

