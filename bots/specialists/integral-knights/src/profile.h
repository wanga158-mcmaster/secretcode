#pragma once
namespace profile {
inline constexpr const char* version_id = "integral-knights-v1";
inline constexpr const char* core_id = "integral-knights";
inline constexpr const char* scenario_id = "integral-knights-first";
inline constexpr unsigned core_number = 6;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr bool draft_only = false;
inline constexpr unsigned bank_threshold = 24;
// A frozen probe manifest can bind explicit fixture indices. -1 selects public ID order.
inline constexpr int probe_group_key = 319;
inline constexpr int probe_member_key = 319;
inline constexpr int probe_tile_index = 288;
inline constexpr int probe_preparation_tile = 195;
inline constexpr int probe_funding_tile = 160;
inline constexpr bool probe_mirror_with_home = true;
inline constexpr unsigned probe_preparation_deadline = 160;
inline constexpr unsigned probe_measurement_departure = 170;
inline constexpr unsigned max_setup_rounds = 240;
inline constexpr unsigned probe_round_limit = 300;
}
