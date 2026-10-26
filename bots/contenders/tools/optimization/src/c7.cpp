#include "opt/common.hpp"
#include "opt/config_space.hpp"
#include "opt/league.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>

namespace opt {
namespace {

using Matrix = league::Matrix;

Json numbers(const std::vector<double>& values) {
    Json result = Json::array();
    for (double value : values) result.push_back(value);
    return result;
}

std::vector<std::string> strings(const Json& values) {
    if (!values.is_array()) throw std::runtime_error("expected an array of strings");
    std::vector<std::string> result;
    std::set<std::string> seen;
    for (const auto& value : values.as_array()) {
        const auto text = value.get<std::string>();
        if (text.empty() || !seen.insert(text).second) throw std::runtime_error("empty or duplicate league identifier");
        result.push_back(text);
    }
    return result;
}

Json defaults() {
    return {{"space", Json::array({
        {{"name", "draft_mode"}, {"type", "categorical"}, {"default", 8}, {"values", Json::array({0,1,3,8,18,19})}},
        {{"name", "shop_plan"}, {"type", "categorical"}, {"default", 7}, {"values", Json::array({6,7,14})}},
        {{"name", "squad_size"}, {"type", "categorical"}, {"default", 1}, {"values", Json::array({1,2,3})}},
        {{"name", "tribute_gems"}, {"type", "categorical"}, {"default", 25}, {"values", Json::array({15,25,40,60,100})}},
        {{"name", "early_shop_gold"}, {"type", "categorical"}, {"default", 0}, {"values", Json::array({0,90,150,240,360})}},
        {{"name", "recall_health_frac"}, {"type", "categorical"}, {"default", 0.5}, {"values", Json::array({0.35,0.5,0.65})}}
    })}};
}

Matrix parse_matrix(const Json& config) {
    Matrix result;
    const auto& rows = config.at("payoffs").as_array();
    for (const auto& row : rows) {
        std::vector<double> values;
        for (const auto& value : row.as_array()) values.push_back(value.get<double>());
        result.push_back(std::move(values));
    }
    league::validate(result);
    return result;
}

int matrix_mode(const Options& options) {
    Run run(options, "c7", "1");
    const auto& config = run.config.at("matrix");
    const Matrix payoff = parse_matrix(config);
    const auto policies = strings(config.at("policies"));
    const auto opponents = strings(config.at("opponents"));
    if (policies.size() != payoff.size() || opponents.size() != payoff.front().size())
        throw std::runtime_error("matrix labels do not match matrix dimensions");
    league::FictitiousPlay solver(policies.size(), opponents.size());
    if (run.resumed) solver.restore(run.state.at("solver"));
    while (!run.stop()) {
        solver.step(payoff);
        run.advance();
        if (run.iteration % 1000 == 0) {
            run.state["solver"] = solver.checkpoint();
            run.checkpoint();
            const auto solution = league::bounds(payoff, solver.row_counts, solver.column_counts);
            run.progress({{"empirical_lower", solution.lower}, {"empirical_upper", solution.upper}, {"empirical_gap", solution.gap}});
        }
    }
    run.state["solver"] = solver.checkpoint();
    run.checkpoint();
    Json report = league::json(league::bounds(payoff, solver.row_counts, solver.column_counts));
    report["mode"] = "provided_matrix";
    report["policies"] = config.at("policies");
    report["opponents"] = config.at("opponents");
    report["provenance"] = config.value("provenance", "user-provided empirical matrix; no native games launched");
    report["steps"] = solver.steps;
    report["payoffs"] = config.at("payoffs");
    write_json(options.out / "mixture.json", report);
    run.finish(report);
    return 0;
}

Json family_columns(const std::vector<std::string>& opponents, const Json& families,
                    const std::string& role) {
    Json result = Json::array();
    std::set<std::string> names;
    for (const auto& opponent : opponents)
        for (const auto& family : families.as_array()) {
            const auto name = family.at("id").get<std::string>();
            const auto maps = strings(family.at("maps"));
            if (maps.empty()) throw std::runtime_error("league family contains no maps");
            const auto id = sha256(role + "\n" + opponent + "\n" + name).substr(0,24);
            if (!names.insert(id).second) throw std::runtime_error("duplicate league opponent/family");
            result.push_back({{"id", id}, {"opponent", opponent}, {"family", name},
                              {"maps", family.at("maps")}, {"role", role}});
        }
    return result;
}

Json cases_for(const Json& column, const Json& config) {
    Json result = Json::array();
    const auto sides = config.value("sides", Json::array({"A", "B"}));
    std::set<std::string> used;
    for (const auto& map : column.at("maps").as_array())
        for (const auto& side : sides.as_array()) {
            const auto s = side.get<std::string>();
            if (s != "A" && s != "B") throw std::runtime_error("league side must be A or B");
            const auto key = map.get<std::string>() + "/" + s;
            if (!used.insert(key).second) throw std::runtime_error("duplicate league map/side");
            result.push_back({{"map", map}, {"side", side},
                              {"opponent", column.value("resolved_opponent",column.at("opponent").get<std::string>())}});
        }
    return result;
}

void population_recipes(Json& request, const Json& state) {
    Json recipes=Json::array();
    std::set<std::string> seen;
    if (state.contains("rows")) for (const auto& row:state.at("rows").as_array()) {
        if (!row.contains("candidate_key") || !row.contains("candidate_identity") || !row.contains("candidate_controller")) continue;
        const auto key=row.at("candidate_key").get<std::string>();
        if (!seen.insert(key).second) continue;
        recipes.push_back({{"key",key},{"knobs",row.at("knobs")},
                           {"controller",row.at("candidate_controller")},{"identity",row.at("candidate_identity")}});
    }
    request["population_configs"]=recipes;
}

Json request_for(const Json& row, const Json& column, const Json& config, const Json* bindings=nullptr) {
    Json result = {{"schema", "contenders-eval/v1"}, {"kind", "config"},
                   {"candidate", Json{{"knobs", row.at("knobs")}}},
                   {"controller", config.value("controller", "anchors/main-v42")},
                   {"baseline", config.value("baseline", "anchors/main-v42")},
                   {"cases", cases_for(column, config)}, {"phase", column.at("role")}};
    if (config.contains("evaluation")) result["evaluation"] = config.at("evaluation");
    if (bindings && bindings->contains("controller_key")) result["controller"]=bindings->at("controller_key");
    if (bindings && bindings->contains("baseline_key")) result["baseline"]=bindings->at("baseline_key");
    if (bindings) population_recipes(result,*bindings);
    return result;
}

Json new_row(const Json& knobs, bool reference = false) {
    return {{"id", sha256(knobs.dump())}, {"knobs", knobs}, {"eligible", true},
            {"reference", reference}, {"cells", Json::object()}, {"validation", Json::object()}};
}

Json legal_seed(const c2::ConfigSpace& space, const Json& seed) {
    if (!seed.is_object()) throw std::runtime_error("league seed must be a knob object");
    const auto result=space.canonical(seed);
    for (const auto& [name,value]:seed.as_object())
        if (!result.contains(name) || result.at(name)!=value)
            throw std::runtime_error("league seed contains an unknown, inactive or illegal knob: "+name);
    std::string reason;
    if (!space.valid(result,&reason)) throw std::runtime_error("illegal league seed: "+reason);
    return result;
}

bool has_positive_health(const Json& value) {
    if (value.is_null()) return false;
    if (value.is_array() || value.is_object()) return !value.empty();
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_number()) return value.get<double>() != 0;
    return true;
}

struct Cell {
    Json value;
    bool subject_healthy{}, infrastructure_healthy{};
};

Cell checked_cell(const Json& response, const Json& request) {
    if (!response.is_object() || !response.contains("cases") || !response.at("cases").is_array())
        throw std::runtime_error("league evaluator response lacks cases");
    const auto& expected = request.at("cases").as_array();
    const auto& actual = response.at("cases").as_array();
    if (actual.size() != expected.size() || actual.empty()) throw std::runtime_error("league evaluator returned incomplete case coverage");
    std::set<std::string> keys, expected_keys;
    for (const auto& item : expected) expected_keys.insert(item.at("map").get<std::string>() + "/" + item.at("side").get<std::string>());
    double total = 0, baseline = 0;
    bool subject_healthy = response.value("subject_healthy", response.value("healthy", false));
    bool infrastructure_healthy = response.value("opponent_healthy", response.value("healthy", false))
        && response.value("baseline_healthy", response.value("healthy", false));
    std::string resolved_opponent, subject_key;
    Json binding = Json::array();
    for (const auto& item : actual) {
        const auto key = item.at("map").get<std::string>() + "/" + item.at("side").get<std::string>();
        if (!expected_keys.count(key) || !keys.insert(key).second) throw std::runtime_error("league evaluator case identity mismatch");
        if (!item.value("complete",false)) throw std::runtime_error("league evaluator returned a nonterminal case");
        const double payoff = item.at("payoff").get<double>();
        const double baseline_payoff = item.at("baseline_payoff").get<double>();
        if (!std::isfinite(payoff) || !std::isfinite(baseline_payoff) || std::abs(payoff) > 1.000001 || std::abs(baseline_payoff) > 1.000001)
            throw std::runtime_error("league payoff is outside [-1,1]");
        total += payoff;
        baseline += baseline_payoff;
        if (item.contains("health") && has_positive_health(item.at("health"))) subject_healthy = false;
        for (const auto* field : {"opponent_health", "baseline_health", "baseline_opponent_health"})
            if (item.contains(field) && has_positive_health(item.at(field))) infrastructure_healthy = false;
        if (!item.contains("identity")) throw std::runtime_error("league evaluator omitted immutable case identity");
        const auto& identity = item.at("identity");
        const auto opponent = identity.value("opponent_key", item.value("opponent", ""));
        if (opponent.empty()) throw std::runtime_error("league evaluator omitted resolved opponent key");
        if (!resolved_opponent.empty() && opponent != resolved_opponent) throw std::runtime_error("opponent changed within league cell");
        resolved_opponent = opponent;
        const auto candidate = identity.value("subject_key", "");
        if (!candidate.empty()) {
            if (!subject_key.empty() && candidate != subject_key) throw std::runtime_error("candidate executable changed within league cell");
            subject_key = candidate;
        }
        Json entry = identity;
        entry["map"] = item.at("map");
        entry["side"] = item.at("side");
        binding.push_back(entry);
    }
    if (response.contains("candidate_identity") && response.at("candidate_identity").contains("key")) {
        const auto key=response.at("candidate_identity").at("key").get<std::string>();
        if (!subject_key.empty() && subject_key!=key) throw std::runtime_error("candidate identity differs from evaluated case bytes");
        subject_key=key;
    }
    Json value = {{"payoff", total / actual.size()}, {"baseline_payoff", baseline / actual.size()},
                  {"case_count", actual.size()}, {"subject_healthy", subject_healthy},
                  {"infrastructure_healthy", infrastructure_healthy}, {"binding", binding},
                  {"opponent_key", resolved_opponent}, {"candidate_key", subject_key}};
    // Cache-hit counters, process paths and timestamps are transport metadata, not fitness.
    value["evaluation_sha256"]=sha256(value.dump());
    if (response.contains("artifacts")) value["artifact_sources"]=response.at("artifacts");
    if (response.contains("candidate_identity")) value["candidate_identity"]=response.at("candidate_identity");
    if (response.contains("identity") && response.at("identity").contains("controller")
        && response.at("identity").at("controller").contains("key"))
        value["candidate_controller"]=response.at("identity").at("controller").at("key");
    if (!response.value("selection_eligible",false) && subject_healthy && infrastructure_healthy)
        throw std::runtime_error("league evaluator rejected selection without a health explanation");
    return {value, subject_healthy, infrastructure_healthy};
}

std::vector<std::size_t> complete_rows(const Json& state) {
    std::vector<std::size_t> result;
    const auto& columns = state.at("columns").as_array();
    const auto& rows = state.at("rows").as_array();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (!rows[i].value("eligible", false)) continue;
        bool complete = true;
        for (const auto& column : columns)
            if (!rows[i].at("cells").contains(column.at("id").get<std::string>())) complete = false;
            else {
                const auto& cell=rows[i].at("cells").at(column.at("id").get<std::string>());
                if (!cell.value("subject_healthy",false) || !cell.value("infrastructure_healthy",false)) complete=false;
            }
        if (complete) result.push_back(i);
    }
    return result;
}

