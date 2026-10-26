#pragma once
namespace profile {
inline constexpr const char* version_id = "catapult-club-v1";
inline constexpr const char* core_id = "catapult-club";
inline constexpr int core_number = 9;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr const char* scenario_id = "catapult-safe-repeat";
inline constexpr unsigned siege_candidate_limit = 16;
inline constexpr unsigned siege_completion_reserve = 64;
inline constexpr unsigned siege_approach_stall = 8;
inline constexpr unsigned siege_visibility_timeout = 4;
inline constexpr unsigned siege_blacklist_rounds = 32;
// Probe IDs are canonical minimum spawn-tile IDs, never engine group handles.
inline constexpr int probe_group = 119;
inline constexpr int probe_group_second = -1;
inline constexpr int probe_waypoint = 33;
inline constexpr int probe_altar = -1;
inline constexpr int probe_cast_group = -1;
inline constexpr unsigned probe_capture_count_before_cast = 5;
}
