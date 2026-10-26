#pragma once
namespace profile {
inline constexpr const char* version_id = "lighthouse-v1";
inline constexpr const char* core_id = "lighthouse";
inline constexpr int core_number = 11;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr bool probe_script = false;
inline constexpr const char* scenario_id = "lighthouse-hidden-respawn";
inline constexpr unsigned ward_candidate_limit = 8;
// v2 measured ~72k before its own logging/gate overhead; reserve actual work.
inline constexpr unsigned ward_start_fuel = 65000;
// Probe IDs are canonical minimum spawn-tile IDs, never engine group handles.
inline constexpr int probe_group = 144;
inline constexpr int probe_group_second = 152;
inline constexpr int probe_waypoint = 351;
inline constexpr int probe_marker = 141;
inline constexpr int probe_firing = 17;
inline constexpr int probe_altar = -1;
inline constexpr int probe_cast_group = -1;
inline constexpr unsigned probe_capture_count_before_cast = 5;
}
