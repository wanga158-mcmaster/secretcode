#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::reclamation;
inline constexpr char name[] = "Reclamation";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "vendetta", "varg", "marcella", "stellar", "tavi"};
inline constexpr char role_summary[] =
    "Mobile independent worker; shielded border reclaimer; durable collector; independent extensions.";
} // namespace p7::profile

