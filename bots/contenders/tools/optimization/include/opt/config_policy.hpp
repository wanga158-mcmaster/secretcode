#pragma once
#include "config_space.hpp"
#include <functional>

namespace opt::c2 {

inline const std::vector<std::string>& context_feature_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> result{"width", "height", "area", "group_count", "camp_count", "total_gems",
            "total_gold", "max_group_gems", "max_group_gold", "gem_group_count", "mean_group_size", "spawn_distance",
            "team_size", "ally_pick_count", "enemy_pick_count"};
        for (const std::string side : {"ally", "enemy"})
            for (const std::string pick : {"ryth", "vesper_vale", "vendetta", "cassi_and_issac", "stellar", "prism",
                "marcella", "willow", "ren", "varg", "tavi", "heart_of_ruin"}) result.push_back(side + "_has_" + pick);
        return result;
    }();
    return names;
}

inline std::vector<Json> configuration_leaves(const Json& tree) {
    std::vector<Json> result;
    std::function<void(const Json&)> visit = [&](const Json& node) {
        if (node.contains("knobs")) {
            if (std::find(result.begin(), result.end(), node.at("knobs")) == result.end()) result.push_back(node.at("knobs"));
        } else { visit(node.at("left")); visit(node.at("right")); }
    };
    visit(tree); return result;
}

