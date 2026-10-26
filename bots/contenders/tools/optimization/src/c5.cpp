#include "opt/learning.hpp"
#include <iostream>

namespace opt {
namespace {
using learning::Actor;
using learning::Episode;
using learning::Adam;

Json case_list(const Json& config, const std::string& name) {
    if (!config.contains(name)) return Json::array();
    Json cases = config.at(name);
    if (!cases.is_array()) throw std::runtime_error(name + " must be an array");
    for (const auto& item : cases.as_array()) {
        if (!item.is_object() || !item.contains("map") || !item.contains("side") || !item.contains("opponent"))
            throw std::runtime_error("each learning case needs map, side and opponent");
        const auto side = item.at("side").get<std::string>();
        if (side != "A" && side != "B") throw std::runtime_error("case side must be A or B");
    }
    return cases;
}

std::string family(const Json& item) {
    return learning::canonical_family(item.value("family", item.at("map").get<std::string>()));
}

void check_splits(const Json& train, const Json& validation) {
    std::set<std::string> families;
    for (const auto& item : train.as_array()) families.insert(family(item));
    for (const auto& item : validation.as_array())
        if (families.contains(family(item))) throw std::runtime_error("training/validation scenario-family leakage: " + family(item));
}

Json request_for(const Json& config, const Actor& actor, std::uint64_t seed, const Json& cases,
                 bool rollouts = true) {
    Json request = config.value("evaluation", Json::object());
    if (!request.is_object()) throw std::runtime_error("evaluation must be an object");
    request["schema"] = "contenders-eval/v1";
    request["kind"] = "policy";
    request["candidate"] = Json{{"policy", actor.to_json(seed, true, rollouts)}};
    request["cases"] = cases;
    request["context_cases"] = cases;
    request["collect_rollouts"] = rollouts;
    request["seed"] = seed;
    if (config.contains("baseline")) request["baseline"] = config.at("baseline");
    if (config.contains("controller")) request["controller"] = config.at("controller");
    else if (config.contains("base")) request["controller"] = config.at("base");
    return request;
}

Json invariant_context(const Json& response) {
    if (response.contains("identity")) {
        Json invariant = response.at("identity");
        invariant.as_object().erase("cases");
        return invariant;
    }
    if (response.contains("environment_identity")) return response.at("environment_identity");
    if (response.contains("context_identity")) return response.at("context_identity");
    throw std::runtime_error("evaluator omitted its frozen environment identity");
}

Json context_maps(const Json& response) {
    Json maps = Json::object();
    if (response.contains("identity") && response.at("identity").contains("cases"))
        for (const auto& item : response.at("identity").at("cases").as_array())
            maps[item.at("map").get<std::string>()] = item.at("map_sha256");
    return maps;
}

void verify_context(const Json& response, const Json& pinned) {
    if (invariant_context(response) != pinned.at("invariants"))
        throw std::runtime_error("native engine/controller/evaluator changed during C5 campaign; no gradient accepted");
    const Json maps = context_maps(response);
    for (const auto& [map, hash] : maps.as_object())
        if (!pinned.at("maps").contains(map) || pinned.at("maps").at(map) != hash)
            throw std::runtime_error("native map changed or was outside the frozen C5 split: " + map);
}

void verify_split_maps(const Json& left, const Json& right) {
    if (!left.contains("maps") || !right.contains("maps")) return;
    for (const auto& [left_map, left_hash] : left.at("maps").as_object())
        for (const auto& [right_map, right_hash] : right.at("maps").as_object())
            if (left_map == right_map || left_hash == right_hash)
                throw std::runtime_error("native split map-content leakage: " + left_map);
}

Json identity_for(const Options& options, const Json& config, const Actor& actor, const Json& cases,
                  const fs::path& work) {
    if (cases.empty()) return Json{{"empty", true}};
    Json request = request_for(config, actor, 1, cases, false);
    request["kind"] = "identity";
    request["target_kind"] = "policy";
    Json response = evaluate(options, request, work);
    return Json{{"invariants", invariant_context(response)}, {"maps", context_maps(response)},
        {"environment_identity", response.contains("environment_identity") ? response.at("environment_identity") : response.at("context_identity")}};
}

std::string candidate_key(const Json& response) {
    if (response.contains("candidate_identity") && response.at("candidate_identity").is_object())
        return response.at("candidate_identity").value("key", "");
    if (response.contains("subject_key")) return response.at("subject_key").get<std::string>();
    return "";
}

struct Evaluation {
    Json raw;
    std::vector<Episode> episodes;
    double score{};
    std::vector<double> paired_baselines;
    std::size_t decisions{}, rejected{};
    bool healthy{};
    Json errors = Json::array();
};

Evaluation train_result(Json response, const Actor& actor, bool require_rollouts,
                        bool require_paired = false, std::size_t expected_episodes = 0) {
    Evaluation out;
    out.raw = std::move(response);
    out.healthy = out.raw.value("healthy", false);
    if (!out.healthy) return out;
    if (!out.raw.contains("episodes")) throw std::runtime_error("native C5 evaluator returned no complete-game episodes");
    std::set<std::string> ids;
    double score = 0;
    for (const auto& value : out.raw.at("episodes").as_array()) {
        try {
            auto episode = learning::parse_episode(value, actor.features, actor.actions, require_rollouts, require_rollouts);
            double paired = 0;
            if (require_paired) {
                paired = value.at("baseline_payoff").get<double>();
                if (!std::isfinite(paired) || paired < -1 || paired > 1 ||
                    value.at("baseline_trajectory_id").get<std::string>().empty() ||
                    value.at("baseline_source_key").get<std::string>().empty() ||
                    !value.at("baseline_healthy").get<bool>())
                    throw std::runtime_error("paired baseline needs its actual healthy native terminal payoff and trajectory identity");
                const auto& provenance = value.at("baseline_provenance");
                const auto map_hash = provenance.at("map_sha256").get<std::string>();
                if (!provenance.at("action_independent").get<bool>() ||
                    provenance.at("side") != value.at("side") ||
                    provenance.at("opponent_key") != value.at("opponent") ||
                    provenance.at("engine") != value.at("engine") || map_hash.size() != 64 ||
                    map_hash.find_first_not_of("0123456789abcdef") != std::string::npos)
                    throw std::runtime_error("paired baseline provenance does not match the observed native episode");
            }
            if (!ids.insert(episode.id).second) throw std::runtime_error("duplicate trajectory in one evaluation");
            for (const auto& obs : episode.observations) {
                if (!require_rollouts) continue;
                const auto expected = actor.probabilities(obs.features, obs.legal);
                for (std::size_t a = 0; a < actor.actions; ++a)
                    if (std::abs(expected[a] - obs.probabilities[a]) > 0.002)
                        throw std::runtime_error("off-policy or incompatible native behavior probabilities");
            }
            out.decisions += episode.observations.size();
            score += episode.payoff;
            out.episodes.push_back(std::move(episode));
            out.paired_baselines.push_back(paired);
        } catch (const std::exception& error) { ++out.rejected; out.errors.push_back(error.what()); }
    }
    // An omitted/failed trajectory is not zero reward. Reject the complete gradient batch.
    if (expected_episodes && out.episodes.size() != expected_episodes) {
        out.errors.push_back("evaluator omitted or duplicated a requested complete-game episode");
        ++out.rejected;
    }
    if (out.rejected || out.episodes.empty() || (require_rollouts && !out.decisions)) {
        out.healthy = false;
        return out;
    }
    out.score = score / static_cast<double>(out.episodes.size());
    return out;
}

Actor initial_actor(const Json& config, const fs::path& input) {
    if (config.contains("initial_policy")) {
        Json value = config.at("initial_policy");
        if (value.is_string()) {
            fs::path path = value.get<std::string>();
            if (path.is_relative()) path = input.parent_path() / path;
            value = read_json(path);
        }
        return Actor::from_json(value);
    }
    Actor out;
    const double neutral_bias = config.value("initial_neutral_bias", 4.0);
    if (!std::isfinite(neutral_bias) || std::abs(neutral_bias) > 16)
        throw std::runtime_error("initial_neutral_bias must be finite in [-16,16]");
    out.weights[out.features] = neutral_bias;
    return out;
}

// Compare actual preference probabilities on fixed legal contexts, rather than
// raw weights (a common logit offset does not change the actor's decisions).
double policy_distance(const Actor& left, const Actor& right) {
    double total = 0;
    for (unsigned probe = 0; probe < 32; ++probe) {
        std::vector<double> x(12, 0);
        x[0] = (probe & 1) ? 0.85 : 0.15;
        x[1] = (probe & 2) ? 1.0 : 0.25;
        x[2] = (probe & 4) ? 0.8 : 0.0;
        x[3] = (probe & 8) ? 0.8 : 0.1;
        x[4] = (probe & 16) ? 0.5 : 0.0;
        x[5] = (probe & 2) ? 0.6 : 0.0;
        x[11] = (probe & 4) ? 0.75 : 0.1;
        const auto p = left.probabilities(x), q = right.probabilities(x);
        for (std::size_t a = 0; a < p.size(); ++a) total += std::abs(p[a] - q[a]);
    }
    return total / (32.0 * 5.0);
}

void export_actor(const fs::path& out, const Actor& actor, const Json& metadata,
                  std::uint64_t seed = 1, bool trace = false) {
    const Actor native_actor = Actor::from_json(actor.to_json());
    Json policy = native_actor.to_json(seed, true, trace);
    policy["training"] = metadata;
    write_json(out / "best-policy.json", policy);
    atomic_write(out / "best-policy.hpp", learning::policy_header(native_actor));
}

Json preserve_evaluation(const fs::path& out, const Json& response, const Json& request) {
    Json retained = learning::preserve_native_artifacts(response, out / "native-artifacts");
    Json evidence = response;
    evidence["durable_artifacts"] = retained;
    write_json(out / "best-evaluation.json", evidence);
    Json reproduction = request;
    if (response.contains("environment_identity")) reproduction["expected_context_identity"] = response.at("environment_identity");
    write_json(out / "best-reproduction-request.json", reproduction);
    return retained;
}
} // namespace

int run_c5(const Options& options) {
    Run run(options, "c5-native-episodic-policy-gradient", "2");
    const auto& cfg = run.config;
    const Json train_cases = case_list(cfg, cfg.contains("train_cases") ? "train_cases" : "cases");
    const Json validation_cases = case_list(cfg, "validation_cases");
    const Json heldout_cases = case_list(cfg, "heldout_cases");
    if (train_cases.empty()) throw std::runtime_error("C5 requires native train_cases or cases");
    check_splits(train_cases, validation_cases);
    check_splits(train_cases, heldout_cases);
    check_splits(validation_cases, heldout_cases);
    Actor actor = initial_actor(cfg, options.input);
    actor.max_decisions = cfg.value<std::size_t>("actor_max_decisions", actor.max_decisions);
    if (!actor.max_decisions || actor.max_decisions > 256) throw std::runtime_error("actor_max_decisions must be in [1,256]");
    const auto initial_actor_identity = sha256(actor.to_json().dump());
    if (actor.features != 12 || actor.actions != 5) throw std::runtime_error("native actor requires 12 features and 5 actions");
    Actor best = actor;
    Adam adam(actor.weights.size());
    double baseline = 0, best_score = -std::numeric_limits<double>::infinity();
    bool has_best = false, selected_validation = false;
    bool reference_evaluated = false, reference_healthy = false;
    Json reference = Json::object();
    std::uint64_t best_seed = 1;
    bool best_trace = false;
    std::uint64_t updates = 0, rejected_batches = 0, native_episodes = 0, decisions = 0, consecutive_rejections = 0;
    std::uint64_t archive_admissions = 0;
    Json archive = Json::array();
    const double rate = cfg.value("learning_rate", 0.01);
    const double decay = cfg.value("baseline_decay", 0.9);
    const double entropy_bonus = cfg.value("entropy_bonus", 0.0);
    const auto validation_every = cfg.value<std::uint64_t>("validation_every", 10);
    const auto archive_every = cfg.value<std::uint64_t>("archive_every", validation_every);
    const double archive_min_distance = cfg.value("archive_min_distance", 0.00001);
    const std::string baseline_mode = cfg.value("baseline_mode", "paired_native");
    const auto batch_cases = std::max<std::size_t>(1, cfg.value<std::size_t>("batch_cases", 2));
    const auto archive_limit = cfg.value<std::size_t>("population_max", 8);
    const double population_fraction = cfg.value("population_fraction", 0.25);
    const auto rejection_limit = cfg.value<std::uint64_t>("max_consecutive_rejections", 3);
    if (!(rate > 0 && rate <= 1) || !(decay >= 0 && decay < 1) || entropy_bonus < 0 ||
        !(population_fraction >= 0 && population_fraction <= 0.5) || archive_min_distance < 0 ||
        (baseline_mode != "paired_native" && baseline_mode != "past_return"))
        throw std::runtime_error("invalid C5 optimizer or population settings");
    if ((validation_every || archive_every) && validation_cases.empty())
        throw std::runtime_error("validation/archive milestones need disjoint validation_cases (or set both frequencies to 0)");

    if (run.resumed) {
        if (run.state.at("initial_actor_identity").get<std::string>() != initial_actor_identity)
            throw std::runtime_error("initial actor artifact changed; start a new C5 campaign");
        actor = Actor::from_json(run.state.at("actor"));
        best = Actor::from_json(run.state.at("best"));
        adam = Adam::from_json(run.state.at("adam"));
        baseline = run.state.at("baseline").get<double>();
        has_best = run.state.at("has_best").get<bool>();
        if (has_best) best_score = run.state.at("best_score").get<double>();
        selected_validation = run.state.at("selected_validation").get<bool>();
        reference_evaluated = run.state.at("reference_evaluated").get<bool>();
        reference_healthy = run.state.at("reference_healthy").get<bool>();
        reference = run.state.at("reference");
        archive_admissions = run.state.at("archive_admissions").get<std::uint64_t>();
        best_seed = run.state.value<std::uint64_t>("best_seed", 1);
        best_trace = run.state.value("best_trace", false);
        updates = run.state.at("updates").get<std::uint64_t>();
        rejected_batches = run.state.at("rejected_batches").get<std::uint64_t>();
        native_episodes = run.state.at("native_episodes").get<std::uint64_t>();
        decisions = run.state.at("decisions").get<std::uint64_t>();
        consecutive_rejections = run.state.value<std::uint64_t>("consecutive_rejections", 0);
        archive = run.state.at("population");
    }
    const Json train_identity = identity_for(options, cfg, actor, train_cases, options.out / "identity-train");
    const Json validation_identity = identity_for(options, cfg, actor, validation_cases, options.out / "identity-validation");
    const Json heldout_identity = identity_for(options, cfg, actor, heldout_cases, options.out / "identity-heldout");
    verify_split_maps(train_identity, validation_identity);
    verify_split_maps(train_identity, heldout_identity);
    verify_split_maps(validation_identity, heldout_identity);
    if (run.resumed && (run.state.at("train_environment") != train_identity ||
                        run.state.at("validation_environment") != validation_identity ||
                        run.state.at("heldout_environment") != heldout_identity))
        throw std::runtime_error("native environment changed; start a new C5 campaign");

    auto save = [&] {
        Json state{{"actor", actor.to_json()}, {"best", best.to_json()}, {"adam", adam.to_json()},
            {"baseline", baseline}, {"has_best", has_best}, {"best_score", has_best ? Json(best_score) : Json{}},
            {"selected_validation", selected_validation}, {"updates", updates}, {"rejected_batches", rejected_batches},
            {"reference_evaluated", reference_evaluated}, {"reference_healthy", reference_healthy},
            {"reference", reference}, {"archive_admissions", archive_admissions},
            {"best_seed", best_seed}, {"best_trace", best_trace},
            {"native_episodes", native_episodes}, {"decisions", decisions}, {"population", archive},
            {"consecutive_rejections", consecutive_rejections},
            {"initial_actor_identity", initial_actor_identity},
            {"train_environment", train_identity}, {"validation_environment", validation_identity},
            {"heldout_environment", heldout_identity}};
        run.checkpoint(state);
    };
    if (!run.resumed) save();

    auto admit = [&](const Evaluation& evaluated, std::uint64_t seed, bool initial, bool improved) {
        if (!archive_limit) return false;
        const auto key = candidate_key(evaluated.raw);
        if (key.empty()) return false;
        double nearest = std::numeric_limits<double>::infinity();
        for (const auto& member : archive.as_array()) {
            if (member.at("key").get<std::string>() == key) return false;
            nearest = std::min(nearest, policy_distance(actor, Actor::from_json(member.at("policy"))));
        }
        if (!initial && !improved && nearest < archive_min_distance) return false;
        const auto retained = learning::preserve_native_artifacts(evaluated.raw, options.out / "native-artifacts");
        Json member{{"key", key}, {"policy", actor.to_json(seed, true, false)},
            {"iteration", run.iteration}, {"validation_payoff", evaluated.score}, {"reference", initial},
            {"admission", initial ? "initial_reference" : improved ? "incumbent" : "periodic_behavioral_diversity"},
            {"durable_artifacts", retained}, {"identity", evaluated.raw.value("candidate_identity", Json::object())},
            {"controller", evaluated.raw.contains("identity") ? evaluated.raw.at("identity").at("controller").at("key") :
                Json(cfg.value("controller", cfg.value("base", "anchors/main-v42")))}};
        // Every admitted recipe remains durable even after its active league slot is
        // replaced. Keep the first reference and latest policy; space the remaining
        // policies by their behavior, independently of strict validation improvement.
        write_json(options.out / "population-history" / (sha256(key) + ".json"), member);
        archive.push_back(member);
        while (archive.size() > archive_limit) {
            std::size_t remove = 0;
            double closest = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i + 1 < archive.size(); ++i) {
                if (archive_limit > 1 && archive.at(i).value("reference", false)) continue;
                double separation = std::numeric_limits<double>::infinity();
                const auto candidate = Actor::from_json(archive.at(i).at("policy"));
                for (std::size_t j = 0; j < archive.size(); ++j)
                    if (i != j) separation = std::min(separation,
                        policy_distance(candidate, Actor::from_json(archive.at(j).at("policy"))));
                if (separation < closest) { closest = separation; remove = i; }
            }
            archive.as_array().erase(archive.as_array().begin() + static_cast<std::ptrdiff_t>(remove));
        }
        ++archive_admissions;
        run.event(Json{{"event", "population_admission"}, {"key", key}, {"iteration", run.iteration},
            {"reason", member.at("admission")}, {"population_size", archive.size()}});
        return true;
    };

