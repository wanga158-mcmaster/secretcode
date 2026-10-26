#pragma once
namespace profile {
inline constexpr const char* version_id = "gremlins-v1";
inline constexpr const char* core_id = "gremlins";
inline constexpr int core_number = 16;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr const char* scenario_id = "gremlins-lock-interruption";
inline constexpr unsigned pursuit_turns = 8, stall_turns = 3, cooldown_turns = 32;
inline constexpr unsigned minimum_bag = 24, setup_limit = 240;
inline constexpr int placement_range = 2;
// Probe IDs are canonical minimum spawn-tile IDs, never engine group handles.
inline constexpr int probe_group = -1;
inline constexpr int probe_group_second = -1;
inline constexpr int probe_waypoint = -1;
inline constexpr int probe_altar = -1;
inline constexpr int probe_cast_group = -1;
inline constexpr unsigned probe_capture_count_before_cast = 5;
}