// A candidate is either an ordinary constexpr configuration, or a bounded tree
// whose leaves are complete coupled configurations. Branches see only the native
// materializer's public map/revealed-draft feature schema, never a map name or bot ID.
class ContextConfigSpace {
    ConfigSpace configurations_;
    bool contextual_;
    Json domains_;
    int depth_{2}, leaves_{4};
    using Path = std::vector<bool>;
    static std::size_t index(std::mt19937_64& rng, std::size_t n) { return static_cast<std::size_t>(rng() % n); }
    static std::size_t leaf_count(const Json& tree) { return tree.contains("knobs") ? 1 : leaf_count(tree.at("left")) + leaf_count(tree.at("right")); }
    static int tree_depth(const Json& tree) { return tree.contains("knobs") ? 0 : 1 + std::max(tree_depth(tree.at("left")), tree_depth(tree.at("right"))); }
    static void paths(const Json& node, Path path, std::vector<Path>& result) {
        result.push_back(path);
        if (node.contains("knobs")) return;
        path.push_back(false); paths(node.at("left"), path, result);
        path.back() = true; paths(node.at("right"), path, result);
    }
    static Json& at(Json& tree, const Path& path) {
        Json* node = &tree; for (const bool right : path) node = &node->at(right ? "right" : "left"); return *node;
    }
    static const Json& at(const Json& tree, const Path& path) {
        const Json* node = &tree; for (const bool right : path) node = &node->at(right ? "right" : "left"); return *node;
    }
    Json wrap(Json tree) const {
        return Json{{"config_policy", Json{{"schema", "contenders-contextual-config/v1"},
            {"feature_schema", "contenders-config-context/v1"}, {"tree", tree}}}};
    }
    Json normalize(const Json& tree) const {
        if (tree.contains("knobs")) return Json{{"knobs", configurations_.canonical(tree.at("knobs"))}};
        auto left = normalize(tree.at("left")), right = normalize(tree.at("right"));
        if (left == right) return left;
        return Json{{"feature", tree.at("feature")}, {"threshold", tree.at("threshold")}, {"left", left}, {"right", right}};
    }
    const Json& domain(const std::string& name) const {
        for (const auto& entry : domains_.as_array()) if (entry.at("name").get<std::string>() == name) return entry;
        throw std::runtime_error("context feature is outside this search domain: " + name);
    }
    bool valid_node(const Json& node, int depth, std::size_t& count) const {
        if (!node.is_object() || depth > depth_) return false;
        if (node.contains("knobs")) return node.size() == 1 && ++count <= static_cast<std::size_t>(leaves_) && configurations_.valid(node.at("knobs"));
        if (node.size() != 4 || !node.contains("feature") || !node.contains("threshold") || !node.contains("left") || !node.contains("right")
            || !node.at("feature").is_string() || !node.at("threshold").is_number()) return false;
        const auto& feature = domain(node.at("feature").get<std::string>());
        const double threshold = node.at("threshold").get<double>();
        if (!std::isfinite(threshold) || std::abs(threshold) > 1e12) return false;
        if (feature.contains("thresholds")) {
            bool found = false; for (const auto& value : feature.at("thresholds").as_array()) if (value.get<double>() == threshold) found = true;
            if (!found) return false;
        } else if (threshold < feature.at("min").get<double>() || threshold > feature.at("max").get<double>()) return false;
        return valid_node(node.at("left"), depth + 1, count) && valid_node(node.at("right"), depth + 1, count);
    }
    Json threshold(const Json& feature, std::mt19937_64& rng) const {
        if (feature.contains("thresholds")) return feature.at("thresholds").at(index(rng, feature.at("thresholds").size()));
        if (feature.value("integer", false)) return std::uniform_int_distribution<std::int64_t>(feature.at("min").get<std::int64_t>(), feature.at("max").get<std::int64_t>())(rng);
        return std::uniform_real_distribution<double>(feature.at("min").get<double>(), feature.at("max").get<double>())(rng);
    }
    Json random_tree(std::mt19937_64& rng, int depth, int budget, bool force = false) const {
        if (depth == 0 || budget < 2 || (!force && (rng() % 4 == 0))) return Json{{"knobs", configurations_.sample(rng)}};
        const auto& feature = domains_.at(index(rng, domains_.size()));
        const int left_budget = 1 + static_cast<int>(rng() % (budget - 1));
        return Json{{"feature", feature.at("name")}, {"threshold", threshold(feature, rng)},
            {"left", random_tree(rng, depth - 1, left_budget)}, {"right", random_tree(rng, depth - 1, budget - left_budget)}};
    }
public:
    explicit ContextConfigSpace(const Json& config) : configurations_(config), contextual_(config.contains("policy_space")), domains_(Json::array()) {
        if (!contextual_) return;
        const auto& policy = config.at("policy_space");
        depth_ = policy.value("max_depth", 2); leaves_ = policy.value("max_leaves", 4);
        if (depth_ < 1 || depth_ > 3 || leaves_ < 2 || leaves_ > 4) throw std::runtime_error("context policy requires max_depth 1..3 and max_leaves 2..4");
        domains_ = policy.at("features");
        if (!domains_.is_array() || domains_.empty()) throw std::runtime_error("policy_space requires nonempty public feature domains");
        std::set<std::string> seen;
        for (const auto& feature : domains_.as_array()) {
            const auto name = feature.at("name").get<std::string>();
            if (std::find(context_feature_names().begin(), context_feature_names().end(), name) == context_feature_names().end() || !seen.insert(name).second)
                throw std::runtime_error("unknown/duplicate public context feature: " + name);
            if (feature.contains("thresholds")) {
                if (!feature.at("thresholds").is_array() || feature.at("thresholds").empty()) throw std::runtime_error("empty context thresholds");
                for (const auto& value : feature.at("thresholds").as_array())
                    if (!value.is_number() || !std::isfinite(value.get<double>()) || std::abs(value.get<double>()) > 1e12) throw std::runtime_error("invalid context threshold");
            } else {
                const double low = feature.at("min").get<double>(), high = feature.at("max").get<double>();
                if (!std::isfinite(low) || !std::isfinite(high) || low > high || std::max(std::abs(low), std::abs(high)) > 1e12) throw std::runtime_error("invalid context domain");
                if (feature.value("integer", false) && (std::floor(low) != low || std::floor(high) != high)) throw std::runtime_error("integer context domain needs integer bounds");
            }
        }
    }
    bool contextual() const { return contextual_; }
    Json baseline() const { return contextual_ ? wrap(Json{{"knobs", configurations_.baseline()}}) : Json{{"knobs", configurations_.baseline()}}; }
    Json canonical(const Json& genome) const {
        return contextual_ ? wrap(normalize(genome.at("config_policy").at("tree"))) : Json{{"knobs", configurations_.canonical(genome.at("knobs"))}};
    }
    bool valid(const Json& genome, std::string* why = nullptr) const {
        try {
            if (!genome.is_object() || genome.size() != 1) throw std::runtime_error("candidate needs exactly knobs or config_policy");
            if (!contextual_) return genome.contains("knobs") && configurations_.valid(genome.at("knobs"), why);
            const auto& policy = genome.at("config_policy");
            if (policy.size() != 3 || policy.value("schema", "") != "contenders-contextual-config/v1" || policy.value("feature_schema", "") != "contenders-config-context/v1") throw std::runtime_error("invalid config policy schema");
            std::size_t count = 0;
            if (!valid_node(policy.at("tree"), 0, count) || canonical(genome) != genome) throw std::runtime_error("illegal/noncanonical context tree");
            return true;
        } catch (const std::exception& error) { if (why) *why = error.what(); return false; }
    }
    Json sample(std::mt19937_64& rng) const {
        return contextual_ ? wrap(normalize(random_tree(rng, depth_, leaves_, true))) : Json{{"knobs", configurations_.sample(rng)}};
    }
    Json mutate(const Json& genome, std::mt19937_64& rng, unsigned mutations = 1) const {
        if (!contextual_) return Json{{"knobs", configurations_.mutate(genome.at("knobs"), rng, mutations)}};
        Json tree = genome.at("config_policy").at("tree");
        for (unsigned m = 0; m < std::max(1u, mutations); ++m) {
            std::vector<Path> choices; paths(tree, {}, choices); const auto path = choices[index(rng, choices.size())];
            Json& node = at(tree, path); const int operation = static_cast<int>(rng() % 4);
            if (operation == 0 || (node.contains("knobs") && static_cast<int>(path.size()) == depth_)) {
                auto leaves = configuration_leaves(node); const auto leaf = leaves[index(rng, leaves.size())];
                node = Json{{"knobs", configurations_.mutate(leaf, rng, 1 + static_cast<unsigned>(rng() % 3))}};
            } else if (operation == 1 && !node.contains("knobs")) {
                const auto& feature = domains_.at(index(rng, domains_.size())); node["feature"] = feature.at("name"); node["threshold"] = threshold(feature, rng);
            } else if (operation == 2) {
                const int budget = leaves_ - static_cast<int>(leaf_count(tree)) + static_cast<int>(leaf_count(node));
                node = random_tree(rng, depth_ - static_cast<int>(path.size()), budget, true);
            } else if (!node.contains("knobs")) {
                const Json replacement = node.at((rng() & 1) ? "left" : "right"); node = replacement;
            } else {
                const int budget = leaves_ - static_cast<int>(leaf_count(tree)) + 1;
                node = random_tree(rng, depth_ - static_cast<int>(path.size()), budget, true);
            }
            tree = normalize(tree);
        }
        return wrap(tree);
    }
    Json crossover(const Json& a, const Json& b, std::mt19937_64& rng) const {
        if (!contextual_) return Json{{"knobs", configurations_.crossover(a.at("knobs"), b.at("knobs"), rng)}};
        const auto& donor = b.at("config_policy").at("tree");
        for (int attempt = 0; attempt < 64; ++attempt) {
            Json tree = a.at("config_policy").at("tree"); std::vector<Path> target_paths, donor_paths;
            paths(tree, {}, target_paths); paths(donor, {}, donor_paths);
            const auto target = target_paths[index(rng, target_paths.size())], source = donor_paths[index(rng, donor_paths.size())];
            const auto replacement = at(donor, source);
            if (target.size() + tree_depth(replacement) > static_cast<std::size_t>(depth_)
                || leaf_count(tree) - leaf_count(at(tree, target)) + leaf_count(replacement) > static_cast<std::size_t>(leaves_)) continue;
            at(tree, target) = replacement; return wrap(normalize(tree));
        }
        return mutate(a, rng);
    }
};