Json solve_state(const Json& state, std::uint64_t steps) {
    const auto indices = complete_rows(state);
    if (indices.empty()) return Json();
    Matrix matrix;
    Json policies = Json::array();
    for (auto index : indices) {
        const auto& row = state.at("rows").at(index);
        std::vector<double> values;
        for (const auto& column : state.at("columns").as_array())
            values.push_back(row.at("cells").at(column.at("id").get<std::string>()).at("payoff").get<double>());
        matrix.push_back(std::move(values));
        policies.push_back({{"id", row.at("id")}, {"knobs", row.at("knobs")},
                            {"candidate_key", row.value("candidate_key", "")}});
    }
    Json solution = league::json(league::solve(matrix, steps));
    solution["policies"] = policies;
    solution["columns"] = state.at("columns");
    Json values = Json::array();
    for (const auto& row : matrix) values.push_back(numbers(row));
    solution["payoffs"] = values;
    solution["excluded_incomplete_policies"] = state.at("rows").size() - indices.size();
    return solution;
}

Json validation_summary(const Json& state, const Json& solution) {
    Json result=Json::array();
    if (solution.is_null()) return result;
    for (const auto& column:state.at("validation_columns").as_array()) {
        double observed=0, covered=0;
        for (std::size_t i=0;i<solution.at("policies").size();++i) {
            const auto weight=solution.at("policy").at(i).get<double>();
            const auto policy=solution.at("policies").at(i).at("id");
            for (const auto& row:state.at("rows").as_array()) {
                if (row.at("id")!=policy || !row.at("validation").contains(column.at("id").get<std::string>())) continue;
                const auto& cell=row.at("validation").at(column.at("id").get<std::string>());
                if (cell.value("subject_healthy",false) && cell.value("infrastructure_healthy",false)) {
                    covered+=weight;
                    observed+=weight*cell.at("payoff").get<double>();
                }
            }
        }
        const auto missing=std::max(0.0,1.0-covered);
        result.push_back({{"opponent",column.value("resolved_opponent",column.at("opponent").get<std::string>())},
                          {"family",column.at("family")},{"maps",column.at("maps")},
                          {"healthy_covered_policy_mass",covered},{"unobserved_or_unhealthy_mass",missing},
                          {"complete",missing<1e-12},{"payoff",missing<1e-12?Json(observed):Json()},
                          {"lower",observed-missing},{"upper",observed+missing},
                          {"claim","reporting-only fixed-panel outcomes; missing or unhealthy policy mass bounded by [-1,1], never scored as zero"}});
    }
    return result;
}

