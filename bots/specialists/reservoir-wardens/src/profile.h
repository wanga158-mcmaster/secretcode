#pragma once
namespace profile {
inline constexpr const char* version_id = "reservoir-wardens-v1";
inline constexpr const char* core_id = "reservoir-wardens";
inline constexpr const char* scenario_id = "reservoir-wardens-first";
inline constexpr int core_number = 12;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
// Collision, resolved from public physical groups in development order.
inline constexpr int probe_group_a = 762, probe_group_b = 384;
inline constexpr int probe_exit_a = 720, probe_exit_b = 342;
inline constexpr unsigned probe_earliest_clear = 100, probe_setup_limit = 240;
}
