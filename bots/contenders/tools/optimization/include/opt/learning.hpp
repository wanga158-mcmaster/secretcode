#pragma once
#include "common.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <set>

namespace opt::learning {

inline const std::vector<std::string> feature_names{
    "roundFraction", "hpFraction", "bagFraction", "goldFraction", "enemyVisibleFraction",
    "alliesVisibleFraction", "travelFraction", "groupGemFraction", "groupGoldFraction",
    "groupHpFraction", "groupHarmless", "homeDistanceFraction"};
inline const std::vector<std::string> action_names{"neutral", "gems", "gold", "short", "home"};
inline constexpr const char* policy_schema = "contenders-policy/v1";
inline constexpr const char* feature_schema = "contenders-actor-context/v1";

inline std::string canonical_family(std::string name) {
    std::replace(name.begin(), name.end(), '\\', '/');
    if (const auto slash = name.find_last_of('/'); slash != std::string::npos) name.erase(0, slash + 1);
    for (char& c : name) {
        if (c == '_') c = '-';
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (name.ends_with(".map26a")) name.resize(name.size() - 7);
    return name;
}

inline Json numbers(const std::vector<double>& values) {
    Json out = Json::array();
    for (double x : values) out.push_back(x);
    return out;
}
inline Json strings(const std::vector<std::string>& values) {
    Json out = Json::array();
    for (const auto& x : values) out.push_back(x);
    return out;
}
inline std::vector<double> read_numbers(const Json& value) {
    if (!value.is_array()) throw std::runtime_error("expected a numeric array");
    std::vector<double> out;
    for (const auto& x : value.as_array()) {
        double number = x.get<double>();
        if (!std::isfinite(number)) throw std::runtime_error("nonfinite learning input");
        out.push_back(number);
    }
    return out;
}

// Native adapters return content-addressed source, WASM and game evidence.
// Finalists must survive cache cleanup, so verify both ends of each durable copy.
inline Json preserve_native_artifacts(const Json& response, const fs::path& store) {
    Json retained = Json::array();
    if (!response.contains("artifacts")) return retained;
    fs::create_directories(store);
    for (const auto& item : response.at("artifacts").as_array()) {
        const auto digest = item.at("sha256").get<std::string>();
        if (digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("invalid native artifact digest");
        fs::path source = item.at("path").get<std::string>();
        if (item.value("path_base", "") == "run_output" && source.is_relative()) {
            for (const auto& component : source)
                if (component == "..") throw std::runtime_error("native artifact path leaves output directory");
            source = store.parent_path() / source;
        }
        const auto size = item.at("size").get<std::uint64_t>();
        if (!fs::is_regular_file(source) || fs::file_size(source) != size || file_sha256(source) != digest)
            throw std::runtime_error("native artifact missing or changed: " + source.string());
        std::string extension = source.extension().string();
        if (extension != ".wasm" && extension != ".zip" && extension != ".json") extension = ".bin";
        const auto target = store / (digest + extension);
        if (!fs::is_regular_file(target)) atomic_write(target, read_text(source));
        if (fs::file_size(target) != size || file_sha256(target) != digest)
            throw std::runtime_error("durable native artifact mismatch: " + target.string());
        Json copy = item;
        copy["cache_path"] = item.value("cache_path", source.string());
        copy["path"] = target.lexically_relative(store.parent_path()).generic_string();
        copy["path_base"] = "run_output";
        retained.push_back(copy);
    }
    return retained;
}

struct Actor {
    std::size_t features{12}, actions{5};
    std::vector<double> weights = std::vector<double>(65, 0.0);
    double logit_clip{20.0};
    std::size_t max_decisions{128};

    Actor() = default;
    Actor(std::size_t f, std::size_t a) : features(f), actions(a), weights(a * (f + 1), 0.0) {
        if (!f || !a) throw std::runtime_error("empty actor dimensions");
    }
    std::vector<double> raw_logits(const std::vector<double>& x) const {
        if (x.size() != features) throw std::runtime_error("actor observation dimension differs");
        std::vector<double> out(actions, 0.0);
        for (std::size_t a = 0; a < actions; ++a) {
            out[a] = weights[a * (features + 1) + features];
            for (std::size_t f = 0; f < features; ++f) out[a] += weights[a * (features + 1) + f] * x[f];
        }
        return out;
    }
    std::vector<double> probabilities(const std::vector<double>& x, const std::vector<bool>& legal = {}) const {
        if (!legal.empty() && legal.size() != actions) throw std::runtime_error("actor legal-mask dimension differs");
        auto out = raw_logits(x);
        double maximum = -std::numeric_limits<double>::infinity();
        for (std::size_t a = 0; a < actions; ++a) {
            if (legal.empty() || legal[a]) {
                out[a] = std::clamp(out[a], -logit_clip, logit_clip);
                maximum = std::max(maximum, out[a]);
            } else out[a] = -std::numeric_limits<double>::infinity();
        }
        if (!std::isfinite(maximum)) throw std::runtime_error("actor has no legal action");
        double total = 0;
        for (double& x : out) { x = std::isfinite(x) ? std::exp(x - maximum) : 0.0; total += x; }
        if (!std::isfinite(total) || total <= 0) throw std::runtime_error("invalid actor distribution");
        for (double& x : out) x /= total;
        return out;
    }
    Json to_json(std::uint64_t seed = 1, bool stochastic = true, bool trace = true) const {
        Json rows = Json::array();
        for (std::size_t a = 0; a < actions; ++a) {
            std::vector<double> row;
            for (std::size_t f = 0; f <= features; ++f) row.push_back(static_cast<float>(weights[a*(features+1)+f]));
            rows.push_back(numbers(row));
        }
        return Json{{"schema", policy_schema}, {"feature_schema", feature_schema},
            {"feature_names", strings(feature_names)}, {"action_names", strings(action_names)},
            {"weights", rows}, {"seed", seed}, {"stochastic", stochastic}, {"trace", trace},
            {"logit_clip", logit_clip}, {"max_decisions", max_decisions},
            {"actor_feature_zero_indices", Json::array({6,7,8,9,10})}};
    }
    static Actor from_json(const Json& value) {
        if (value.at("schema").get<std::string>() != policy_schema) throw std::runtime_error("unsupported policy schema");
        const auto& rows = value.at("weights").as_array();
        if (rows.empty()) throw std::runtime_error("empty policy weights");
        auto first = read_numbers(rows.front());
        if (first.size() < 2) throw std::runtime_error("policy requires features and bias");
        Actor out(first.size() - 1, rows.size());
        out.weights.clear();
        for (const auto& row : rows) {
            auto x = read_numbers(row);
            if (x.size() != first.size()) throw std::runtime_error("ragged policy weights");
            for (double weight : x) {
                if (std::abs(weight) > 16) throw std::runtime_error("policy weight exceeds native runtime domain");
                out.weights.push_back(static_cast<float>(weight));
            }
        }
        out.logit_clip = value.value("logit_clip", 20.0);
        out.max_decisions = value.value<std::size_t>("max_decisions", 128);
        if (out.logit_clip != 20.0) throw std::runtime_error("native actor requires logit clip 20");
        if (!out.max_decisions || out.max_decisions > 256) throw std::runtime_error("invalid native decision cap");
        if (out.features == 12 && out.actions == 5) {
            if (value.contains("feature_schema") && value.at("feature_schema").get<std::string>() != feature_schema)
                throw std::runtime_error("actor feature schema differs from native runtime");
            if (value.contains("feature_names") && value.at("feature_names") != strings(feature_names))
                throw std::runtime_error("actor feature order differs from native runtime");
            if (value.contains("action_names") && value.at("action_names") != strings(action_names))
                throw std::runtime_error("actor action order differs from native runtime");
        }
        return out;
    }
};

struct Adam {
    std::vector<double> m, v;
    std::uint64_t t{};
    explicit Adam(std::size_t n = 0) : m(n, 0), v(n, 0) {}
    void update(std::vector<double>& parameters, std::vector<double> gradient, double rate,
                double max_norm = 5, double weight_limit = 10) {
        if (gradient.size() != parameters.size()) throw std::runtime_error("gradient dimension differs");
        if (m.empty()) { m.assign(parameters.size(), 0); v.assign(parameters.size(), 0); }
        if (m.size() != parameters.size() || v.size() != parameters.size()) throw std::runtime_error("Adam state dimension differs");
        double norm = 0;
        for (double g : gradient) { if (!std::isfinite(g)) throw std::runtime_error("nonfinite gradient"); norm += g*g; }
        norm = std::sqrt(norm);
        const double scale = max_norm > 0 && norm > max_norm ? max_norm / norm : 1;
        ++t;
        const double c1 = 1 - std::pow(0.9, static_cast<double>(t));
        const double c2 = 1 - std::pow(0.999, static_cast<double>(t));
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            const double g = gradient[i] * scale;
            m[i] = 0.9*m[i] + 0.1*g;
            v[i] = 0.999*v[i] + 0.001*g*g;
            parameters[i] -= rate * (m[i]/c1) / (std::sqrt(v[i]/c2) + 1e-8);
            if (weight_limit > 0) parameters[i] = std::clamp(parameters[i], -weight_limit, weight_limit);
        }
    }
    Json to_json() const { return Json{{"m", numbers(m)}, {"v", numbers(v)}, {"t", t}}; }
    static Adam from_json(const Json& value) {
        Adam out;
        out.m = read_numbers(value.at("m")); out.v = read_numbers(value.at("v"));
        out.t = value.at("t").get<std::uint64_t>();
        if (out.m.size() != out.v.size()) throw std::runtime_error("ragged Adam checkpoint");
        return out;
    }
};

struct Observation {
    std::vector<double> features, probabilities;
    std::vector<bool> legal;
    int action{}, actor{}, round{}, decision{};
};
struct Episode {
    std::string id, family, source_key, engine, split;
    double payoff{};
    std::vector<Observation> observations;
};

inline std::vector<bool> mask(const Json& input, std::size_t actions) {
    if (input.is_null()) return std::vector<bool>(actions, true);
    if (!input.is_array() || input.size() != actions) throw std::runtime_error("legal mask dimension differs");
    std::vector<bool> out;
    for (const auto& x : input.as_array()) out.push_back(x.get<bool>());
    return out;
}

inline Episode parse_episode(const Json& input, std::size_t features = 12, std::size_t actions = 5,
                              bool require_probabilities = false, bool require_logged_decisions = true) {
    Episode out;
    out.id = input.at("trajectory_id").get<std::string>();
    out.family = input.value("family", input.value("map", out.id));
    out.source_key = input.value("source_key", "");
    out.engine = input.value("engine", "");
    out.split = input.value("split", "");
    out.payoff = input.at("payoff").get<double>();
    if (out.id.empty() || out.family.empty() || !std::isfinite(out.payoff) || out.payoff < -1 || out.payoff > 1)
        throw std::runtime_error("invalid episode identity or terminal payoff");
    if (!input.at("subject_healthy").get<bool>() || !input.at("opponent_healthy").get<bool>())
        throw std::runtime_error("unhealthy episode is not learning data");
    if (!input.at("complete").get<bool>() || (require_logged_decisions && !input.at("logging_complete").get<bool>()))
        throw std::runtime_error("incomplete episode is not learning data");
    const std::string declared = input.at("feature_schema").get<std::string>();
    if (declared != feature_schema) throw std::runtime_error("incompatible actor feature schema: " + declared);
    std::map<int, int> last_decision;
    std::map<int, int> last_round;
    for (const auto& item : input.at("observations").as_array()) {
        Observation obs;
        obs.features = read_numbers(item.at("features"));
        obs.action = item.at("action").get<int>();
        for (const auto* field : {"actor", "round", "decision"})
            if (!item.at(field).is_number_integer()) throw std::runtime_error("actor record needs explicit integer identity/order");
        obs.actor = item.at("actor").get<int>(); obs.round = item.at("round").get<int>();
        obs.decision = item.at("decision").get<int>();
        if (obs.actor < 0 || obs.round < 0 || obs.decision < 1) throw std::runtime_error("invalid actor identity/order");
        if (obs.features.size() != features || obs.action < 0 || obs.action >= static_cast<int>(actions))
            throw std::runtime_error("observation/action dimension differs");
        for (double f : obs.features) if (f < -1e-6 || f > 1.000001) throw std::runtime_error("actor feature lies outside normalized native domain");
        if (features == 12) for (std::size_t f = 6; f <= 10; ++f)
            if (obs.features[f] != 0) throw std::runtime_error("job-specific data leaked into actor context");
        obs.legal = mask(item.contains("legal_mask") ? item.at("legal_mask") : Json{}, actions);
        if (!obs.legal[static_cast<std::size_t>(obs.action)]) throw std::runtime_error("demonstrated action is illegal");
        if ((!last_decision.contains(obs.actor) && obs.decision != 1) ||
            (last_decision.contains(obs.actor) && obs.decision != last_decision[obs.actor] + 1))
            throw std::runtime_error("gap or duplicate actor decision index");
        if (last_round.contains(obs.actor) && obs.round <= last_round[obs.actor])
            throw std::runtime_error("actor decisions are not ordered by owner round");
        last_decision[obs.actor] = obs.decision;
        last_round[obs.actor] = obs.round;
        if (item.contains("probabilities")) {
            obs.probabilities = read_numbers(item.at("probabilities"));
            if (obs.probabilities.size() != actions) throw std::runtime_error("probability dimension differs");
            double total = std::accumulate(obs.probabilities.begin(), obs.probabilities.end(), 0.0);
            if (std::abs(total - 1) > 0.002 || obs.probabilities[obs.action] <= 0)
                throw std::runtime_error("invalid sampled probability");
            for (std::size_t a = 0; a < actions; ++a)
                if (obs.probabilities[a] < 0 || obs.probabilities[a] > 1 || (!obs.legal[a] && obs.probabilities[a] > 1e-6))
                    throw std::runtime_error("invalid or illegal action probability");
        } else if (require_probabilities) throw std::runtime_error("REINFORCE needs behavior probabilities");
        out.observations.push_back(std::move(obs));
    }
    return out;
}

inline std::vector<double> policy_loss_gradient(const Actor& actor, const Observation& obs,
                                                double advantage, double entropy_bonus = 0) {
    auto p = obs.probabilities.empty() ? actor.probabilities(obs.features, obs.legal) : obs.probabilities;
    auto raw = actor.raw_logits(obs.features);
    std::vector<double> gradient(actor.weights.size(), 0);
    double entropy = 0;
    for (double v : p) if (v > 0) entropy -= v * std::log(v);
    for (std::size_t a = 0; a < actor.actions; ++a) {
        if (!obs.legal.empty() && !obs.legal[a]) continue;
        if (raw[a] <= -actor.logit_clip || raw[a] >= actor.logit_clip) continue;
        double delta = advantage * (p[a] - (a == static_cast<std::size_t>(obs.action) ? 1.0 : 0.0));
        if (p[a] > 0) delta += entropy_bonus * p[a] * (std::log(p[a]) + entropy);
        const std::size_t start = a * (actor.features + 1);
        for (std::size_t f = 0; f < actor.features; ++f) gradient[start + f] = delta * obs.features[f];
        gradient[start + actor.features] = delta;
    }
    return gradient;
}

inline double entropy(const std::vector<double>& p) {
    double out = 0; for (double x : p) if (x > 0) out -= x*std::log(x); return out;
}
inline std::string policy_header(const Actor& actor) {
    std::string out = "#pragma once\n// Exported linear actor. Features must follow policy.json; legality remains in the executor.\n";
    out += "#include <algorithm>\n#include <array>\n#include <cmath>\nnamespace learned_policy {\n";
    out += "inline constexpr int features=" + std::to_string(actor.features) + ",actions=" + std::to_string(actor.actions) + ";\n";
    out += "inline constexpr float weights[actions][features+1]={\n";
    for (std::size_t a = 0; a < actor.actions; ++a) {
        out += "{";
        for (std::size_t f = 0; f <= actor.features; ++f) {
            if (f) out += ',';
            std::string literal = Json(static_cast<float>(actor.weights[a*(actor.features+1)+f])).dump();
            if (literal.find_first_of(".eE") == std::string::npos) literal += ".0";
            out += literal + "f";
        }
        out += "},\n";
    }
    out += "};\ninline std::array<float,actions> probabilities(const std::array<float,features>& x){std::array<float,actions> p{};float m=-20.0f,s=0;for(int a=0;a<actions;++a){float z=weights[a][features];for(int f=0;f<features;++f)z+=weights[a][f]*x[f];p[a]=std::clamp(z,-";
    out += std::to_string(actor.logit_clip) + "f," + std::to_string(actor.logit_clip) + "f";
    out += ");m=std::max(m,p[a]);}for(float&v:p){v=std::exp(v-m);s+=v;}for(float&v:p)v/=s;return p;}\n}\n";
    return out;
}

} // namespace opt::learning