bool fully_evaluated(const Json& row, const Json& columns) {
    for (const auto& column : columns.as_array())
        if (!row.at("cells").contains(column.at("id").get<std::string>())) return false;
    return true;
}

Json preserve_artifacts(const Json& row,const Options& options) {
    Json result=Json::array();
    std::set<std::string> copied;
    for(const auto& [column,cell]:row.at("cells").as_object()) {
        if(!cell.contains("artifact_sources"))continue;
        for(const auto& artifact:cell.at("artifact_sources").as_array()) {
            const auto sha=artifact.at("sha256").get<std::string>();
            if(sha.size()!=64 || !std::all_of(sha.begin(),sha.end(),[](char c){return std::isxdigit(static_cast<unsigned char>(c));}))
                throw std::runtime_error("invalid native artifact hash");
            if(!copied.insert(sha).second)continue;
            const fs::path source_hint(artifact.at("path").get<std::string>());
            const auto target=options.out/"artifacts"/(sha+source_hint.extension().string());
            fs::create_directories(target.parent_path());
            if(!fs::is_regular_file(target)) {
                // A transferred campaign already carries hash-verified exports.
                // Its old machine's cache path is relevant only if an export is
                // missing and needs to be read again from this machine's cache.
                const auto source=fs::weakly_canonical(source_hint);
                const auto allowed=fs::weakly_canonical(options.repo/".cache/contenders/optimization");
                const auto relative=source.lexically_relative(allowed);
                if(relative.empty() || relative.is_absolute() || *relative.begin()=="..")
                    throw std::runtime_error("native artifact escaped owned optimizer cache");
                if(!fs::is_regular_file(source) || file_sha256(source)!=sha)
                    throw std::runtime_error("native winning artifact missing or corrupt: "+source.string());
                fs::copy_file(source,target,fs::copy_options::none);
            }
            if(file_sha256(target)!=sha)throw std::runtime_error("preserved winning artifact is corrupt");
            result.push_back({{"path",target.lexically_relative(options.out).generic_string()},{"sha256",sha},{"role",artifact.value("role","")}});
        }
    }
    return result;
}

