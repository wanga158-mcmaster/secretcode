#include "opt/common.hpp"
#include "opt/sim.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace opt {
namespace {

struct AtlasNode {
    Json state, action;
    std::int64_t parent{-1};
    int depth{}, elapsed{};
    bool open{true};
};
using QueueEntry = std::tuple<int, double, int, std::size_t>;

// Optimistic damage bound: every living friendly body is allowed to hit every
// remaining target member every round, without travel, cooldown or survival
// costs. This can be much too generous, but cannot promise a faster clear than
// the restricted action model allows. Capture/exit time is deliberately zero.
int priority_round(const Json& state) {
    const int elapsed = sim::summary(state)["elapsed_rounds"].get<int>();
    const auto& goal = state["goal"];
    const auto kind = goal.value("kind", "capture_depart");
    if (kind != "clear" && kind != "capture_depart") return elapsed;
    const int target_group = goal.value("group", 0);
    double health = 0.0, float_slack = 0.0;
    int members = 0;
    for (const auto& camp : state["camps"].as_array()) {
        if (camp.value("group", 0) != target_group) continue;
        const float hp = camp.value("hp", 0.0f);
        if (hp <= 0.0f) continue;
        health += hp; ++members;
        float_slack += std::nextafter(hp, std::numeric_limits<float>::infinity()) - hp;
    }
    if (health <= 0.0 || members == 0) return elapsed;
    double damage = 0.0;
    for (const auto& actor : state["actors"].as_array()) {
        if (actor.value("hp", 0.0) <= 0.0) continue;
        double enhanced = actor.value("enhanced_damage", 0.0), cast = 0.0;
        for (const auto& spell : actor["spells"].as_array()) {
            enhanced = std::max(enhanced, spell.value("enhanced_damage", 0.0));
            cast = std::max(cast, spell.value("damage", 0.0));
        }
        const double basic = actor.value("strength", 0.0) + enhanced;
        damage += members * std::max(2.0 * basic, basic + cast) * actor.value("camp_damage_multiplier", 1.0)
                  + 8.0 * float_slack;
    }
    if (damage <= 0.0 || !std::isfinite(damage)) return elapsed;
    const double extra = std::max(0.0, std::ceil(health / damage) - 1.0);
    return elapsed + static_cast<int>(std::min(extra, static_cast<double>(std::numeric_limits<int>::max() / 4)));
}

// This orders only states sharing the same optimistic round bound. It grants
// no proof credit: travel, capture progress and health remain in the full state.
double progress_tie_break(const Json& state) {
    const auto& goal = state["goal"];
    const int group = goal.value("group", 0), team = goal.value("team", 0);
    double remaining_hp = 0.0, approach = 1000.0, departure = 0.0;
    bool paid = false;
    int capture_left = state["settings"].value("capture_time", 5) + 1;
    for (const auto& g : state["groups"].as_array()) if (g.value("id", 0) == group) {
        paid = g["payouts"][team].get<int>() > 0;
        if (g.value("capture_team", -1) == team && g.value("capture_left", -1) >= 0)
            capture_left = g["capture_left"].get<int>();
    }
    for (const auto& camp : state["camps"].as_array()) if (camp.value("group", 0) == group) {
        remaining_hp += std::max(0.0, camp.value("hp", 0.0));
        for (const auto& actor : state["actors"].as_array()) {
            if (actor.value("team", 0) != team || actor.value("hp", 0.0) <= 0.0) continue;
            const int distance = sim::chebyshev(actor, camp);
            if (camp.value("hp", 0.0) > 0.0 || remaining_hp == 0.0)
                approach = std::min(approach, static_cast<double>(distance));
            if (distance == 0) departure += 1.0;
        }
    }
    if (paid) return departure * 1e6 - sim::utility(state, team);
    return remaining_hp * 1e6 + capture_left * 1e4 + approach * 100.0 - sim::utility(state, team) * 0.001;
}

Json arr_numbers(const Json& x, const std::string& key) {
    return x.contains(key) ? x[key] : Json::array();
}

// Location and living-body identity are not scalarized. A faster plan in a
// different place does not automatically dominate a useful continuation there.
bool dominates(const Json& a, const Json& b) {
    // The simulator supplies the remaining permissions, owner/round phase,
    // channels and other world state. Absent that context, keep both outcomes.
    if (!a.contains("continuation_signature") || !b.contains("continuation_signature")) return false;
    if (a["continuation_signature"].dump() != b["continuation_signature"].dump()) return false;
    if (a["positions"].dump() != b["positions"].dump()) return false;
    if (arr_numbers(a, "alive").dump() != arr_numbers(b, "alive").dump()) return false;
    bool strict = false;
    auto less = [&](double x, double y) {
        if (x > y) return false;
        strict = strict || x < y;
        return true;
    };
    auto greater = [&](double x, double y) { return less(-x, -y); };
    if (!less(a["elapsed_rounds"].get<double>(), b["elapsed_rounds"].get<double>())) return false;
    for (const auto& key : {"hp", "shields", "shield_ticks"}) {
        const auto av = arr_numbers(a, key), bv = arr_numbers(b, key);
        if (av.size() != bv.size()) return false;
        for (std::size_t i = 0; i < av.size(); ++i)
            if (!greater(av[i].get<double>(), bv[i].get<double>())) return false;
    }
    const auto ac = arr_numbers(a, "cooldowns"), bc = arr_numbers(b, "cooldowns");
    if (ac.size() != bc.size()) return false;
    for (std::size_t i = 0; i < ac.size(); ++i) {
        if (ac[i].size() != bc[i].size()) return false;
        for (std::size_t j = 0; j < ac[i].size(); ++j)
            if (!less(ac[i][j].get<double>(), bc[i][j].get<double>())) return false;
    }
    const auto ast = arr_numbers(a, "stun"), bst = arr_numbers(b, "stun");
    if (ast.size() != bst.size()) return false;
    for (std::size_t i = 0; i < ast.size(); ++i)
        if (!less(ast[i].get<double>(), bst[i].get<double>())) return false;
    const auto& ar = a["resources"];
    const auto& br = b["resources"];
    for (const auto& key : {"score", "gems", "gold"}) {
        const auto av = arr_numbers(ar, key), bv = arr_numbers(br, key);
        if (av.size() != bv.size()) return false;
        for (std::size_t i = 0; i < av.size(); ++i)
            if (!greater(av[i].get<double>(), bv[i].get<double>())) return false;
    }
    return strict || a.dump() == b.dump();
}

void set_path(Json& root, const std::string& path, const Json& value) {
    Json* node = &root;
    std::size_t begin = 0;
    for (;;) {
        const auto dot = path.find('.', begin);
        const auto token = path.substr(begin, dot == std::string::npos ? dot : dot - begin);
        if (token.empty()) throw std::runtime_error("C4 axis has an empty path component");
        if (node->is_array()) {
            if (!std::all_of(token.begin(), token.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
                throw std::runtime_error("C4 array axis path requires an integer index");
            const auto index = static_cast<std::size_t>(std::stoull(token));
            if (index >= node->size()) throw std::runtime_error("C4 axis array index out of range");
            if (dot == std::string::npos) { (*node)[index] = value; return; }
            node = &(*node)[index];
        } else {
            if (!node->is_object()) throw std::runtime_error("C4 axis traverses a scalar");
            if (dot == std::string::npos) { (*node)[token] = value; return; }
            if (!node->contains(token)) throw std::runtime_error("C4 axis traverses a missing object member");
            node = &(*node)[token];
        }
        begin = dot + 1;
    }
}

Json make_cases(const Json& config) {
    Json cases = Json::array();
    const auto max_cases = std::max(1, config.value("max_cases", 1024));
    if (config.contains("cases")) {
        for (const auto& c : config["cases"].as_array()) cases.push_back(c);
    }
    for (const auto& field : {"curriculum", "archive", "scenarios"})
        if (config.contains(field) && config[field].is_array())
            for (const auto& c : config[field].as_array()) cases.push_back(c);
    if (config.contains("generate")) {
        const auto& generate = config["generate"];
        if (!generate.contains("template")) throw std::runtime_error("C4 generate.template is required");
        Json generated = Json::array({generate["template"]});
        if (generate.contains("axes")) {
            for (const auto& axis : generate["axes"].as_array()) {
                if (!axis.contains("values") || axis["values"].empty()) throw std::runtime_error("C4 axis values must be nonempty");
                Json next = Json::array();
                for (const auto& c : generated.as_array()) for (const auto& value : axis["values"].as_array()) {
                    if (next.size() + cases.size() >= static_cast<std::size_t>(max_cases))
                        throw std::runtime_error("C4 generated case family exceeds max_cases; raise max_cases or partition axes explicitly");
                    auto child = c;
                    set_path(child, axis["path"].get<std::string>(), value);
                    next.push_back(child);
                }
                generated = std::move(next);
            }
        }
        std::size_t generated_index = 0;
        for (auto c : generated.as_array()) {
            c["id"] = c.value("id", "generated") + "/" + std::to_string(generated_index++);
            cases.push_back(std::move(c));
        }
    }
    if (cases.empty() && config.contains("scenario")) cases.push_back(config["scenario"]);
    if (cases.empty()) throw std::runtime_error("C4 requires cases, scenario, or generate.template");
    if (cases.size() > static_cast<std::size_t>(max_cases)) throw std::runtime_error("C4 case count exceeds max_cases");
    std::set<std::string> ids;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        if (cases[i].contains("scenario")) {
            auto wrapper = cases[i];
            cases[i] = wrapper["scenario"];
            wrapper.as_object().erase("scenario");
            cases[i]["atlas_import"] = wrapper;
            cases[i]["id"] = cases[i].value("id", "imported") + "/import-" + std::to_string(i);
            if (wrapper.contains("split")) cases[i]["split"] = wrapper["split"];
        }
        if (!cases[i].contains("id")) cases[i]["id"] = "case-" + std::to_string(i);
        if (!ids.insert(cases[i]["id"].get<std::string>()).second)
            throw std::runtime_error("C4 case ids must be unique");
    }
    return cases;
}

Json solution_from_path(const Json& initial, const Json& actions, const std::string& source) {
    auto state = sim::initial(initial);
    Json keys = Json::array(), incoming = Json::array();
    auto first = sim::summary(state);
    for (std::size_t i = 0; i < first["hp"].size(); ++i) incoming.push_back(0.0);
    for (const auto& action : actions.as_array()) {
        if (initial.contains("action_subset")) {
            bool allowed = false;
            for (const auto& type : initial["action_subset"].as_array()) allowed = allowed || type == action["type"];
            if (!allowed) throw std::runtime_error("C4 trace uses an action outside scenario.action_subset");
        }
        keys.push_back(sha256(sim::state_key(state)));
        auto before = sim::summary(state);
        state = sim::step(state, action);
        auto after = sim::summary(state);
        for (std::size_t i = 0; i < incoming.size(); ++i) {
            double damage = std::max(0.0, before["hp"][i].get<double>() - after["hp"][i].get<double>());
            if (before.contains("shields") && after.contains("shields"))
                damage += std::max(0.0, before["shields"][i].get<double>() - after["shields"][i].get<double>());
            incoming[i] = incoming[i].get<double>() + damage;
        }
    }
    auto summary = sim::summary(state);
    if (summary.contains("damage_taken")) incoming = summary["damage_taken"];
    return Json{{"source", source}, {"actions", actions}, {"state_keys_sha256", keys},
                {"outcome", summary}, {"terminal_state", sim::encode_state(state)},
                {"status", sim::status(state)}, {"incoming_by_actor", incoming}};
}

Json greedy_solution(const Json& scenario, int limit) {
    auto state = sim::initial(scenario);
    Json actions = Json::array();
    std::set<std::string> seen;
    for (int n = 0; n < limit && !sim::terminal(state); ++n) {
        if (!seen.insert(sim::state_key(state)).second) break;
        auto action = sim::baseline_action(state);
        if (scenario.contains("action_subset")) {
            bool allowed = false;
            for (const auto& type : scenario["action_subset"].as_array()) allowed = allowed || type == action["type"];
            if (!allowed) action = Json{{"type", "end"}};
        }
        actions.push_back(action);
        state = sim::step(state, action);
    }
    return solution_from_path(scenario, actions, "greedy_reference");
}

Json native_request(const Json& scenario, const Json& solution) {
    const auto& actions = solution["actions"];
    Json states = Json::array();
    auto state = sim::initial(scenario); states.push_back(sim::encode_state(state));
    for (const auto& action : actions.as_array()) { state = sim::step(state, action); states.push_back(sim::encode_state(state)); }
    return Json{{"schema", "contenders-scenario-validation/v2"}, {"kind", "scenario_validation"},
        {"scenario", scenario}, {"actions", actions}, {"expected_states", states},
        {"scenario_sha256", sha256(scenario.dump())}, {"trace_sha256", sha256(actions.dump())},
        {"prediction_sha256", sha256(states.dump())}, {"model_version", sim::version},
        {"comparison_projection", "restricted-model-after-every-action"},
        {"qualification", "request_only_not_native_validation"}};
}

Json validate_trace(const Options& options, const Json& scenario, const Json& solution,
                    const fs::path& directory) {
    fs::create_directories(directory);
    if (solution["actions"].size() > 4096)
        return Json{{"native_validated", false}, {"status", "validation_trace_limit"}};
    const auto request = native_request(scenario, solution);
    const auto request_path = fs::absolute(directory / "request.json"), response_path = fs::absolute(directory / "response.json");
    write_json(request_path, request);
    const auto request_relative = request_path.lexically_relative(fs::absolute(options.out)).generic_string();
    const auto response_relative = response_path.lexically_relative(fs::absolute(options.out)).generic_string();
    if (!options.has("validator")) return Json{{"native_validated", false}, {"status", "request_exported"}, {"request", request_relative}};
    std::vector<std::string> command{options.get("validator")};
    for (const auto& arg : options.values("validator-arg")) command.push_back(arg);
    command.insert(command.end(), {"--request", request_path.string(), "--response", response_path.string(), "--repo", options.repo.string()});
    const auto process = run_process(command, options.repo, directory / "stdout.log", directory / "stderr.log", options.real("validator-timeout", 120));
    if (process.exit_code != 0 || process.timed_out || !fs::exists(response_path))
        return Json{{"native_validated", false}, {"status", "validator_failed"}, {"exit_code", process.exit_code}, {"timed_out", process.timed_out}};
    const auto response = read_json(response_path);
    const bool bound = response.value("scenario_sha256", "") == request["scenario_sha256"].get<std::string>()
        && response.value("trace_sha256", "") == request["trace_sha256"].get<std::string>()
        && response.value("prediction_sha256", "") == request["prediction_sha256"].get<std::string>();
    const bool valid = bound && response.value("native", false) && response.value("passed", false)
        && response.value("scope", "") == "entire-trace"
        && response.value("comparison_projection", "") == "restricted-model-after-every-action";
    return Json{{"native_validated", valid}, {"status", valid ? "validated_best_trace_only" : "unsupported_or_mismatched"},
        {"hashes_bound", bound}, {"request", request_relative}, {"response", response_relative}};
}

bool retain_solution(Json& solutions, Json candidate, std::size_t maximum, bool& truncated) {
    const auto candidate_key = sim::state_key(candidate["terminal_state"]);
    for (const auto& old : solutions.as_array())
        if (sim::state_key(old["terminal_state"]) == sim::state_key(candidate["terminal_state"]) ||
            dominates(old["outcome"], candidate["outcome"])) return false;
    auto& values = solutions.as_array();
    values.erase(std::remove_if(values.begin(), values.end(), [&](const Json& old) {
        return dominates(candidate["outcome"], old["outcome"]);
    }), values.end());
    values.push_back(std::move(candidate));
    std::stable_sort(values.begin(), values.end(), [](const Json& a, const Json& b) {
        return a["outcome"]["elapsed_rounds"].get<int>() < b["outcome"]["elapsed_rounds"].get<int>();
    });
    if (values.size() > maximum) { values.resize(maximum); truncated = true; }
    return std::any_of(values.begin(), values.end(), [&](const Json& kept) {
        return sim::state_key(kept["terminal_state"]) == candidate_key;
    });
}

Json encode_nodes(const std::vector<AtlasNode>& nodes) {
    Json out = Json::array();
    for (const auto& node : nodes)
        out.push_back(Json{{"s", sim::encode_state(node.state)}, {"a", node.action},
                           {"p", node.parent}, {"d", node.depth}, {"r", node.elapsed}, {"o", node.open}});
    return out;
}

Json path_for(const std::vector<AtlasNode>& nodes, std::size_t id) {
    std::vector<Json> reverse;
    for (;;) {
        const auto& node = nodes.at(id);
        if (node.parent < 0) break;
        reverse.push_back(node.action);
        id = static_cast<std::size_t>(node.parent);
    }
    Json actions = Json::array();
    for (auto it = reverse.rbegin(); it != reverse.rend(); ++it) actions.push_back(*it);
    return actions;
}

Json cost_modes(const Json& scenario, const Json& solutions) {
    Json out = Json::array();
    const auto initial_state = sim::initial(scenario);
    const auto initial_summary = sim::summary(initial_state);
    const auto goal = scenario.contains("goal") ? scenario["goal"] : Json::object();
    const auto kind = goal.value("kind", "capture_depart");
    const auto endpoint = kind == "capture_depart" ? "release" : kind;
    const int goal_team = goal.value("team", 0);
    // A contested trace is not an uncontested service mode for a schedule.
    for (const auto& actor : scenario["actors"].as_array())
        if (actor.value("team", 0) != goal_team) return out;
    for (const auto& solution : solutions.as_array()) {
        // A sacrificed body is a legitimate frontier outcome, but is not a
        // reusable all-participant service mode for the economy scheduler.
        bool all_alive = true;
        for (const auto& alive : solution["outcome"]["alive"].as_array()) all_alive = all_alive && alive.get<bool>();
        if (!all_alive) continue;
        Json participants = Json::array(), stats = Json::array(), required = Json::array();
        const auto& actors = scenario["actors"];
        for (std::size_t i = 0; i < actors.size(); ++i) {
            participants.push_back(actors[i].contains("id") ? actors[i]["id"] : Json(static_cast<int>(i)));
            Json point = Json{{"hp", actors[i].value("hp", actors[i].value("max_hp", 0.0))},
                              {"max_hp", actors[i].value("max_hp", actors[i].value("hp", 0.0))},
                              {"strength", actors[i].value("strength", 0.0)},
                              {"items", actors[i].contains("items") ? actors[i]["items"] : Json::array()}};
            stats.push_back(point);
            required.push_back(point["items"]);
        }
        out.push_back(Json{{"id", scenario.value("id", "case") + "/" + std::to_string(out.size())},
                           {"initial_case_id", scenario.value("id", "case")},
                           {"scenario_sha256", sha256(scenario.dump())}, {"endpoint", endpoint},
                           {"participants", participants}, {"start_positions", initial_summary["positions"]},
                           {"end_positions", solution["outcome"]["positions"]},
                           {"work_rounds", solution["outcome"]["elapsed_rounds"]},
                           {"tested_actor_stats", stats}, {"incoming_by_actor", solution["incoming_by_actor"]},
                           {"incoming_measure", "model cumulative damage if supplied; otherwise summed HP and shield losses, excluding overkill"},
                           {"entry_hp_min", initial_summary["hp"]}, {"entry_hp_max", initial_summary["hp"]},
                           {"required_items", required}, {"reward_resources", solution["outcome"]["resources"]},
                           {"qualification", "model_only"}, {"safe_region", "exact tested initial state only"}});
        auto& record = out[out.size() - 1];
        record["entry_state"] = sim::encode_state(initial_state);
        record["exit_state"] = solution["terminal_state"];
        record["action_trace"] = solution["actions"];
        record["endpoint_continuation"] = solution["outcome"]["continuation_signature"];
        record["round_cost_semantics"] = "elapsed world-round difference; endpoint permissions are not automatically fresh";
    }
    return out;
}

std::string cpp_string(const std::string& value) { return Json(value).dump(); }

void export_policy(const fs::path& path, const Json& completed) {
    std::ostringstream out;
    out << "#pragma once\n// Model-only exact-state trace table. Not an information-safe deployed policy.\n"
           "namespace contender_atlas {\nstruct Step { const char* case_id; const char* state_sha256; const char* action_json; };\n"
           "inline constexpr Step steps[] = {\n";
    std::size_t count = 0;
    for (const auto& item : completed.as_array()) {
        if (item["frontier"].empty()) continue;
        const auto& best = item["frontier"][0];
        for (std::size_t i = 0; i < best["actions"].size(); ++i) {
            out << "  {" << cpp_string(item["case_id"].get<std::string>()) << ','
                << cpp_string(best["state_keys_sha256"][i].get<std::string>()) << ','
                << cpp_string(best["actions"][i].dump()) << "},\n";
            ++count;
        }
    }
    if (count == 0) out << "  {nullptr,nullptr,nullptr},\n";
    out << "};\ninline constexpr unsigned step_count = " << count << ";\n}\n";
    atomic_write(path, out.str());
}

} // namespace

int run_c4(const Options& options) {
    Run run(options, "c4-atlas", "4");
    const auto cases = make_cases(run.config);
    Json family = Json::array();
    for (const auto& item : cases.as_array()) family.push_back(item);
    const auto family_hash = sha256(family.dump());
    if (run.resumed && run.state.value("case_family_sha256", "") != family_hash)
        throw std::runtime_error("C4 expanded case family changed since checkpoint; use a new output directory");
    Json catalog = Json::object();
    fs::path catalog_path = run.config.value("catalog", "");
    if (!catalog_path.empty()) {
        if (catalog_path.is_relative()) catalog_path = options.input.parent_path() / catalog_path;
        catalog = read_json(catalog_path);
    } else if (fs::exists(options.repo / "docs/reference/runtime-catalog.json")) {
        catalog_path = options.repo / "docs/reference/runtime-catalog.json";
        catalog = read_json(catalog_path);
    }
    const auto catalog_hash = catalog_path.empty() ? std::string("none") : file_sha256(catalog_path);
    if (run.resumed && run.state.value("catalog_sha256", "none") != catalog_hash)
        throw std::runtime_error("C4 runtime catalog changed since checkpoint");
    const auto search = run.config.contains("search") ? run.config["search"] : Json::object();
    const auto max_nodes = static_cast<std::size_t>(std::max(1, search.value("max_nodes", 20000)));
    const auto max_state_bytes = search.value<std::uint64_t>("max_state_json_bytes", 268435456);
    if (max_state_bytes < 1024 || max_state_bytes > 536870912)
        throw std::runtime_error("C4 max_state_json_bytes must be 1024..536870912; partition larger searches");
    const auto max_frontier = static_cast<std::size_t>(std::max(1, search.value("max_frontier", 8000)));
    const auto max_solutions = static_cast<std::size_t>(std::max(1, search.value("max_solutions", 64)));
    const auto max_depth = std::max(1, search.value("max_depth", 256));
    const auto checkpoint_every = static_cast<std::uint64_t>(std::max(1, search.value("checkpoint_every", 10000)));
    const auto baseline_steps = std::max(1, search.value("baseline_steps", 2000));
    const auto rollout_every = std::max(1, search.value("rollout_every", 256));
    const auto rollout_steps = std::clamp(search.value("rollout_steps", 512), 1, 4096);
    std::size_t case_index = run.state.value("case_index", 0);
    Json completed = Json::array();
    if (run.state.contains("completed_refs")) for (const auto& ref : run.state["completed_refs"].as_array()) {
        const auto path = options.out / ref["path"].get<std::string>();
        if (file_sha256(path) != ref["sha256"].get<std::string>()) throw std::runtime_error("C4 completed case shard changed or disappeared");
        completed.push_back(read_json(path));
    }
    Json scenario, solutions = Json::array(), baseline, rollout_seed;
    std::vector<AtlasNode> nodes;
    std::set<QueueEntry> queue;
    std::map<std::string, int> visited;
    std::uint64_t expanded = 0, duplicates = 0, pruned = 0, failures = 0;
    std::uint64_t state_bytes = 0, memory_cap_discards = 0;
    std::uint64_t rollout_trials = 0, rollout_solutions = 0;
    int pruned_round = std::numeric_limits<int>::max();
    bool pareto_truncated = false;

    auto publish_incumbent = [&] {
        if (solutions.empty()) return;
        const Json record{{"case", scenario}, {"frontier", solutions}, {"qualification", "model_only"}};
        write_json(options.out / "incumbent.json", record);
        write_json(options.out / "incumbents" / (sha256(scenario.dump()).substr(0, 20) + ".json"), record);
    };

    auto save = [&] {
        Json refs = Json::array();
        for (std::size_t i = 0; i < completed.size(); ++i) {
            const auto relative = fs::path("completed") / (std::to_string(i) + ".json");
            const auto path = options.out / relative;
            if (!fs::exists(path)) write_json(path, completed[i]);
            refs.push_back(Json{{"path", relative.generic_string()}, {"sha256", file_sha256(path)}});
        }
        Json saved{{"catalog_sha256", catalog_hash}, {"case_family_sha256", family_hash}, {"sim_version", sim::version},
                   {"case_index", case_index}, {"completed_refs", refs}};
        if (!nodes.empty()) {
            const auto active = Json{{"scenario", scenario}, {"solutions", solutions}, {"baseline", baseline},
                                    {"nodes", encode_nodes(nodes)}, {"expanded", expanded},
                                    {"duplicates", duplicates}, {"pruned", pruned}, {"failures", failures},
                                    {"memory_cap_discards", memory_cap_discards},
                                    {"rollout_trials", rollout_trials}, {"rollout_solutions", rollout_solutions},
                                    {"pruned_round", pruned_round}, {"pareto_truncated", pareto_truncated}};
            // Alternate two compact arenas. A crash while writing the inactive
            // slot leaves the previous checkpoint's referenced arena untouched.
            const int slot = 1 - run.state.value("active_slot", 0);
            const auto relative = "active-search-" + std::to_string(slot) + ".json";
            const auto body = active.dump();
            if (body.size() > 805306368) throw std::runtime_error("C4 active checkpoint exceeds safe shard size; reduce max_state_json_bytes/max_solutions");
            atomic_write(options.out / relative, body);
            saved["active_slot"] = slot;
            saved["active_ref"] = Json{{"path", relative}, {"sha256", sha256(body)}};
        }
        run.checkpoint(saved);
    };

    if (run.resumed && run.state.contains("active_ref")) {
        if (run.state.value("sim_version", "") != sim::version) throw std::runtime_error("C4 simulator version changed");
        const auto path = options.out / run.state["active_ref"]["path"].get<std::string>();
        if (file_sha256(path) != run.state["active_ref"]["sha256"].get<std::string>()) throw std::runtime_error("C4 active arena shard changed or disappeared");
        const auto active = read_json(path);
        scenario = active["scenario"]; solutions = active["solutions"]; baseline = active["baseline"];
        expanded = active.value<std::uint64_t>("expanded", 0); duplicates = active.value<std::uint64_t>("duplicates", 0);
        pruned = active.value<std::uint64_t>("pruned", 0); failures = active.value<std::uint64_t>("failures", 0);
        memory_cap_discards = active.value<std::uint64_t>("memory_cap_discards", 0);
        rollout_trials = active.value<std::uint64_t>("rollout_trials", 0);
        rollout_solutions = active.value<std::uint64_t>("rollout_solutions", 0);
        pruned_round = active.value("pruned_round", std::numeric_limits<int>::max());
        pareto_truncated = active.value("pareto_truncated", false);
        for (const auto& value : active["nodes"].as_array()) {
            AtlasNode node{sim::decode_state(value["s"]), value["a"], value["p"].get<std::int64_t>(),
                           value["d"].get<int>(), value["r"].get<int>(), value["o"].get<bool>()};
            const auto id = nodes.size();
            state_bytes += node.state.dump().size();
            auto key = sim::state_key(node.state);
            auto found = visited.find(key);
            if (found == visited.end() || node.depth < found->second) visited[key] = node.depth;
            if (node.open) queue.emplace(priority_round(node.state), progress_tie_break(node.state), node.depth, id);
            nodes.push_back(std::move(node));
        }
    }

    auto close_case = [&] {
        Json result{{"case_id", scenario.value("id", "case-" + std::to_string(case_index))},
                    {"split", scenario.value("split", "unspecified")}, {"scenario", scenario},
                    {"scenario_sha256", sha256(scenario.dump())}, {"sim_version", sim::version},
                    {"qualification", "model_only; no native replay validation performed by this engine"},
                    {"frontier", solutions}, {"baseline", baseline}, {"nodes", nodes.size()},
                    {"expanded", expanded}, {"duplicates", duplicates}, {"failures", failures},
                    {"retained_state_json_bytes", state_bytes}, {"memory_cap_discards", memory_cap_discards},
                    {"rollout_trials", rollout_trials}, {"rollout_solutions", rollout_solutions},
                    {"discarded_states", pruned}, {"pareto_truncated", pareto_truncated},
                    {"status", pruned == 0 && !pareto_truncated ? "finite_domain_exhausted" : "bounded_approximation"}};
        if (!solutions.empty()) {
            const int upper = solutions[0]["outcome"]["elapsed_rounds"].get<int>();
            const int lower = pruned == 0 ? upper : std::min(upper, pruned_round);
            result["minimum_rounds_bound"] = Json{{"lower", lower}, {"upper", upper},
                                                  {"exact_in_model", lower == upper}};
            if (baseline["status"] == Json("success")) {
                const int base = baseline["outcome"]["elapsed_rounds"].get<int>();
                result["baseline_minus_optimal_rounds_interval"] = Json::array({base - upper, base - lower});
            }
        }
        result["cost_modes"] = cost_modes(scenario, solutions);
        if (!solutions.empty()) {
            const auto directory = options.out / "native-validation" / sha256(scenario.dump()).substr(0, 20);
            result["native_validation"] = validate_trace(options, scenario, solutions[0], directory / "best");
            if (baseline["status"] == Json("success"))
                result["baseline_native_validation"] = validate_trace(options, scenario, baseline, directory / "baseline");
        }
        completed.push_back(std::move(result));
        run.event(Json{{"type", "case_finished"}, {"case_id", scenario.value("id", "case")},
                       {"expanded", expanded}, {"solutions", solutions.size()}, {"discarded", pruned}});
        ++case_index; nodes.clear(); queue.clear(); visited.clear();
        save();
    };

    while (case_index < cases.size() && !run.stop()) {
        if (nodes.empty()) {
            auto specification = cases[case_index];
            const auto id = specification.value("id", "case-" + std::to_string(case_index));
            Json start;
            try {
                bool named = false;
                for (const auto& field : {"actors", "camps"}) if (specification.contains(field))
                    for (const auto& entry : specification[field].as_array()) named = named || entry.contains("design");
                if (!named) scenario = specification;
                else scenario = sim::expand_case(specification, catalog);
                scenario["id"] = id;
                if (specification.contains("split")) scenario["split"] = specification["split"];
                const int team = scenario.contains("goal") ? scenario["goal"].value("team", 0) : 0;
                for (const auto& actor : scenario["actors"].as_array())
                    if (actor.value("team", 0) != team)
                        throw std::runtime_error("C4 is cooperative clearing: use C9 for adversarial actor choices");
                start = sim::initial(scenario);
                if (scenario.contains("action_subset")) {
                    if (!scenario["action_subset"].is_array() || scenario["action_subset"].empty())
                        throw std::runtime_error("C4 action_subset must be a nonempty array");
                    bool end = false;
                    for (const auto& type : scenario["action_subset"].as_array()) {
                        const auto name = type.get<std::string>();
                        if (name != "basic" && name != "cast" && name != "move" && name != "end" && name != "priority" && name != "recall" && name != "tribute")
                            throw std::runtime_error("C4 unknown action_subset type");
                        end = end || name == "end";
                    }
                    if (!end) throw std::runtime_error("C4 action_subset must retain owner end");
                }
                baseline = greedy_solution(scenario, baseline_steps);
                rollout_seed = baseline;
                if (scenario.contains("reference_actions")) {
                    baseline = solution_from_path(scenario, scenario["reference_actions"], "supplied_executor_reference");
                    baseline["reference_identity"] = scenario.contains("reference_identity") ? scenario["reference_identity"] : Json("unspecified");
                }
                if (run.config.contains("reference_traces")) for (const auto& reference : run.config["reference_traces"].as_array())
                    if (reference["case_id"] == Json(id)) {
                        baseline = solution_from_path(scenario, reference["actions"], "supplied_executor_reference");
                        baseline["reference_identity"] = reference.value("identity", "unspecified");
                    }
            } catch (const std::exception& error) {
                completed.push_back(Json{{"case_id", id}, {"split", specification.value("split", "unspecified")},
                    {"scenario", specification}, {"status", "invalid_or_unsupported_case"},
                    {"error", error.what()}, {"qualification", "rejected"},
                    {"frontier", Json::array()}, {"cost_modes", Json::array()}});
                run.event(Json{{"type", "case_rejected"}, {"case_id", id}, {"error", error.what()}});
                ++case_index; run.advance(); save(); continue;
            }
            auto summary = sim::summary(start);
            nodes.push_back(AtlasNode{start, Json(), -1, 0, summary["elapsed_rounds"].get<int>(), true});
            state_bytes = start.dump().size(); memory_cap_discards = 0;
            rollout_trials = rollout_solutions = 0;
            queue.emplace(priority_round(start), progress_tie_break(start), 0, 0);
            visited[sim::state_key(start)] = 0;
            expanded = duplicates = pruned = failures = 0;
            pruned_round = std::numeric_limits<int>::max(); pareto_truncated = false;
            solutions = Json::array();
            if (baseline["status"] == Json("success")) retain_solution(solutions, baseline, max_solutions, pareto_truncated);
            if (rollout_seed["status"] == Json("success")) retain_solution(solutions, rollout_seed, max_solutions, pareto_truncated);
            publish_incumbent();
            run.event(Json{{"type", "case_started"}, {"case_id", id}, {"baseline_status", baseline["status"]}});
            save();
        }
        if (queue.empty()) { close_case(); continue; }
        const auto item = *queue.begin(); queue.erase(queue.begin());
        const auto id = std::get<3>(item);
        if (!nodes[id].open) continue;
        nodes[id].open = false;
        const auto state = nodes[id].state;
        const auto depth = nodes[id].depth;
        const auto elapsed = nodes[id].elapsed;
        const auto current_status = sim::status(state);
        if (current_status == "active" && expanded % static_cast<std::uint64_t>(rollout_every) == 0) {
            // A bounded feasible completion makes an early search prefix useful
            // before exhaustive enumeration reaches a distant capture endpoint.
            // Its replay supplies an upper bound only; queue bounds are unchanged.
            ++rollout_trials;
            auto continuation = state;
            auto actions = path_for(nodes, id);
            for (int step = 0; step < rollout_steps && !sim::terminal(continuation); ++step) {
                auto action = sim::baseline_action(continuation);
                if (scenario.contains("action_subset")) {
                    bool allowed = false;
                    for (const auto& type : scenario["action_subset"].as_array()) allowed = allowed || type == action["type"];
                    if (!allowed) action = Json{{"type", "end"}};
                }
                actions.push_back(action); continuation = sim::step(continuation, action);
            }
            if (sim::status(continuation) == "success") {
                auto candidate = solution_from_path(scenario, actions, "search_prefix_feasible_rollout");
                if (retain_solution(solutions, std::move(candidate), max_solutions, pareto_truncated)) {
                    ++rollout_solutions; publish_incumbent();
                    run.event(Json{{"type", "rollout_frontier_improved"}, {"case_id", scenario.value("id", "case")},
                        {"expanded", expanded}, {"frontier_size", solutions.size()}});
                }
            }
        }
        if (current_status == "success") {
            auto solution = solution_from_path(scenario, path_for(nodes, id), "search");
            if (solution["status"] != Json("success") ||
                sim::state_key(solution["terminal_state"]) != sim::state_key(state))
                throw std::runtime_error("C4 predecessor trace failed deterministic model replay");
            if (retain_solution(solutions, std::move(solution), max_solutions, pareto_truncated)) {
                run.event(Json{{"type", "frontier_improved"}, {"case_id", scenario.value("id", "case")},
                               {"elapsed_rounds", elapsed}, {"frontier_size", solutions.size()}, {"expanded", expanded}});
                publish_incumbent();
            }
        } else if (current_status == "failure") {
            ++failures;
        } else if (depth >= max_depth) {
            ++pruned; pruned_round = std::min(pruned_round, elapsed);
        } else {
            const auto actions = sim::legal_actions(state);
            for (const auto& action : actions) {
                if (scenario.contains("action_subset")) {
                    bool allowed = false;
                    for (const auto& type : scenario["action_subset"].as_array()) allowed = allowed || type == action["type"];
                    if (!allowed) continue;
                }
                auto child = sim::step(state, action);
                const auto key = sim::state_key(child);
                const auto found = visited.find(key);
                if (found != visited.end() && found->second <= depth + 1) { ++duplicates; continue; }
                const int rounds = sim::summary(child)["elapsed_rounds"].get<int>();
                const auto child_bytes = static_cast<std::uint64_t>(child.dump().size());
                if (nodes.size() >= max_nodes || child_bytes > max_state_bytes - std::min(state_bytes, max_state_bytes)) {
                    ++pruned; pruned_round = std::min(pruned_round, rounds);
                    if (child_bytes > max_state_bytes - std::min(state_bytes, max_state_bytes)) ++memory_cap_discards;
                    continue;
                }
                visited[key] = depth + 1;
                const auto next_id = nodes.size();
                nodes.push_back(AtlasNode{std::move(child), action, static_cast<std::int64_t>(id), depth + 1, rounds, true});
                state_bytes += child_bytes;
                queue.emplace(priority_round(nodes[next_id].state), progress_tie_break(nodes[next_id].state), depth + 1, next_id);
                if (queue.size() > max_frontier) {
                    const auto last = std::prev(queue.end());
                    nodes[std::get<3>(*last)].open = false;
                    ++pruned; pruned_round = std::min(pruned_round, std::get<0>(*last));
                    queue.erase(last);
                }
            }
        }
        ++expanded; run.advance();
        if (run.iteration % checkpoint_every == 0) {
            save();
            Json metrics{{"case_index", case_index}, {"case_count", cases.size()}, {"expanded", expanded},
                         {"retained_nodes", nodes.size()}, {"pending_states", queue.size()},
                         {"retained_state_json_bytes", state_bytes}, {"memory_cap_discards", memory_cap_discards},
                         {"rollout_trials", rollout_trials}, {"rollout_solutions", rollout_solutions},
                         {"frontier_solutions", solutions.size()}, {"discarded_states", pruned}};
            if (!solutions.empty()) {
                const int upper = solutions[0]["outcome"]["elapsed_rounds"].get<int>();
                int lower = queue.empty() ? upper : std::min(upper, std::get<0>(*queue.begin()));
                if (pruned != 0) lower = std::min(lower, pruned_round);
                metrics["best_rounds"] = upper;
                metrics["round_lower_bound"] = lower;
                metrics["exact_minimum_rounds_in_model"] = lower == upper;
            }
            run.progress(metrics);
        }
    }
    // Finishing the last expansion can exhaust the domain exactly at the work cap.
    if (!nodes.empty() && queue.empty()) close_case();
    save();
    Json output{{"engine", "c4-atlas"}, {"sim_version", sim::version}, {"capabilities", sim::capabilities()},
                {"catalog_sha256", catalog_hash}, {"case_family_sha256", family_hash},
                {"case_count", cases.size()}, {"completed_cases", completed.size()},
                {"cases", completed}, {"status", case_index == cases.size() ? "completed" : "checkpointed"},
                {"qualification", "restricted-model solutions; native replay validation remains required"},
                {"iterations", run.iteration}};
    int rejected = 0;
    for (const auto& item : completed.as_array())
        if (item["status"] == Json("invalid_or_unsupported_case")) ++rejected;
    output["rejected_cases"] = rejected;
    output["search_limits"] = Json{{"max_nodes_per_case", max_nodes}, {"max_frontier_per_case", max_frontier},
        {"max_state_json_bytes_per_case", max_state_bytes}, {"max_depth", max_depth}, {"max_solutions", max_solutions},
        {"rollout_every_expansions", rollout_every}, {"rollout_action_limit", rollout_steps},
        {"memory_semantics", "state JSON payload cap; actual heap also includes JSON objects, keys, queue and predecessor/action metadata"}};
    output["search_order"] = "best-first optimistic damage-round lower bound; ties favor damage, capture and departure progress without changing bounds";
    output["checkpoint_storage"] = "content-checked completed case shards and alternating compact active arenas; checkpoint.json stores references, not accumulated search trees";
    if (case_index == cases.size() && rejected != 0) output["status"] = "completed_with_rejections";
    if (!nodes.empty()) {
        Json active{{"case_id", scenario.value("id", "case")}, {"scenario", scenario},
                    {"scenario_sha256", sha256(scenario.dump())}, {"frontier", solutions},
                    {"pending_states", queue.size()}, {"expanded", expanded}, {"discarded_states", pruned},
                    {"retained_state_json_bytes", state_bytes}, {"memory_cap_discards", memory_cap_discards},
                    {"rollout_trials", rollout_trials}, {"rollout_solutions", rollout_solutions},
                    {"pareto_truncated", pareto_truncated}, {"baseline", baseline}};
        if (!solutions.empty()) {
            const auto upper = solutions[0]["outcome"]["elapsed_rounds"].get<int>();
            int lower = queue.empty() ? upper : std::get<0>(*queue.begin());
            lower = std::min(lower, upper);
            if (pruned != 0) lower = std::min(lower, pruned_round);
            active["minimum_rounds_bound"] = Json{{"lower", lower}, {"upper", upper}, {"exact_in_model", lower == upper}};
        }
        output["active"] = active;
        if (!solutions.empty())
            output["active"]["native_validation"] = validate_trace(options, scenario, solutions[0],
                options.out / "native-validation" / sha256(scenario.dump()).substr(0, 20) / "best");
    }
    Json modes = Json::array();
    for (const auto& item : completed.as_array()) for (const auto& mode : item["cost_modes"].as_array()) modes.push_back(mode);
    if (!nodes.empty()) for (const auto& mode : cost_modes(scenario, solutions).as_array()) modes.push_back(mode);
    write_json(options.out / "atlas.json", output);
    write_json(options.out / "cost-modes.json", Json{{"schema", "contenders-atlas-cost-modes/v1"}, {"qualification", "model_only"}, {"modes", modes}});
    auto export_cases = completed;
    if (!nodes.empty()) export_cases.push_back(Json{{"case_id", scenario.value("id", "case")}, {"frontier", solutions}});
    export_policy(options.out / "atlas_policy.hpp", export_cases);
    run.finish(output);
    return rejected == static_cast<int>(cases.size()) ? 2 : 0;
}
} // namespace opt
