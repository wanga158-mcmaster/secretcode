#pragma once
namespace profile {
inline constexpr const char* version_id = "conveyor-v1";
inline constexpr const char* core_id = "conveyor";
inline constexpr const char* scenario_id = "conveyor-first";
inline constexpr unsigned core_number = 3;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr bool draft_only = false;
inline constexpr unsigned bank_threshold = 24;
// First public ID-ordered qualifying fixture on development map collision.
// Mirror physical member tiles with our public home; production ignores these.
inline constexpr int probe_group_key = 215;
inline constexpr int probe_other_group_key = 299;
inline constexpr int probe_member_key = 299;
inline constexpr int probe_tile_index = 215;
inline constexpr bool probe_mirror_with_home = true;
inline constexpr unsigned max_setup_rounds = 240;
inline constexpr unsigned probe_round_limit = 300;
}
