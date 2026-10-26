#pragma once

namespace mosaic::cfg {
// The clean scaffold remains the control until the independently derived controller qualifies.
inline constexpr bool enabled = false;
// Sparse selected-command service; the R13 interpreter remains its reference.
inline constexpr bool selected_service = false;
inline constexpr bool direct_proposals = false;
inline constexpr bool conversion_service = false;
inline constexpr bool lifecycle_proposals = false;
inline constexpr bool service_continuations = false;
inline constexpr int trace = 0;
}