double expected_response(const Json& row, const Json& columns, const Json& solution) {
    double result = 0;
    if (solution.is_null()) return -std::numeric_limits<double>::infinity();
    if (columns.size() != solution.at("columns").size()) throw std::runtime_error("league solution belongs to a different population");
    for (std::size_t i = 0; i < columns.size(); ++i)
        result += row.at("cells").at(columns.at(i).at("id").get<std::string>()).at("payoff").get<double>()
                * solution.at("adversary").at(i).get<double>();
    return result;
}

void verify_cached_evidence(const Run& run) {
    auto verify_row=[&](const Json& row) {
        for (const auto* role:{"cells","validation"}) {
            const auto& columns=run.state.at(std::string(role)=="cells"?"columns":"validation_columns");
            for (const auto& [column_id,cell]:row.at(role).as_object()) {
                const fs::path relative(cell.at("response_path").get<std::string>());
                if (relative.empty() || relative.is_absolute() || relative.lexically_normal().begin()->string()=="..")
                    throw std::runtime_error("league cached response escaped campaign output");
                const auto path=run.options.out/relative;
                if (!fs::is_regular_file(path) || file_sha256(path)!=cell.at("response_sha256").get<std::string>())
                    throw std::runtime_error("league cached response is missing or corrupt: "+path.string());
                const Json* column=nullptr;
                for (const auto& value:columns.as_array()) if(value.at("id").get<std::string>()==column_id)column=&value;
                if (!column) throw std::runtime_error("league cached response has an unknown matrix column");
                const auto checked=checked_cell(read_json(path),request_for(row,*column,run.config,&run.state));
                if (checked.value.at("evaluation_sha256")!=cell.at("evaluation_sha256"))
                    throw std::runtime_error("league cached fitness differs from its native evidence");
            }
        }
    };
    for (const auto& row:run.state.at("rows").as_array())verify_row(row);
    if (!run.state.at("pending").is_null())verify_row(run.state.at("pending"));
}

