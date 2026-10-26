#pragma once
#include "opt/common.hpp"
#include "opt/sim.hpp"
#include <functional>

namespace opt::scenarios {

struct Limits {
    std::size_t oracle_nodes{6000};
    std::size_t beam_width{96};
    std::size_t max_actions{1024};
    std::size_t population{24};
    std::size_t archive{96};
    std::size_t mutation_attempts{48};
    std::size_t max_blocked{18};
    std::size_t minimization_trials{12};
    std::size_t memory_mb{1024};
    double minimum_regret{1.0};
    std::string baseline{"default"};
};

Limits limits(const Json& config);
// Validation deliberately adds map-generation constraints to the local model:
// groups are cardinally connected, different groups are not cardinally adjacent,
// and the terrain has one connected walkable region. Unsupported local-model
// features are rejected by sim::initial; this does not prove native reachability.
Json validate(const Json& scenario);
std::string descriptor(const Json& scenario);
Json mutate(const Json& parent, std::mt19937_64& rng, const Limits& bounds);
Json evaluate_case(const Json& scenario, const Limits& bounds,
                   const std::function<bool()>& stop = {});
// Finite reactive opponent programs use only legal model actions. The same
// program is applied to both comparison traces and every oracle rollout.
sim::Action opponent_action(const sim::State& state, const std::string& policy);
// A native adapter must reconstruct this case and compare the declared model
// projection after every ordered action. Exporting a request is not validation.
Json validation_request(const Json& evaluation, const std::string& which,
                        std::size_t state_limit = 2048);
// Try simpler versions while preserving a completed feasible alternative with
// at least minimum_regret. The bounded search is not an optimality certificate.
Json minimize(const Json& evaluation, const Json& original_seed, const Limits& bounds,
              const std::function<bool()>& stop = {});
Json default_case(const Json& catalog = Json());

} // namespace opt::scenarios