    auto validate = [&](bool initial, bool archive_due) {
        const auto seed = cfg.value<std::uint64_t>("validation_seed", 1931);
        Json request = request_for(cfg, actor, seed, validation_cases, false);
        request["population_policies"] = archive;
        Json raw = evaluate(options, request,
            options.out / "evaluations" / (initial ? "reference" : "validation-" + std::to_string(run.iteration)));
        verify_context(raw, validation_identity);
        Evaluation evaluated = train_result(std::move(raw), actor, false, false, validation_cases.size());
        if (initial) {
            reference_evaluated = true;
            reference_healthy = evaluated.healthy;
        }
        if (!evaluated.healthy) {
            run.event(Json{{"event", initial ? "rejected_reference" : "rejected_validation"},
                {"iteration", run.iteration}, {"errors", evaluated.errors}});
            return false;
        }
        if (initial) {
            reference = Json{{"payoff", evaluated.score}, {"seed", seed}, {"key", candidate_key(evaluated.raw)},
                {"environment", validation_identity}, {"cases", validation_cases.size()}};
            if (evaluated.raw.contains("paired_objective")) {
                reference["paired_payoff"] = evaluated.raw.at("paired_objective");
                reference["fixed_controller_payoff"] = evaluated.score - evaluated.raw.at("paired_objective").get<double>();
            }
            write_json(options.out / "initial-reference.json", reference);
            Json evidence = evaluated.raw;
            evidence["durable_artifacts"] = learning::preserve_native_artifacts(evaluated.raw, options.out / "native-artifacts");
            write_json(options.out / "initial-reference-evaluation.json", evidence);
        }
        const bool improved = !selected_validation || !has_best || evaluated.score > best_score;
        if (improved) {
            best = actor; best_score = evaluated.score; has_best = true; selected_validation = true;
            best_seed = seed; best_trace = false;
            preserve_evaluation(options.out, evaluated.raw, request);
            export_actor(options.out, best, Json{{"selection", "fixed disjoint-family validation"},
                {"iteration", run.iteration}, {"score", best_score}, {"environment", validation_identity},
                {"initial_reference", reference}}, best_seed, best_trace);
        }
        if (initial || archive_due || improved) admit(evaluated, seed, initial, improved);
        run.progress(Json{{"validation_payoff", evaluated.score}, {"selection_score", best_score},
            {"initial_reference", initial}, {"population_size", archive.size()}, {"iteration", run.iteration},
            {"gain_over_initial", reference.contains("payoff") ? evaluated.score - reference.at("payoff").get<double>() : 0}});
        return true;
    };