void save_best(Run& run, bool final = false) {
    const Json solution = solve_state(run.state, run.config.value<std::uint64_t>("matrix_steps", 10000));
    if (!solution.is_null()) run.state["last_solution"] = solution;
    const Json& best = run.state.at("last_solution");
    Json report = {{"schema", "contenders-league-result/v1"}, {"mode", "native_best_response"},
                   {"native_batches", run.iteration}, {"candidate_count", run.state.value<std::uint64_t>("candidate_count",0)},
                   {"policy_archive_size", run.state.at("rows").size()}, {"opponent_family_count", run.state.at("columns").size()},
                   {"mixture", best}, {"pending_candidate", run.state.at("pending")},
                   {"status", run.state.value("stop_reason", "search_cap_reached")},
                   {"qualification", "empirical restricted-population result; no release or full-game equilibrium claim"},
                   {"validation_used_for_selection", false}};
    report["current_opponent_columns_have_evaluated_policies"]=!solution.is_null();
    report["mixture_scope"]=solution.is_null()?"last complete training matrix; newly added columns or policies remain unevaluated":"fully evaluated eligible-policy submatrix of current opponent columns; unevaluated or unhealthy archived policies are explicitly excluded";
    if (run.state.contains("health_failure")) report["health_failure"] = run.state.at("health_failure");
    if(!run.state.at("pending").is_null() && run.state.at("pending").value("admission_test_passed",false))
        report["pending_candidate_artifacts"]=preserve_artifacts(run.state.at("pending"),run.options);
    if (!best.is_null()) {
        Json policy_artifacts=Json::array();
        Json deployment=Json::array();
        for(const auto& policy:best.at("policies").as_array())
            for(const auto& archived:run.state.at("rows").as_array())
                if(archived.at("id")==policy.at("id")) {
                    const auto artifacts=preserve_artifacts(archived,run.options);
                    policy_artifacts.push_back({{"policy",policy.at("id")},{"artifacts",artifacts}});
                    const auto i=deployment.size();
                    Json entry{{"policy",policy.at("id")},{"probability",best.at("policy").at(i)},
                               {"candidate_key",archived.value("candidate_key","")},{"knobs",archived.at("knobs")},
                               {"artifacts",artifacts}};
                    if(archived.contains("candidate_identity"))entry["candidate_identity"]=archived.at("candidate_identity");
                    if(archived.contains("candidate_controller"))entry["controller"]=archived.at("candidate_controller");
                    deployment.push_back(entry);
                }
        report["policy_artifacts"]=policy_artifacts;
        write_json(run.options.out/"deployment.json",Json{{"schema","contenders-league-deployment/v1"},
            {"selection","Sample one listed policy before a game; all champions use that same module. Probabilities solve only the reported empirical training matrix."},
            {"policies",deployment},{"mixture_scope",report.at("mixture_scope")},
            {"release_qualified",false}});
        std::size_t index = 0;
        double security = -2;
        const auto& matrix = best.at("payoffs").as_array();
        for (std::size_t i = 0; i < matrix.size(); ++i) {
            double worst = 2;
            for (const auto& value : matrix[i].as_array()) worst = std::min(worst, value.get<double>());
            if (worst > security) { security = worst; index = i; }
        }
        const auto& policy = best.at("policies").at(index);
        write_json(run.options.out / "best-overrides.json", policy.at("knobs"));
        Json row = new_row(policy.at("knobs"));
        Json best_request=request_for(row,best.at("columns").at(0),run.config,&run.state);
        best_request["cases"]=Json::array();
        for(const auto& column:best.at("columns").as_array()) {
            const auto cases=cases_for(column,run.config);
            for(const auto& item:cases.as_array())best_request["cases"].push_back(item);
        }
        write_json(run.options.out / "best-native-request.json",best_request);
        atomic_write(run.options.out / "best-config.hpp",c2::configuration_header(policy.at("knobs")));
        report["best_pure_policy"] = policy;
        report["best_pure_empirical_worst_family_payoff"] = security;
        report["validation"] = Json::array();
        for (const auto& row_value : run.state.at("rows").as_array())
            if (!row_value.at("validation").empty()) report["validation"].push_back({{"policy", row_value.at("id")}, {"cells", row_value.at("validation")}});
        report["validation_mixture"]=validation_summary(run.state,best);
    }
    write_json(run.options.out / "mixture.json", report);
    if (final) run.finish(report);
    else write_json(run.options.out/"status.json",report);
}

} // namespace

