#pragma once
#include <array>
#include "../../common/src/native_types.h"

namespace p7::profile {
inline constexpr auto design = live::Design::islander;
inline constexpr char name[] = "Islander";
// Discretionary kit-compatible draft order, not a calibrated strength ranking.
// Core ordinals preserve the controller's work roles in three-pick games.
inline constexpr std::array<const char*, 5> draft_order{
    "vendetta", "ren", "vesper_vale", "tavi", "marcella"};
inline constexpr char role_summary[] =
    "Mobile first specialist; mobile second specialist; connected supplier with its own dash; independent extensions.";
} // namespace p7::profile

