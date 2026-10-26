#pragma once
namespace profile {
inline constexpr const char* version_id = "porcupine-v1";
inline constexpr const char* core_id = "porcupine";
inline constexpr const char* scenario_id = "porcupine-first";
inline constexpr unsigned core_number = 8;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr bool draft_only = false;
inline constexpr unsigned bank_threshold = 24;
// Rows: first ID-ordered all-weak multi-attacker camp on the first
// development map with such a camp. All selectors mirror by map symmetry.
inline constexpr int probe_group_key = 168;
inline constexpr int probe_member_key = 168;
inline constexpr int probe_tile_index = 117;
inline constexpr int probe_funding_first_key = 848;
inline constexpr int probe_funding_second_key = 1551;
inline constexpr unsigned max_setup_rounds = 240;
inline constexpr unsigned probe_round_limit = 300;
}
