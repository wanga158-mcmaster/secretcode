#pragma once
// The per-champion controller of contenders/compound. Each champion runs its own instance and
// decides alone (decentralised), coordinating only through the 8 shared bytes: a claim byte per
// champion (target group or convergence mode) and one convergence beacon.
//
// Doctrine: durable dispersed farmers. Gold is reinvested at once (shop from ~90 gold, Health
// first), farming continues beside enemies that cannot kill us, and kills come from converging
// three or more champions on an isolated enemy.
#include <cambc.h>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "econ_model.h"
#include "model.h"
#include "world.h"

namespace cb {

using cambc::vec2;

struct GroupBelief
{
    int alive_at{0};             // round from which the group is believed alive
    int seen{-1};                // last round any member tile was in view
    bool reward{};               // cleared, reward waiting to be captured
    std::span<float> hp;         // believed member health (siege progress persists); view
                                 // into Brain::member_hp_
    int banned_until{-1};        // no targeting before this round
};

struct Enemy
{
    vec2 pos{};
    float hp{}, max_hp{}, str{}, focus{};
    int range{1};
    bool ranged{};
    int gems{};
    cambc::champion_type type{};
    std::uint32_t handle{};
    float dps{};                 // estimated damage per round on a champion
    bool attackable{};           // within our attack range now
};

struct Threat
{
    vec2 tile{};
    float str{};
    int range{};
    bool hidden{};   // not in view (a bush or out of sight): possibly awake
};

struct Ally
{
    vec2 pos{};
    float hp{}, max_hp{};
    float dps{};
    int slot{-1};
};

// Up to nine candidate tiles with scores, without heap allocation.
struct Moves
{
    std::array<vec2, 9> tile{};
    std::array<float, 9> score{};
    int n{};
    void add(vec2 t, float s)
    {
        if (n < 9) { tile[static_cast<std::size_t>(n)] = t; score[static_cast<std::size_t>(n)] = s; ++n; }
    }
};

enum class Task : std::uint8_t { none, farm, capture, bank, home, converge, fight };

struct AbilitySlot
{
    int slot{-1};
    cambc::ability_kind kind{};
    float range{};
    float modifier{};
    float radius{};
    bool exclusive{};            // casting excludes walking this turn
    bool targets_camps{}, targets_enemies{}, targets_ground{}, targets_self{};
};

class Brain
{
public:
    void turn();

private:
    void turn_body();
    // ---- identity (first turn) ----
    bool inited_{};
    int slot_{-1};
    int team_size_{};
    cambc::champion_type type_{};
    cm::Role role_{cm::Role::other};
    bool ranged_{};
    std::uint32_t handle_{};
    std::vector<std::uint32_t> ally_handles_;
    std::optional<AbilitySlot> reset_, spell_, area_, dash_, shield_, stun_;
    float base_move_{1};
    float camp_mult_design_{1.0f};
    std::vector<cm::ShopStep> plan_;

    // ---- per-turn snapshot ----
    int round_{};
    vec2 pos_{};
    float hp_{}, max_hp_{}, str_{}, focus_{};
    int gold_{}, gems_{};
    int range_{1};
    bool on_spawn_{};
    std::array<std::uint32_t, 42> items_{};
    float taken_camp_{1.0f};     // camp damage multiplier after our emblems
    float taken_all_{1.0f};      // all damage multiplier (Durability)
    float gold_mult_{1.0f};      // Vesper / Treasure
    float dps_camp_{};           // forecast damage per round on camps
    float dps_champ_{};          // forecast damage per round on champions
    std::vector<Enemy> enemies_;
    std::vector<Ally> allies_;
    cambc::bitboard camp_occ_;   // visible camp occupancy
    cambc::bitboard awake_occ_;  // visible awake camps
    cambc::bitboard visible_;    // our vision
    cambc::bitboard champ_occ_;  // visible champions, both teams
    std::vector<Threat> threats_; // awake harmful monsters in view near us (this turn)
    std::array<vec2, 8> revealed_{};  // tiles a failed move revealed occupied this turn
    int n_revealed_{};
    bool took_damage_{};
    bool heal_trip_{};           // left a camp fight to heal: recall as soon as it is safe
    bool gained_{};              // gems or gold rose since our last turn (a capture landed)
    float last_hp_{-1};
    int last_gems_{-1}, last_gold_{-1};

    // ---- value model (econ_model.h) ----
    std::vector<cm::CampSpec> camp_specs_;
    bool specs_ready_{};
    float lambda_{0.3f};          // gems per gold (the marginal item's value per gold)
    float purchase_rate_{};       // gems a round per gold of the best purchase (lambda / race left)
    float farm_rate_cache_{0.5f}; // net farming value a round (refreshed with lambda)
    float plan_gem_rate_{};       // the team's gem pace under a gem-focused plan (race horizon)
    std::array<float, 3> growth_{};
    float threat_dps_{};          // damage a round a contact puts on us (observed envelope)
    float threat_prior_{};        // the two strongest enemy kits at base (before any contact)
    int contact_turns_{};         // turns observed in contact
    float observed_threat_{};     // envelope of champion damage a contact turn
    float hp_loss_{};             // health lost since our last turn
    int contacts_{};              // contact episodes (an enemy within 4 after a quiet spell)
    int last_contact_round_{-1000};
    float dmg_rate_{};            // camp damage a round while farming (EMA)
    float carried_ema_{};
    cm::EmblemPassives emblems_{};
    void build_camp_specs();
    cm::Profile profile() const;
    cm::Econ econ() const;
    void update_threat();
    void update_lambda();

