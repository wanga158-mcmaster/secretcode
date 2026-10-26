#pragma once
#include "opt/common.hpp"
#include <string>
#include <vector>

// Restricted, fully observed local combat laboratory. This is deliberately not a
// replacement for the official engine. Every result carries capabilities().
namespace opt::sim {
using State = Json;
using Action = Json;
inline constexpr const char* version = "restricted-local-v4";

// Scenario schema: width,height,blocked:[[x,y]], altars:[[x,y]],
// actors:[{id,team,x,y,spawn:[x,y],hp,max_hp,strength,focus,range,ranged,
//           mitigation,shield,shield_ticks,stun,priority,gems,gold,
//           spells:[{kind:none|damage|stun|reset|shield,range,radius,damage,
//                    shield,duration,cooldown,max_cooldown,exclusive}]}],
// camps:[{id,group,x,y,hp,max_hp,strength,range,gems,gold,respawn,stun}],
// settings:{capture_time,camp_sleep_time,recall_time,spawn_healing,
//           submit_multiplier,submit_minimum,winning_gems,max_rounds},
// goal:{kind:capture_depart|bank|clear|survive,team,group,score,rounds}.
// Explicit scalar stats mean effects are already applied. Unrepresented passives,
// vision, dashes, items and respawns are rejected or declared outside this domain.
// An optional catalog expansion via expand_case() imports supported kit metadata.
State initial(const Json& scenario);
State decode_state(const Json& checkpoint);
Json encode_state(const State& state);
std::vector<Action> legal_actions(const State& state);
State step(const State& state, const Action& action);
std::string status(const State& state); // active, success, failure
bool terminal(const State& state);
std::string state_key(const State& state); // includes allowances, order and phase
Json summary(const State& state); // elapsed_rounds,hp,cooldowns,positions,resources
double utility(const State& state, int team = 0);
Action baseline_action(const State& state);
Json capabilities();
Json expand_case(const Json& specification, const Json& runtime_catalog);
int active_actor(const State& state);
int chebyshev(const Json& a, const Json& b);
} // namespace opt::sim
