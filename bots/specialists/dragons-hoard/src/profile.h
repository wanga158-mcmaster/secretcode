#pragma once
#include <array>
namespace profile {
inline constexpr const char* version_id = "dragons-hoard-v1";
inline constexpr const char* core_id = "dragons-hoard";
inline constexpr const char* scenario_id = "dragons-hoard-six-capture-circuit";
inline constexpr int core_number = 15;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr bool draft_only = false;
inline constexpr unsigned bank_threshold = 144;
inline constexpr unsigned deadline_reserve = 128;
inline constexpr unsigned projection_reserve = 16;
inline constexpr unsigned altar_retry_rounds = 8;
inline constexpr unsigned max_setup_rounds = 240;
inline constexpr unsigned probe_round_limit = 300;
// Canonical minimum physical spawn-tile IDs, reflected x -> width-1-x for B.
inline constexpr std::array<int, 6> probe_circuit{348,410,472,534,596,658};
inline constexpr int probe_cast_group = 632;
inline constexpr int probe_cast_tile = 628;
inline constexpr int probe_altar = 475;
inline constexpr unsigned probe_capture_count_before_cast = 5;
}
