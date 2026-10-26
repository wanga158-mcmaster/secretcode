#pragma once
// One champion of the warband. Every champion runs the same code: the acting leader (the first
// available champion in turn order) chooses the pack's objective and writes it to shared memory;
// every champion, the leader included, then acts as a pack member.
#include <cambc.h>
#include <array>
#include <vector>
#include "protocol.h"
#include "sense.h"
#include "world.h"

namespace wb {

struct EnemyMemory {
    vec2 pos{};
    int seen = -10000;         // round last seen
    float hp = 0, maxhp = 1, dps = 0;
    int gems = 0;
    bool dead = false;
    int death_round = -10000;
};

struct AllyMemory {
    vec2 pos{};
    int seen = -10000;
    float hp = 0, maxhp = 1, dps = 0;
};

class Bot {
public:
    void play_turn();

private:
    friend struct Probe;                // native scratch caller fixtures; no production behavior
    // ---- persistent ----
    World world_;
    bool init_ = false;
    std::vector<cambc::champion> allies_, enemies_;
    int me_ = -1;                       // my ally index (turn order)
    const Kit* kit_ = nullptr;
    std::vector<EnemyMemory> emem_;
    std::vector<AllyMemory> amem_;
    int capture_group_ = -1;            // group I am capturing or walking to capture
    int task_ = -1;                     // camp group of the zone I am working
    int task_since_ = 0;
    int anchor_moved_ = 0;              // leader: round the anchor last advanced
    int watch_group_ = -1;              // leader: focus group under the progress watchdog
    int watch_round_ = 0;               // round it last made progress
    float watch_hp_ = 0;                // its believed remaining health then
    int stuck_ = 0;
    vec2 last_pos_{-1, -1};
    int leader_since_ = -1;             // round I became the acting leader
    int gather_since_ = -1;
    int hold_group_ = -1, hold_since_ = 0;  // ranged waiting for a tank on this group since
    int objective_since_ = 0;
    int last_objective_ = -1;
    bool banking_ = false;
    int healing_ = 0;                   // at spawn, waiting for full health
    std::array<int, 13> budget_{};      // diagnostics: champion-rounds per activity
    void count_budget(int k);

    // ---- per turn ----
    int round_ = 0;
    vec2 pos_{};
    float hp_ = 0, maxhp_ = 1;
    bool at_spawn_ = false;
    std::vector<Unit> vis_allies_;      // visible allies other than me
    std::vector<Unit> vis_enemies_;
    PackMsg pack_;
    bool i_lead_ = false;
    bitboard blocked_;                  // terrain plus visible units and living camps
    bitboard awake_;                    // visible awake camp tiles
    bitboard living_;                   // visible living camp tiles
    std::vector<cambc::ability> abil_;  // held copies of my two ability slots (cheap cooldown reads)
    struct AwakeCamp { vec2 tile; int range; int group; float str; };
    std::vector<AwakeCamp> awake_list_; // the same, as a list with ranges
    bool blocked_ready_ = false;

    // bot.cpp
    void init();
    void sense();
    void update_enemy_memory();
    int acting_leader() const;
    void write_pack();
    void lead();
    void choose_objective();
    void steer_anchor();
    void pack_strength(float& dps, float& spare, int& members) const;
    void report_sighting();
    void act();
    void dead_turn();
    void channel_turn();

    // fight.cpp: held-ability helpers (is_ready() costs ~480 fuel; these ~40-100)
    bool can_cast_now() const;
    bool slot_ready(int s) const;
    bool slot_use(int s);
    bool slot_use(int s, const cambc::champion& t);
    bool slot_use(int s, const cambc::camp& t);
    bool slot_use(int s, vec2 t);
    // fight.cpp
    bool fight_turn();
    float local_ratio(bool include_memory) const;
    int choose_focus() const;
    bool engage(const Unit& target);
    bool retreat();
    bool cast_stun(const Unit& target);
    bool cast_damage(const Unit& target);
    bool basic_attack_enemy(const Unit& target);
    void use_defensive();

    // act.cpp
    bool converge_turn();
    bool clear_group(int g, bool allow_wake);
    bool capture_turn();
    bool task_turn();
    bool task_feasible(int g) const;
    bool bank_turn(bool force);
    bool recover_turn();
    bool follow_anchor(int within);
    bool avoid_camps();
    int camp_target(int g, int max_dist) const;
    bool attack_camp(int g);
    bool in_camp_danger(vec2 p, int ignore_group) const;
    bool tank_engaged(int g) const;

    // nav.cpp
    const bitboard& blocked();
    bool free_tile(vec2 p);
    // Tiles the engine called occupied although nothing visible stood there (a body in a bush, a
    // respawned camp): avoided for a few rounds.
    struct BadTile { vec2 tile; int until; };
    std::array<BadTile, 8> bad_tiles_{};
    void mark_bad(vec2 p);
    bool move_error(const cambc::error& e, vec2 to);
    bool step_field(const cambc::distance_field& f, int stop_at, int avoid_group);
    bool goto_tile(vec2 target, int within);
    bool goto_group(int g, int within);
    bool goto_altar();
    bool goto_home();
    bool step_away(const std::vector<vec2>& threats, int toward_anchor);

    // shop.cpp
    void shop();
    void ward();
};

} // namespace wb
