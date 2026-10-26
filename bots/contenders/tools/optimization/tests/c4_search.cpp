#include "opt/common.hpp"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace opt;

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "c4") {
        install_signal_handlers();
        try { return run_c4(parse_options(argc, argv)); }
        catch (const std::exception& error) { std::cerr << "C4 error: " << error.what() << '\n'; return 1; }
    }
    const fs::path repo = argc > 1 ? fs::absolute(argv[1]) : fs::current_path();
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto scratch = repo / ".cache/contenders/optimization/c4-search" / ("test-" + std::to_string(token));
    fs::create_directories(scratch);
    Json scenario{{"id", "two-hits"}, {"width", 1}, {"height", 2},
                  {"actors", Json::array({Json{{"id", "actor0"}, {"team", 0}, {"x", 0}, {"y", 0},
                      {"spawn", Json::array({0, 0})}, {"hp", 100}, {"max_hp", 100}, {"strength", 20},
                      {"focus", 0}, {"range", 1}, {"ranged", false}, {"spells", Json::array()}}})},
                  {"camps", Json::array({Json{{"id", "camp0"}, {"group", 0}, {"x", 0}, {"y", 1},
                      {"hp", 40}, {"max_hp", 40}, {"strength", 0}, {"range", 0},
                      {"gems", 7}, {"gold", 0}, {"respawn", 50}}})},
                  {"settings", Json{{"max_rounds", 3}, {"capture_time", 1}, {"camp_sleep_time", 2},
                      {"recall_time", 5}, {"spawn_healing", 1}, {"submit_multiplier", 0.1},
                      {"submit_minimum", 1}, {"winning_gems", 1000}}},
                  {"goal", Json{{"kind", "clear"}, {"team", 0}, {"group", 0}}}};
    Json config{{"cases", Json::array({scenario})}, {"search", Json{{"max_nodes", 10000},
                  {"max_frontier", 5000}, {"max_depth", 40}, {"max_solutions", 64},
                  {"checkpoint_every", 7}, {"baseline_steps", 100}}}};
    const auto input = scratch / "input.json";
    write_json(input, config);
    Options full;
    full.mode = "c4"; full.repo = repo; full.input = input;
    full.out = scratch / "full"; full.iterations = 10000; full.seed = 17;
    assert(run_c4(full) == 0);
    const auto whole = read_json(full.out / "atlas.json");
    assert(whole["status"] == Json("completed"));
    assert(whole["completed_cases"].get<int>() == 1);
    const auto& result = whole["cases"][0];
    assert(!result["frontier"].empty());
    assert(result["minimum_rounds_bound"]["upper"].get<int>() == 1);
    assert(result["minimum_rounds_bound"]["lower"].get<int>() == 1);
    assert(result["minimum_rounds_bound"]["exact_in_model"].get<bool>());
    assert(result["status"] == Json("finite_domain_exhausted"));
    assert(fs::exists(full.out / "atlas_policy.hpp"));
    const auto exported = read_json(full.out / "cost-modes.json");
    assert(!exported["modes"].empty());
    assert(exported["modes"][0]["endpoint"] == Json("clear"));
    assert(exported["modes"][0]["qualification"] == Json("model_only"));

    Options partial = full;
    partial.out = scratch / "restart"; partial.iterations = 5;
    assert(run_c4(partial) == 0);
    const auto paused = read_json(partial.out / "atlas.json");
    assert(paused["status"] == Json("checkpointed"));
    assert(paused["case_family_sha256"].get<std::string>().size() == 64);
    const auto pause_checkpoint = read_json(partial.out / "checkpoint.json")["state"];
    assert(pause_checkpoint.contains("active_ref") && !pause_checkpoint.contains("active"));
    const auto arena = partial.out / pause_checkpoint["active_ref"]["path"].get<std::string>();
    assert(file_sha256(arena) == pause_checkpoint["active_ref"]["sha256"].get<std::string>());
    partial.resume = true; partial.iterations = full.iterations;
    assert(run_c4(partial) == 0);
    const auto restarted = read_json(partial.out / "atlas.json");
    assert(restarted.dump() == whole.dump());
    const auto finish_checkpoint = read_json(partial.out / "checkpoint.json")["state"];
    assert(finish_checkpoint["completed_refs"].size() == 1 && !finish_checkpoint.contains("completed"));

    auto bounded_config = config;
    bounded_config["search"]["max_nodes"] = 3;
    bounded_config["search"]["max_frontier"] = 1;
    const auto bounded_input = scratch / "bounded.json";
    write_json(bounded_input, bounded_config);
    Options bounded = full;
    bounded.input = bounded_input; bounded.out = scratch / "bounded";
    assert(run_c4(bounded) == 0);
    const auto limited = read_json(bounded.out / "atlas.json");
    assert(limited["cases"][0]["status"] == Json("bounded_approximation"));
    assert(limited["cases"][0]["discarded_states"].get<int>() > 0);

    auto memory_config = config;
    memory_config["search"]["max_state_json_bytes"] = 3000;
    const auto memory_input = scratch / "memory.json"; write_json(memory_input, memory_config);
    Options memory = full; memory.input = memory_input; memory.out = scratch / "memory";
    assert(run_c4(memory) == 0);
    const auto memory_result = read_json(memory.out / "atlas.json");
    assert(memory_result["cases"][0]["memory_cap_discards"].get<int>() > 0);
    assert(memory_result["cases"][0]["retained_state_json_bytes"].get<int>() <= 3000);

    auto capture_config = config;
    capture_config["cases"][0]["goal"]["kind"] = "capture_depart";
    capture_config["cases"][0]["settings"]["max_rounds"] = 6;
    capture_config["cases"][0]["action_subset"] = Json::array({"basic", "move", "end"});
    const auto capture_input = scratch / "capture.json"; write_json(capture_input, capture_config);
    Options capture = full; capture.input = capture_input; capture.out = scratch / "capture";
    assert(run_c4(capture) == 0);
    const auto capture_result = read_json(capture.out / "atlas.json");
    assert(!capture_result["cases"][0]["frontier"].empty());
    const auto& best = capture_result["cases"][0]["frontier"][0];
    assert(best["status"] == Json("success"));
    for (const auto& action : best["actions"].as_array())
        assert(action["type"] == Json("basic") || action["type"] == Json("move") || action["type"] == Json("end"));
    const auto request_path = capture.out / capture_result["cases"][0]["native_validation"]["request"].get<std::string>();
    const auto request = read_json(request_path);
    assert(request["expected_states"].size() == best["actions"].size() + 1);
    assert(request["prediction_sha256"] == Json(sha256(request["expected_states"].dump())));
    assert(capture_result["cases"][0]["cost_modes"][0]["endpoint"] == Json("release"));

    auto reference_config = capture_config;
    reference_config["cases"][0]["reference_actions"] = best["actions"];
    reference_config["cases"][0]["reference_identity"] = "frozen-test-executor";
    const auto reference_input = scratch / "reference.json"; write_json(reference_input, reference_config);
    Options reference = full; reference.input = reference_input; reference.out = scratch / "reference";
    assert(run_c4(reference) == 0);
    const auto reference_result = read_json(reference.out / "atlas.json");
    assert(reference_result["cases"][0]["baseline"]["source"] == Json("supplied_executor_reference"));

    auto sacrifice_config = config;
    auto dead_actor = sacrifice_config["cases"][0]["actors"][0];
    dead_actor["id"] = "dead-participant"; dead_actor["hp"] = 0;
    sacrifice_config["cases"][0]["actors"].push_back(dead_actor);
    const auto sacrifice_input = scratch / "sacrifice.json"; write_json(sacrifice_input, sacrifice_config);
    Options sacrifice = full; sacrifice.input = sacrifice_input; sacrifice.out = scratch / "sacrifice";
    assert(run_c4(sacrifice) == 0);
    const auto sacrifice_result = read_json(sacrifice.out / "atlas.json");
    assert(!sacrifice_result["cases"][0]["frontier"].empty());
    assert(sacrifice_result["cases"][0]["cost_modes"].empty());

    bool rejected = false;
    auto duplicate_config = config;
    duplicate_config["cases"].push_back(scenario);
    const auto duplicate_input = scratch / "duplicate.json";
    write_json(duplicate_input, duplicate_config);
    Options duplicate = full;
    duplicate.input = duplicate_input; duplicate.out = scratch / "duplicate";
    try { run_c4(duplicate); } catch (const std::exception&) { rejected = true; }
    assert(rejected);
    auto imported_config = config;
    imported_config["cases"] = Json::array({Json{{"scenario", scenario},
        {"schema", "contenders-curriculum-case/v1"},
        {"provenance", Json{{"origin", "constructed-test-curriculum"}}}}});
    const auto imported_input = scratch / "imported.json";
    write_json(imported_input, imported_config);
    Options imported = full;
    imported.input = imported_input; imported.out = scratch / "imported";
    assert(run_c4(imported) == 0);
    const auto imported_result = read_json(imported.out / "atlas.json");
    assert(imported_result["cases"][0]["scenario"]["atlas_import"]["provenance"]["origin"] == Json("constructed-test-curriculum"));
    assert(imported_result["cases"][0]["case_id"] == Json("two-hits/import-0"));

    auto mixed_config = config;
    auto adversarial = scenario;
    adversarial["id"] = "requires-adversarial-search";
    adversarial["actors"][0]["team"] = 1;
    mixed_config["cases"].push_back(adversarial);
    auto unmodeled_item = scenario;
    unmodeled_item["id"] = "requires-item-transition-model";
    unmodeled_item["actors"][0]["items"] = Json::array({"diagnostic-unknown-item"});
    mixed_config["cases"].push_back(unmodeled_item);
    const auto mixed_input = scratch / "mixed.json";
    write_json(mixed_input, mixed_config);
    Options mixed = full;
    mixed.input = mixed_input; mixed.out = scratch / "mixed";
    assert(run_c4(mixed) == 0);
    const auto mixed_result = read_json(mixed.out / "atlas.json");
    assert(mixed_result["status"] == Json("completed_with_rejections"));
    assert(mixed_result["rejected_cases"].get<int>() == 2);
    assert(mixed_result["cases"][1]["status"] == Json("invalid_or_unsupported_case"));
    assert(mixed_result["cases"][2]["status"] == Json("invalid_or_unsupported_case"));
    std::cout << "C4: exact optimum, trace export, deterministic restart, family identity, bounded status, curriculum imports and rejection guards passed\n";
    return 0;
}
