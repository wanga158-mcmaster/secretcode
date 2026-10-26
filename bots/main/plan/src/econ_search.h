#pragma once
// Planner search settings and the model-explicit entry point (harness and tests); plan_team() in
// econ_plan.h uses static_model() and search_params().
#include "econ_model.h"
#include "econ_plan.h"

namespace mb::econ {

struct SearchParams
{
    int bank{ 40 };              // bag size that triggers a trip to the nearest altar
    float gold_w{ 0.25f };       // gem-equivalents per gold in target value
    int kmax{ 3 };               // largest party
    int recruit{ 12 };           // allies free within this many rounds may be recruited
    int wait_max{ 20 };          // longest wait for a respawning group after arrival
    float recall_frac{ 0.35f };  // recall below this health fraction after a clear
    float shop_gold{ 150 };      // recall to shop with this much gold (0 = never) ...
    float shop_frac{ 0.8f };     // ... and below this health fraction
    bool capture_recall{ true }; // a capturer that will recall channels on the tile during the wait
    float reserve_rounds{ 2 };   // strikes an uncovered tank keeps in reserve (x group max strength)
    float safety{ 1.0f };        // a member needs safety x its expected damage in health
    int max_rounds{ 2000 };
    int max_tasks{ 48 };         // tasks recorded per champion
    int rollout_m{ 1 };          // candidates scored by playouts per decision (1 = greedy)
    int rollout_cost{ 250 };     // nodes one playout is assumed to take (rollouts only when affordable)
    int reserve_nodes{ 300 };    // nodes kept for the greedy remainder of the plan
};
SearchParams& search_params();

// Profiling hook (probe only): when set, run() accumulates fuel per phase into prof[0..2]
// (next, candidates, apply).
inline unsigned long long (*g_fuel)() = nullptr;
inline unsigned long long g_prof[3]{};

struct SearchStats
{
    long nodes{};
    int horizon{};
    bool finished{};
    int banked{};
    int changed{};
    long scanned{}, passed{}, forecasts{};  // groups scanned / past the bound / clear forecasts
};

// The static model with its roster set from the snapshot's champion types (bot: econ_sdk.cpp).
const StaticModel* model_for(const Snapshot& s);

Plan plan_with(const StaticModel& m, const Snapshot& s, int node_budget, const SearchParams& o, SearchStats* stats);

} // namespace mb::econ
