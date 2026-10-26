#pragma once
// Inputs of the network (DESIGN.md section 7): the state vector and, per head, the legal candidates with their
// facts. Pure functions of (Obs, Memory, Map, Paths); no choices.
#include <cstdint>
#include "map.h"
#include "obs.h"

namespace zero {

inline constexpr int kState = 48;

enum Head : std::uint8_t { h_goal, h_step, h_attack, h_cast0, h_cast1, h_shop, h_draft, h_tail, h_ranged, h_action, h_aux, h_priority, h_quantity, h_count };
inline constexpr int kPhi[h_count] = { 24, 24, 16, 24, 24, 16, 24, 16, 24, 24, 24, 4, 4 };
inline constexpr int kMaxPhi = 24;
inline constexpr int kMaxCands = 24;

enum GoalKind : std::uint8_t { g_farm, g_bank, g_recall, g_engage, g_avoid, g_hold, g_assist, g_none, g_kinds };

// One candidate: what it is (for the action layer) and its facts (for the network).
struct Cand
{
    std::uint8_t kind{};   // head-specific: goal kind; step: 0 move / 1 stay / 2 recall / 3 submit; attack: 0 none / 1 camp / 2 champion; ranged: step kinds + 4 camp / 5 champion; ...
    std::int32_t target{ -1 };  // group / altar / enemy id / item id / champion pick / tile index
    std::int32_t count{ 1 };  // one engine command may buy multiple copies; all legacy commands default to one
    Tile at{};             // tile for moves, dashes and ground casts
    float phi[kMaxPhi]{};
};

struct CandSet
{
    Head head{};
    int n{};
    Cand c[kMaxCands];
};

struct Context
{
    const Map* map{};
    Paths* paths{};
    const Obs* obs{};
    const Memory* mem{};
};

// Option layer (config[10], default off): the legacy state carries the champion type in 4 bits and the current
// goal kind one-hot (8) instead of the 13-way type one-hot, so every head can condition on the chosen option.
extern bool goal_state_facts;

// Measurement hook: the bot sets it to the engine's fuel counter; native tools leave it null.
extern std::uint64_t (*fuel_hook)();

// Engineering bound (bot only; 0 = none): goal_candidates abandons its work, returning no candidates, when the
// fuel left falls below this floor at one of its section boundaries. The caller then defers the decision.
extern std::uint64_t goal_fuel_floor;

// Fuel profiler (diagnostics, config[4] > 0): mark(i) charges the fuel used since the previous mark to slot i.
namespace prof {
inline constexpr int kSlots = 64;
extern bool on;
void begin();
void mark_on(int slot);
inline void mark(int slot) { if (on) mark_on(slot); }
void report(int round);
}  // namespace prof

void state_vector(const Context& ctx, float* x);  // kState values
void goal_candidates(const Context& ctx, CandSet& out);
void step_candidates(const Context& ctx, CandSet& out);   // uses mem->goal_kind / goal_target
void attack_candidates(const Context& ctx, CandSet& out);
// Ranged movement and basic attacks share one allowance and one learned decision. Stay is the sole no-op;
// attacks keep their first 16 facts and use phi[22]/phi[23] to distinguish camp/champion from step facts.
void ranged_candidates(const Context& ctx, CandSet& out);
void cast_candidates(const Context& ctx, int slot, CandSet& out);
void shop_candidates(const Context& ctx, CandSet& out);
// End of turn: none, or start a recall that the next turn may keep (one turn end done) or cancel at no cost. `home`
// = standing on our spawn now; `committed` = a chosen recall or a tribute is running.
void tail_candidates(const Context& ctx, bool home, bool committed, CandSet& out);

}  // namespace zero
