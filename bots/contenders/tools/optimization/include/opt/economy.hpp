#pragma once
#include "common.hpp"
#include <memory>
#include <set>
#include <unordered_map>

namespace opt::economy {

// C3 is a macro-job scheduling model, not a replacement game engine. Its service
// modes carry their own provenance and entry-state coverage. Times are integral
// boundaries; travel never grants a free same-input attack.
struct Point { int x{}, y{}; bool operator==(const Point&) const = default; };
struct Actor {
    std::string id, champion;
    int tile{}, spawn{}, available{}, range{1};
    double hp{1200}, max_hp{1200}, strength{45}, gold{}, earned_gold{};
    int gems{};
    bool field_shop{};
    std::vector<int> items;
};
struct Item {
    std::string id;
    double cost{}, strength{}, health{};
    std::vector<double> strength_by_actor;
    int max_count{20};
};
struct Mode {
    std::string id, qualification{"model_assumption"}, endpoint{"clear"};
    std::vector<int> actors;
    std::vector<double> incoming, hp_min, hp_max, strength_min, strength_max;
    std::vector<double> tested_max_hp;
    std::vector<double> tested_gold;
    std::vector<int> tested_gems;
    std::vector<int> tested_range;
    std::vector<std::vector<int>> required_items;
    std::vector<int> entry_tiles, end_tiles;
    int work_rounds{1};
    bool generated_basic{}, explicit_estimate{};
    Json provenance{Json::object()};
};
struct Camp {
    std::string id;
    std::vector<int> members;
    std::vector<double> member_hp;
    int gems{}, respawn{50};
    double gold{}, hp{}, strength{};
    int range{};
    bool known_available{true};
    std::vector<Mode> modes;
};
struct Event {
    int at{}, kind{}, group{-1}, actor{-1};
    std::vector<int> participants, end_tiles;
    std::vector<double> incoming;
    int recipient{-1}, member{-1}, return_tile{-1};
};
struct State {
    int now{}, deferred{};
    int banked{}, winning_round{-1};
    std::vector<Actor> actors;
    // 0 alive, 1 reserved/living, 2 cleared/reward, 3 waiting to respawn.
    std::vector<int> stage, ready;
    std::vector<Event> events;
    std::size_t trace{};
};
struct Trace { std::size_t parent{}; Json action{Json::object()}; };
struct Instance {
    std::string id, resolved_identity;
    int width{}, height{}, horizon{2000}, goal{1000}, capture_ticks{5}, recall_ticks{5};
    double submit_multiplier{0.1};
    int submit_minimum{1};
    bool allow_estimates{};
    std::vector<unsigned char> blocked;
    std::vector<int> altars, spawn_tiles;
    std::vector<Actor> actors;
    std::vector<Camp> camps;
    std::vector<Item> items;
    Json provenance{Json::object()};
};
struct Expansion { State state; Json action; };

Instance load_instance(const Json& config, const fs::path& repo);
State initial_state(const Instance& instance, const Json& config);
Json state_json(const State& state);
State state_from_json(const Json& value);
Json trace_json(const std::vector<Trace>& traces, std::size_t index);
std::string state_key(const State& state);
std::vector<int> distances(const Instance&, const State&, int source, bool block_awake = true,
                           int opened_group = -1);
void process_events(const Instance&, State&, int until);
std::vector<Expansion> expand(const Instance&, const State&);
bool better(const State& candidate, const State& incumbent);
double priority(const Instance&, const State&);
Json validate_plan(const Instance&, const State& initial, const Json& actions);

} // namespace opt::economy
