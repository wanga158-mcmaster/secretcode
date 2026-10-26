#pragma once
namespace profile {
inline constexpr const char* version_id = "domino-v1";
inline constexpr const char* core_id = "domino";
inline constexpr const char* scenario_id = "domino-first";
inline constexpr unsigned core_number = 7;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr bool draft_only = false;
inline constexpr unsigned bank_threshold = 24;
// Rows: harmless physical three-cluster group. Legal basic-only preparation
// removes the third member and leaves exactly one lethal/nonlethal pair.
inline constexpr int probe_group_key = 848;
inline constexpr int probe_member_key = 848;
inline constexpr int probe_survivor_key = 898;
inline constexpr int probe_remove_key = 948;
inline constexpr int probe_tile_index = 897;
inline constexpr bool probe_mirror_with_home = true;
inline constexpr unsigned max_setup_rounds = 240;
inline constexpr unsigned probe_round_limit = 300;
}
