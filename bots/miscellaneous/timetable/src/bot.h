#pragma once
// The champion: executes its part of the team plan and repairs it locally.
#include <cambc.h>
#include <optional>
#include <vector>
#include "kit.h"
#include "plan.h"
#include "tactics.h"
#include "world.h"

namespace tt {

enum class Mode { farm, capture, bank, home, flee };

// Shared-memory status of a body (bits 6-7 of its claim byte).
enum Status : int { kSolo = 0, kCapturing = 1, kParty = 2, kAbsent = 3 };

// What this champion knows about one camp group (its own sightings plus team claims).
struct GroupKnow {
    GroupKnow() {}
    int respawn_at = -1;     // round the group respawns (known from a capture or a sighting)
    int seen = -1000;        // last round any member tile was in view
    bool dead = false;       // all members dead when last seen (reward or respawning)
    bool reward = false;     // reward available when last seen
    int banned_until = -1;
    int danger_until = -1;   // enemies were seen near it
    float left[kMaxMembers];      // remaining health per member (index in Group::members), once valid
    bool left_valid = false;
    bool left_changed = false;  // remaining spec must be rebuilt
    bool damaged = false;
};

struct EnemyMem {
    Fighter f;
    int seen = -1000;
    cambc::champion_type type = cambc::champion_type::ryth;
};

struct CampMem {
    int member = -1;
    int seen = -1000;
};

class Bot {
public:
    void play_turn();

private:
    // --- setup and observation
    void init_self();
    void observe();
    void update_groups();
    void update_threats();
    void read_claims();
    void write_claim();

    // --- decisions
    bool handle_channel();
    bool handle_enemies();
    bool handle_health();
    bool handle_bank();
    void farm();
    int choose_target(bool relaxed);
    int choose_target_safe(bool relaxed);
    int choose_itinerary(bool relaxed);
    // Everything about taking g that does not depend on the party size (one computation per group).
    struct RateCtx {
        bool ok = false;
        const GroupSpec* spec = nullptr;
        bool reward = false;   // cleared: only the capture is left
        float travel = 0, wait = 0, trip = 0, home = 0, value = 0;
        float taken = 1, shield = 0;
        bool fragile = false;
    };
    bool rate_ctx(int g, RateCtx& c) const;
    float rate_n(const RateCtx& c, int g, int n) const;
    // Gems (plus weighted gold) per champion-round of taking g with n bodies (E8 labour law).
    float target_rate(int g, int n) const;
    // The party size with the best rate for g within our unit (0 when nothing works).
    int party_size(int g, float* rate) const;
    int party_size(const RateCtx& c, int g, float* rate) const;
    bool allowed(int g, bool relaxed) const;
    int leader() const;
    bool needs_party(int g) const;
    // What is left of a group (its full spec until damage is seen), cached: copies cost ~1.2K fuel.
    const GroupSpec& remaining_spec(int g) const;
    // Solo clear of a group at full health and our current stats (recomputed when they change).
    struct SoloCache {
        float dps = -1, max_hp = -1;
        Clear clear;
        float damage = 0;
        bool party = false;
        float value = 0;
    };
    const SoloCache& solo(int g) const;
    int alive_mates() const;
    int mates_near() const;

    // --- actions
    bool try_move(vec2 q);
    bool step_on_field(const Field* f, bool avoid_danger);
    bool step_greedy(vec2 goal);
    bool step_to_safety(bool home);
    bool leave_camp_tiles();
    bool travel_dash(const Field* f);
    bool fight_group(int g, bool party);
    bool party_fight(int g);
    bool capture_group(int g);
    void shop();
    void cast_after_attack(int g);
    bool attack_member(int m);
    bool try_recall(const char* why);

    // --- helpers
    Fighter self_fighter() const;
    bool tile_free(vec2 p) const;
    float camp_in_at(vec2 p) const;
    float danger_at(vec2 p) const;
    bool reserve_ok(vec2 p) const;
    float my_dps() const;
    int field_dist(int g) const;
    int here(const Field* f) const;
    int estimate_dist(int g) const;
    bool group_awake(int g) const;
    bool mate_on_group(int g) const;

    bool init_ = false;
    Kit kit_;
    Plan plan_;
    bool plan_ok_ = false;
    int slot_ = -1;
    int unit_ = -1;
    int n_bodies_ = 0;
    std::vector<int> mates_;           // body slots of my unit, me included
    std::vector<cambc::champion> allies_, enemies_;
    std::vector<int> ally_slot_;       // body slot of each allies_ entry
    std::vector<GroupKnow> know_;
    mutable std::vector<SoloCache> solo_;
    mutable std::vector<GroupSpec> rem_;
    mutable std::vector<char> rem_dirty_;
    std::vector<EnemyMem> enemy_mem_;
    std::vector<CampMem> camp_mem_;
    std::vector<int> claims_;          // per body slot: claimed group or -1
    std::vector<int> status_;          // per body slot: Status
    std::vector<int> body_absent_;     // per body slot: last round the body was absent

    // task state
    Mode mode_ = Mode::farm;
    int target_ = -1;
    bool party_ = false;
    int stage_since_ = -1;
    int target_since_ = 0;
    int next_choose_ = 0;
    int stuck_ = 0;
    int engaged_round_ = -1;
    int last_hit_round_ = -1;
    int last_gems_ = 0, last_gold_ = 0;
    int captures_ = 0;
    int deaths_ = 0;
    bool was_alive_ = true;
    int home_since_ = -1;
    long first_fuel_[4] = {-1, -1, -1, -1};
    // Time ledger: rounds per activity (dead, recall, tribute, flee, retreat, bank walk, travel,
    // approach, fight, stage, capture, respawn wait, idle, spawn), logged every 400 rounds.
    static constexpr int kActs = 14;
    int ledger_[kActs] = {};
    void account(const char* path);
    bool first_logged_ = false;

    // turn snapshot
    int round_ = 0;
    vec2 pos_;
    float hp_ = 0, max_hp_ = 1, str_ = 0, focus_ = 0;
    int gems_ = 0, gold_ = 0;
    bool can_move_ = false, can_attack_ = false, can_cast_ = false;
    bool on_spawn_ = false;
    bool moved_ = false, attacked_ = false, cast_ = false;
    int cooldown_[2] = {0, 0};
    cambc::bitboard visible_, camp_occ_, units_, scratch_;
    std::vector<Fighter> enemies_now_;   // visible and remembered enemies
    std::vector<Fighter> allies_now_;    // visible allies (not self)
    std::vector<int> allies_now_slot_;   // their body slots
    std::vector<CampThreat> camps_now_;  // awake camps (visible and remembered)
    std::vector<int> camps_now_member_;  // their member indices
};

} // namespace tt
