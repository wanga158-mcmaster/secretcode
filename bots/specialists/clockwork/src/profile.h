#pragma once
#include <array>
#ifndef CLOCKWORK_PLANNING_REPAIR
#define CLOCKWORK_PLANNING_REPAIR 1
#endif
#ifndef CLOCKWORK_FUEL_DIAGNOSTICS
#define CLOCKWORK_FUEL_DIAGNOSTICS 0
#endif
#ifndef CLOCKWORK_COMPONENT_TRACE
#define CLOCKWORK_COMPONENT_TRACE 0
#endif
#ifndef CLOCKWORK_STARTUP_REPAIR
#define CLOCKWORK_STARTUP_REPAIR 1
#endif
#ifndef CLOCKWORK_REACHABILITY_REPAIR
#define CLOCKWORK_REACHABILITY_REPAIR 1
#endif
#ifndef CLOCKWORK_CONTINUATION_REPAIR
#define CLOCKWORK_CONTINUATION_REPAIR 1
#endif
namespace profile {
inline constexpr const char* version_id = "clockwork-v2";
inline constexpr const char* core_id = "clockwork";
inline constexpr const char* scenario_id = "clockwork-first";
inline constexpr unsigned core_number = 2;
inline constexpr bool core_enabled = true;
inline constexpr bool probe_mode = false;
inline constexpr bool draft_only = false;
inline constexpr unsigned bank_threshold = 24;
// Diagnostic-only physical group selectors, reflected through public map
// symmetry when our public spawn is the later canonical home.
inline constexpr int probe_group_key = 299;
inline constexpr int probe_member_key = -1;
inline constexpr int probe_tile_index = 678;
inline constexpr std::array<unsigned, 3> probe_circuit_keys{299, 468, 678};
inline constexpr unsigned probe_preparation_timer = 24;
inline constexpr bool probe_mirror_with_home = true;
inline constexpr unsigned max_setup_rounds = 240;
inline constexpr unsigned probe_round_limit = 300;
}
