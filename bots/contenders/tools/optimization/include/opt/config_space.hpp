#pragma once
#include "common.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>
#include <sstream>

namespace opt::c2 {

// A mixed, conditional space of actual constexpr overrides. Inactive dimensions
// are removed before hashing/evaluation, so changing an inert tail is not evidence.
class ConfigSpace {
    Json specification_, fixed_{Json::object()}, parameters_{Json::array()}, constraints_{Json::array()};

    static bool equal_number(const Json& a, const Json& b) {
        return a.is_number() && b.is_number() ? a.get<double>() == b.get<double>() : a == b;
    }
    static bool override_value(const Json& value) {
        if (value.is_boolean() || value.is_number()) return true;
        if (!value.is_array() || value.empty()) return false;
        for (const auto& item : value.as_array()) if (!item.is_boolean() && !item.is_number()) return false;
        return true;
    }
    static bool identifier(const std::string& name) {
        return !name.empty() && !std::isdigit(static_cast<unsigned char>(name.front()))
            && std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
    }
    static bool member(const Json& values, const Json& value) {
        for (const auto& entry : values.as_array()) if (equal_number(entry, value)) return true;
        return false;
    }
    static std::size_t choose(std::mt19937_64& rng, std::size_t n) {
        if (!n) throw std::runtime_error("empty configuration domain");
        return std::uniform_int_distribution<std::size_t>(0, n - 1)(rng);
    }
    static bool active(const Json& parameter, const Json& current) {
        if (!parameter.contains("active_if")) return true;
        for (const auto& [name, allowed] : parameter.at("active_if").as_object()) {
            if (!current.contains(name)) return false;
            if (allowed.is_array()) { if (!member(allowed, current.at(name))) return false; }
            else if (!equal_number(allowed, current.at(name))) return false;
        }
        return true;
    }
    static bool number_in(const Json& parameter, const Json& value, bool integer) {
        if (!value.is_number() || (integer && std::floor(value.get<double>()) != value.get<double>())) return false;
        const double v = value.get<double>();
        return std::isfinite(v) && v >= parameter.at("min").get<double>() && v <= parameter.at("max").get<double>();
    }
    static bool shop_valid(const Json& parameter, const Json& value) {
        if (!value.is_array() || value.size() != parameter.value<std::size_t>("length", 8)) return false;
        const auto families = parameter.at("families");
        const auto stats = parameter.at("stats");
        const auto milestones = parameter.value<Json>("milestones", Json::array({5}));
        const auto tail = parameter.value<Json>("tail_families", families);
        bool ended = false, unlimited = false;
        std::set<int> used;
        int finite = 0;
        for (const auto& step : value.as_array()) {
            if (!step.is_number_integer()) return false;
            const int code = step.get<int>();
            if (code == -1) { ended = true; continue; }
            if (ended || unlimited || code < 0) return false;
            const int family = code / 100, stat = (code / 10) % 10, count = code % 10;
            if (!member(stats, stat)) return false;
            if (!count) {
                if (!member(tail, family)) return false;
                unlimited = true;
            } else {
                if (!member(families, family) || !member(milestones, count) || !used.insert(family).second) return false;
                ++finite;
            }
        }
        return unlimited && finite >= parameter.value<int>("min_steps", 2)
            && finite <= parameter.value<int>("max_steps", 5);
    }
    static bool legal_value(const Json& parameter, const Json& value) {
        const auto type = parameter.value("type", "categorical");
        if (type == "categorical") return member(parameter.at("values"), value);
        if (type == "integer") return number_in(parameter, value, true);
        if (type == "real") return number_in(parameter, value, false);
        if (type == "shop_sequence") return shop_valid(parameter, value);
        if (type == "permutation") {
            if (!value.is_array() || value.size() != parameter.at("values").size()) return false;
            std::set<std::string> seen;
            for (const auto& item : value.as_array())
                if (!member(parameter.at("values"), item) || !seen.insert(item.dump()).second) return false;
            return true;
        }
        throw std::runtime_error("unsupported C2 parameter type: " + type);
    }
    static Json random_value(const Json& parameter, std::mt19937_64& rng) {
        const auto type = parameter.value("type", "categorical");
        if (type == "categorical") return parameter.at("values").at(choose(rng, parameter.at("values").size()));
        if (type == "integer") return std::uniform_int_distribution<std::int64_t>(parameter.at("min").get<std::int64_t>(), parameter.at("max").get<std::int64_t>())(rng);
        if (type == "real") return std::uniform_real_distribution<double>(parameter.at("min").get<double>(), parameter.at("max").get<double>())(rng);
        if (type == "permutation") {
            Json result = parameter.at("values");
            std::shuffle(result.as_array().begin(), result.as_array().end(), rng);
            return result;
        }
        if (type == "shop_sequence") {
            Json families = parameter.at("families");
            std::shuffle(families.as_array().begin(), families.as_array().end(), rng);
            const auto& stats = parameter.at("stats");
            const auto milestones = parameter.value<Json>("milestones", Json::array({5}));
            const auto tail = parameter.value<Json>("tail_families", families);
            const int low = parameter.value<int>("min_steps", 2), high = parameter.value<int>("max_steps", 5);
            const int count = std::uniform_int_distribution<int>(low, high)(rng);
            Json result = Json::array();
            for (int i = 0; i < count; ++i)
                result.push_back(100 * families.at(i).get<int>() + 10 * stats.at(choose(rng, stats.size())).get<int>()
                    + milestones.at(choose(rng, milestones.size())).get<int>());
            result.push_back(100 * tail.at(choose(rng, tail.size())).get<int>() + 10 * stats.at(choose(rng, stats.size())).get<int>());
            while (result.size() < parameter.value<std::size_t>("length", 8)) result.push_back(-1);
            return result;
        }
        throw std::runtime_error("unsupported C2 parameter type");
    }
    static Json default_value(const Json& parameter) {
        if (parameter.contains("default")) return parameter.at("default");
        const auto type = parameter.value("type", "categorical");
        if (type == "categorical") return parameter.at("values").at(0);
        if (type == "integer" || type == "real") return parameter.at("min");
        if (type == "permutation") return parameter.at("values");
        std::mt19937_64 deterministic(0);
        return random_value(parameter, deterministic);
    }
    bool constraints_hold(const Json& knobs, std::string* why) const {
        for (const auto& rule : constraints_.as_array()) {
            const auto name = rule.at("left").get<std::string>();
            if (!knobs.contains(name)) continue;
            const auto left = knobs.at(name);
            Json right = rule.at("right");
            if (right.is_string()) {
                if (!knobs.contains(right.get<std::string>())) continue;
                right = knobs.at(right.get<std::string>());
            }
            const auto op = rule.value("op", "le");
            bool okay = false;
            if (op == "eq" || op == "ne") okay = equal_number(left, right) == (op == "eq");
            else {
                if (!left.is_number() || !right.is_number()) throw std::runtime_error("ordered constraint requires numbers");
                const double a = left.get<double>(), b = right.get<double>();
                if (op == "le") okay = a <= b;
                else if (op == "lt") okay = a < b;
                else if (op == "ge") okay = a >= b;
                else if (op == "gt") okay = a > b;
                else throw std::runtime_error("unknown C2 constraint operator: " + op);
            }
            if (!okay) { if (why) *why = "constraint failed: " + name + " " + op + " " + rule.at("right").dump(); return false; }
        }
        return true;
    }
    Json assemble(const Json& proposed, std::mt19937_64* rng) const {
        Json result = fixed_;
        for (const auto& parameter : parameters_.as_array()) {
            if (!active(parameter, result)) continue;
            const auto name = parameter.at("name").get<std::string>();
            if (proposed.contains(name) && legal_value(parameter, proposed.at(name))) result[name] = proposed.at(name);
            else result[name] = rng ? random_value(parameter, *rng) : default_value(parameter);
        }
        return result;
    }
public:
    explicit ConfigSpace(const Json& config) : specification_(config) {
        fixed_ = config.value<Json>("fixed_knobs", Json::object());
        parameters_ = config.at("space");
        constraints_ = config.value<Json>("constraints", Json::array());
        if (!fixed_.is_object() || !parameters_.is_array() || parameters_.empty() || !constraints_.is_array())
            throw std::runtime_error("C2 requires nonempty space array, fixed_knobs object, constraints array");
        std::set<std::string> names;
        for (const auto& [name, value] : fixed_.as_object()) {
            if (!identifier(name) || !override_value(value)) throw std::runtime_error("invalid C2 fixed override: " + name);
            names.insert(name);
        }
        for (const auto& parameter : parameters_.as_array()) {
            const auto name = parameter.at("name").get<std::string>();
            if (!identifier(name) || names.count(name)) throw std::runtime_error("invalid/duplicate C2 knob: " + name);
            if (parameter.contains("active_if")) for (const auto& [prior, allowed] : parameter.at("active_if").as_object())
                if (!names.count(prior)) throw std::runtime_error("active_if must reference an earlier parameter/fixed knob: " + prior);
            const auto type = parameter.value("type", "categorical");
            if (type == "categorical" || type == "permutation") {
                const auto values = parameter.at("values");
                if (!values.is_array() || values.empty()) throw std::runtime_error("C2 values must be a nonempty array");
                std::set<std::string> unique;
                for (const auto& v : values.as_array()) {
                    if (!override_value(v)) throw std::runtime_error("C2 domains must contain scalar or flat numeric/boolean overrides");
                    if (!unique.insert(v.dump()).second) throw std::runtime_error("duplicate C2 domain value");
                }
            } else if (type == "integer" || type == "real") {
                if (!parameter.at("min").is_number() || !parameter.at("max").is_number()
                    || parameter.at("min").get<double>() > parameter.at("max").get<double>()) throw std::runtime_error("bad numeric range");
                if (type == "integer" && (std::floor(parameter.at("min").get<double>()) != parameter.at("min").get<double>()
                    || std::floor(parameter.at("max").get<double>()) != parameter.at("max").get<double>())) throw std::runtime_error("integer range requires integer bounds");
            } else if (type == "shop_sequence") {
                const int length = parameter.value<int>("length", 8), low = parameter.value<int>("min_steps", 2), high = parameter.value<int>("max_steps", 5);
                const auto families = parameter.at("families"), stats = parameter.at("stats");
                if (length != 8 || low < 0 || high < low || high >= length || high > static_cast<int>(families.size())
                    || !families.is_array() || families.empty() || !stats.is_array() || stats.empty()) throw std::runtime_error("bad C2 shop_sequence domain");
                std::set<int> unique;
                for (const auto& f : families.as_array()) if (!f.is_number_integer() || f.get<int>() < 0 || f.get<int>() > 6 || !unique.insert(f.get<int>()).second) throw std::runtime_error("bad/duplicate shop family");
                for (const auto& s : stats.as_array()) if (!s.is_number_integer() || s.get<int>() < 0 || s.get<int>() > 3) throw std::runtime_error("bad shop stat");
                const auto milestones = parameter.value<Json>("milestones", Json::array({5}));
                if (!milestones.is_array() || milestones.empty()) throw std::runtime_error("empty shop milestones");
                for (const auto& m : milestones.as_array()) if (!m.is_number_integer() || m.get<int>() < 1 || m.get<int>() > 9) throw std::runtime_error("bad shop milestone");
                const auto tail = parameter.value<Json>("tail_families", families);
                if (!tail.is_array() || tail.empty()) throw std::runtime_error("empty shop tail families");
                for (const auto& f : tail.as_array()) if (!f.is_number_integer() || f.get<int>() < 0 || f.get<int>() > 6) throw std::runtime_error("bad shop tail family");
            } else throw std::runtime_error("unsupported C2 parameter type: " + type);
            if (!legal_value(parameter, default_value(parameter))) throw std::runtime_error("invalid C2 default: " + name);
            names.insert(name);
        }
        for (const auto& rule : constraints_.as_array()) {
            if (!names.count(rule.at("left").get<std::string>())) throw std::runtime_error("C2 constraint references unknown left knob");
            const auto& right = rule.at("right");
            if (right.is_string() && !names.count(right.get<std::string>())) throw std::runtime_error("C2 constraint references unknown right knob");
            const auto op = rule.value("op", "le");
            if (op != "le" && op != "lt" && op != "ge" && op != "gt" && op != "eq" && op != "ne") throw std::runtime_error("unknown C2 constraint operator");
        }
        if (!constraints_hold(baseline(), nullptr)) throw std::runtime_error("C2 default violates constraints");
    }
    Json baseline() const { return assemble(Json::object(), nullptr); }
    Json canonical(const Json& knobs) const {
        if (!knobs.is_object()) throw std::runtime_error("C2 knobs must be an object");
        return assemble(knobs, nullptr);
    }
    bool valid(const Json& knobs, std::string* why = nullptr) const {
        if (!knobs.is_object()) { if (why) *why = "knobs is not object"; return false; }
        Json expected = fixed_;
        for (const auto& parameter : parameters_.as_array()) {
            const auto name = parameter.at("name").get<std::string>();
            if (!active(parameter, expected)) continue;
            if (!knobs.contains(name) || !legal_value(parameter, knobs.at(name))) { if (why) *why = "missing/illegal active knob: " + name; return false; }
            expected[name] = knobs.at(name);
        }
        if (knobs != expected) { if (why) *why = "unknown or inactive knobs"; return false; }
        return constraints_hold(expected, why);
    }
    Json sample(std::mt19937_64& rng) const {
        for (int attempt = 0; attempt < 1024; ++attempt) {
            auto result = assemble(Json::object(), &rng);
            if (constraints_hold(result, nullptr)) return result;
        }
        throw std::runtime_error("C2 could not sample feasible configuration after 1024 attempts; check constraints");
    }
    Json mutate(const Json& knobs, std::mt19937_64& rng, unsigned mutations = 1) const {
        for (int attempt = 0; attempt < 128; ++attempt) {
            Json proposed = canonical(knobs);
            for (unsigned m = 0; m < std::max(1u, mutations); ++m) {
                const auto& parameter = parameters_.at(choose(rng, parameters_.size()));
                const auto name = parameter.at("name").get<std::string>();
                if (!active(parameter, proposed)) continue;
                // An earlier mutation may have just activated this branch. Its old
                // canonical genotype deliberately has no value for this dimension.
                if (!proposed.contains(name)) proposed[name] = random_value(parameter, rng);
                if (parameter.value("type", "categorical") == "permutation" && proposed.at(name).size() > 1) {
                    auto& permutation = proposed[name].as_array();
                    const auto a = choose(rng, permutation.size());
                    auto b = choose(rng, permutation.size() - 1); if (b >= a) ++b;
                    std::swap(permutation[a], permutation[b]);
                } else proposed[name] = random_value(parameter, rng);
            }
            auto result = assemble(proposed, &rng);
            if (constraints_hold(result, nullptr)) return result;
        }
        return sample(rng);
    }
    Json crossover(const Json& a, const Json& b, std::mt19937_64& rng) const {
        Json proposed = Json::object();
        for (const auto& parameter : parameters_.as_array()) {
            const auto name = parameter.at("name").get<std::string>();
            const Json& parent = (rng() & 1) ? a : b;
            if (parent.contains(name)) proposed[name] = parent.at(name);
        }
        auto result = assemble(proposed, &rng);
        return constraints_hold(result, nullptr) ? result : sample(rng);
    }
};

inline std::string cpp_literal(const Json& value) {
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number()) return value.dump();
    if (value.is_array()) {
        std::string out = "{ ";
        for (std::size_t i = 0; i < value.size(); ++i) { if (i) out += ", "; out += cpp_literal(value.at(i)); }
        return out + " }";
    }
    throw std::runtime_error("configuration override must be numeric, boolean or a flat array");
}

inline std::string configuration_header(const Json& knobs) {
    std::ostringstream out;
    out << "#pragma once\n#include <array>\n// C2 export. Values override the pinned controller, not standalone game rules.\nnamespace optimized_configuration {\n";
    for (const auto& [name, value] : knobs.as_object()) {
        if (value.is_array()) out << "inline constexpr std::array<int, " << value.size() << "> " << name << " = " << cpp_literal(value) << ";\n";
        else out << "inline constexpr " << (value.is_boolean() ? "bool " : value.is_number_integer() ? "long long " : "double ") << name << " = " << cpp_literal(value) << ";\n";
    }
    out << "}\n";
    return out.str();
}
} // namespace opt::c2
