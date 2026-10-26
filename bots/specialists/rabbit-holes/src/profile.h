#pragma once
namespace profile {
inline constexpr const char* version_id = "rabbit-holes-v1";
inline constexpr const char* core_id = "rabbit-holes";
inline constexpr int core_number = 10;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr const char* scenario_id = "rabbit-holes-cardinal-hole-circuit";
inline constexpr int draft_candidate_limit = 32;
inline constexpr int minimum_walk_separation = 6;
inline constexpr int minimum_savings = 4;
inline constexpr unsigned approach_stall_turns = 8;
inline constexpr unsigned hidden_collision_blacklist_rounds = 4;
inline constexpr unsigned max_setup_rounds = 240;
inline constexpr unsigned probe_round_limit = 300;
// Separately authored legal laboratory map, never official screening behavior.
// Select the fixed side using our public spawn mask, without private team APIs.
inline constexpr int probe_home_marker = 405; // (2,13) in Team A spawn
inline constexpr int probe_group = 300;
inline constexpr int probe_group_reverse = 288;
inline constexpr int probe_waypoint = 293;
inline constexpr int probe_waypoint_reverse = 295;
inline constexpr int probe_altar = 350;
}
