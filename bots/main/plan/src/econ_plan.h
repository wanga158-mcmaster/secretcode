#pragma once
// Team economy planner interface (medium track, cfg::team_plan). Contract between the planner
// (econ_*.h/.cpp: model + search, owned by the planner worker) and the executor (brain.cpp etc.,
// owned by the executor worker). Every champion instance runs the same deterministic search on the
// same inputs, so instances agree on the plan without communicating; search effort is a fixed node
// budget, never gated on remaining fuel (that would make instances disagree).
#include <cstdint>
#include <vector>
#include "util.h"

namespace mb::econ {

enum class TaskKind : std::uint8_t
{
    clear,   // walk to `group`, clear it with `party` (staging until the party is there), then the
             // `capturer` holds a member tile until the reward; the others move to their next task
    bank,    // walk to `altar` and tribute the whole bag
    recall,  // recall (shop on spawn by the existing shop rules), then continue
    wait,    // hold position until `until_round` (e.g. a respawn), then continue
};

struct Task
{
    TaskKind kind{ TaskKind::wait };
    std::int16_t group{ -1 };          // World::groups index (clear)
    std::int16_t altar{ -1 };          // World::altars index (bank)
    std::uint8_t party{};              // bit i = roster slot i (Knowledge::allies order) takes part
    std::int8_t capturer{ -1 };        // roster slot that captures (clear)
    std::int16_t start_round{ -1 };    // planned start (arrival / engagement) round, for sync and drift checks
    std::int16_t end_round{ -1 };      // planned completion round of this champion's part
};

// Planner input: what every instance can know identically (map, roster designs, round, banked gems),
// plus the champion states. For allies that are not in view, the planner uses its own predictions
// (the previous plan) unless the executor supplies observed values.
struct ChampState
{
    std::int8_t type{ -1 };    // cambc::champion_type; set so the model needs no Knowledge (draft phase)
    bool alive{ true };
    int respawn_round{ -1 };   // when dead
    vec2 pos{};
    int free_round{};          // round the champion can start a new task (after its current one)
    float hp{}, max_hp{};
    float strength{}, focus{};
    int gold{};
    int gems{};                // carried
    std::uint8_t emblems{};    // completed emblem families held: bit = econ::Emblem (econ_model.h); 0 = infer from stats
};

struct GroupState
{
    bool alive{ true };        // false: respawning until respawn_round
    int respawn_round{};
    float hp_left{ -1.0f };    // -1 = full
};

struct Snapshot
{
    int round{};
    int banked{};                      // our tributed gems
    std::vector<ChampState> champs;    // roster order (Knowledge::allies)
    std::vector<GroupState> groups;    // World::groups order
};

struct Plan
{
    int built_round{ -1 };
    int predicted_finish{ -1 };        // round the plan banks the winning gems (diagnostic)
    std::vector<std::vector<Task>> tasks;  // per roster slot, in order
};

// Build a team plan from `s` with at most `node_budget` search expansions. Deterministic.
Plan plan_team(const Snapshot& s, int node_budget);

// Resumable search: the same result as plan_team, built in fixed slices over several turns (every
// instance calls plan_step with the same node counts on the same rounds, so all agree). plan_step
// returns true when the search is finished.
struct SearchState;
SearchState* plan_begin(const Snapshot& s, int node_budget);  // owned by the planner; one live state at a time
bool plan_step(SearchState& st, int nodes);
Plan plan_result(const SearchState& st);
void plan_end(SearchState* st);

// Draft-phase plan (first draft turn: 1M fuel, whole map visible, no Knowledge): the executor fills
// Snapshot from the map and our deterministic roster (types from the draft order), calls plan_team,
// and encodes the per-champion task queues into draft memory (encode/decode are the executor's).

} // namespace mb::econ
