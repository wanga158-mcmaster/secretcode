#include "opt/config_policy.hpp"
#include <iostream>

using opt::Json;
int main(int argc, char** argv) {
    try {
        Json config{{"space", Json::array({Json{{"name", "x"}, {"type", "integer"}, {"min", 0}, {"max", 9}, {"default", 0}},
            Json{{"name", "draft_mode"}, {"values", Json::array({8, 99})}, {"default", 8}}})},
            {"policy_space", Json{{"max_depth", 3}, {"max_leaves", 4}, {"features", Json::array({
                Json{{"name", "width"}, {"thresholds", Json::array({32, 48, 64})}},
                Json{{"name", "enemy_has_ryth"}, {"thresholds", Json::array({0.5})}}})}}}};
        opt::c2::ContextConfigSpace space(config); std::mt19937_64 rng(793); std::size_t checks = 0;
        auto check = [&](bool okay) { ++checks; if (!okay) throw std::runtime_error("context policy invariant failed at check " + std::to_string(checks)); };
        check(space.valid(space.baseline()));
        Json branch = space.sample(rng);
        for (int i = 0; i < 2000; ++i) {
            auto sampled = space.sample(rng); check(space.valid(sampled));
            auto mutated = space.mutate(sampled, rng, 3); check(space.valid(mutated));
            check(space.valid(space.crossover(mutated, branch, rng))); branch = mutated;
        }
        auto malformed = space.baseline(); malformed["config_policy"]["tree"]["knobs"]["unknown"] = 1;
        check(!space.valid(malformed));
        const auto leaf = space.baseline().at("config_policy").at("tree");
        auto equal = space.baseline(); equal["config_policy"]["tree"] = Json{{"feature", "width"}, {"threshold", 48}, {"left", leaf}, {"right", leaf}};
        check(!space.valid(equal)); check(space.canonical(equal) == space.baseline());
        auto illegal = equal; illegal["config_policy"]["tree"]["feature"] = "opponent_identifier"; check(!space.valid(illegal));
        const auto header = opt::c2::policy_cpp_header(branch.at("config_policy"));
        check(header.find("select_configuration") != std::string::npos && header.find("optimized_leaf0") != std::string::npos);
        if (argc > 1) {
            const auto production = opt::read_json(argv[1]); opt::c2::ContextConfigSpace actual(production);
            check(actual.valid(actual.baseline()));
            for (int i = 0; i < 500; ++i) {
                const auto sample = actual.sample(rng); check(actual.valid(sample));
                check(actual.valid(actual.mutate(sample, rng, 3))); check(actual.valid(actual.crossover(sample, actual.baseline(), rng)));
            }
        }
        if (argc > 2) opt::atomic_write(argv[2], header);
        std::cout << checks << " contextual configuration checks passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
