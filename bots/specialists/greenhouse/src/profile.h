#pragma once
namespace profile {
inline constexpr const char* version_id = "greenhouse-v1";
inline constexpr const char* core_id = "greenhouse";
inline constexpr const char* scenario_id = "greenhouse-first";
inline constexpr unsigned core_number = 4;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr bool draft_only = false;
// First ID-ordered development map, first reachable safe gold-only singleton.
inline constexpr int probe_group_key = 299;
inline constexpr int probe_member_key = -1;
inline constexpr int probe_tile_index = 299;
// Identical equipment-free funding circuit. Comparison g_299 stays untouched.
inline constexpr int probe_funding_group_keys[] = {468, 678, 1006, 1206, 1365};
inline constexpr bool probe_mirror_with_home = true;
inline constexpr unsigned max_setup_rounds = 240;
inline constexpr unsigned probe_round_limit = 300;
}
