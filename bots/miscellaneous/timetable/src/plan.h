#pragma once
// The central plan: computed in the draft from the whole map, carried in draft memory, executed
// and repaired by each champion. Pure C++ (no SDK) so tests can drive it.
//
// The plan partitions the bodies into units (solo, pairs, trios or the whole team) and gives each
// unit a set of camp groups. A unit clears its dangerous groups together (no member could survive
// them alone) and splits up for the groups one member can take. The structure and the assignment
// are chosen by a circuit-throughput model of the map (value per round of each unit's tour).
#include <cstddef>
#include <cstdint>
#include "model.h"

namespace tt {

inline constexpr std::uint8_t kPlanVersion = 4;
inline constexpr int kNoOwner = 7;
inline constexpr int kMaxUnits = kMaxBodies;
inline constexpr int kMaxMatrix = 32;   // groups whose pairwise distances travel in the plan

// Everything the planner needs, built by the draft (or by a champion as a fallback).
struct PlanInput {
    int width = 0, height = 0;
    int n_groups = 0;
    GroupSpec groups[kMaxGroups];
    int d_ours[kMaxGroups] = {};     // walking tiles from our spawn to the group
    int d_theirs[kMaxGroups] = {};   // from the enemy spawn
    int d_altar[kMaxGroups] = {};    // to the nearest altar's submit tiles
    int altar_of[kMaxGroups] = {};   // index of that altar
    std::uint16_t dist[kMaxGroups][kMaxGroups] = {}; // group to group walking tiles
    int n_bodies = 0;
    BodySpec bodies[kMaxBodies];
    float gold_weight = 0.25f;
    int territory_margin = 0;
    // Groups up to this many tiles nearer the enemy spawn than ours are still plannable, valued by
    // the chance we take them: weight = clamp(0.5 + (d_theirs - d_ours) / contest_scale, floor, 1).
    int contest_margin = 6;
    float contest_scale = 8.0f;
    float contest_floor = 0.25f;
    float camp_margin = 1.35f;
    int capture_rounds = 6;
    int bank_gems = 40;
    int siege_max_trips = 4;
    // Health of a unit that can be spent before someone must leave (rotation is imperfect).
    float pool_share = 0.8f;
    int structure = -1;              // -1: best of all structures; else force one (tests, knobs)
};

// A unit as the planner sees it.
struct UnitSpec {
    int n = 0;
    int body[kMaxBodies] = {};
    float dps = 0;        // combined damage per round against camps
    float pool = 0;       // health the unit can spend with rotation
    float best_hp = 0;    // its sturdiest member
    float shield = 0;     // shield absorbed per round, summed
    float gold_mult = 1;  // mean passive gold bonus
    bool dash = true;     // every member dashes
};

// Unit structures the planner compares.
enum class Structure : int { solo = 0, pairs = 1, trios = 2, team = 3, count = 4 };

// The plan as carried in draft memory.
struct Plan {
    bool valid = false;
    int width = 0, height = 0;
    int n_groups = 0, n_bodies = 0, n_units = 0;
    int structure = 0;
    int roster[kMaxBodies] = {-1, -1, -1, -1, -1, -1}; // champion_type per body slot
    int body_unit[kMaxBodies] = {};  // unit of each body
    int owner[kMaxGroups];      // unit or kNoOwner
    bool party[kMaxGroups];     // the unit must clear it together
    bool ours[kMaxGroups];      // in our territory
    int tour[kMaxGroups];       // position in the owner's tour
    int bank_gems = 40;
    float gold_weight = 0.25f;
    int est_rounds = 0;              // planner's estimate of rounds to the winning gems
    float est_rate[kMaxUnits] = {};  // planned gems per round per unit
    // Pairwise distances between the first kMaxMatrix "matrix groups" (in matrix order).
    int n_matrix = 0;
    int matrix_group[kMaxMatrix];
    std::uint16_t mdist[kMaxMatrix][kMaxMatrix];
    int matrix_index[kMaxGroups]; // group -> matrix slot or -1

    int distance(int a, int b) const
    {
        const int ia = a >= 0 && a < kMaxGroups ? matrix_index[a] : -1;
        const int ib = b >= 0 && b < kMaxGroups ? matrix_index[b] : -1;
        if (ia < 0 || ib < 0) return -1;
        return mdist[ia][ib];
    }
    int unit_size(int u) const
    {
        int n = 0;
        for (int b = 0; b < n_bodies; ++b)
            if (body_unit[b] == u) ++n;
        return n;
    }
};

// Serialise into at most `cap` bytes; returns the byte count (0 when it does not fit).
std::size_t encode_plan(const Plan& plan, std::uint8_t* out, std::size_t cap);
// Parse; plan.valid is false when the bytes are not a plan of this version.
Plan decode_plan(const std::uint8_t* in, std::size_t size);
void decode_plan_into(const std::uint8_t* in, std::size_t size, Plan& out);

// A unit's tour cost model: rounds per cycle and what a cycle yields.
struct Circuit {
    float travel = 0, work = 0, bank = 0, heal = 0, wait = 0;
    float value = 0, gems = 0;
    float cycle() const { return travel + work + bank + heal + wait; }
    float rate() const { const float c = cycle(); return c > 0 ? gems / c : 0.0f; }
    float value_rate() const { const float c = cycle(); return c > 0 ? value / c : 0.0f; }
};

// Bodies grouped into units for a structure (sturdy members paired with fragile ones).
int form_units(const PlanInput& in, Structure s, UnitSpec* units);

class Planner {
public:
    explicit Planner(const PlanInput& in) : in_(in) {}
    // Stop planning when fuel() drops below floor (the draft passes cambc::fuel_remaining).
    void set_fuel(long (*fuel)(), long floor) { fuel_ = fuel; floor_ = floor; }
    bool low() const { return fuel_ && fuel_() < floor_; }
    int structures_done() const { return done_; }
    // Compare the structures and return the best plan (budget: rough guest work units).
    Plan solve(long budget);
    // Circuit of the given group set for unit u of the current structure, in the given order.
    Circuit circuit(int u, const int* groups, int m) const;
    bool feasible(int u, int g) const { return feasible_[u][g]; }
    int last_ops() const { return static_cast<int>(ops_); }
    float structure_score(int s) const { return scores_[s]; }

private:
    void prepare_groups();
    void prepare_units(Structure s);
    void initial_assignment();
    bool improve_pass(long budget);
    float unit_score(int u) const;
    void order_tour(int* groups, int m) const;
    Plan build_plan(Structure s) const;

    const PlanInput& in_;
    UnitSpec units_[kMaxUnits];
    int n_units_ = 0;
    Clear clears_[kMaxUnits][kMaxGroups];
    float damage_[kMaxUnits][kMaxGroups];
    bool feasible_[kMaxUnits][kMaxGroups];
    bool solo_[kMaxGroups] = {};       // some single body can take it
    bool ours_[kMaxGroups] = {};
    float weight_[kMaxGroups];        // contest weight: share of a group's yield we expect
    int owner_[kMaxGroups] = {};
    float value_[kMaxUnits][kMaxGroups];
    float score_[kMaxUnits] = {};
    float scores_[static_cast<int>(Structure::count)] = {};
    mutable long ops_ = 0;
    long (*fuel_)() = nullptr;
    long floor_ = 0;
    int done_ = 0;
};

} // namespace tt