    // ---- beliefs ----
    std::vector<GroupBelief> gb_;
    std::vector<float> member_hp_;
    std::vector<int> member_awake_;   // last round each monster was known awake (seen, or hit us unseen)
    std::array<std::uint8_t, 8> shared_{};
    cm::Beacon beacon_{};
    int our_banked_{}, their_banked_{};
    int our_banked_r0_{}, their_banked_r0_{};

    // ---- task state ----
    Task task_{Task::none};
    int target_{-1};
    int target_round_{-1000};
    int target_party_{1};            // party size the target was chosen for
    int stage_since_{-1};            // round staging began (party not yet assembled)
    int capture_tile_member_{-1};
    vec2 converge_pos_{-1, -1};
    int converge_since_{-1};
    int stuck_turns_{};
    vec2 last_pos_{-1, -1};
    int engaged_round_{-1000};
    int fight_group_{-1};        // progress watchdog: group, its health when progress was last seen
    float fight_hp_{};
    int fight_progress_round_{-1000};
    int bank_stuck_{};
    int bank_block_until_{-1};
    int last_attack_round_{-1000};
    int fight_start_round_{-1000};
    int beacon_published_{-1000};
    int recall_started_{-1000};
    int spawn_hold_{};

    // ---- activity log (cfg::debug_level == 3) ----
    char act_{'?'};
    std::array<char, 104> act_log_{};
    int act_n_{};
    void act(char c) { act_ = c; }
    void flush_activity();

    // ---- brain.cpp ----
    void trace(const char* what, int a = 0, int b = 0) const;   // cfg::debug_level > 0 only
    void first_turn();
    void snapshot();
    void observe_enemies();
    void observe_allies();
    void observe_camps();
    void read_shared();
    void write_shared(cm::Mode mode, int value);
    void update_derived_stats();
    float estimate_enemy_dps(const cambc::champion& c, float str, float focus) const;
    int race_left() const;
    float gold_worth() const;    // gems per gold now

    // ---- econ.cpp: shop, recall, bank ----
    void shop();
    void shop_by_value();
    bool want_recall(bool& for_shop);
    bool safe_to_channel() const;
    bool try_recall();
    bool want_bank() const;
    bool do_bank();
    int nearest_altar(int& dist) const;

    // ---- farm.cpp: targets, camp fights, capture ----
    void farm();
    void choose_target();
    int travel_to(int g) const;   // rounds to reach attack range of group g (-1 unreachable)
    float score_group(int g, int travel, float hp_now, bool& needs_heal, int party = 1) const;
    int claimants(int g) const;          // allies whose claim byte names group g
    int party_present(int g) const;      // allies in view claiming g within 6 of it, plus us
    bool stage_for_party(int g);         // wait out of reach until the party is assembled
    cm::MonsterSet believed_monsters(int g) const;
    bool fight_camp(int g);
    void drop_target(int ban);
    bool heal_trip();            // escape out of reach, then recall (true when it acted)
    bool continue_home();        // walk the last tiles home (true when it acted)
    bool capture(int g);
    bool camp_threatens(vec2 at) const;
    float camp_incoming(vec2 at) const;
    bool leave_camp_tiles();
    bool is_claimed_by_ally(int g) const;

    // ---- act.cpp: movement, attacks, abilities ----
    bool step_field(const cambc::distance_field& f, bool allow_equal);
    bool step_towards(vec2 goal);
    bool step_score(const Moves& m);
    bool flee_from(const std::vector<vec2>& threats, vec2 home);
    bool attack_camp(int g, int member);
    bool attack_enemy(const Enemy& e);
    bool cast_reset_and_attack(const cambc::unit& target);
    bool cast_spell_on_camp(int g, int member);
    bool cast_area_on_camps(int g);
    bool cast_spell_on_enemy(const Enemy& e);
    bool cast_shield_if_hit();
    bool walk_to_attack_position(int g, int member);
    bool try_productive_dash(vec2 goal, const cambc::distance_field* field = nullptr, int group = -1);
    bool tile_free(vec2 t) const;
    int neighbours(vec2 p, std::array<vec2, 8>& out) const;

    // ---- pvp.cpp ----
    bool handle_enemies();
    bool handle_beacon();
    cm::SkirmishResult forecast_fight(const std::vector<Enemy>& foes, bool with_beacon_allies) const;
    bool isolated(const Enemy& e) const;
};

}  // namespace cb
