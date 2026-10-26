#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::gatehouse;
inline constexpr char name[] = "Gatehouse";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "heart_of_ruin", "marcella", "willow", "ren", "tavi"};
inline constexpr char role_summary[] =
    "Shielded mobile guard; durable enclave producer; visible healing producer; independent extensions.";
} // namespace p7::profile