int run_c7(const Options& options) {
    if (options.has("replay-best")) {
        const auto request = read_json(options.out / "best-native-request.json");
        const auto response = evaluate(options, request, options.out / "replay-best");
        write_json(options.out / "replay-result.json", response);
        return response.value("selection_eligible", false) ? 0 : 2;
    }
    const Json initial = read_json(options.input);
    if (initial.contains("matrix")) return matrix_mode(options);
    Run run(options, "c7", "2");
    const Json& config = run.config;
    const Json search = config.contains("search") ? config.at("search") : (config.contains("space") ? config : defaults());
    c2::ConfigSpace space(search);
    if (!config.contains("opponents") || !config.contains("families"))
        throw std::runtime_error("native league requires opponents and map families");
    const auto train_opponents = strings(config.at("opponents"));
    if (train_opponents.empty()) throw std::runtime_error("native league requires at least one opponent");
    const Json train_columns = family_columns(train_opponents, config.at("families"), "training");
    if (train_columns.empty()) throw std::runtime_error("native league requires at least one map family");
    Json validation_columns = Json::array();
    if (config.contains("validation_families")) {
        const auto validation_opponents = config.contains("validation_opponents") ? strings(config.at("validation_opponents")) : train_opponents;
        validation_columns = family_columns(validation_opponents, config.at("validation_families"), "validation");
        std::set<std::string> training_cases;
        for (const auto& column : train_columns.as_array()) {
            const auto cases=cases_for(column,config);
            for (const auto& item : cases.as_array())
                training_cases.insert(item.at("map").get<std::string>() + "/" + item.at("side").get<std::string>() + "/" + item.at("opponent").get<std::string>());
        }
        for (const auto& column : validation_columns.as_array()) {
            const auto cases=cases_for(column,config);
            for (const auto& item : cases.as_array())
                if (training_cases.count(item.at("map").get<std::string>() + "/" + item.at("side").get<std::string>() + "/" + item.at("opponent").get<std::string>()))
                    throw std::runtime_error("league training and validation game keys overlap");
        }
    }
    if (!run.resumed) {
        Json rows = Json::array();
        rows.push_back(new_row(space.baseline(),true));
        if (config.contains("seeds")) for (const auto& seed : config.at("seeds").as_array()) rows.push_back(new_row(legal_seed(space,seed)));
        Json seen = Json::array();
        std::set<std::string> unique;
        for (const auto& row : rows.as_array()) {
            const auto id = row.at("id").get<std::string>();
            if (!unique.insert(id).second) throw std::runtime_error("duplicate seed policy");
            seen.push_back(id);
        }
        run.state = {{"rows", rows}, {"columns", train_columns}, {"validation_columns", validation_columns},
                     {"seen", seen}, {"pending", Json()}, {"last_solution", Json()}, {"candidate_count",0},
                     {"validation_epoch",0}, {"validation_queue",Json::array()}, {"validation_position",0},
                     {"proposal_seed_position",0}};
        run.checkpoint();
    }
    if (run.resumed) verify_cached_evidence(run);
    // The cheap identity operation checks toolchain, controller, baseline and map/opponent
    // bytes before using prior fitness. Native cases remain the authoritative evaluation.
    Json identity_request = request_for(run.state.at("rows").at(0), run.state.at("columns").at(0), config);
    population_recipes(identity_request,run.state);
    identity_request["kind"] = "identity";
    identity_request["cases"] = Json::array();
    for (const auto& column : train_columns.as_array()) {
        const auto cases=cases_for(column,config);
        for (const auto& item : cases.as_array()) identity_request["cases"].push_back(item);
    }
    for (const auto& column : validation_columns.as_array()) {
        const auto cases=cases_for(column,config);
        for (const auto& item : cases.as_array()) identity_request["cases"].push_back(item);
    }
    const auto identity_response = evaluate(options, identity_request, options.out / "identity");
    if (!identity_response.contains("environment_identity")) throw std::runtime_error("league evaluator must support kind=identity before fitness reuse");
    const auto environment = identity_response.at("environment_identity");
    if (run.state.contains("environment_identity") && run.state.at("environment_identity") != environment)
        throw std::runtime_error("league native environment changed; prior fitness is not reusable");
    run.state["environment_identity"] = environment;
    if(identity_response.contains("identity")) {
        const auto& binding=identity_response.at("identity");
        if(binding.contains("controller") && binding.at("controller").contains("key")) run.state["controller_key"]=binding.at("controller").at("key");
        if(binding.contains("baseline") && binding.at("baseline").contains("key")) run.state["baseline_key"]=binding.at("baseline").at("key");
        if(!run.state.contains("map_bindings"))run.state["map_bindings"]=Json::object();
        if(binding.contains("cases"))for(const auto& item:binding.at("cases").as_array()) {
            const auto name=fs::path(item.at("map").get<std::string>()).stem().string();
            const auto hash=item.at("map_sha256");
            if(run.state["map_bindings"].contains(name) && run.state["map_bindings"].at(name)!=hash)
                throw std::runtime_error("league map contents changed after binding");
            run.state["map_bindings"][name]=hash;
        }
    }
    run.checkpoint();
    const auto matrix_steps = config.value<std::uint64_t>("matrix_steps",10000);
    const auto max_policies = config.value<std::size_t>("max_policies",64);
    const auto max_opponents = config.value<std::size_t>("max_opponents",24);
    if (!max_policies || max_opponents < train_opponents.size()) throw std::runtime_error("invalid league archive limits");
    if (run.state.value("stop_reason","")=="search_cap_reached" || run.state.value("stop_reason","")=="interrupted_checkpointed")
        run.state.as_object().erase("stop_reason");
    std::uint64_t bookkeeping = 0;
    // A recorded plateau, full archive or health block is a terminal result for
    // this exact input. Merely reopening it must not consume another 256 draws
    // or continue past an infrastructure failure. Caps/interruption above remain
    // resumable and can be extended without changing the sampled trajectory.
    const bool terminal_result=run.state.contains("stop_reason");
    while (!terminal_result) {
        const bool pending_complete=!run.state.at("pending").is_null()
            && (!run.state.at("pending").value("eligible",false) || fully_evaluated(run.state.at("pending"),run.state.at("columns")));
        if (run.stop() && !pending_complete) break;
        if (++bookkeeping > 1000000) throw std::runtime_error("league made no bounded progress");
        Json* row = nullptr;
        Json* column = nullptr;
        bool validation = false;
        for (auto& archived : run.state["rows"].as_array()) {
            if (run.stop()) break;
            if (!archived.value("eligible",false)) continue;
            for (auto& candidate_column : run.state["columns"].as_array())
                if (!archived.at("cells").contains(candidate_column.at("id").get<std::string>())) {
                    row = &archived; column = &candidate_column; break;
                }
            if (row) break;
        }
        if (!row && !run.state.at("pending").is_null()) {
            auto& pending = run.state["pending"];
            if (pending.value("eligible",false) && !fully_evaluated(pending,run.state.at("columns"))) {
                for (auto& candidate_column : run.state["columns"].as_array())
                    if (!pending.at("cells").contains(candidate_column.at("id").get<std::string>())) {
                        row=&pending; column=&candidate_column; break;
                    }
            } else {
                const auto solution = solve_state(run.state,matrix_steps);
                const double response_value = pending.value("eligible",false) ? expected_response(pending,run.state.at("columns"),solution) : -2;
                const bool admissible = pending.value("eligible",false) && (solution.is_null()
                    || response_value > solution.at("empirical_upper").get<double>() + config.value("admission_margin",0.0));
                const bool capacity=run.state.at("rows").size()<max_policies;
                pending["best_response_value"] = std::isfinite(response_value) ? Json(response_value) : Json();
                pending["admission_test_passed"]=admissible;
                pending["admitted"] = admissible && capacity;
                write_json(options.out / "candidates" / (pending.at("id").get<std::string>()+".json"),pending);
                run.event({{"event","candidate_complete"},{"policy",pending.at("id")},{"admitted",admissible && capacity},
                           {"eligible",pending.at("eligible")},{"best_response_value",pending.at("best_response_value")}});
                if (admissible) {
                    if (!capacity) {
                        run.state["stop_reason"]="policy_archive_full_candidate_ready";
                        run.checkpoint();
                        break;
                    }
                    const auto admitted = pending;
                    if(config.value("admit_generated_opponents",true)
                        && (!admitted.contains("candidate_identity") || !admitted.contains("candidate_controller")))
                        throw std::runtime_error("generated league opponent lacks exact reconstruction provenance");
                    run.state["rows"].push_back(admitted);
                    run.state["last_solution"]=solve_state(run.state,matrix_steps);
                    if (config.value("admit_generated_opponents",true) && admitted.contains("candidate_key")) {
                        const auto key = admitted.at("candidate_key").get<std::string>();
                        std::set<std::string> opponents;
                        for (const auto& existing : run.state.at("columns").as_array()) opponents.insert(existing.at("opponent").get<std::string>());
                        if (!key.empty() && key.find('@') != std::string::npos && !opponents.count(key) && opponents.size() < max_opponents) {
                            const auto extras=family_columns({key},config.at("families"),"training");
                            for (const auto& extra : extras.as_array()) run.state["columns"].push_back(extra);
                            run.event({{"event","opponent_admitted"},{"opponent",key}});
                        }
                    }
                }
                run.state["candidate_count"] = run.state.at("candidate_count").get<std::uint64_t>()+1;
                run.state["pending"]=Json();
                const auto expanded=solve_state(run.state,matrix_steps);
                if (!expanded.is_null()) run.state["last_solution"]=expanded;
                run.checkpoint();
                continue;
            }
        }
        if (run.stop()) break;
        if (!row) {
            const auto count = run.state.at("candidate_count").get<std::uint64_t>();
            const auto every = config.value<std::uint64_t>("validation_every",10);
            if (every && count && count/every > run.state.at("validation_epoch").get<std::uint64_t>() && !validation_columns.empty()) {
                run.state["validation_epoch"]=count/every;
                Json queue = Json::array();
                const auto solution = solve_state(run.state,matrix_steps);
                if (!solution.is_null())
                    for (std::size_t i=0;i<solution.at("policies").size();++i)
                        if (solution.at("policy").at(i).get<double>() > 1e-6)
                            for (std::size_t j=0;j<validation_columns.size();++j)
                                queue.push_back({{"policy",solution.at("policies").at(i).at("id")},{"column",j}});
                run.state["validation_queue"]=queue;
                run.state["validation_position"]=0;
                run.checkpoint();
            }
            auto position=run.state.at("validation_position").get<std::size_t>();
            auto& queue=run.state["validation_queue"].as_array();
            while (position<queue.size() && !row) {
                const auto& entry=queue[position];
                for (auto& archived:run.state["rows"].as_array())
                    if (archived.at("id")==entry.at("policy")) {
                        auto& candidate_column=run.state["validation_columns"].at(entry.at("column").get<std::size_t>());
                        if (!archived.at("validation").contains(candidate_column.at("id").get<std::string>())) {
                            row=&archived; column=&candidate_column; validation=true;
                        }
                    }
                if (!row) run.state["validation_position"]=++position;
            }
        }
        if (row) {
            const auto request=request_for(*row,*column,config,&run.state);
            const auto work=options.out/"evaluations"/sha256(request.dump());
            const auto response=evaluate(options,request,work);
            auto cell=checked_cell(response,request);
            for(const auto& binding:cell.value.at("binding").as_array())
                for(const auto* field:{"engine","toolchain","baseline_key"}) {
                    if(!binding.contains(field))throw std::runtime_error("league immutable binding omitted "+std::string(field));
                    const auto state_key=std::string("bound_")+field;
                    if(run.state.contains(state_key) && run.state.at(state_key)!=binding.at(field))
                        throw std::runtime_error("league "+std::string(field)+" changed during campaign");
                    run.state[state_key]=binding.at(field);
                }
            if(!run.state.contains("map_bindings"))run.state["map_bindings"]=Json::object();
            for(const auto& binding:cell.value.at("binding").as_array()) {
                const auto name=binding.at("map").get<std::string>();
                const auto hash=binding.at("map_sha256");
                if(run.state["map_bindings"].contains(name) && run.state["map_bindings"].at(name)!=hash)
                    throw std::runtime_error("league map contents changed during campaign");
                run.state["map_bindings"][name]=hash;
            }
            const auto column_id=column->at("id").get<std::string>();
            if (column->contains("resolved_opponent") && column->at("resolved_opponent")!=cell.value.at("opponent_key"))
                throw std::runtime_error("league opponent bytes changed after initial binding");
            (*column)["resolved_opponent"]=cell.value.at("opponent_key");
            write_json(work/"response.json",response);
            cell.value["response_path"]=(work/"response.json").lexically_relative(options.out).generic_string();
            cell.value["response_sha256"]=file_sha256(work/"response.json");
            (*row)[validation ? "validation" : "cells"][column_id]=cell.value;
            const auto candidate_key=cell.value.at("candidate_key").get<std::string>();
            if (!candidate_key.empty()) {
                if (row->contains("candidate_key") && row->at("candidate_key").get<std::string>()!=candidate_key)
                    throw std::runtime_error("league candidate source changed between opponent evaluations");
                (*row)["candidate_key"]=candidate_key;
            }
            for (const auto* field:{"candidate_identity","candidate_controller"}) {
                if (!cell.value.contains(field)) continue;
                if (row->contains(field) && row->at(field)!=cell.value.at(field))
                    throw std::runtime_error("league candidate reconstruction provenance changed between evaluations");
                (*row)[field]=cell.value.at(field);
            }
            if (!cell.subject_healthy && !validation) (*row)["eligible"]=false;
            run.advance();
            bookkeeping=0;
            run.event({{"event",validation ? "validation_cell" : "training_cell"},{"policy",row->at("id")},
                       {"column",column_id},{"payoff",cell.value.at("payoff")},{"subject_healthy",cell.subject_healthy},
                       {"infrastructure_healthy",cell.infrastructure_healthy}});
            if (!cell.infrastructure_healthy) {
                // Held-out health diagnoses stop the campaign but cannot rewrite a
                // training policy's eligibility or the matrix used by selection.
                if (!validation) (*row)["eligible"]=false;
                run.state["stop_reason"]="blocked_opponent_or_baseline_health";
                run.state["health_failure"]={{"policy",row->at("id")},{"column",column_id},{"response_path",cell.value.at("response_path")}};
                run.checkpoint();
                break;
            }
            if (validation) run.state["validation_position"]=run.state.at("validation_position").get<std::size_t>()+1;
            const auto solution=solve_state(run.state,matrix_steps);
            if (!solution.is_null()) run.state["last_solution"]=solution;
            run.progress({{"policies",run.state.at("rows").size()},{"opponent_families",run.state.at("columns").size()},
                          {"empirical_lower",solution.is_null()?Json():solution.at("empirical_lower")},
                          {"empirical_gap",solution.is_null()?Json():solution.at("empirical_gap")}});
            run.checkpoint();
            save_best(run);
            continue;
        }
        const auto solution=solve_state(run.state,matrix_steps);
        if (!solution.is_null()) run.state["last_solution"]=solution;
        std::set<std::string> seen;
        for (const auto& value:run.state.at("seen").as_array()) seen.insert(value.get<std::string>());
        Json knobs;
        bool generated=false;
        if (config.contains("proposal_seeds")) {
            auto position=run.state.value<std::size_t>("proposal_seed_position",0);
            while(position<config.at("proposal_seeds").size() && !generated) {
                knobs=legal_seed(space,config.at("proposal_seeds").at(position++));
                const auto id=sha256(knobs.dump());
                if(!seen.count(id)) {
                    run.state["pending"]=new_row(knobs);
                    run.state["pending"]["proposal_source"]="provided_warm_start";
                    run.state["seen"].push_back(id);
                    generated=true;
                }
            }
            run.state["proposal_seed_position"]=position;
        }
        for (unsigned tries=0;tries<256 && !generated;++tries) {
            if (run.state.at("rows").empty() || std::generate_canonical<double,53>(run.rng)<config.value("random_fraction",0.25)) knobs=space.sample(run.rng);
            else {
                std::size_t parent=static_cast<std::size_t>(run.rng()%run.state.at("rows").size());
                if (!solution.is_null()) {
                    std::vector<double> weights;
                    for (const auto& p:solution.at("policy").as_array()) weights.push_back(p.get<double>());
                    const auto chosen=std::discrete_distribution<std::size_t>(weights.begin(),weights.end())(run.rng);
                    const auto id=solution.at("policies").at(chosen).at("id");
                    for (std::size_t i=0;i<run.state.at("rows").size();++i) if (run.state.at("rows").at(i).at("id")==id) parent=i;
                }
                knobs=space.mutate(run.state.at("rows").at(parent).at("knobs"),run.rng,config.value<unsigned>("mutations",2));
            }
            knobs=space.canonical(knobs);
            const auto id=sha256(knobs.dump());
            if (!seen.count(id)) {
                run.state["pending"]=new_row(knobs);
                run.state["seen"].push_back(id);
                generated=true;
            }
        }
        if (!generated) { run.state["stop_reason"]="no_new_candidate_after_256_attempts"; run.checkpoint(); break; }
        run.checkpoint();
    }
    if (!run.state.contains("stop_reason")) run.state["stop_reason"]=interrupted()?"interrupted_checkpointed":"search_cap_reached";
    run.checkpoint();
    save_best(run,true);
    std::cout << "C7: " << run.iteration << " native opponent-family batches; " << run.state.at("rows").size()
              << " archived policies. Output: " << (options.out/"mixture.json").string() << '\n';
    return run.state.value("stop_reason","")=="blocked_opponent_or_baseline_health" ? 2 : 0;
}

} // namespace opt
