#include "opt/learning.hpp"
#include <cassert>
#include <iostream>

using namespace opt;
using namespace opt::learning;

int main() {
    assert(canonical_family(R"(maps\00gem_fields.MAP26A)") == "00gem-fields");
    Actor actor;
    actor.weights[0] = 0.4;
    actor.weights[13] = -0.2;
    Observation observation;
    observation.features.assign(12, 0);
    observation.features[0] = 0.7;
    observation.features[1] = 0.4;
    observation.action = 2;
    observation.legal.assign(5, true);
    const double advantage = -0.7, entropy_bonus = 0.03;
    auto gradient = policy_loss_gradient(actor, observation, advantage, entropy_bonus);
    auto loss = [&](const Actor& x) {
        auto probabilities = x.probabilities(observation.features, observation.legal);
        return -advantage*std::log(probabilities[observation.action]) - entropy_bonus*entropy(probabilities);
    };
    for (std::size_t w : {std::size_t(0), std::size_t(12), std::size_t(13), std::size_t(26), std::size_t(64)}) {
        const double original = actor.weights[w], epsilon = 1e-5;
        actor.weights[w] = original + epsilon;
        const double plus = loss(actor);
        actor.weights[w] = original - epsilon;
        const double minus = loss(actor);
        actor.weights[w] = original;
        assert(std::abs((plus-minus)/(2*epsilon) - gradient[w]) < 1e-7);
    }
    auto probabilities = actor.probabilities(observation.features, {true,false,true,false,true});
    assert(probabilities[1] == 0 && probabilities[3] == 0);
    assert(std::abs(std::accumulate(probabilities.begin(), probabilities.end(), 0.0)-1) < 1e-12);
    actor.weights[12] = 100;
    auto clipped = policy_loss_gradient(actor, observation, 1);
    assert(clipped[12] == 0);
    Actor large;
    large.weights[12] = 10000;
    large.weights[25] = -10000;
    for (double p : large.probabilities(observation.features)) assert(std::isfinite(p));

    Actor learned;
    Adam adam(learned.weights.size());
    observation.action = 3;
    const double before = learned.probabilities(observation.features)[3];
    for (int i = 0; i < 30; ++i)
        adam.update(learned.weights, policy_loss_gradient(learned, observation, 1), 0.03);
    assert(learned.probabilities(observation.features)[3] > before + 0.3);
    Actor restored = Actor::from_json(learned.to_json());
    Adam restored_adam = Adam::from_json(adam.to_json());
    for (double& weight : learned.weights) weight = static_cast<float>(weight);
    auto g = policy_loss_gradient(learned, observation, 1);
    adam.update(learned.weights, g, 0.01);
    restored_adam.update(restored.weights, g, 0.01);
    assert(learned.weights == restored.weights);
    assert(adam.to_json() == restored_adam.to_json());

    Json obs{{"features", numbers(observation.features)}, {"action", 3}, {"actor", 1},
        {"round", 10}, {"decision", 1}, {"legal_mask", Json::array({true,true,true,true,true})},
        {"probabilities", numbers(learned.probabilities(observation.features))}};
    Json episode{{"trajectory_id", "case1"}, {"family", "map1"}, {"source_key", "frozen-key"},
        {"engine", "1.0.3"}, {"feature_schema", feature_schema}, {"subject_healthy", true},
        {"opponent_healthy", true}, {"complete", true}, {"logging_complete", true}, {"payoff", 1},
        {"observations", Json::array({obs})}};
    assert(parse_episode(episode, 12, 5, true).observations.size() == 1);
    episode["observations"].push_back(obs);
    bool duplicate_rejected = false;
    try { parse_episode(episode, 12, 5, true); } catch (...) { duplicate_rejected = true; }
    assert(duplicate_rejected);
    episode["observations"] = Json::array({obs});
    episode["observations"][0]["features"][6] = 0.5;
    bool leak_rejected = false;
    try { parse_episode(episode); } catch (...) { leak_rejected = true; }
    assert(leak_rejected);
    episode["observations"][0]["features"][6] = 0;
    episode["opponent_healthy"] = false;
    bool health_rejected = false;
    try { parse_episode(episode); } catch (...) { health_rejected = true; }
    assert(health_rejected);

    const fs::path artifact_test = ".cache/contenders/optimization/learning/artifact-test";
    const auto source = fs::absolute(artifact_test / "source.wasm");
    atomic_write(source, std::string("wasm\0fixture", 12));
    Json declared{{"artifacts", Json::array({Json{{"path", source.string()},
        {"sha256", file_sha256(source)}, {"size", fs::file_size(source)}, {"role", "raw_wasm"}}})}};
    const auto retained = preserve_native_artifacts(declared, artifact_test / "retained");
    assert(read_text(artifact_test / retained[0].at("path").get<std::string>()) == read_text(source));
    assert(preserve_native_artifacts(Json{{"artifacts", retained}}, artifact_test / "retained") == retained);
    atomic_write(source, "tampered");
    bool artifact_rejected = false;
    try { preserve_native_artifacts(declared, artifact_test / "retained"); } catch (...) { artifact_rejected = true; }
    assert(artifact_rejected);
    std::cout << "C5 gradient, clipping, Adam continuation, dataset and artifact guards passed\n";
}
