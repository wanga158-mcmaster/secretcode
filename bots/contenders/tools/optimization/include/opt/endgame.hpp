#pragma once
#include "common.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace opt::endgame {

// A deliberately explicit, full-information two-body banking/denial subgame.
// This is an exact transition system for the declared domain, not a claim that
// it is the complete CAMBC engine. No camps, hidden state, shopping, passives
// (except stun immunity), dashes, linked bodies or third-party actors are modeled.
// Each packet has <=64 cases, each with <=16 altars and <=32 remaining rounds.
struct Position { int x{}, y{}; bool operator==(const Position&) const = default; };
struct Settings {
    int winning_gems{1000}, max_rounds{2000}, submit_range{1}, submit_divisor{10};
    int lock_hits{5}, lock_range{2}, respawn_time{60}, kill_gold{100};
    int ability_cooldown{8}, shield_duration{5};
};
struct Actor {
    Position pos{}, spawn{};
    int hp{100}, max_hp{100}, shield{}, shield_timer{-1};
    int strength{45}, attack_range{1}, spell_damage{100}, spell_range{4};
    int spell_stun{1}, stun{-1}, respawn{-1};
    int gems{}, locks{1};
    std::int64_t gold{};
    // Restricted generic kit: slot0 is damage/stun, slot1 is attack reset.
    // A native adapter must explicitly map champion slots (Ryth/Tavi differ).
    std::array<int,2> cooldown{};
    bool ranged{}, spell_exclusive{}, stun_immune{};
    bool can_move{true}, can_attack{true}, can_cast{true}, used_lock{};
    bool tribute_started{}, tribute_cancelled{};
    int channel_altar{-1}, channel_timer{-1};
    bool alive() const { return hp > 0; }
};
struct State {
    std::array<Actor,2> actors;
    std::array<int,2> score{}, kills{};
    std::array<std::int64_t,2> earned_gold{};
    std::vector<std::array<int,2>> altar_locks;
    int round{1998}, slot{};
    int current_team() const { return ((round & 1) ? 0 : 1) ^ slot; }
};
struct Action {
    std::string kind{"end"};
    int x{}, y{}, altar{-1};
    bool operator==(const Action&) const = default;
};
struct Domain {
    Settings settings;
    int width{7}, height{5};
    std::vector<std::string> terrain;
    std::vector<Position> altars;
    bool walkable(Position p) const;
};

struct Case { std::string id; Domain domain; State state; Json definition; };
Case parse_case(const Json& input, const std::string& fallback_id = "endgame");
Json state_json(const State& s);
State state_from_json(const Json& j);
Json action_json(const Action& action);
Action action_from_json(const Json& j);
std::string state_key(const State& s);
bool terminal(const Domain& d, const State& s);
int terminal_payoff(const Domain& d, const State& s); // Team A: -1 or +1, never guessed draw.
double heuristic(const Domain& d, const State& s);   // Move ordering only, never a proof bound.
std::vector<Action> legal_actions(const Domain& d, const State& s);
std::vector<Action> planning_actions(const Domain& d, const State& s);
State transition(const Domain& d, const State& state, const Action& action);
int maximum_remaining_actions(const Domain& d, const State& s);

// Exposed for exact owner-boundary tests and controlled corpus import. These
// are also the actual functions called by transition(), not separate test models.
void owner_start(const Domain& d, State& s);
void owner_end(const Domain& d, State& s);

} // namespace opt::endgame
