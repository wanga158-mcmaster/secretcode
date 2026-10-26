#pragma once
#ifndef FIREFLIES_BOUNDED_STARTUP
#define FIREFLIES_BOUNDED_STARTUP 0
#endif
#ifndef FIREFLIES_CONNECTED_CATALOG
#define FIREFLIES_CONNECTED_CATALOG 0
#endif
#ifndef FIREFLIES_CAPTURE_DEFENSE
#define FIREFLIES_CAPTURE_DEFENSE 0
#endif
namespace profile {
inline constexpr const char* version_id = "fireflies-v1";
inline constexpr const char* core_id = "fireflies";
inline constexpr const char* scenario_id = "fireflies-first";
inline constexpr unsigned core_number = 1;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr bool draft_only = false;
inline constexpr unsigned bank_threshold = 12;
// Diagnostic-only canonical fixture. Mirror these keys when our public spawn
// is the later symmetric spawn. Production never reads these selectors.
// collision's first ID-ordered solo wolf: (10,5), 22 gems, outside altar radius 4.
inline constexpr int probe_group_key = 215;
inline constexpr int probe_member_key = -1;
inline constexpr int probe_tile_index = 215;
inline constexpr bool probe_mirror_with_home = true;
inline constexpr unsigned max_setup_rounds = 240;
inline constexpr unsigned probe_round_limit = 300;
}
