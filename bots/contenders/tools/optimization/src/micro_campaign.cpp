#include "opt/micro_campaign.hpp"
#include "opt/micro.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>

namespace opt::micro {
Options native_options(const Options&);
bool campaign_requested(const Json& config) {
    return config.contains("cases") || config.contains("inventory");
}
}
namespace opt {
bool micro_campaign_requested(const Json& config) { return micro::campaign_requested(config); }
namespace {
Json merged(Json left, const Json& right) {
    if (!left.is_object() || !right.is_object()) return right;
    for (const auto& [key, value] : right.as_object())
        left[key] = left.contains(key) && left.at(key).is_object() && value.is_object() ? merged(left.at(key), value) : value;
    return left;
}
double number(const Json& value, const std::string& field, double fallback = 0) {
    return value.contains(field) && value.at(field).is_number() ? value.at(field).get<double>() : fallback;
}
const Json* champion(const Json& state, int id) {
    if (!state.contains("champions")) return nullptr;
    for (const auto& c : state.at("champions").as_array()) if (c.value("id", -1) == id) return &c;
    return nullptr;
}
double distance(const Json& a, const Json& b) {
    return std::max(std::abs(number(a, "x") - number(b, "x")), std::abs(number(a, "y") - number(b, "y")));
}
Json dependencies(const Options& options) {
    Json result = Json::object();
    // Run already binds custom evaluator executables and script arguments. Do
    // not invalidate an independent adapter campaign on unused native files.
    if (options.has("evaluator")) return Json{{"custom_evaluator", true}};
    for (const auto& relative : {"bots/contenders/tools/optimization/adapters/native_micro.py",
         "bots/contenders/tools/optimization/adapters/native_micro.mjs", "tools/replay-profiles.json", "tools/replay-interaction-native.mjs"}) {
        const fs::path path = options.repo / relative;
        if (fs::is_regular_file(path)) result[relative] = file_sha256(path);
    }
    return result;
}
std::string snapshot_replay(Run& run, Json& scenario) {
    if (!scenario.is_object() || !scenario.contains("replay")) throw std::runtime_error("micro case needs scenario.replay");
    fs::path source = scenario.at("replay").get<std::string>();
    if (source.is_relative()) source = run.options.repo / source;
    const std::string hash = file_sha256(source);
    if (scenario.contains("replay_sha256") && scenario.at("replay_sha256").get<std::string>() != hash)
        throw std::runtime_error("micro campaign replay content changed: " + source.string());
    const auto destination = run.options.out / "replays" / (hash + ".repl26a");
    if (!fs::exists(destination)) atomic_write(destination, read_text(source));
    if (file_sha256(destination) != hash) throw std::runtime_error("micro campaign retained replay copy changed");
    scenario["replay"] = fs::absolute(destination).string();
    scenario["replay_sha256"] = hash;
    return hash;
}
bool add_case(Run& run, Json item, const Json& metadata) {
    if (!item.is_object()) throw std::runtime_error("micro campaign cases must be objects");
    Json config = merged(run.config.value<Json>("defaults", Json::object()), item);
    config.as_object().erase("id"); config.as_object().erase("metadata");
    config.as_object().erase("cases"); config.as_object().erase("inventory"); config.as_object().erase("campaign");
    config["schema"] = "contenders-micro/v1";
    if (!config.contains("scenario") || !config.contains("objective")) throw std::runtime_error("micro case needs scenario and objective");
    auto& scenario = config["scenario"];
    snapshot_replay(run, scenario);
    const int actor = scenario.value("actor", -1), input = scenario.value("input", -1);
    if (actor < 0 || actor > 15 || input < 0) throw std::runtime_error("micro case must bind an actual input and actor");
    if (!scenario.contains("participants")) scenario["participants"] = Json::array({actor});
    if (!scenario.contains("background_policy")) scenario["background_policy"] = "yield";
    const std::string kind = config.at("objective").value("kind", "");
    if (kind != "combat" && kind != "farm") throw std::runtime_error("micro case objective must be combat or farm");
    Json canonical = config;
    canonical["scenario"]["replay"] = "sha256:" + scenario.at("replay_sha256").get<std::string>();
    const std::string key = sha256(canonical.dump());
    if (run.state["case_states"].contains(key)) {
        run.state["duplicate_cases"] = run.state.value<std::uint64_t>("duplicate_cases", 0) + 1;
        return false;
    }
    const auto relative = fs::path("cases") / key;
    const auto input_path = relative / "input.json";
    write_json(run.options.out / input_path, config);
    Json entry{{"key", key}, {"id", item.value("id", key.substr(0, 12))}, {"config", input_path.generic_string()},
        {"config_sha256", file_sha256(run.options.out / input_path)},
        {"directory", (relative / "run").generic_string()}, {"kind", kind}, {"scenario", scenario}, {"metadata", metadata}};
    run.state["cases"].push_back(entry);
    run.state["case_states"][key] = Json{{"evaluations", 0}, {"status", "pending"}, {"attempts", 0}};
    return true;
}
struct Candidate { Json config, metadata; std::vector<std::string> features; std::string source; int input{}; };
std::vector<Candidate> proposals(const Json& source, const Json& inventory, const Json& settings) {
    if (!inventory.value("native", false) || !inventory.value("supported", false) || !inventory.contains("states"))
        throw std::runtime_error("micro inventory must be supported actual native state");
    std::vector<Candidate> out;
    const double combat_radius = settings.value("combat_radius", 12.0), farm_radius = settings.value("farm_radius", 12.0);
    const int round_bucket = std::max(1, settings.value("round_bucket", 100));
    const std::string source_id = source.value("id", source.at("replay").get<std::string>());
    const std::string family = source.value("family", source_id);
    std::set<std::string> kinds;
    if (settings.contains("kinds")) for (const auto& kind : settings.at("kinds").as_array()) kinds.insert(kind.get<std::string>());
    else kinds = {"combat", "farm"};
    for (const auto& state : inventory.at("states").as_array()) {
        const int owner = state.value("owner", -1), input = state.value("input", -1);
        const Json* actor = champion(state, owner);
        if (!actor || owner < 0 || input < 0 || !actor->value("alive", true) || number(*actor, "health") <= 0) continue;
        std::set<int> visible;
        if (actor->contains("personalVisibleChampions")) for (const auto& id : actor->at("personalVisibleChampions").as_array()) visible.insert(id.get<int>());
        const Json* enemy = nullptr;
        for (const auto& c : state.at("champions").as_array()) if (c.value("team", -1) != actor->value("team", -1) &&
            c.value("alive", true) && number(c, "health") > 0 && visible.count(c.value("id", -1)) && distance(*actor, c) <= combat_radius &&
            (!enemy || distance(*actor, c) < distance(*actor, *enemy))) enemy = &c;
        const Json* camp = nullptr;
        if (state.contains("camps")) for (const auto& c : state.at("camps").as_array()) if (c.value("group_id", -1) >= 0 &&
            number(c, "health") > 0 && c.value("visible", true) && distance(*actor, c) <= farm_radius &&
            (!camp || distance(*actor, c) < distance(*actor, *camp))) camp = &c;
        const int hp_bucket = std::clamp(static_cast<int>(4 * number(*actor, "health") / std::max(1.0, number(*actor, "maxHealth", number(*actor, "health")))), 0, 3);
        const std::string kit = std::to_string(actor->value("type", -1));
        const std::string items = actor->contains("items") ? actor->at("items").dump() : "[]";
        const std::string cooldowns = actor->contains("cooldowns") ? actor->at("cooldowns").dump() : "[]";
        const std::string availability = actor->contains("availability") ? actor->at("availability").dump() : "{}";
        const std::string channel = actor->contains("channel") ? actor->at("channel").dump() : "{}";
        for (const auto& kind : kinds) {
            if ((kind == "combat" && !enemy) || (kind == "farm" && !camp) || (kind != "combat" && kind != "farm")) continue;
            Json scenario = source;
            for (const auto& key : {"id", "family", "first_input", "last_input"}) scenario.as_object().erase(key);
            scenario["input"] = input; scenario["actor"] = owner;
            scenario["participants"] = kind == "combat" ? Json::array({owner, enemy->at("id")}) : Json::array({owner});
            if (!scenario.contains("background_policy")) scenario["background_policy"] = "reactive-v1";
            Json objective{{"kind", kind}};
            if (kind == "farm") { objective["group"] = camp->at("group_id"); objective["require_capture"] = true; objective["require_depart"] = true; }
            Json metadata{{"selection", "actual_owner_visible_replay_inventory"}, {"source_id", source_id}, {"family", family},
                {"kit", actor->value("type", -1)}, {"items", actor->value<Json>("items", Json::array())}, {"health_bucket", hp_bucket},
                {"cooldowns", actor->value<Json>("cooldowns", Json::array())}, {"availability", actor->value<Json>("availability", Json::object())},
                {"round", state.value("round", 0)}, {"source_input", input}, {"owner", owner}, {"scope", "unchanged retained native boundary; no reconstructed kits or hidden-state restore"}};
            const std::string opponent = kind == "combat" ? std::to_string(enemy->value("type", -1)) : "camp:" + std::to_string(camp->value("type", -1));
            std::vector<std::string> features{"kind:" + kind, "family:" + family, "kit:" + kit, "items:" + items,
                "hp:" + std::to_string(hp_bucket), "cooldowns:" + cooldowns, "availability:" + availability,
                "channel:" + channel, "timing:" + std::to_string(state.value("round", 0) / round_bucket), "opponent:" + opponent};
            metadata["opponent_family"] = opponent;
            out.push_back({Json{{"id", source_id + "-" + kind + "-" + std::to_string(input) + "-" + std::to_string(owner)},
                {"scenario", scenario}, {"objective", objective}}, metadata, features, source_id, input});
        }
    }
    return out;
}
void mine(Run& run) {
    if (!run.config.contains("inventory") || run.state.value("mining_complete", false)) return;
    const Json& settings = run.config.at("inventory");
    if (!settings.is_object() || !settings.contains("sources")) throw std::runtime_error("micro inventory needs sources array");
    const auto& sources = settings.at("sources").as_array();
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (run.stop()) return;
        Json source = sources[i];
        if (!source.is_object() || !source.contains("replay")) throw std::runtime_error("inventory source needs replay");
        const int first = source.value("first_input", 0), last = source.value("last_input", first + 3000);
        if (first < 0 || last < first || last - first > 3000) throw std::runtime_error("inventory interval must contain at most3001 native inputs");
        Json scenario = source;
        for (const auto& key : {"id", "family", "first_input", "last_input"}) scenario.as_object().erase(key);
        snapshot_replay(run, scenario);
        source["replay"] = scenario.at("replay"); source["replay_sha256"] = scenario.at("replay_sha256");
        const std::string key = sha256(source.dump());
        const auto relative = fs::path("inventory") / (key + ".json");
        Json inventory;
        if (run.state["inventories"].contains(key)) {
            const Json& saved = run.state.at("inventories").at(key);
            if (file_sha256(run.options.out / relative) != saved.at("sha256").get<std::string>()) throw std::runtime_error("saved micro inventory changed");
            inventory = read_json(run.options.out / relative);
        } else {
            Json request{{"schema", "contenders-micro/v1"}, {"kind", "inventory"}, {"scenario", scenario}, {"first_input", first}, {"last_input", last}};
            inventory = evaluate(micro::native_options(run.options), request, run.options.out / "inventory" / "calls" / key);
            if (inventory.at("identity").at("replay_sha256") != scenario.at("replay_sha256")) throw std::runtime_error("native inventory replay binding mismatch");
            write_json(run.options.out / relative, inventory);
            run.state["inventories"][key] = Json{{"path", relative.generic_string()}, {"sha256", file_sha256(run.options.out / relative)}, {"states", inventory.at("states").size()}};
            run.checkpoint();
        }
        auto found = proposals(source, inventory, settings);
        candidates.insert(candidates.end(), found.begin(), found.end());
    }
    const std::size_t maximum = settings.value<std::size_t>("max_cases", 16), per_source = settings.value<std::size_t>("per_source", maximum);
    const int gap = settings.value("min_input_gap", 8);
    if (!maximum || maximum > 4096 || !per_source || gap < 0) throw std::runtime_error("invalid micro mining limits");
    std::map<std::string, unsigned> coverage;
    std::map<std::string, std::vector<int>> selected_inputs;
    std::set<std::size_t> selected;
    Json selected_rows = Json::array();
    while (selected.size() < maximum) {
        std::size_t best = candidates.size(); double best_score = -1;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const auto& candidate = candidates[i];
            const auto& inputs = selected_inputs[candidate.source];
            if (selected.count(i) || inputs.size() >= per_source || std::any_of(inputs.begin(), inputs.end(), [&](int value) { return std::abs(value - candidate.input) < gap; })) continue;
            double score = 0;
            for (std::size_t j = 0; j < candidate.features.size(); ++j) score += (j == 0 ? 5.0 : j == 1 || j == 2 ? 3.0 : 1.0) / (1.0 + coverage[candidate.features[j]]);
            if (score > best_score) { best = i; best_score = score; }
        }
        if (best == candidates.size()) break;
        const auto& candidate = candidates[best]; selected.insert(best);
        selected_inputs[candidate.source].push_back(candidate.input);
        for (const auto& feature : candidate.features) ++coverage[feature];
        if (add_case(run, candidate.config, candidate.metadata)) selected_rows.push_back(candidate.metadata);
    }
    Json covered = Json::object(); for (const auto& [key, count] : coverage) covered[key] = count;
    run.state["mining"] = Json{{"native_inventory_sources", sources.size()}, {"candidate_contexts", candidates.size()},
        {"selected", selected_rows}, {"stratum_coverage", covered}, {"selection", "deterministic rarity-weighted diversity with exact-case dedup and per-source temporal spacing"}};
    run.state["mining_complete"] = true;
    write_json(run.options.out / "mined-cases.json", run.state.at("mining"));
    run.checkpoint();
}
std::uint64_t child_count(const fs::path& directory, Json* checkpoint = nullptr, const fs::path& input = {}) {
    if (!fs::is_regular_file(directory / "checkpoint.json")) return 0;
    const Json saved = read_json(directory / "checkpoint.json");
    Json body = saved; body.as_object().erase("checkpoint_sha256");
    if (saved.value("schema", "") != "contenders-checkpoint/v1" || saved.value("checkpoint_sha256", "") != sha256(body.dump()))
        throw std::runtime_error("micro child checkpoint checksum/schema mismatch");
    const Json manifest = read_json(directory / "run.json");
    if (saved.value("identity", "") != manifest.value("identity", "")) throw std::runtime_error("micro child checkpoint identity mismatch");
    if (!input.empty() && manifest.at("specification").at("config") != read_json(input))
        throw std::runtime_error("micro child manifest configuration mismatch");
    if (checkpoint) *checkpoint = saved;
    return saved.value<std::uint64_t>("iteration", 0);
}
bool runnable(const Json& saved, std::uint64_t cap) {
    const auto status = saved.value("status", "");
    return saved.value<std::uint64_t>("evaluations", 0) < cap && status != "failed" && status != "exhausted" && status != "resource_limited";
}
void export_campaign(Run& run) {
    Json rows = Json::array(), coverage = Json::object(), families = Json::object(), kits = Json::object();
    std::uint64_t complete = 0, failures = 0, improvements = 0;
    for (const auto& entry : run.state.at("cases").as_array()) {
        const std::string key = entry.at("key").get<std::string>();
        Json row = entry; row["search"] = run.state.at("case_states").at(key);
        const auto directory = run.options.out / entry.at("directory").get<std::string>();
        if (fs::is_regular_file(directory / "report.json")) {
            const Json report = read_json(directory / "report.json");
            Json comparison{{"status", report.value("status", "unknown")}, {"native_evaluations", report.value<std::uint64_t>("native_evaluations", 0)},
                {"report", (fs::path(entry.at("directory").get<std::string>()) / "report.json").generic_string()}};
            if (report.contains("baseline")) comparison["baseline"] = report.at("baseline");
            if (report.contains("best")) {
                comparison["best"] = report.at("best");
                if (report.at("best").value("task_complete", false)) ++complete;
            }
            if (report.contains("best") && report.contains("baseline") && number(report.at("best"), "value") > number(report.at("baseline"), "value") + 1e-9) ++improvements;
            for (const auto& name : {"principal_variation", "root_action", "native_identity", "elapsed_seconds", "nodes", "backend_authority", "horizon"}) if (report.contains(name)) comparison[name] = report.at(name);
            comparison["native_health"] = Json{{"last_child_exit_code", row.at("search").value("exit_code", -1)},
                {"report_matches_checkpoint", report.contains("native_evaluations") && report.at("native_evaluations").get<std::uint64_t>() == row.at("search").value<std::uint64_t>("evaluations", 0)},
                {"scope", "Native backend/process status; rejected action proposals remain explicit search evidence, not assumed engine faults."}};
            Json artifacts = Json::object();
            for (const auto& file : {"frontier.json", "policy.json", "native-trace.json", "baseline.json", "root-state.json", "candidate-actions.csv"})
                if (fs::is_regular_file(directory / file)) artifacts[file] = (fs::path(entry.at("directory").get<std::string>()) / file).generic_string();
            comparison["artifacts"] = artifacts;
            row["comparison"] = comparison;
        }
        if (row.at("search").value("status", "") == "failed") ++failures;
        const std::string kind = entry.at("kind").get<std::string>(); coverage[kind] = coverage.value<std::uint64_t>(kind, 0) + 1;
        const std::string family = entry.at("metadata").value("family", "explicit_case");
        families[family] = families.value<std::uint64_t>(family, 0) + 1;
        if (entry.at("metadata").contains("kit")) {
            const auto kit = entry.at("metadata").at("kit").dump(); kits[kit] = kits.value<std::uint64_t>(kit, 0) + 1;
        }
        rows.push_back(row);
    }
    write_json(run.options.out / "case-comparisons.json", Json{{"schema", "contenders-micro-campaign-comparisons/v1"}, {"cases", rows},
        {"task_complete_cases", complete}, {"improved_local_objective_cases", improvements}, {"failed_cases", failures},
        {"coverage", Json{{"objectives", coverage}, {"source_families", families}, {"mined_kits", kits}}},
        {"scope", "Per-case native finite-horizon comparisons. Values across different objectives, kits or worlds are not averaged or ranked."}});
    std::ostringstream text;
    text << "# Native micro campaign\n\n" << rows.size() << " retained cases; " << run.iteration << " evaluated native prefixes.\n\n"
         << "| Case | Objective | Prefixes | Status |\n|---|---|---:|---|\n";
    for (const auto& row : rows.as_array()) text << "| " << row.at("id").get<std::string>() << " | " << row.at("kind").get<std::string>() << " | "
        << row.at("search").value<std::uint64_t>("evaluations", 0) << " | " << row.at("search").value("status", "pending") << " |\n";
    text << "\nOpen case-comparisons.json for each baseline, principal variation and frontier. Every case uses its unchanged retained replay prefix; other-owner behavior is declared in that case. These are local search results, not complete-game strength or an optimality proof.\n";
    atomic_write(run.options.out / "campaign.md", text.str());
}
}
int run_micro_campaign(const Options& options) {
    Run run(options, "c12-native-micro-campaign", "1");
    const Json deps = dependencies(options);
    if (run.resumed) {
        if (run.state.at("dependencies") != deps) throw std::runtime_error("micro campaign backend/sampler/profile changed; use a new output directory");
    } else {
        run.state = Json{{"dependencies", deps}, {"cases", Json::array()}, {"case_states", Json::object()}, {"inventories", Json::object()},
            {"duplicate_cases", 0}, {"explicit_complete", false}, {"mining_complete", !run.config.contains("inventory")}, {"next_case", 0}};
        run.checkpoint();
    }
    if (!run.state.value("explicit_complete", false)) {
        if (run.config.contains("cases")) for (const auto& item : run.config.at("cases").as_array()) add_case(run, item, item.value<Json>("metadata", Json::object()));
        run.state["explicit_complete"] = true; run.checkpoint();
    }
    try { mine(run); }
    catch (const std::exception&) { if (!run.stop()) throw; }
    const auto& cases = run.state.at("cases");
    for (const auto& [key, saved] : run.state.at("inventories").as_object()) {
        (void)key;
        if (file_sha256(run.options.out / saved.at("path").get<std::string>()) != saved.at("sha256").get<std::string>())
            throw std::runtime_error("retained micro inventory changed");
    }
    // A contained child may commit a checkpoint before the parent is interrupted.
    // Reconcile every case before assigning another global quota, including the
    // previous in-flight case when the round-robin cursor already moved on.
    for (const auto& entry : cases.as_array()) {
        if (file_sha256(entry.at("scenario").at("replay").get<std::string>()) != entry.at("scenario").at("replay_sha256").get<std::string>())
            throw std::runtime_error("retained micro replay changed");
        if (file_sha256(run.options.out / entry.at("config").get<std::string>()) != entry.at("config_sha256").get<std::string>())
            throw std::runtime_error("retained micro child configuration changed");
        auto& saved = run.state["case_states"][entry.at("key").get<std::string>()];
        const auto actual = child_count(run.options.out / entry.at("directory").get<std::string>(), nullptr,
                                        run.options.out / entry.at("config").get<std::string>());
        const auto accounted = saved.value<std::uint64_t>("evaluations", 0);
        if (actual < accounted) throw std::runtime_error("micro child checkpoint regressed");
        if (actual > accounted) { run.advance(actual - accounted); saved["evaluations"] = actual; }
        if (saved.value("status", "") == "running" || saved.value("status", "") == "paused_at_limit" || saved.value("status", "") == "paused_anytime") saved["status"] = "partial";
    }
    run.state.as_object().erase("in_flight");
    run.checkpoint();
    const Json campaign = run.config.value<Json>("campaign", Json::object());
    const std::uint64_t per_case = campaign.value<std::uint64_t>("nodes_per_case", std::max<std::uint64_t>(1, options.iterations / std::max<std::size_t>(1, cases.size())));
    if (!per_case) throw std::runtime_error("campaign nodes_per_case must be positive");
    bool paused_at_child_limit = false;
    bool paused_anytime = false;
    while (!run.stop() && !cases.empty() && run.state.value("mining_complete", false)) {
        std::size_t remaining_cases = 0;
        std::uint64_t active_spent = 0;
        for (const auto& entry : cases.as_array()) {
            const auto& saved = run.state.at("case_states").at(entry.at("key").get<std::string>());
            if (runnable(saved, per_case)) {
                ++remaining_cases; active_spent += saved.value<std::uint64_t>("evaluations", 0);
            }
        }
        if (!remaining_cases) break;
        const std::size_t cursor = run.state.value<std::size_t>("next_case", 0) % cases.size();
        const auto& entry = cases.at(cursor);
        const std::string key = entry.at("key").get<std::string>();
        auto& saved = run.state["case_states"][key];
        run.state["next_case"] = (cursor + 1) % cases.size();
        if (!runnable(saved, per_case)) continue;
        const auto directory = run.options.out / entry.at("directory").get<std::string>();
        const std::uint64_t old_count = saved.value<std::uint64_t>("evaluations", 0);
        Json child_checkpoint;
        const auto actual_count = child_count(directory, &child_checkpoint, run.options.out / entry.at("config").get<std::string>());
        if (actual_count < old_count) throw std::runtime_error("micro child checkpoint regressed");
        if (actual_count > old_count) { run.advance(actual_count - old_count); saved["evaluations"] = actual_count; }
        if (run.stop()) { run.checkpoint(); break; }
        const auto global_remaining = options.iterations - run.iteration;
        std::size_t rank = 0;
        for (std::size_t i = 0; i < cursor; ++i) {
            const auto& prior = run.state.at("case_states").at(cases.at(i).at("key").get<std::string>());
            if (runnable(prior, per_case)) ++rank;
        }
        const auto total_active_budget = active_spent + global_remaining;
        const auto quota = std::min(per_case, total_active_budget / remaining_cases + (rank < total_active_budget % remaining_cases ? 1 : 0));
        if (quota <= actual_count) continue;
        const auto target = std::min(quota, actual_count + global_remaining);
        std::vector<std::string> command{options.executable.string(), "c12", "--repo", options.repo.string(), "--input", (run.options.out / entry.at("config").get<std::string>()).string(),
            "--out", directory.string(), "--iterations", std::to_string(target), "--seed", std::to_string(options.seed ^ std::stoull(key.substr(0, 16), nullptr, 16)), "--threads", std::to_string(options.threads)};
        if (fs::is_regular_file(directory / "run.json")) command.push_back("--resume");
        for (const auto& [flag, values] : options.extra) for (const auto& value : values)
            command.push_back("--" + flag + "=" + value);
        if (options.seconds > 0) {
            const double child_seconds = std::max(0.01, run.remaining_seconds() - 0.2);
            command.insert(command.end(), {"--seconds", std::to_string(child_seconds)});
        }
        saved["status"] = "running"; saved["attempts"] = saved.value<std::uint64_t>("attempts", 0) + 1;
        run.state["in_flight"] = key; run.checkpoint();
        const auto attempt = run.options.out / "case-calls" / key / std::to_string(saved.at("attempts").get<std::uint64_t>());
        const auto result = run_process(command, options.repo, attempt / "stdout.log", attempt / "stderr.log", options.seconds > 0 ? run.remaining_seconds() : 0);
        const auto new_count = child_count(directory, nullptr, run.options.out / entry.at("config").get<std::string>());
        if (new_count < actual_count || new_count > target) throw std::runtime_error("micro child evaluation count escaped assigned quota");
        run.advance(new_count - actual_count); saved["evaluations"] = new_count; saved["exit_code"] = result.exit_code;
        std::string child_status;
        if (fs::is_regular_file(directory / "run-status.json")) child_status = read_json(directory / "run-status.json").value("status", "");
        std::string terminal_status;
        if (fs::is_regular_file(directory / "report.json")) {
            const auto report = read_json(directory / "report.json");
            if (report.value<std::uint64_t>("native_evaluations", 0) == new_count) terminal_status = report.value("status", "");
        }
        saved["child_terminal_status"] = terminal_status;
        paused_at_child_limit = child_status == "time_limit_checkpointed" || child_status == "interrupted_checkpointed";
        if (result.timed_out || interrupted() || time_limit_reached() || paused_at_child_limit) saved["status"] = "paused_at_limit";
        else if (result.exit_code != 0) saved["status"] = "failed";
        else if (terminal_status == "resource_limited") saved["status"] = "resource_limited";
        else if (terminal_status == "exhausted_declared_tree") saved["status"] = "exhausted";
        else if (new_count >= per_case) saved["status"] = "case_budget_complete";
        else if (new_count > actual_count) saved["status"] = "partial";
        else { saved["status"] = "paused_anytime"; paused_anytime = true; }
        run.state.as_object().erase("in_flight");
        run.event(Json{{"kind", "micro_case"}, {"case", key}, {"evaluations", new_count}, {"status", saved.at("status")}});
        run.progress(Json{{"cases", cases.size()}, {"native_evaluations", run.iteration}, {"last_case", entry.at("id")}});
        run.checkpoint(); export_campaign(run);
        if (paused_at_child_limit || paused_anytime) break;
    }
    export_campaign(run);
    std::uint64_t failed = 0, completed = 0, resource_capped = 0;
    for (const auto& [key, saved] : run.state.at("case_states").as_object()) {
        (void)key;
        if (saved.value("status", "") == "failed") ++failed;
        if (saved.value("status", "") == "resource_limited") ++resource_capped;
        if (saved.value<std::uint64_t>("evaluations", 0) >= per_case || saved.value("status", "") == "exhausted") ++completed;
    }
    Json result{{"schema", "contenders-micro-campaign-result/v1"}, {"status", failed ? "completed_with_case_failures" : run.stop() || paused_at_child_limit ? "stopped_at_limit_with_checkpoints" : paused_anytime ? "paused_anytime_with_checkpoints" : resource_capped ? "resource_limited" : "case_budgets_complete_or_exhausted"},
        {"native_evaluations", run.iteration}, {"cases", cases.size()}, {"duplicate_cases", run.state.at("duplicate_cases")},
        {"completed_or_exhausted_cases", completed}, {"failed_cases", failed}, {"resource_capped_cases", resource_capped},
        {"per_case_budget", per_case}, {"mining_complete", run.state.at("mining_complete")}, {"comparisons", "case-comparisons.json"}, {"human_report", "campaign.md"},
        {"scope", "Exact retained native prefixes and actual kits/items. Per-case frontiers and feasible controls; no cross-world heuristic average or complete-game strength claim."}};
    if (run.state.contains("mining")) result["mining"] = run.state.at("mining");
    run.checkpoint(); run.finish(result); return 0;
}
}
