#pragma once
namespace profile {
inline constexpr const char* version_id = "breakpoint-brigade-v1";
inline constexpr const char* core_id = "breakpoint-brigade";
inline constexpr const char* scenario_id = "breakpoint-brigade-first";
inline constexpr unsigned core_number = 5;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr bool draft_only = false;
inline constexpr unsigned bank_threshold = 12;
// First development map then first numeric singleton harmless gold group.
inline constexpr int probe_group_key = 299;
inline constexpr int probe_member_key = -1;
inline constexpr int probe_tile_index = 298;
inline constexpr bool probe_mirror_with_home = true;
// Queried passive_gold is zero. Legally clear/capture these other harmless
// clusters with no equipment or abilities before touching the comparison one.
inline constexpr int probe_funding_group_keys[] = {468, 678, 1006};
inline constexpr unsigned max_setup_rounds = 240;
inline constexpr unsigned probe_round_limit = 300;
}
