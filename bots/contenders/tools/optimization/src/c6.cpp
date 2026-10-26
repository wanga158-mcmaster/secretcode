#include "opt/learning.hpp"
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace opt {
namespace {
using learning::Actor;
using learning::Adam;
using learning::Episode;
using learning::Observation;

struct C6Dataset {
    std::vector<Episode> episodes;
    Json raw{Json::array()}, files{Json::array()};
    std::map<std::string, std::string> family_split;
    std::vector<std::size_t> train, validation, test;
    std::size_t duplicates{}, decisions{};
    std::string digest, split_digest;
};

fs::path c6_path(const fs::path& name, const Options& options) {
    if (name.is_absolute()) return name;
    const fs::path relative = options.input.parent_path() / name;
    if (fs::exists(relative)) return relative;
    if (!options.repo.empty() && fs::exists(options.repo / name)) return options.repo / name;
    return name;
}

void c6_append_document(const Json& doc, Json& episodes) {
    if (doc.is_array()) for (const auto& e : doc.as_array()) episodes.push_back(e);
    else if (doc.is_object() && doc.contains("episodes")) {
        if (doc.contains("information_safe") && !doc.at("information_safe").get<bool>())
            throw std::runtime_error("C6 refuses an explicitly information-unsafe dataset");
        for (const auto& e : doc.at("episodes").as_array()) episodes.push_back(e);
    } else if (doc.is_object() && doc.contains("trajectory_id")) episodes.push_back(doc);
    else throw std::runtime_error("C6 dataset must contain episode records, not unbound state labels");
}

void c6_load_file(const fs::path& file, Json& episodes, Json& files) {
    const std::string bytes = read_text(file);
    files.push_back(Json{{"path", fs::absolute(file).generic_string()}, {"sha256", sha256(bytes)}});
    if (file.extension() == ".jsonl") {
        std::istringstream lines(bytes);
        std::string line;
        std::size_t number = 0;
        while (std::getline(lines, line)) {
            ++number;
            if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
            try { c6_append_document(Json::parse(line), episodes); }
            catch (const std::exception& e) { throw std::runtime_error("C6 JSONL line " + std::to_string(number) + ": " + e.what()); }
        }
    } else c6_append_document(Json::parse(bytes), episodes);
}

C6Dataset c6_dataset(Json raw, const Json& config, std::uint64_t seed, Json files = Json::array()) {
    C6Dataset out;
    out.files = std::move(files);
    std::map<std::string, std::string> seen, explicit_family;
    std::map<std::string, Json> canonical;
    const std::size_t limit = config.value<std::size_t>("max_decisions", 2000000);
    if (!limit) throw std::runtime_error("C6 max_decisions must be positive");
    for (const auto& record : raw.as_array()) {
        Episode episode = learning::parse_episode(record, 12, 5, false);
        if (episode.source_key.empty() || episode.engine.empty())
            throw std::runtime_error("C6 needs source_key and engine provenance for every trajectory");
        if (episode.observations.empty()) throw std::runtime_error("C6 trajectory contains no actor decisions");
        const std::string body = record.dump();
        const std::string hash = sha256(body);
        auto duplicate = seen.find(episode.id);
        if (duplicate != seen.end()) {
            if (duplicate->second != hash) throw std::runtime_error("C6 conflicting duplicate trajectory: " + episode.id);
            ++out.duplicates;
            continue;
        }
        seen.emplace(episode.id, hash);
        canonical.emplace(episode.id, record);
        if (!episode.split.empty()) {
            if (episode.split != "train" && episode.split != "validation" && episode.split != "test")
                throw std::runtime_error("C6 split must be train, validation or test");
            auto [at, fresh] = explicit_family.emplace(episode.family, episode.split);
            if (!fresh && at->second != episode.split) throw std::runtime_error("C6 family leaks across explicit splits: " + episode.family);
        }
        out.decisions += episode.observations.size();
        if (out.decisions > limit) throw std::runtime_error("C6 max_decisions exceeded; partition whole families into separate campaigns");
        out.episodes.push_back(std::move(episode));
    }
    if (out.episodes.empty()) throw std::runtime_error("C6 has no learning trajectories");
    std::sort(out.episodes.begin(), out.episodes.end(), [](const Episode& a, const Episode& b) { return a.id < b.id; });
    for (const auto& [id, record] : canonical) out.raw.push_back(record);
    out.digest = sha256(out.raw.dump());
    std::set<std::string> families;
    for (const auto& e : out.episodes) families.insert(e.family);
    if (families.size() < 3) throw std::runtime_error("C6 requires at least three whole families for train/validation/test");
    if (config.contains("family_splits")) {
        for (const auto& [supplied_family, split] : config.at("family_splits").as_object()) {
            const auto family = families.contains(supplied_family) ? supplied_family : learning::canonical_family(supplied_family);
            const std::string role = split.get<std::string>();
            if (!families.contains(family)) throw std::runtime_error("C6 configured split names absent family: " + family);
            if (role != "train" && role != "validation" && role != "test") throw std::runtime_error("invalid C6 family split");
            if (explicit_family.contains(family) && explicit_family[family] != role)
                throw std::runtime_error("C6 config contradicts trajectory family split");
            explicit_family[family] = role;
        }
    }
    if (!explicit_family.empty()) {
        if (explicit_family.size() != families.size()) throw std::runtime_error("C6 explicit family splits must cover every family");
        out.family_split = explicit_family;
    } else {
        std::vector<std::string> ranked(families.begin(), families.end());
        std::sort(ranked.begin(), ranked.end(), [seed](const std::string& a, const std::string& b) {
            return sha256(std::to_string(seed) + ":" + a) < sha256(std::to_string(seed) + ":" + b);
        });
        const double vf = config.value("validation_fraction", 0.2), tf = config.value("test_fraction", 0.2);
        if (!(vf > 0 && tf > 0 && vf + tf < 1)) throw std::runtime_error("C6 split fractions must be positive and sum below one");
        std::size_t vn = std::max<std::size_t>(1, static_cast<std::size_t>(ranked.size() * vf));
        std::size_t tn = std::max<std::size_t>(1, static_cast<std::size_t>(ranked.size() * tf));
        while (vn + tn >= ranked.size()) { if (vn >= tn && vn > 1) --vn; else if (tn > 1) --tn; else break; }
        for (std::size_t i = 0; i < ranked.size(); ++i)
            out.family_split[ranked[i]] = i < vn ? "validation" : i < vn + tn ? "test" : "train";
    }
    Json roles = Json::object();
    for (const auto& [family, split] : out.family_split) roles[family] = split;
    out.split_digest = sha256(roles.dump());
    for (std::size_t i = 0; i < out.episodes.size(); ++i) {
        const auto& split = out.family_split.at(out.episodes[i].family);
        (split == "train" ? out.train : split == "validation" ? out.validation : out.test).push_back(i);
    }
    if (out.train.empty() || out.validation.empty() || out.test.empty()) throw std::runtime_error("C6 needs nonempty train, validation and test families");
    return out;
}

struct C6Metrics {
    double cross_entropy{}, accuracy{}, value_mse{};
    std::size_t episodes{}, decisions{};
    Json json() const { return Json{{"cross_entropy", cross_entropy}, {"accuracy", accuracy}, {"value_mse", value_mse},
        {"episodes", episodes}, {"decisions", decisions}, {"weighting", "equal trajectory weight"}}; }
};

double c6_value(const std::vector<double>& w, const std::vector<double>& x) {
    double value = w.back();
    for (std::size_t i = 0; i < x.size(); ++i) value += w[i] * x[i];
    return std::tanh(value);
}

C6Metrics c6_metrics(const Actor& actor, const std::vector<double>& value, const C6Dataset& data,
                     const std::vector<std::size_t>& indices) {
    C6Metrics out;
    for (const auto index : indices) {
        const auto& e = data.episodes.at(index);
        double loss = 0, correct = 0, mse = 0;
        for (const auto& o : e.observations) {
            const auto p = actor.probabilities(o.features, o.legal);
            loss -= std::log(std::max(1e-15, p[o.action]));
            correct += std::distance(p.begin(), std::max_element(p.begin(), p.end())) == o.action;
            const double error = c6_value(value, o.features) - e.payoff;
            mse += error * error;
        }
        const double n = static_cast<double>(e.observations.size());
        out.cross_entropy += loss/n; out.accuracy += correct/n; out.value_mse += mse/n;
        ++out.episodes; out.decisions += e.observations.size();
    }
    if (out.episodes) { out.cross_entropy /= out.episodes; out.accuracy /= out.episodes; out.value_mse /= out.episodes; }
    return out;
}

Json c6_actor_state(const Actor& actor) {
    return Json{{"weights", learning::numbers(actor.weights)}, {"logit_clip", actor.logit_clip}, {"max_decisions", actor.max_decisions}};
}
Actor c6_actor_restore(const Json& state) {
    Actor out;
    out.weights = learning::read_numbers(state.at("weights"));
    if (out.weights.size() != 65) throw std::runtime_error("C6 actor checkpoint dimensions differ");
    out.logit_clip = state.at("logit_clip").get<double>();
    out.max_decisions = state.value("max_decisions", 128);
    if (out.logit_clip != 20.0) throw std::runtime_error("C6 native actor requires logit clip 20");
    if (out.max_decisions < 1 || out.max_decisions > 256) throw std::runtime_error("C6 actor decision cap differs from runtime domain");
    return out;
}

void c6_update(Actor& actor, std::vector<double>& value, Adam& actor_adam, Adam& value_adam,
               const C6Dataset& data, std::mt19937_64& rng, std::size_t batch, double rate,
               double value_rate, double l2, bool fit_actor = true) {
    if (!batch) throw std::runtime_error("C6 batch_size must be positive");
    std::vector<double> ga(actor.weights.size(), 0), gv(value.size(), 0);
    std::uniform_int_distribution<std::size_t> episode_pick(0, data.train.size() - 1);
    for (std::size_t b = 0; b < batch; ++b) {
        const auto& e = data.episodes[data.train[episode_pick(rng)]];
        std::uniform_int_distribution<std::size_t> decision_pick(0, e.observations.size() - 1);
        Observation o = e.observations[decision_pick(rng)];
        o.probabilities.clear(); // Teacher probabilities are not the learner's current distribution.
        if (fit_actor) {
            const auto g = learning::policy_loss_gradient(actor, o, 1.0, 0.0);
            for (std::size_t i = 0; i < ga.size(); ++i) ga[i] += g[i] / batch;
        }
        const double prediction = c6_value(value, o.features);
        const double delta = (prediction - e.payoff) * (1 - prediction*prediction) / batch;
        for (std::size_t f = 0; f < o.features.size(); ++f) gv[f] += delta * o.features[f];
        gv.back() += delta;
    }
    for (std::size_t i = 0; i < ga.size(); ++i) ga[i] += l2 * actor.weights[i];
    for (std::size_t i = 0; i < gv.size(); ++i) gv[i] += l2 * value[i];
    if (fit_actor) actor_adam.update(actor.weights, ga, rate, 5, 10);
    value_adam.update(value, gv, value_rate, 5, 20);
}

Actor c6_reference_teacher() {
    Actor out;
    auto set = [&](std::size_t a, std::size_t f, double w) { out.weights[a * 13 + f] = w; };
    set(1, 12, 1); set(1, 0, 0.8); set(1, 2, -1.5);
    set(2, 12, 1.25); set(2, 0, -2); set(2, 3, -0.8);
    set(3, 12, 0.1); set(3, 1, -0.5); set(3, 4, 1.5); set(3, 5, -0.5);
    set(4, 12, -0.5); set(4, 3, 3); set(4, 2, 2); set(4, 1, -1); set(4, 11, 1);
    return out;
}

std::string c6_family(const Json& item) {
    return learning::canonical_family(item.value("family", item.at("map").get<std::string>()));
}

Json c6_request(const Json& config, const Json& policy, const Json& cases, bool rollouts) {
    Json request{{"schema", "contenders-eval/v1"}, {"kind", "policy"}, {"target_kind", "policy"},
        {"collect_rollouts", rollouts}, {"cases", cases}, {"candidate", Json{{"policy", policy}}}};
    if (config.contains("controller")) request["controller"] = config.at("controller");
    else if (config.contains("base")) request["controller"] = config.at("base");
    if (config.contains("baseline")) request["baseline"] = config.at("baseline");
    if (config.contains("evaluation")) request["evaluation"] = config.at("evaluation");
    if (config.contains("qualification_cases")) {
        Json context = cases;
        std::set<std::string> seen;
        for (const auto& item : cases.as_array()) seen.insert(item.dump());
        for (const auto& list : {config.at("cases"), config.at("qualification_cases")})
            for (const auto& item : list.as_array()) if (seen.insert(item.dump()).second) context.push_back(item);
        request["context_cases"] = context;
    }
    return request;
}

Json c6_policy(const Json& supplied, const Options& options) {
    Json given = supplied;
    if (given.is_object() && given.contains("policy")) given = given.at("policy");
    if (given.is_string()) given = read_json(c6_path(given.get<std::string>(), options));
    Actor teacher = Actor::from_json(given);
    return teacher.to_json(given.value<std::uint64_t>("seed", options.seed), given.value("stochastic", false), true);
}

Json c6_select_teacher(const Options& options, const Json& config, Run& run) {
    const Json& cases = config.at("qualification_cases");
    if (cases.empty()) throw std::runtime_error("C6 qualification_cases must not be empty");
    std::set<std::string> collection_families, selection_families;
    for (const auto& item : config.at("cases").as_array()) collection_families.insert(c6_family(item));
    for (const auto& item : cases.as_array()) {
        const auto family = c6_family(item);
        if (collection_families.contains(family)) throw std::runtime_error("C6 teacher selection leaks into fitted/held-out family: " + family);
        selection_families.insert(family);
    }
    const auto minimum = config.value<std::size_t>("qualification_min_families", 4);
    if (minimum < 4 || selection_families.size() < minimum)
        throw std::runtime_error("C6 teacher qualification requires at least four separate selection families");
    const double gain = config.value("qualification_min_gain", 0.0);
    if (!std::isfinite(gain) || gain < 0 || gain > 2) throw std::runtime_error("invalid C6 qualification_min_gain");
    const auto file = options.out / "teacher-selection.json";
    if (run.resumed && fs::is_regular_file(file)) {
        const Json selection = read_json(file);
        if (selection.value("schema", "") != "contenders-teacher-selection/v1" ||
            run.state.value("teacher_selection_digest", "") != sha256(selection.dump()))
            throw std::runtime_error("C6 teacher selection changed on resume");
        learning::preserve_native_artifacts(Json{{"artifacts", selection.at("durable_artifacts")}}, options.out / "native-artifacts");
        return selection;
    }
    Json candidates = Json::array(), rows = Json::array(), retained = Json::array();
    if (config.contains("teacher_policy")) candidates.push_back(c6_policy(config.at("teacher_policy"), options));
    if (!config.contains("teacher_policy") || config.value("include_reference_teacher", true))
        candidates.push_back(c6_reference_teacher().to_json(options.seed, false, true));
    if (config.contains("teacher_candidates")) for (const auto& item : config.at("teacher_candidates").as_array())
        candidates.push_back(c6_policy(item, options));
    std::set<std::string> candidate_digests;
    Json unique = Json::array();
    for (const auto& policy : candidates.as_array()) if (candidate_digests.insert(sha256(policy.dump())).second) unique.push_back(policy);
    Json selected = Actor().to_json(options.seed, false, true), environment;
    double best_bound = gain;
    bool qualified = false;
    for (std::size_t index = 0; index < unique.size(); ++index) {
        Json response = evaluate(options, c6_request(config, unique[index], cases, false), options.out / "teacher-qualification" / std::to_string(index));
        if (!response.contains("environment_identity")) throw std::runtime_error("C6 qualifier omitted native environment identity");
        if (environment.is_null()) environment = response.at("environment_identity");
        else if (environment != response.at("environment_identity")) throw std::runtime_error("C6 teacher qualifier environment changed");
        Json row{{"policy_digest", sha256(unique[index].dump())}, {"healthy", response.value("healthy", false)}, {"qualified", false}};
        if (response.value("healthy", false)) {
            const Json artifacts = learning::preserve_native_artifacts(response, options.out / "native-artifacts");
            for (const auto& item : artifacts.as_array()) retained.push_back(item);
            response["durable_artifacts"] = artifacts;
            if (!response.contains("cases") || response.at("cases").size() != cases.size())
                throw std::runtime_error("C6 teacher qualifier returned incomplete paired cases");
            std::map<std::string, std::vector<double>> clusters;
            std::set<std::string> keys;
            for (const auto& item : response.at("cases").as_array()) {
                const auto key = item.at("map").get<std::string>() + ":" + item.at("side").get<std::string>() + ":" + item.value("requested_opponent", "");
                if (!keys.insert(key).second || !item.value("complete", false)) throw std::runtime_error("C6 teacher qualifier repeated or incomplete case");
                const double payoff = item.at("payoff").get<double>(), baseline = item.at("baseline_payoff").get<double>();
                if (!std::isfinite(payoff) || !std::isfinite(baseline) || std::abs(payoff) > 1 || std::abs(baseline) > 1)
                    throw std::runtime_error("C6 teacher qualifier has invalid terminal payoff");
                clusters[c6_family(item)].push_back(payoff - baseline);
            }
            if (clusters.size() != selection_families.size()) throw std::runtime_error("C6 qualifier family coverage differs");
            std::vector<double> differences;
            for (const auto& [family, values] : clusters) {
                if (!selection_families.contains(family)) throw std::runtime_error("C6 qualifier returned an unexpected family");
                differences.push_back(std::accumulate(values.begin(), values.end(), 0.0) / values.size());
            }
            const double mean = std::accumulate(differences.begin(), differences.end(), 0.0) / differences.size();
            double variance = 0;
            for (double x : differences) variance += (x - mean) * (x - mean);
            const double se = std::sqrt(variance / (differences.size() * (differences.size() - 1)));
            const double critical[] = {0, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228};
            const double t = differences.size() <= 11 ? critical[differences.size() - 1] : 2.228;
            const double correction = std::sqrt(std::log(40.0 * unique.size()) / std::log(40.0));
            const double bound = mean - t * correction * se;
            const bool accepted = bound > gain;
            row["families"] = clusters.size(); row["paired_mean_gain"] = mean; row["family_standard_error"] = se;
            row["lower_bound"] = bound; row["qualified"] = accepted;
            if (accepted && bound > best_bound) { best_bound = bound; selected = unique[index]; qualified = true; }
        }
        write_json(options.out / "teacher-qualification" / (std::to_string(index) + ".json"), response);
        rows.push_back(row);
    }
    Json selection{{"schema", "contenders-teacher-selection/v1"}, {"selected_policy", selected}, {"qualified", qualified},
        {"status", qualified ? "qualified on frozen selection families" : "no qualified expert found; neutral actor baseline fallback"},
        {"environment_identity", environment}, {"candidates", rows}, {"durable_artifacts", retained},
        {"criterion", "positive family-cluster paired terminal-payoff lower screen versus native baseline; Student-t approximation with heuristic candidate-count penalty, not a confidence guarantee"},
        {"scope", "selection families separate from fitting/validation/test; limited macroactor qualification, not general mastery or action-level expertise"}};
    write_json(file, selection);
    return selection;
}

std::string c6_value_header(const std::vector<double>& weights) {
    std::string out = "#pragma once\n#include <array>\n#include <cmath>\nnamespace learned_value {\n";
    out += "inline constexpr double weights[13]={";
    for (std::size_t i = 0; i < weights.size(); ++i) { if (i) out += ','; out += Json(weights[i]).dump(); }
    out += "};\ninline double predict(const std::array<double,12>& x){double z=weights[12];for(int i=0;i<12;++i)z+=weights[i]*x[i];return std::tanh(z);}\n}\n";
    return out;
}

std::vector<double> c6_constant_value(const C6Dataset& data) {
    double mean = 0;
    for (auto index : data.train) mean += data.episodes[index].payoff / data.train.size();
    std::vector<double> weights(13, 0);
    weights.back() = std::atanh(std::clamp(mean, -0.999999, 0.999999));
    return weights;
}

std::vector<double> c6_time_value(const C6Dataset& data, const std::vector<double>& constant) {
    auto weights = constant;
    // Fixed deterministic train-only baseline fit; no held-out labels or run RNG.
    for (int step = 0; step < 500; ++step) {
        double round_gradient = 0, bias_gradient = 0;
        for (auto index : data.train) {
            const auto& episode = data.episodes[index];
            const double scale = 1.0 / (data.train.size() * episode.observations.size());
            for (const auto& observation : episode.observations) {
                const double p = c6_value(weights, observation.features);
                const double delta = (p - episode.payoff) * (1 - p*p) * scale;
                round_gradient += delta * observation.features[0];
                bias_gradient += delta;
            }
        }
        weights[0] -= 0.2 * round_gradient;
        weights[12] -= 0.2 * bias_gradient;
    }
    return weights;
}

struct C6ValueChoice {
    std::vector<double> weights;
    std::string method;
    double constant_mse{}, neutral_mse{}, round_mse{}, context_mse{};
};

C6ValueChoice c6_choose_value(const Actor& actor, const C6Dataset& data, const std::vector<double>& context,
                             const std::vector<double>& constant, const std::vector<double>& round, double margin) {
    C6ValueChoice choice{constant, "train_mean_constant"};
    choice.constant_mse = c6_metrics(actor, constant, data, data.validation).value_mse;
    const std::vector<double> neutral(13, 0);
    choice.neutral_mse = c6_metrics(actor, neutral, data, data.validation).value_mse;
    choice.round_mse = c6_metrics(actor, round, data, data.validation).value_mse;
    choice.context_mse = c6_metrics(actor, context, data, data.validation).value_mse;
    if (choice.neutral_mse < choice.constant_mse) { choice.weights = neutral; choice.method = "neutral_constant"; }
    if (choice.round_mse + margin < std::min(choice.constant_mse, choice.neutral_mse)) { choice.weights = round; choice.method = "round_only"; }
    if (choice.context_mse + margin < std::min({choice.constant_mse, choice.neutral_mse, choice.round_mse})) {
        choice.weights = context; choice.method = "linear_actor_context";
    }
    return choice;
}

Json c6_value_document(const std::vector<double>& weights, const std::string& method) {
    return Json{{"schema", "contenders-value/v1"}, {"feature_schema", learning::feature_schema},
        {"feature_names", learning::strings(learning::feature_names)}, {"weights", learning::numbers(weights)},
        {"activation", "tanh"}, {"method", method},
        {"target", "recorded terminal payoff under the collected policy, opponent and observation distribution"},
        {"scope", "observation value only; no changed-action or counterfactual ranking labels"}};
}

Json c6_fixture() {
    Json episodes = Json::array();
    for (int family = 0; family < 9; ++family) {
        Json observations = Json::array();
        for (int d = 0; d < 30; ++d) {
            const double x = (d % 10) / 9.0;
            std::vector<double> f(12, 0); f[0] = x; f[1] = 1-x; f[2] = (d % 3) / 2.0;
            observations.push_back(Json{{"features", learning::numbers(f)}, {"action", x > 0.5 ? 1 : 2},
                {"actor", 0}, {"round", d}, {"decision", d+1},
                {"legal_mask", Json::array({true,true,true,true,true})}});
        }
        episodes.push_back(Json{{"trajectory_id", "diagnostic-trajectory-"+std::to_string(family)},
            {"family", "diagnostic-family-"+std::to_string(family)}, {"source_key", "diagnostic/reference-actor"},
            {"engine", "diagnostic-label-function/v1"}, {"feature_schema", learning::feature_schema},
            {"subject_healthy", true}, {"opponent_healthy", true}, {"complete", true}, {"logging_complete", true},
            {"payoff", family % 2 ? 1.0 : -1.0}, {"observations", observations}});
    }
    return episodes;
}
} // namespace

int c6_self_test() {
    const auto data = c6_dataset(c6_fixture(), Json::object(), 17);
    Actor actor;
    std::vector<double> value(13, 0);
    Adam aa(65), va(13);
    std::mt19937_64 rng(42);
    const auto initial = c6_metrics(actor, value, data, data.validation);
    for (int i = 0; i < 400; ++i) c6_update(actor, value, aa, va, data, rng, 32, 0.03, 0.01, 0);
    const auto trained = c6_metrics(actor, value, data, data.validation);
    if (!(trained.cross_entropy < initial.cross_entropy * 0.3 && trained.accuracy > 0.95))
        throw std::runtime_error("C6 diagnostic held-out learning check failed");
    bool rejected = false;
    Json leaking = c6_fixture();
    leaking[0]["split"] = "train"; leaking[1]["family"] = leaking[0]["family"]; leaking[1]["split"] = "test";
    try { (void)c6_dataset(leaking, Json::object(), 17); } catch (...) { rejected = true; }
    if (!rejected) throw std::runtime_error("C6 accepted family leakage");
    rejected = false;
    Json conflicting = c6_fixture(); conflicting.push_back(conflicting[0]); conflicting[9]["payoff"] = 0;
    try { (void)c6_dataset(conflicting, Json::object(), 17); } catch (...) { rejected = true; }
    if (!rejected) throw std::runtime_error("C6 accepted conflicting duplicate");
    const auto exact = c6_dataset(c6_fixture(), Json::object(), 17);
    Actor resumed = c6_actor_restore(Json::parse(c6_actor_state(actor).dump()));
    Adam aa2 = Adam::from_json(Json::parse(aa.to_json().dump()));
    Adam va2 = Adam::from_json(Json::parse(va.to_json().dump()));
    std::vector<double> value2 = value;
    std::ostringstream saved_rng; saved_rng << rng;
    std::mt19937_64 rng2; std::istringstream(saved_rng.str()) >> rng2;
    for (int i = 0; i < 30; ++i) {
        c6_update(actor, value, aa, va, exact, rng, 16, 0.01, 0.01, 0.0001);
        c6_update(resumed, value2, aa2, va2, exact, rng2, 16, 0.01, 0.01, 0.0001);
    }
    if (actor.weights != resumed.weights || value != value2 || aa.to_json() != aa2.to_json() || va.to_json() != va2.to_json())
        throw std::runtime_error("C6 checkpoint continuation differs");
    Actor copied = c6_reference_teacher();
    copied.max_decisions = 256;
    if (c6_actor_restore(c6_actor_state(copied)).max_decisions != 256)
        throw std::runtime_error("C6 actor checkpoint lost its decision cap");
    const auto original = copied.weights;
    std::vector<double> copied_value(13, 0);
    Adam copied_actor_adam(65), copied_value_adam(13);
    for (int i = 0; i < 20; ++i)
        c6_update(copied, copied_value, copied_actor_adam, copied_value_adam, exact, rng2, 16, 0.01, 0.01, 0.0001, false);
    if (copied.weights != original || copied_actor_adam.t != 0 || copied_value_adam.t != 20)
        throw std::runtime_error("C6 known-teacher fast path changed the actor or skipped value fitting");
    Json balanced = c6_fixture();
    const auto original_count = balanced.size();
    for (std::size_t i = 0; i < original_count; ++i) {
        Json opposite = balanced[i];
        opposite["trajectory_id"] = opposite.at("trajectory_id").get<std::string>() + "-opposite";
        opposite["payoff"] = -opposite.at("payoff").get<double>();
        balanced.push_back(opposite);
    }
    const auto balanced_data = c6_dataset(balanced, Json::object(), 17);
    const auto constant = c6_constant_value(balanced_data), round = c6_time_value(balanced_data, constant);
    std::vector<double> poor_value(13, 0); poor_value.back() = 10;
    if (c6_choose_value(copied, balanced_data, poor_value, constant, round, 0.01).method != "train_mean_constant")
        throw std::runtime_error("C6 failed to abstain from a critic worse than train-only baselines");
    for (auto& episode : balanced.as_array()) for (auto& observation : episode["observations"].as_array())
        observation["features"][1] = (episode.at("payoff").get<double>() + 1) / 2;
    const auto signal_data = c6_dataset(balanced, Json::object(), 17);
    std::vector<double> signal_value(13, 0); Adam signal_actor_adam(65), signal_value_adam(13);
    for (int i = 0; i < 500; ++i)
        c6_update(copied, signal_value, signal_actor_adam, signal_value_adam, signal_data, rng2, 32, 0.01, 0.03, 0, false);
    if (c6_choose_value(copied, signal_data, signal_value, c6_constant_value(signal_data),
        c6_time_value(signal_data, c6_constant_value(signal_data)), 0.01).method != "linear_actor_context")
        throw std::runtime_error("C6 failed to retain genuine held-out observation-value signal");
    return 0;
}

int run_c6(const Options& options) {
    if (options.has("self-test")) return c6_self_test();
    Run run(options, "c6-imitation-value", "3");
    Json raw = Json::array(), files = Json::array(), native_identity;
    Json native_artifacts = Json::array();
    std::string teacher_digest, artifact_manifest_digest, teacher_selection_digest;
    Json teacher_selection;
    Actor compact_teacher;
    Json exact_teacher;
    bool known_compact_teacher = false;
    const bool diagnostic = run.config.value("diagnostic_fixture", false);
    if (diagnostic && (run.config.contains("dataset") || run.config.contains("datasets") || run.config.contains("episodes") || run.config.contains("cases")))
        throw std::runtime_error("C6 diagnostic fixture cannot be mixed with supplied/native data");
    if (diagnostic) raw = c6_fixture();
    const bool external = diagnostic || run.config.contains("dataset") || run.config.contains("datasets") || run.config.contains("episodes");
    if (run.config.contains("episodes")) c6_append_document(run.config, raw);
    if (run.config.contains("dataset")) c6_load_file(c6_path(run.config.at("dataset").get<std::string>(), options), raw, files);
    if (run.config.contains("datasets")) for (const auto& file : run.config.at("datasets").as_array())
        c6_load_file(c6_path(file.get<std::string>(), options), raw, files);
    if (!external) {
        if (!run.config.contains("cases") || run.config.at("cases").empty())
            throw std::runtime_error("C6 needs dataset episodes or native teacher cases");
        Actor teacher = c6_reference_teacher();
        Json supplied_teacher;
        if (run.config.contains("qualification_cases")) {
            teacher_selection = c6_select_teacher(options, run.config, run);
            teacher_selection_digest = sha256(teacher_selection.dump());
            supplied_teacher = teacher_selection.at("selected_policy");
            teacher = Actor::from_json(supplied_teacher);
            for (const auto& item : teacher_selection.at("durable_artifacts").as_array()) native_artifacts.push_back(item);
        } else if (run.config.contains("teacher_policy")) {
            const auto& given = run.config.at("teacher_policy");
            supplied_teacher = given.is_string() ? read_json(c6_path(given.get<std::string>(), options)) : given;
            teacher = Actor::from_json(supplied_teacher);
        }
        if (teacher.features != 12 || teacher.actions != 5) throw std::runtime_error("C6 native teacher shape differs");
        const auto teacher_seed = supplied_teacher.value<std::uint64_t>("seed", options.seed);
        const bool teacher_stochastic = supplied_teacher.value("stochastic", false);
        Json actual_teacher = teacher.to_json(teacher_seed, teacher_stochastic, true);
        if (supplied_teacher.contains("max_decisions")) actual_teacher["max_decisions"] = supplied_teacher.at("max_decisions");
        compact_teacher = Actor::from_json(actual_teacher);
        exact_teacher = actual_teacher;
        known_compact_teacher = true;
        teacher_digest = sha256(actual_teacher.dump());
        if (run.resumed && run.state.value("teacher_digest", "") != teacher_digest)
            throw std::runtime_error("C6 supplied teacher or collection semantics changed on resume");
        Json request = c6_request(run.config, actual_teacher, run.config.at("cases"), true);
        request["kind"] = "identity";
        const Json identity = evaluate(options, request, options.out / "teacher-identity");
        if (!identity.contains("environment_identity")) throw std::runtime_error("C6 evaluator omitted environment_identity");
        native_identity = identity.at("environment_identity");
        if (!teacher_selection.is_null() && teacher_selection.at("environment_identity") != native_identity)
            throw std::runtime_error("C6 selection and collection environments differ");
        if (run.resumed) {
            if (!run.state.contains("native_identity") || run.state.at("native_identity") != native_identity)
                throw std::runtime_error("C6 native teacher environment changed on resume");
        } else {
            run.state = Json{{"stage", "collecting"}, {"native_identity", native_identity}, {"teacher_digest", teacher_digest},
                {"teacher_selection_digest", teacher_selection_digest}};
            run.checkpoint();
        }
        if (run.resumed && fs::is_regular_file(options.out / "dataset.json")) {
            const auto manifest_path = options.out / "teacher-artifacts.json";
            if (fs::is_regular_file(manifest_path)) {
                const Json manifest = read_json(manifest_path);
                if (manifest.value("schema", "") != "contenders-native-artifacts/v1")
                    throw std::runtime_error("C6 native artifact manifest schema differs");
                artifact_manifest_digest = sha256(manifest.dump());
                if (run.state.contains("artifact_manifest_digest") &&
                    run.state.at("artifact_manifest_digest").get<std::string>() != artifact_manifest_digest)
                    throw std::runtime_error("C6 native artifact manifest changed on resume");
                native_artifacts = learning::preserve_native_artifacts(manifest, options.out / "native-artifacts");
            } else if (!run.state.value("artifact_manifest_digest", "").empty())
                throw std::runtime_error("C6 native artifact manifest missing on resume");
            c6_load_file(options.out / "dataset.json", raw, files);
            if (run.state.value("stage", "training") == "collecting" &&
                run.state.value("collected_digest", "") != sha256(raw.dump()))
                throw std::runtime_error("C6 collecting-stage dataset differs from its committed teacher response");
        } else {
            request["kind"] = "policy";
            Json response = evaluate(options, request, options.out / "teacher-evaluation");
            if (!response.value("healthy", false)) throw std::runtime_error("C6 native teacher evaluation is unhealthy");
            if (!response.contains("environment_identity") || response.at("environment_identity") != native_identity)
                throw std::runtime_error("C6 native teacher evaluator identity changed during collection");
            const Json collection_artifacts = learning::preserve_native_artifacts(response, options.out / "native-artifacts");
            for (const auto& item : collection_artifacts.as_array()) native_artifacts.push_back(item);
            const Json manifest{{"schema", "contenders-native-artifacts/v1"}, {"artifacts", native_artifacts}};
            write_json(options.out / "teacher-artifacts.json", manifest);
            artifact_manifest_digest = sha256(manifest.dump());
            response["durable_artifacts"] = native_artifacts;
            if (response.contains("episodes")) c6_append_document(response, raw);
            else throw std::runtime_error("C6 native teacher returned no episodes");
            run.state["collected_digest"] = sha256(raw.dump());
            run.state["artifact_manifest_digest"] = artifact_manifest_digest;
            run.checkpoint();
            write_json(options.out / "teacher-response.json", response);
            write_json(options.out / "teacher-policy.json", actual_teacher);
            write_json(options.out / "dataset.json", Json{{"schema", "contenders-imitation-dataset/v1"},
                {"information_safe", true}, {"teacher_status", teacher_selection.is_null() ? "unqualified supplied/reference actor; strength not asserted" : teacher_selection.at("status")},
                {"episodes", raw}});
        }
    }
    auto data = c6_dataset(std::move(raw), run.config, options.seed, std::move(files));
    const auto constant_value = c6_constant_value(data), round_value = c6_time_value(data, constant_value);
    const double value_margin = run.config.value("value_min_validation_gain", 0.01);
    if (!std::isfinite(value_margin) || value_margin < 0 || value_margin > 4)
        throw std::runtime_error("invalid C6 value_min_validation_gain");
    Actor actor, best_actor;
    std::vector<double> value(13, 0), best_value(13, 0);
    Adam aa(65), va(13);
    double best_loss = std::numeric_limits<double>::infinity();
    std::uint64_t best_iteration = 0;
    const double value_weight = run.config.value("validation_value_weight", 0.25);
    const double rate = run.config.value("learning_rate", 0.01), value_rate = run.config.value("value_learning_rate", rate);
    const double l2 = run.config.value("l2", 0.0001);
    const std::size_t batch = run.config.value<std::size_t>("batch_size", 64);
    const std::uint64_t validate_every = run.config.value<std::uint64_t>("validation_every", 25);
    const std::uint64_t checkpoint_every = run.config.value<std::uint64_t>("checkpoint_every", 25);
    const std::uint64_t early_stop_patience = run.config.value<std::uint64_t>("early_stop_patience", 0);
    bool converged = run.resumed && run.state.value("converged", false);
    const bool fit_actor = run.config.value("refit_actor", !known_compact_teacher);
    if (!fit_actor && !known_compact_teacher) throw std::runtime_error("C6 cannot copy an unknown teacher; opaque datasets require actor fitting");
    if (!(std::isfinite(rate) && std::isfinite(value_rate) && std::isfinite(value_weight) && std::isfinite(l2) &&
          rate > 0 && value_rate > 0 && value_weight >= 0 && l2 >= 0 && batch && batch <= 1000000 && validate_every && checkpoint_every))
        throw std::runtime_error("invalid C6 training controls");
    const bool resumed_training = run.resumed && run.state.value("stage", "training") == "training";
    if (resumed_training) {
        if (run.state.at("dataset_digest").get<std::string>() != data.digest || run.state.at("split_digest").get<std::string>() != data.split_digest)
            throw std::runtime_error("C6 dataset or whole-family split changed on resume");
        actor = c6_actor_restore(run.state.at("actor")); best_actor = c6_actor_restore(run.state.at("best_actor"));
        value = learning::read_numbers(run.state.at("value")); best_value = learning::read_numbers(run.state.at("best_value"));
        if (value.size() != 13 || best_value.size() != 13) throw std::runtime_error("C6 value dimensions differ");
        aa = Adam::from_json(run.state.at("actor_adam")); va = Adam::from_json(run.state.at("value_adam"));
        best_loss = run.state.at("best_validation_objective").get<double>();
        best_iteration = run.state.at("best_iteration").get<std::uint64_t>();
        if (run.state.at("fit_actor").get<bool>() != fit_actor) throw std::runtime_error("C6 actor fitting/copy semantics changed on resume");
    } else {
        if (!fit_actor) actor = compact_teacher;
        const auto metrics = c6_metrics(actor, value, data, data.validation);
        best_loss = metrics.cross_entropy + value_weight * metrics.value_mse;
        best_actor = actor; best_value = value;
    }
    Json roles = Json::object();
    for (const auto& [family, split] : data.family_split) roles[family] = split;
    write_json(options.out / "dataset-manifest.json", Json{{"schema", "contenders-imitation-data-manifest/v1"},
        {"dataset_digest", data.digest}, {"split_digest", data.split_digest}, {"families", roles},
        {"episodes", data.episodes.size()}, {"decisions", data.decisions}, {"identical_duplicates_reused", data.duplicates},
        {"files", data.files}, {"feature_schema", learning::feature_schema},
        {"data_origin", diagnostic ? "explicit diagnostic label fixture; zero native game evidence" : external ? "supplied compatible trajectories" : "native actor trajectories"},
        {"normalization", "fixed runtime features; no statistics fitted on validation/test"},
        {"actor_method", fit_actor ? "supervised cross-entropy" : "exact known compact teacher copy; only value fitting needs optimization"},
        {"native_identity", native_identity}, {"teacher_digest", teacher_digest}, {"artifact_manifest_digest", artifact_manifest_digest},
        {"teacher_selection_digest", teacher_selection_digest},
        {"information_boundary", "only the declared actor context; job-specific actor indices6-10 must be zero"}});
    auto save = [&] {
        run.state = Json{{"stage", "training"}, {"dataset_digest", data.digest}, {"split_digest", data.split_digest}, {"native_identity", native_identity}, {"teacher_digest", teacher_digest},
            {"teacher_selection_digest", teacher_selection_digest},
            {"artifact_manifest_digest", artifact_manifest_digest},
            {"actor", c6_actor_state(actor)}, {"best_actor", c6_actor_state(best_actor)},
            {"value", learning::numbers(value)}, {"best_value", learning::numbers(best_value)},
            {"actor_adam", aa.to_json()}, {"value_adam", va.to_json()}, {"best_validation_objective", best_loss},
            {"best_iteration", best_iteration}, {"fit_actor", fit_actor}, {"converged", converged}, {"normalization", "fixed runtime features"}};
        run.checkpoint();
        write_json(options.out / "best-policy.json", fit_actor ? best_actor.to_json(options.seed, false, false) : exact_teacher);
        atomic_write(options.out / "best-policy.hpp", learning::policy_header(best_actor));
        write_json(options.out / "latest-policy.json", fit_actor ? actor.to_json(options.seed, false, false) : exact_teacher);
        if (known_compact_teacher) write_json(options.out / "teacher-copy.json", exact_teacher);
        const auto selected = c6_choose_value(best_actor, data, best_value, constant_value, round_value, value_margin);
        write_json(options.out / "value.json", c6_value_document(selected.weights, selected.method));
        atomic_write(options.out / "value.hpp", c6_value_header(selected.weights));
        write_json(options.out / "fitted-value.json", c6_value_document(best_value, "linear_actor_context; raw fitted candidate"));
        atomic_write(options.out / "fitted-value.hpp", c6_value_header(best_value));
        write_json(options.out / "round-only-value.json", c6_value_document(round_value, "round_only; train-only baseline"));
    };
    if (!resumed_training) save();
    while (!run.stop() && !converged) {
        c6_update(actor, value, aa, va, data, run.rng, batch, rate, value_rate, l2, fit_actor);
        run.advance();
        if (run.iteration % validate_every == 0) {
            const auto validation = c6_metrics(actor, value, data, data.validation);
            const double objective = validation.cross_entropy + value_weight * validation.value_mse;
            if (objective < best_loss) { best_loss = objective; best_iteration = run.iteration; best_actor = actor; best_value = value; }
            run.progress(Json{{"train", c6_metrics(actor, value, data, data.train).json()}, {"validation", validation.json()},
                {"validation_objective", objective}, {"best_iteration", best_iteration}});
            if (early_stop_patience && (run.iteration - best_iteration) / validate_every >= early_stop_patience)
                converged = true;
        }
        if (run.iteration % checkpoint_every == 0) save();
    }
    save();
    const Actor exported = Actor::from_json(best_actor.to_json(options.seed, false, false));
    const auto selected_value = c6_choose_value(exported, data, best_value, constant_value, round_value, value_margin);
    const auto train = c6_metrics(exported, selected_value.weights, data, data.train);
    const auto validation = c6_metrics(exported, selected_value.weights, data, data.validation);
    const auto test = c6_metrics(exported, selected_value.weights, data, data.test);
    const double test_constant_mse = c6_metrics(exported, constant_value, data, data.test).value_mse;
    const double test_neutral_mse = c6_metrics(exported, std::vector<double>(13, 0), data, data.test).value_mse;
    const double test_round_mse = c6_metrics(exported, round_value, data, data.test).value_mse;
    const bool heldout_context_supported = selected_value.method == "linear_actor_context" &&
        test.value_mse + value_margin < std::min({test_constant_mse, test_neutral_mse, test_round_mse});
    write_json(options.out / "value-qualification.json", Json{{"schema", "contenders-value-qualification/v1"},
        {"selected_method", selected_value.method}, {"context_passed_heldout_gate", heldout_context_supported},
        {"status", selected_value.method != "linear_actor_context" ? "simple baseline output; no learned context claim" : heldout_context_supported ?
            "held-out observation prediction improved; native action-value use still unqualified" : "context failed held-out baseline gate; fitted critic is analysis-only"},
        {"selected_test_mse", test.value_mse}, {"best_baseline_test_mse", std::min({test_constant_mse, test_neutral_mse, test_round_mse})},
        {"scope", "report-only qualification of the validation-selected artifact; never reselects weights/model using test labels"}});
    Json teacher_transfer = Json::object();
    if (!external && fs::is_regular_file(options.out / "teacher-response.json")) {
        const Json response = read_json(options.out / "teacher-response.json");
        std::map<std::string, std::array<double, 3>> totals;
        if (response.contains("cases")) for (const auto& item : response.at("cases").as_array()) {
            const auto family = c6_family(item);
            if (!data.family_split.contains(family)) throw std::runtime_error("C6 native transfer case has an unexpected family");
            auto& total = totals[data.family_split.at(family)];
            total[0] += item.at("payoff").get<double>(); total[1] += item.at("baseline_payoff").get<double>(); total[2] += 1;
        }
        for (const auto& [role, total] : totals) teacher_transfer[role] = Json{{"episodes", total[2]},
            {"teacher_payoff", total[0] / total[2]}, {"native_baseline_payoff", total[1] / total[2]},
            {"paired_gain", (total[0] - total[1]) / total[2]}, {"use", "report only; never selects the teacher or critic"}};
    }
    Json result{{"engine", "C6 imitation and terminal-value learning"}, {"status", "completed configured updates or stopped with checkpoint"},
        {"updates", run.iteration}, {"best_iteration", best_iteration}, {"dataset_digest", data.digest},
        {"stopping_reason", converged ? "validation patience exhausted; checkpoint remains resumable without redundant fitting" : "configured update/time cap or interruption"},
        {"train", train.json()}, {"validation", validation.json()}, {"test_report_only", test.json()},
        {"current_validation", c6_metrics(actor, value, data, data.validation).json()},
        {"scope", "supervised imitation of compatible actor decisions; reference teacher strength is not assumed"},
        {"actor_method", fit_actor ? "supervised cross-entropy" : "exact known compact teacher copy; no actor rediscovery campaign"},
        {"teacher_qualification", teacher_selection.is_null() ? Json{{"status", "not qualified; supplied or reference data only"}} : teacher_selection},
        {"teacher_transfer_report_only", teacher_transfer},
        {"readiness", Json{{"qualified_teacher_found", !teacher_selection.is_null() && teacher_selection.at("qualified").get<bool>()},
            {"context_value_supported_on_validation", selected_value.method == "linear_actor_context"}, {"counterfactual_action_value_ready", false},
            {"context_value_passed_heldout_gate", heldout_context_supported},
            {"essential_scope", "five native planner preferences; personal actor context. This does not model primitive champion movement/ability/attack decisions or reactive changed-action continuations"},
            {"next_evidence", "supply independently improved compatible C5 actors or richer information-safe expert datasets; expand frozen families/opponents before transfer claims"}}},
        {"value_selection", Json{{"method", selected_value.method}, {"minimum_validation_gain", value_margin},
            {"validation_constant_mse", selected_value.constant_mse}, {"validation_round_only_mse", selected_value.round_mse},
            {"validation_neutral_mse", selected_value.neutral_mse},
            {"validation_fitted_context_mse", selected_value.context_mse},
            {"context_supported_on_validation", selected_value.method == "linear_actor_context"},
            {"test_constant_mse_report_only", test_constant_mse},
            {"test_neutral_mse_report_only", test_neutral_mse},
            {"test_round_only_mse_report_only", test_round_mse},
            {"test_fitted_context_mse_report_only", c6_metrics(exported, best_value, data, data.test).value_mse},
            {"scope", "baseline fallback selected on validation only; test never selects a model; observation value cannot rank counterfactual actions"}}},
        {"data_origin", diagnostic ? "explicit diagnostic label fixture; zero native game evidence" : external ? "supplied compatible trajectories" : "native actor trajectories"},
        {"native_evidence", Json{{"manifest", external ? "not applicable: supplied trajectories" : "teacher-artifacts.json"},
            {"retained_descriptors", native_artifacts.size()}, {"manifest_digest", artifact_manifest_digest},
            {"store", "native-artifacts/; verified source, WASM and game evidence"}}},
        {"qualification", "held-out label/value quality only; exported weights are float32, while deployed float arithmetic/fuel and complete-game strength remain separate native checks"},
        {"artifacts", Json{{"actor", "best-policy.json"}, {"cpp_actor", "best-policy.hpp"}, {"value", "value.json"},
            {"cpp_value", "value.hpp"}, {"value_qualification", "value-qualification.json"}, {"fitted_value", "fitted-value.json"}, {"data", "dataset-manifest.json"}, {"checkpoint", "checkpoint.json"},
            {"native_manifest", external ? "not applicable" : "teacher-artifacts.json"}}}};
    if (known_compact_teacher) result["teacher_copy_baseline"] = Json{{"validation", c6_metrics(compact_teacher, selected_value.weights, data, data.validation).json()},
        {"test_report_only", c6_metrics(compact_teacher, selected_value.weights, data, data.test).json()},
        {"note", "already deployable actor; copy is an alternative to label fitting, not a newly learned gain"}};
    run.finish(result);
    std::ostringstream report;
    report << "C6 imitation/value result\n\n" << "Updates: " << run.iteration << "; selected validation update: " << best_iteration << ".\n"
           << "Whole-family held-out accuracy: " << test.accuracy << "; cross entropy: " << test.cross_entropy << "; value MSE: " << test.value_mse << ".\n"
           << "Value output: " << selected_value.method << ". Validation MSEs: constant " << selected_value.constant_mse
           << ", neutral " << selected_value.neutral_mse << ", round-only " << selected_value.round_mse << ", fitted context " << selected_value.context_mse << ".\n"
           << (selected_value.method == "linear_actor_context" ? "Context improved validation prediction; independent game qualification remains required.\n" : "The fitted context has insufficient validation support; use the selected simple baseline.\n")
           << (heldout_context_supported ? "Held-out observation prediction improved; this still cannot rank changed actions.\n" : "No held-out context-value qualification: fitted critic is analysis-only. See value-qualification.json.\n")
           << "Use best-policy.json with the native policy materializer, or best-policy.hpp with the documented 12-feature actor context.\n"
           << (fit_actor ? "Actor fitted from trajectory labels.\n" : "Teacher was already a deployable compact actor: copied exactly instead of rediscovering its weights. Only the value function was fitted.\n")
           << "This measures imitation, not stronger game play. Test labels were never used for gradient updates or checkpoint selection.\n";
    atomic_write(options.out / "report.txt", report.str());
    return 0;
}
} // namespace opt
