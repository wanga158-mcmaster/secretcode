#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::interior_lines;
inline constexpr char name[] = "Interior Lines";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "ren", "marcella", "tavi", "varg", "vesper_vale"};
inline constexpr char role_summary[] =
    "Mobile reserve; durable first wing; field-shopping second wing; independent extensions.";
} // namespace p7::profile

