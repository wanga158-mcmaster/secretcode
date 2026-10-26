#pragma once
#include "opt/common.hpp"
#include <array>

namespace opt::micro {
inline constexpr const char* version="native-micro-search/v3";
Options native_options(const Options&);
// Transitions and permissions come from the pinned native engine. This compact
// record is search bookkeeping, never a replacement combat simulator.
struct Action {
    std::string type;
    int x{-1},y{-1},slot{-1},target_kind{-1},target_id{-1};
    Json json() const;
    static Action parse(const Json&);
    bool operator==(const Action&) const = default;
};
// Complete deterministic proposal ordering. Neither legality nor the action
// domain is approximated: rare action classes get a turn before ground fanout.
std::vector<Action> interleave_actions(std::vector<Action>);
struct Evaluation {
    double value{};
    bool task_complete{},local_win{},local_loss{},unstable{};
    Json metrics{Json::object()};
};
Evaluation evaluate(const Json& initial,const Json& state,const Json& objective,int team,int actor);
struct Node {
    std::uint64_t parent{},visits{};
    Action incoming;
    int owner{-1},team{-1},round{},turns{},same_turn_actions{};
    double value{},estimate{};
    bool evaluated{},terminal{},invalid{},cutoff{},task_complete{};
    std::size_t next_action{};
    std::vector<Action> actions;
    std::vector<std::uint64_t> children;
    Json metrics{Json::object()};
};
Json encode(const Node&);
Node decode(const Json&);
std::vector<Action> prefix(const std::vector<Node>&,std::uint64_t node);
}
namespace opt { int run_micro(const Options&); }
