#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::harvest_and_exit;
inline constexpr char name[] = "Harvest and Exit";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "vendetta", "ren", "marcella", "tavi", "vesper_vale"};
inline constexpr char role_summary[] =
    "Mobile first raider; mobile second raider; durable home producer; independent extensions.";
} // namespace p7::profile