    if (!reference_evaluated && !validation_cases.empty() && !run.stop()) {
        validate(true, true);
        save();
    }

    while (!run.stop() && (!reference_evaluated || reference_healthy)) {
        for (double& weight : actor.weights) weight = static_cast<float>(weight);
        Json batch = Json::array();
        std::vector<std::size_t> order(train_cases.size());
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), run.rng);
        for (std::size_t i = 0; i < std::min(batch_cases, order.size()); ++i) {
            Json item = train_cases.at(order[i]);
            if (!archive.empty() && std::generate_canonical<double, 53>(run.rng) < population_fraction) {
                const auto index = static_cast<std::size_t>(run.rng() % archive.size());
                item["opponent"] = archive.at(index).at("key");
                item["population_opponent"] = true;
            }
            batch.push_back(item);
        }
        const auto seed = run.rng() & 0x7fffffffULL;
        const auto work = options.out / "evaluations" / ("train-" + std::to_string(run.iteration));
        Json training_request = request_for(cfg, actor, seed, batch);
        training_request["population_policies"] = archive;
        Json raw = evaluate(options, training_request, work);
        verify_context(raw, train_identity);
        Evaluation evaluated = train_result(std::move(raw), actor, true, baseline_mode == "paired_native", batch.size());
        if (evaluated.healthy) {
            consecutive_rejections = 0;
            if (!selected_validation && (!has_best || evaluated.score > best_score)) {
                best = actor; best_score = evaluated.score; has_best = true;
                best_seed = seed; best_trace = true;
                preserve_evaluation(options.out, evaluated.raw, training_request);
                export_actor(options.out, best, Json{{"selection", "training; no validation milestone yet"},
                    {"iteration", run.iteration}, {"score", best_score}}, best_seed, best_trace);
            }
            std::vector<double> gradient(actor.weights.size(), 0);
            double total_entropy = 0, mean_advantage = 0, advantage_square = 0;
            for (std::size_t e = 0; e < evaluated.episodes.size(); ++e) {
                const auto& episode = evaluated.episodes[e];
                // A separate fixed controller's terminal payoff does not depend on
                // this actor's sampled actions. Subtracting it is a control variate,
                // not a counterfactual label or a change to the win/draw/loss target.
                const double control = baseline_mode == "paired_native" ? evaluated.paired_baselines[e] : baseline;
                const double advantage = episode.payoff - control;
                mean_advantage += advantage;
                advantage_square += advantage * advantage;
                for (const auto& observation : episode.observations) {
                    const auto g = learning::policy_loss_gradient(actor, observation, advantage, entropy_bonus);
                    for (std::size_t w = 0; w < g.size(); ++w) gradient[w] += g[w];
                    total_entropy += learning::entropy(observation.probabilities);
                }
            }
            for (double& g : gradient) g /= static_cast<double>(evaluated.episodes.size());
            adam.update(actor.weights, gradient, rate, cfg.value("gradient_norm", 5.0), 10.0);
            baseline = decay*baseline + (1-decay)*evaluated.score;
            ++updates;
            native_episodes += evaluated.episodes.size();
            decisions += evaluated.decisions;
            run.progress(Json{{"training_payoff", evaluated.score}, {"baseline", baseline},
                {"episodes", evaluated.episodes.size()}, {"decisions", evaluated.decisions},
                {"entropy", total_entropy / static_cast<double>(evaluated.decisions)}, {"updates", updates},
                {"advantage_mean", mean_advantage / static_cast<double>(evaluated.episodes.size())},
                {"advantage_rms", std::sqrt(advantage_square / static_cast<double>(evaluated.episodes.size()))}});
        } else {
            ++rejected_batches;
            ++consecutive_rejections;
            run.event(Json{{"event", "rejected_gradient_batch"}, {"iteration", run.iteration},
                {"rejected_episodes", evaluated.rejected}, {"errors", evaluated.errors},
                {"reason", "health, incomplete trace or probability/schema mismatch"}});
        }
        run.advance();

        const bool archive_due = archive_every && run.iteration % archive_every == 0;
        const bool validation_due = validation_every && run.iteration % validation_every == 0;
        if (!validation_cases.empty() && (archive_due || validation_due))
            validate(false, archive_due);
        save();
        if (rejection_limit && consecutive_rejections >= rejection_limit) {
            run.event(Json{{"event", "stopped_repeated_invalid_batches"}, {"consecutive_rejections", consecutive_rejections}});
            break;
        }
    }
    if (!has_best) {
        best = actor;
        export_actor(options.out, best, Json{{"selection", "no accepted native learning batch; initial policy only"}});
    }
    write_json(options.out / "population.json", archive);
    write_json(options.out / "current-policy.json", actor.to_json(1, true, false));
    Json heldout{{"status", heldout_cases.empty() ? "not_configured" : "not_run_before_target_or_without_validation_selection"},
        {"purpose", "report only; never selects weights, incumbent or population"}};
    bool failed_heldout = false;
    if (!heldout_cases.empty() && selected_validation && !interrupted() && run.iteration >= options.iterations) {
        Json request = request_for(cfg, best, best_seed, heldout_cases, false);
        request["population_policies"] = archive;
        Json raw = evaluate(options, request, options.out / "evaluations" / ("heldout-" + std::to_string(run.iteration)));
        verify_context(raw, heldout_identity);
        const auto evaluated = train_result(raw, best, false, false, heldout_cases.size());
        raw["durable_artifacts"] = learning::preserve_native_artifacts(raw, options.out / "native-artifacts");
        write_json(options.out / "heldout-evaluation.json", raw);
        request["expected_context_identity"] = raw.at("environment_identity");
        write_json(options.out / "heldout-reproduction-request.json", request);
        heldout["status"] = evaluated.healthy ? "healthy_complete" : "failed_health_or_completeness";
        heldout["payoff"] = evaluated.score;
        heldout["cases"] = heldout_cases.size();
        heldout["policy_key"] = candidate_key(raw);
        heldout["evidence"] = "heldout-evaluation.json";
        if (raw.contains("paired_objective")) heldout["paired_payoff"] = raw.at("paired_objective");
        failed_heldout = !evaluated.healthy;
    }
    const bool failed_reference = reference_evaluated && !reference_healthy;
    Json result{{"engine", "C5"}, {"algorithm", "episodic shared-actor REINFORCE with action-independent control variate and Adam"},
        {"advantage_baseline", baseline_mode}, {"initial_reference", reference},
        {"heldout", heldout},
        {"gain_over_initial_reference", has_best && reference.contains("payoff") ? Json(best_score - reference.at("payoff").get<double>()) : Json{}},
        {"iterations", run.iteration}, {"updates", updates}, {"native_episodes", native_episodes},
        {"decisions", decisions}, {"rejected_batches", rejected_batches}, {"population_size", archive.size()},
        {"archive_admissions", archive_admissions}, {"archive_every", archive_every},
        {"population_diversity", "mean absolute preference-probability difference on 32 fixed legal personal contexts; preserve initial/latest, prune nearest redundant history"},
        {"status", failed_reference ? "stopped_invalid_reference" : failed_heldout ? "failed_heldout_health" : rejection_limit && consecutive_rejections >= rejection_limit ? "stopped_invalid_evaluation" : "completed_or_checkpointed"},
        {"best_score", has_best ? Json(best_score) : Json{}}, {"validated_selection", selected_validation},
        {"best_seed", best_seed}, {"best_trace", best_trace},
        {"objective", entropy_bonus == 0 ? "engine terminal payoff" : "terminal payoff plus explicit entropy regularization"},
        {"entropy_bonus", entropy_bonus}, {"policy", "best-policy.json"}, {"header", "best-policy.hpp"},
        {"native_evidence", has_best ? "best-evaluation.json, best-reproduction-request.json and verified native-artifacts/ source, WASM and games" :
            "no accepted native evaluation; initial policy only"},
        {"scope", "native hierarchy: 5 job-score preferences on eligible planner turns; 7 active personal features in 12-slot schema; not primitive champion movement/attack/ability learning or recurrent MAPPO"},
        {"baseline_semantics", "paired_native subtracts a separate same-case healthy fixed-controller terminal payoff; past_return subtracts the previous batches' scalar mean. Both are action-independent variance reduction, never counterfactual action labels."},
        {"qualification", "training/validation only; untraced deployment and independent complete-game qualification remain separate"},
        {"resume", "population, weights, Adam, baseline, RNG and native environment identities retained; unfinished evaluator calls are retried through exact-key cache"}};
    run.finish(result);
    std::cout << "C5: " << updates << " accepted updates, " << native_episodes << " episodes, best-policy.json exported\n";
    return failed_reference || failed_heldout || (rejection_limit && consecutive_rejections >= rejection_limit) ? 2 : 0;
}
} // namespace opt
