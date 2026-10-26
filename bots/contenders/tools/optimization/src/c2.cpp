#include "opt/common.hpp"
#include "opt/config_policy.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>

namespace opt {
namespace {

std::string case_key(const Json& item) {
    return item.at("map").get<std::string>() + "|" + item.at("side").get<std::string>()
        + "|" + item.at("opponent").get<std::string>();
}

Json expand_case_sets(Json config) {
    for (const std::string split : {"train", "validation", "qualification"}) {
        const auto original = config.at("cases").at(split);
        if (original.is_array()) continue;
        if (!original.is_object()) throw std::runtime_error("C2 case split must be an array or maps/opponents object");
        Json expanded = Json::array();
        const auto sides = original.value<Json>("sides", Json::array({"A", "B"}));
        for (const auto& map : original.at("maps").as_array())
            for (const auto& opponent : original.at("opponents").as_array())
                for (const auto& side : sides.as_array()) expanded.push_back(Json{{"map", map}, {"side", side}, {"opponent", opponent}});
        config["cases"][split] = expanded;
    }
    return config;
}

void validate_cases(const Json& config) {
    if (!config.at("cases").is_object()) throw std::runtime_error("C2 cases must contain train, validation and qualification arrays");
    std::map<std::string, std::string> map_split;
    for (const std::string split : {"train", "validation", "qualification"}) {
        const auto& cases = config.at("cases").at(split);
        if (!cases.is_array() || cases.empty()) throw std::runtime_error("C2 requires a nonempty " + split + " case array");
        std::set<std::string> seen;
        std::map<std::string, int> sides;
        for (const auto& item : cases.as_array()) {
            const auto map = item.at("map").get<std::string>(), side = item.at("side").get<std::string>();
            const auto opponent = item.at("opponent").get<std::string>();
            if (map.empty() || opponent.empty() || (side != "A" && side != "B")) throw std::runtime_error("invalid C2 case");
            if (!seen.insert(case_key(item)).second) throw std::runtime_error("duplicate C2 case: " + case_key(item));
            const auto map_name = fs::path(map).stem().string();
            if (map_split.count(map_name) && map_split[map_name] != split) throw std::runtime_error("C2 split leakage: map " + map + " appears in " + map_split[map_name] + " and " + split);
            map_split[map_name] = split;
            sides[map + "|" + opponent] |= side == "A" ? 1 : 2;
        }
        for (const auto& [key, mask] : sides) if (mask != 3) throw std::runtime_error("C2 case family requires both sides: " + split + "/" + key);
    }
}

Json prefix_cases(const Json& cases, std::size_t count) {
    Json result = Json::array();
    for (std::size_t i = 0; i < std::min(count, cases.size()); ++i) result.push_back(cases.at(i));
    return result;
}

Json context_cases(const Json& config) {
    Json result = Json::array();
    for (const std::string split : {"train", "validation", "qualification"})
        for (const auto& item : config.at("cases").at(split).as_array()) result.push_back(item);
    return result;
}

void verify_context(Run& run) {
    Json request{{"schema", "contenders-eval/v1"}, {"kind", "identity"},
        {"controller", run.config.value("controller", "anchors/main-v42")},
        {"baseline", run.config.value<Json>("baseline", Json{{"knobs", Json::object()}})},
        {"cases", context_cases(run.config)}, {"context_cases", context_cases(run.config)}};
    if (run.config.contains("evaluation")) request["evaluation"] = run.config.at("evaluation");
    const auto response = evaluate(run.options, request, run.options.out / "work" / "context-identity");
    if (!response.contains("context_identity")) throw std::runtime_error("C2 evaluator identity response needs context_identity");
    if (response.contains("identity") && response.at("identity").contains("cases")) {
        // The adapter resolves map files and hashes before playing. Disjoint names
        // alone cannot prevent identical geometry copied under different names.
        std::map<std::string, std::string> declared, hash_split;
        for (const std::string split : {"train", "validation", "qualification"})
            for (const auto& item : run.config.at("cases").at(split).as_array()) declared[fs::path(item.at("map").get<std::string>()).stem().string()] = split;
        for (const auto& item : response.at("identity").at("cases").as_array()) {
            const auto name = fs::path(item.at("map").get<std::string>()).stem().string();
            if (!declared.count(name)) throw std::runtime_error("C2 native identity returned an undeclared map");
            const auto hash = item.at("map_sha256").get<std::string>(), split = declared.at(name);
            if (hash_split.count(hash) && hash_split.at(hash) != split) throw std::runtime_error("C2 split leakage: byte-identical native maps appear in multiple roles");
            hash_split[hash] = split;
        }
    }
    if (run.state.contains("context_identity") && run.state.at("context_identity") != response.at("context_identity"))
        throw std::runtime_error("C2 evaluator context changed since checkpoint; use a new output directory");
    run.state["context_identity"] = response.at("context_identity");
    run.checkpoint();
}

std::vector<std::size_t> training_stages(const Json& config) {
    const auto count = config.at("cases").at("train").size();
    const auto search = config.value<Json>("search", Json::object());
    std::set<std::size_t> unique;
    const auto stage_values = search.value<Json>("stages", Json::array({4, 12, 40}));
    for (const auto& n : stage_values.as_array()) {
        const auto v = n.get<std::size_t>();
        if (!v) throw std::runtime_error("C2 stage sizes must be positive");
        unique.insert(std::min(v, count));
    }
    unique.insert(count);
    // Every native screening prefix must contain complete side pairs. A scalar
    // threshold on Team A alone can hide an ordering weakness.
    for (const auto n : unique) {
        std::map<std::string, int> sides;
        const auto prefix = prefix_cases(config.at("cases").at("train"), n);
        for (const auto& item : prefix.as_array())
            sides[item.at("map").get<std::string>() + "|" + item.at("opponent").get<std::string>()]
                |= item.at("side").get<std::string>() == "A" ? 1 : 2;
        for (const auto& [key, mask] : sides) if (mask != 3) throw std::runtime_error("C2 stage prefix cuts a side pair: " + std::to_string(n) + "/" + key);
    }
    return {unique.begin(), unique.end()};
}

double score_of(const Json& response) {
    if (!response.value("healthy", false)) return -3.0;
    const double score = response.contains("paired_objective") ? response.at("paired_objective").get<double>() : response.at("objective").get<double>();
    if (!std::isfinite(score) || score < -2.000001 || score > 2.000001) throw std::runtime_error("invalid C2 evaluator objective");
    return score;
}

Json mean_and_se(const std::vector<double>& values) {
    if (values.empty()) return Json{{"n", 0}, {"mean", nullptr}, {"se", nullptr}};
    double sum = 0; for (const auto value : values) sum += value;
    const double mean = sum / values.size(); double variance = 0;
    for (const auto value : values) variance += (value - mean) * (value - mean);
    return Json{{"n", values.size()}, {"mean", mean},
        {"se", values.size() > 1 ? Json(std::sqrt(variance / (values.size() - 1) / values.size())) : Json(nullptr)}};
}

Json describe_case_rows(const Json& cases) {
    int wins = 0, losses = 0, draws = 0, gained = 0, lost = 0, draw_pairs = 0;
    std::vector<double> win_deltas, margin_deltas;
    for (const auto& row : cases.as_array()) {
        if (!row.contains("payoff") || !row.at("payoff").is_number()) continue;
        const double payoff = row.at("payoff").get<double>();
        if (payoff > 0) ++wins; else if (payoff < 0) ++losses; else ++draws;
        if (row.contains("baseline_payoff") && row.at("baseline_payoff").is_number()) {
            const double baseline = row.at("baseline_payoff").get<double>();
            win_deltas.push_back((payoff - baseline) / 2);
            if (!payoff || !baseline) ++draw_pairs;
            else if (payoff > baseline) ++gained;
            else if (payoff < baseline) ++lost;
        }
        if (row.contains("margin") && row.contains("baseline_margin")
            && row.at("margin").is_number() && row.at("baseline_margin").is_number())
            margin_deltas.push_back(row.at("margin").get<double>() - row.at("baseline_margin").get<double>());
    }
    const int n = wins + losses + draws, discordant = gained + lost;
    long double tail = 0;
    for (int i = 0; i <= std::min(gained, lost); ++i)
        tail += std::exp(std::lgamma(static_cast<long double>(discordant) + 1)
            - std::lgamma(static_cast<long double>(i) + 1)
            - std::lgamma(static_cast<long double>(discordant - i) + 1)
            - discordant * std::log(2.0L));
    Json result{{"n", n}, {"wins", wins}, {"losses", losses}, {"draws", draws},
        {"paired_gained", gained}, {"paired_lost", lost}, {"pairs_with_draw", draw_pairs},
        {"mcnemar_p_binary_pairs", std::min(1.0, static_cast<double>(2 * tail))},
        {"paired_win_score_delta", mean_and_se(win_deltas)}, {"paired_gem_margin_delta", mean_and_se(margin_deltas)},
        {"interpretation", "descriptive fixed-panel paired results; draw pairs excluded from binary McNemar; no optional-stopping or search-selection correction"}};
    if (n && !draws) {
        const double p = static_cast<double>(wins) / n, z2 = 1.959963984540054 * 1.959963984540054;
        const double center = (p + z2 / (2 * n)) / (1 + z2 / n);
        const double half = 1.959963984540054 * std::sqrt(p * (1 - p) / n + z2 / (4.0 * n * n)) / (1 + z2 / n);
        result["wilson_95"] = Json::array({center - half, center + half});
    } else result["wilson_95"] = nullptr;
    return result;
}

Json statistics_of(const Json& response) {
    if (!response.contains("cases") || !response.at("cases").is_array()) return Json{{"status", "per-key outcomes unavailable"}};
    const auto& rows = response.at("cases");
    Json result = describe_case_rows(rows), by_opponent = Json::object(), by_map = Json::object();
    for (const auto& row : rows.as_array()) {
        const auto opponent = row.value("opponent", "unknown"), map = row.value("map", "unknown");
        if (!by_opponent.contains(opponent)) by_opponent[opponent] = Json::array();
        if (!by_map.contains(map)) by_map[map] = Json::array();
        by_opponent[opponent].push_back(row); by_map[map].push_back(row);
    }
    result["by_opponent"] = Json::object(); result["by_map"] = Json::object();
    for (const auto& [name, cases] : by_opponent.as_object()) result["by_opponent"][name] = describe_case_rows(cases);
    for (const auto& [name, cases] : by_map.as_object()) result["by_map"][name] = describe_case_rows(cases);
    return result;
}

Json summary_of(const Json& response, std::size_t count, const fs::path& file) {
    return Json{{"score", score_of(response)}, {"objective", response.value("objective", -1.0)},
        {"healthy", response.value("healthy", false)}, {"subject_healthy", response.value("subject_healthy", response.value("healthy", false))},
        {"opponent_healthy", response.value("opponent_healthy", response.value("healthy", false))},
        {"cases", count}, {"response_file", file.generic_string()}, {"response_sha256", file_sha256(file)},
        {"statistics", response.contains("cases") && response.at("cases").is_array()
            ? describe_case_rows(response.at("cases")) : Json{{"status", "per-key outcomes unavailable"}}}};
}

Json evaluate_split(Run& run, const std::string& id, const Json& candidate, const std::string& split, std::size_t count) {
    auto& entry = run.state["entries"][id];
    if (!entry.contains(split)) entry[split] = Json::object();
    const auto key = std::to_string(count);
    if (entry[split].contains(key)) {
        const auto cached = entry[split].at(key);
        const fs::path file = run.options.out / cached.at("response_file").get<std::string>();
        if (!fs::exists(file) || file_sha256(file) != cached.at("response_sha256").get<std::string>())
            throw std::runtime_error("C2 cached response missing or changed: " + file.string());
        run.event(Json{{"event", "evaluation_cache_hit"}, {"candidate", id}, {"split", split}, {"cases", count}});
        return cached;
    }
    Json request{{"schema", "contenders-eval/v1"}, {"kind", "config"},
        {"candidate", candidate}, {"cases", prefix_cases(run.config.at("cases").at(split), count)},
        {"controller", run.config.value("controller", "anchors/main-v42")},
        {"baseline", run.config.value<Json>("baseline", Json{{"knobs", Json::object()}})},
        {"phase", split}, {"seed", run.options.seed}, {"context_cases", context_cases(run.config)}};
    if (run.config.contains("evaluation")) request["evaluation"] = run.config.at("evaluation");
    if (run.state.contains("context_identity")) request["expected_context_identity"] = run.state.at("context_identity");
    const auto relative = fs::path("evaluations") / (id + "-" + split + "-" + key + ".json");
    const auto work = run.options.out / "work" / (id + "-" + split + "-" + key);
    run.event(Json{{"event", "evaluation_start"}, {"candidate", id}, {"split", split}, {"cases", count}});
    const auto response = evaluate(run.options, request, work);
    if (!response.is_object() || !response.contains("healthy") || !response.at("healthy").is_boolean())
        throw std::runtime_error("C2 evaluator must return an explicit healthy boolean");
    if (!response.contains("context_identity")) throw std::runtime_error("C2 evaluator omitted its pinned context identity");
    if (response.at("healthy").get<bool>()) {
        if (!response.contains("cases") || !response.at("cases").is_array() || response.at("cases").size() != count)
            throw std::runtime_error("C2 healthy evaluator response has incomplete case coverage");
        std::set<std::string> expected, observed;
        for (const auto& item : request.at("cases").as_array())
            expected.insert(fs::path(item.at("map").get<std::string>()).stem().string() + "|"
                + item.at("side").get<std::string>() + "|" + item.at("opponent").get<std::string>());
        for (const auto& row : response.at("cases").as_array()) {
            if (!row.value("complete", false)) throw std::runtime_error("C2 healthy evaluator returned a truncated or unconfirmed game");
            const auto key = fs::path(row.at("map").get<std::string>()).stem().string() + "|"
                + row.at("side").get<std::string>() + "|" + row.value("requested_opponent", row.at("opponent").get<std::string>());
            if (!observed.insert(key).second) throw std::runtime_error("C2 evaluator duplicated a game key");
        }
        if (expected != observed) throw std::runtime_error("C2 evaluator response does not match requested game keys");
        double objective = 0, paired = 0;
        for (const auto& row : response.at("cases").as_array()) {
            const double payoff = row.at("payoff").get<double>(), baseline = row.at("baseline_payoff").get<double>();
            auto terminal = [](double value) { return value == -1 || value == 0 || value == 1; };
            if (!terminal(payoff) || !terminal(baseline)) throw std::runtime_error("C2 healthy case lacks a terminal win payoff");
            objective += payoff / count; paired += (payoff - baseline) / count;
        }
        if (std::abs(objective - response.at("objective").get<double>()) > 1e-9
            || std::abs(paired - response.at("paired_objective").get<double>()) > 1e-9)
            throw std::runtime_error("C2 aggregate fitness disagrees with terminal case outcomes");
    }
    if (response.contains("context_identity")) {
        if (run.state.contains("context_identity") && run.state.at("context_identity") != response.at("context_identity"))
            throw std::runtime_error("C2 evaluator context changed; start a new campaign instead of mixing versions");
        run.state["context_identity"] = response.at("context_identity");
    }
    write_json(run.options.out / relative, response);
    auto summary = summary_of(response, count, run.options.out / relative);
    // summary_of hashes a real path; keep relocatable references inside state.
    summary["response_file"] = relative.generic_string();
    entry[split][key] = summary;
    run.event(Json{{"event", "evaluation_end"}, {"candidate", id}, {"split", split}, {"cases", count},
        {"healthy", summary.at("healthy")}, {"score", summary.at("score")}});
    run.checkpoint();
    return summary;
}

void verify_saved_evidence(const Run& run) {
    for (const auto& [id, entry] : run.state.at("entries").as_object())
        for (const std::string split : {"train", "validation", "qualification"}) {
            if (!entry.contains(split)) continue;
            for (const auto& [count, summary] : entry.at(split).as_object()) {
                const fs::path file = run.options.out / summary.at("response_file").get<std::string>();
                if (!fs::is_regular_file(file) || file_sha256(file) != summary.at("response_sha256").get<std::string>())
                    throw std::runtime_error("C2 saved evidence missing/changed: " + id + "/" + split + "/" + count);
            }
        }
}

std::vector<std::string> ranked_parents(const Run& run, std::size_t full, std::size_t elites) {
    struct Item { std::string id; std::size_t coverage; double score; };
    std::vector<Item> items;
    for (const auto& [id, entry] : run.state.at("entries").as_object()) {
        if (!entry.contains("train")) continue;
        std::size_t largest = 0; double score = -3;
        for (const auto& [n, value] : entry.at("train").as_object()) {
            const auto count = static_cast<std::size_t>(std::stoull(n));
            if (value.value("healthy", false) && count > largest) { largest = count; score = value.at("score").get<double>(); }
        }
        if (largest) items.push_back({id, largest, score});
    }
    std::sort(items.begin(), items.end(), [full](const Item& a, const Item& b) {
        if ((a.coverage == full) != (b.coverage == full)) return a.coverage == full;
        if (a.coverage != b.coverage) return a.coverage > b.coverage;
        if (a.score != b.score) return a.score > b.score;
        return a.id < b.id;
    });
    std::vector<std::string> result;
    for (std::size_t i = 0; i < std::min(elites, items.size()); ++i) result.push_back(items[i].id);
    return result;
}

bool promote(const Run& run, const std::string& id, std::size_t count, double score, double fraction, std::size_t population) {
    std::vector<double> scores;
    for (const auto& [other, entry] : run.state.at("entries").as_object()) {
        if (other == id || !entry.contains("train") || !entry.at("train").contains(std::to_string(count))) continue;
        const auto& result = entry.at("train").at(std::to_string(count));
        if (result.value("healthy", false)) scores.push_back(result.at("score").get<double>());
    }
    if (scores.size() < population) return true;
    std::sort(scores.begin(), scores.end(), std::greater<double>());
    const auto index = std::min(scores.size() - 1, static_cast<std::size_t>(std::ceil(fraction * scores.size())) - 1);
    return score >= scores[index];
}

void export_best(Run& run) {
    const auto id = run.state.value("best", "");
    if (id.empty()) return;
    const auto& entry = run.state.at("entries").at(id);
    const auto& candidate = entry.at("candidate");
    const auto directory = run.options.out / "best";
    fs::create_directories(directory);
    write_json(directory / "candidate.json", candidate);
    if (candidate.contains("config_policy")) {
        write_json(directory / "configuration-policy.json", candidate.at("config_policy"));
        atomic_write(directory / "configuration.hpp", c2::policy_cpp_header(candidate.at("config_policy")));
    } else {
        write_json(directory / "overrides.json", candidate.at("knobs"));
        atomic_write(directory / "configuration.hpp", c2::configuration_header(candidate.at("knobs")));
    }
    Json reproduce{{"schema", "contenders-c2-reproduce/v1"}, {"engine", "c2"}, {"campaign_identity", run.identity},
        {"candidate_id", id}, {"controller", run.config.value("controller", "anchors/main-v42")},
        {"candidate", candidate}, {"baseline", run.config.value<Json>("baseline", Json{{"knobs", Json::object()}})},
        {"cases", run.config.at("cases")}, {"scope", "best healthy full-training candidate; validation/qualification never enter search fitness"},
        {"qualification", "no release or strength certification"}};
    if (run.state.contains("context_identity")) reproduce["context_identity"] = run.state.at("context_identity");
    write_json(directory / "campaign.json", reproduce);
    Json native_request{{"schema", "contenders-eval/v1"}, {"kind", "config"},
        {"controller", run.config.value("controller", "anchors/main-v42")}, {"candidate", candidate},
        {"baseline", run.config.value<Json>("baseline", Json{{"knobs", Json::object()}})},
        {"cases", run.config.at("cases").at("qualification")}, {"context_cases", context_cases(run.config)},
        {"campaign_identity", run.identity}, {"candidate_id", id}, {"phase", "qualification"}};
    if (run.state.contains("context_identity")) native_request["expected_context_identity"] = run.state.at("context_identity");
    if (run.config.contains("evaluation")) native_request["evaluation"] = run.config.at("evaluation");
    write_json(directory / "reproduce.json", native_request);
    const auto n = std::to_string(run.config.at("cases").at("train").size());
    const auto& summary = entry.at("train").at(n);
    const auto native_result = read_json(run.options.out / summary.at("response_file").get<std::string>());
    write_json(directory / "native-result.json", native_result);
    if (native_result.contains("artifacts")) {
        Json copied = Json::array();
        std::size_t index = 0;
        for (const auto& artifact : native_result.at("artifacts").as_array()) {
            if (!artifact.is_object() || !artifact.contains("path") || !artifact.contains("sha256")) continue;
            const fs::path source = artifact.at("path").get<std::string>();
            const auto destination = directory / "artifacts" / (std::to_string(index++) + "-" + artifact.at("sha256").get<std::string>() + "-" + source.filename().string());
            fs::create_directories(destination.parent_path());
            // An exported campaign remains usable after its original native cache
            // is pruned or the complete output directory moves to another worker.
            if (!fs::is_regular_file(destination)) {
                if (!fs::is_regular_file(source) || file_sha256(source) != artifact.at("sha256").get<std::string>())
                    throw std::runtime_error("C2 selected native artifact missing/changed: " + source.string());
                fs::copy_file(source, destination);
            }
            if (file_sha256(destination) != artifact.at("sha256").get<std::string>())
                throw std::runtime_error("C2 preserved native artifact changed: " + destination.string());
            Json saved = artifact; saved["path"] = fs::relative(destination, directory).generic_string(); copied.push_back(saved);
        }
        write_json(directory / "artifacts.json", copied);
    }
}

Json report(Run& run, const std::vector<std::size_t>& stages, const std::string& status) {
    const auto best = run.state.value("best", "");
    Json result{{"engine", "c2"}, {"status", status}, {"candidate_attempts", run.iteration},
        {"unique_candidates", run.state.at("entries").size()}, {"best_candidate", best},
        {"search_objective", "paired terminal win payoff on training keys; unhealthy results cannot select an incumbent"},
        {"selection", "training only; validation and final qualification are reporting-only"},
        {"coverage", Json{{"training_keys", run.config.at("cases").at("train").size()},
            {"validation_keys", run.config.at("cases").at("validation").size()},
            {"qualification_keys", run.config.at("cases").at("qualification").size()}}},
        {"limitations", Json::array({"Frozen-controller configuration optimization, not a universal draft score or validated combat model.",
            "Empirical candidate search and racing provide no global optimality bound.",
            "Screening audits measure potential discarded winners; repeated deterministic keys are reused, not new samples.",
            "All pool/legacy maps have historical use; split means withheld from this campaign, not previously unseen.",
            "Finalists require independent arena qualification before adoption."})}};
    if (!best.empty()) {
        result["best"] = run.state.at("entries").at(best);
        for (const std::string split : {"train", "validation", "qualification"}) {
            const auto count = std::to_string(run.config.at("cases").at(split).size());
            if (result["best"].contains(split) && result["best"][split].contains(count)) {
                const auto& summary = result["best"][split].at(count);
                const auto response = read_json(run.options.out / summary.at("response_file").get<std::string>());
                result["best"][split][count]["statistics"] = statistics_of(response);
            }
        }
    }
    result["screening_audits"] = run.state.value<Json>("audits", Json::array());
    if (run.state.contains("context_identity")) result["context_identity"] = run.state.at("context_identity");
    std::ostringstream text;
    text << "# Coupled configuration search\n\nStatus: " << status << ". Candidate attempts: " << run.iteration
         << "; unique configurations: " << run.state.at("entries").size() << ".\n\n";
    if (best.empty()) text << "No healthy candidate completed all training keys. There is no qualified incumbent.\n";
    else {
        const auto& entry = run.state.at("entries").at(best);
        const auto& train = entry.at("train").at(std::to_string(stages.back()));
        text << "Best full-training candidate: `" << best << "`, paired payoff " << train.at("score").get<double>()
             << " over " << stages.back() << " keys. See best/candidate.json and best/configuration.hpp.\n\n";
        for (const std::string split : {"validation", "qualification"}) {
            const auto n = std::to_string(run.config.at("cases").at(split).size());
            if (entry.contains(split) && entry.at(split).contains(n)) {
                const auto& value = entry.at(split).at(n);
                text << split << ": paired payoff " << value.at("score").get<double>() << " on " << n
                     << " keys, healthy=" << value.at("healthy").dump() << ".\n\n";
            } else text << split << ": unplayed.\n\n";
        }
    }
    text << "The exported policy is conditional on the pinned executable controller. Public-context trees choose configurations during drafting and freeze each gameplay instance's configuration at its first turn. Validation and qualification never influence mutations or parent selection.\n\n"
         << "A screening prefix is an economical selector, not a strength result. Audits promote some screened-out configurations to the complete training set. No optimum or competitive improvement is certified by this report.\n";
    atomic_write(run.options.out / "report.md", text.str());
    return result;
}
} // namespace

int run_c2(const Options& options) {
    Options opts = options;
    if (opts.input.empty()) {
        opts.input = opts.repo / "bots/contenders/tools/optimization/examples/c2-native.json";
        if (!fs::exists(opts.input)) throw std::runtime_error("C2 requires --input CONFIG.json");
    }
    Run run(opts, "c2", "3");
    run.config = expand_case_sets(run.config);
    c2::ContextConfigSpace space(run.config);
    validate_cases(run.config);
    const auto stages = training_stages(run.config);
    const auto search = run.config.value<Json>("search", Json::object());
    const auto population = search.value<std::size_t>("population", 16), elites = search.value<std::size_t>("elites", 6);
    const auto audit_every = search.value<std::uint64_t>("audit_every", 10), validation_every = search.value<std::uint64_t>("validation_every", 10);
    const auto fraction = search.value("promote_fraction", 0.35), random_fraction = search.value("random_fraction", 0.25);
    if (!population || !elites || fraction <= 0 || fraction > 1 || random_fraction < 0 || random_fraction > 1)
        throw std::runtime_error("bad C2 search settings");
    const auto seeds = run.config.value<Json>("seed_candidates", Json::array());
    if (!seeds.is_array()) throw std::runtime_error("C2 seed_candidates must contain complete native candidate genomes");
    for (const auto& seed : seeds.as_array()) {
        std::string why;
        if (!space.valid(seed, &why)) throw std::runtime_error("invalid C2 warm-start candidate: " + why);
    }
    if (!run.resumed) {
        run.state = Json{{"entries", Json::object()}, {"pending", nullptr}, {"best", ""}, {"audits", Json::array()}};
        run.checkpoint();
    }
    verify_saved_evidence(run);
    verify_context(run);
    std::string status = run.state.value("terminal_status", "completed");
    while (!run.stop() && status == "completed") {
        if (run.state.at("pending").is_null()) {
            Json knobs;
            bool found = false;
            for (int attempt = 0; attempt < 256 && !found; ++attempt) {
                if (!run.iteration && attempt == 0) knobs = space.baseline();
                else if (attempt == 0 && run.iteration > 0 && run.iteration <= seeds.size()) knobs = seeds.at(run.iteration - 1);
                else {
                    const auto parents = ranked_parents(run, stages.back(), elites);
                    const auto unit = std::generate_canonical<double, 53>(run.rng);
                    if (parents.empty() || unit < random_fraction) knobs = space.sample(run.rng);
                    else {
                        const auto& a = run.state.at("entries").at(parents[run.rng() % parents.size()]).at("candidate");
                        if (parents.size() > 1 && (run.rng() & 1)) {
                            const auto& b = run.state.at("entries").at(parents[run.rng() % parents.size()]).at("candidate");
                            knobs = space.crossover(a, b, run.rng);
                        } else knobs = a;
                        knobs = space.mutate(knobs, run.rng, 1 + static_cast<unsigned>(run.rng() % 3));
                    }
                }
                std::string why;
                if (!space.valid(knobs, &why)) throw std::runtime_error("internal C2 invalid proposal: " + why);
                const auto id = sha256(knobs.dump());
                if (run.state.at("entries").contains(id)) continue;
                run.state["entries"][id] = Json{{"candidate", knobs}, {"train", Json::object()}, {"complete", false}, {"rejected", false}, {"proposal", run.iteration}};
                run.state["pending"] = Json{{"id", id}, {"stage", 0}, {"audited", false}, {"would_screen", false}, {"validation_done", false}};
                found = true;
            }
            if (!found) { status = "sampling_plateau"; run.state["terminal_status"] = status; run.checkpoint(); break; }
            // Save the sampled candidate AND post-sampling RNG before any expensive call.
            // A killed evaluator resumes this candidate, not another draw from the space.
            run.checkpoint();
        }
        const auto id = run.state.at("pending").at("id").get<std::string>();
        const auto knobs = run.state.at("entries").at(id).at("candidate");
        if (!space.valid(knobs)) throw std::runtime_error("checkpoint contains invalid C2 configuration");
        bool screened = run.state.at("pending").value("screened", false);
        std::size_t stage = run.state.at("pending").at("stage").get<std::size_t>();
        for (; !screened && stage < stages.size() && !run.stop(); ++stage) {
            const auto summary = evaluate_split(run, id, knobs, "train", stages[stage]);
            run.state["pending"]["stage"] = stage + 1;
            if (!summary.value("healthy", false)) {
                run.state["entries"][id]["rejected"] = true;
                run.state["entries"][id]["rejection"] = "health";
                screened = true; run.state["pending"]["screened"] = true; break;
            }
            if (stage + 1 < stages.size()) {
                const bool ordinary = promote(run, id, stages[stage], summary.at("score").get<double>(), fraction, population);
                const bool audit = !ordinary && audit_every && ((run.iteration + 1) % audit_every == 0);
                if (!ordinary) { run.state["pending"]["would_screen"] = true; run.state["entries"][id]["screened_at"] = stages[stage]; }
                if (audit) { run.state["pending"]["audited"] = true; run.state["entries"][id]["audited"] = true; }
                if (!ordinary && !audit && !run.state.at("pending").value("audited", false)) {
                    run.state["entries"][id]["rejected"] = true;
                    run.state["entries"][id]["rejection"] = "screened";
                    screened = true; run.state["pending"]["screened"] = true; break;
                }
            }
            run.checkpoint();
        }
        // When stopped inside a stage sequence the pending work is preserved for resume.
        if (!screened && stage < stages.size()) { status = "stopped"; run.checkpoint(); break; }
        bool changed_best = false;
        if (!run.state.at("pending").value("finalized", false) && !screened) {
            auto& entry = run.state["entries"][id];
            entry["complete"] = true;
            const auto candidate_score = entry.at("train").at(std::to_string(stages.back())).at("score").get<double>();
            const auto previous = run.state.value("best", "");
            if (previous.empty() || candidate_score > run.state.at("entries").at(previous).at("train").at(std::to_string(stages.back())).at("score").get<double>()) {
                run.state["best"] = id; changed_best = true;
                run.event(Json{{"event", "new_training_incumbent"}, {"candidate", id}, {"score", candidate_score}});
                export_best(run);
            }
            if (run.state.at("pending").value("audited", false)) {
                run.state["audits"].push_back(Json{{"candidate", id}, {"full_score", candidate_score}, {"became_training_incumbent", changed_best}});
            }
        }
        const auto best = run.state.value("best", "");
        if (!run.state.at("pending").value("finalized", false)) {
            run.state["pending"]["validation_candidate"] = !best.empty()
                && (changed_best || (validation_every && (run.iteration + 1) % validation_every == 0)) ? best : "";
            run.state["pending"]["finalized"] = true;
            run.checkpoint();
        }
        const auto validation_candidate = run.state.at("pending").value("validation_candidate", "");
        if (!validation_candidate.empty() && !run.state.at("pending").value("validation_done", false)) {
            if (run.stop()) { status = "stopped"; run.checkpoint(); break; }
            evaluate_split(run, validation_candidate, run.state.at("entries").at(validation_candidate).at("candidate"), "validation", run.config.at("cases").at("validation").size());
            run.state["pending"]["validation_done"] = true;
        }
        const auto completed_id = id;
        run.state["pending"] = nullptr;
        run.advance();
        const auto current_best = run.state.value("best", "");
        const Json best_score = current_best.empty() ? Json(nullptr)
            : run.state.at("entries").at(current_best).at("train").at(std::to_string(stages.back())).at("score");
        run.progress(Json{{"candidate", completed_id}, {"unique_candidates", run.state.at("entries").size()},
            {"best_candidate", current_best}, {"best_training_score", best_score}, {"screened_or_unhealthy", screened}});
        run.checkpoint();
    }
    if (run.iteration < opts.iterations && status == "completed") status = "stopped";
    const auto best = run.state.value("best", "");
    const bool within_time = run.remaining_seconds() > 0;
    if (!best.empty() && run.iteration >= opts.iterations && within_time && !interrupted()
        && search.value("qualification_on_finish", false)) {
        // The candidate is frozen before touching the qualification set. Its score is
        // never read by mutation, promotion, parent selection, or incumbent choice.
        evaluate_split(run, best, run.state.at("entries").at(best).at("candidate"), "qualification", run.config.at("cases").at("qualification").size());
    }
    export_best(run);
    run.checkpoint();
    run.finish(report(run, stages, status));
    std::cout << "C2 " << status << ": " << run.iteration << " candidates; best=" << best << "\n";
    return 0;
}
} // namespace opt