inline std::string policy_cpp_header(const Json& policy) {
    const auto& tree = policy.at("tree"); const auto leaves = configuration_leaves(tree);
    std::string result = "#pragma once\n#include <array>\n// Public raw-unit context; gameplay choice is fixed at first gameplay turn.\n";
    for (std::size_t i = 0; i < leaves.size(); ++i) {
        auto header = configuration_header(leaves[i]);
        const std::string marker = "namespace optimized_configuration";
        const auto start = header.find(marker);
        if (start == std::string::npos) throw std::runtime_error("configuration header namespace missing");
        header.replace(start, marker.size(), "namespace optimized_leaf" + std::to_string(i)); result += header + "\n";
    }
    result += "namespace optimized_configuration {\ninline constexpr std::array<const char*, " + std::to_string(context_feature_names().size()) + "> feature_names = {";
    for (const auto& name : context_feature_names()) result += "\"" + name + "\",";
    result += "};\nusing Context = std::array<double, " + std::to_string(context_feature_names().size()) + ">;\n";
    std::function<std::string(const Json&)> expression = [&](const Json& node) -> std::string {
        if (node.contains("knobs")) return std::to_string(std::find(leaves.begin(), leaves.end(), node.at("knobs")) - leaves.begin());
        const auto feature = std::find(context_feature_names().begin(), context_feature_names().end(), node.at("feature").get<std::string>()) - context_feature_names().begin();
        return "(x[" + std::to_string(feature) + "] <= " + node.at("threshold").dump() + " ? " + expression(node.at("left")) + " : " + expression(node.at("right")) + ")";
    };
    result += "inline int select_configuration(const Context& x) { return " + expression(tree) + "; }\n}\n";
    return result;
}
} // namespace opt::c2
