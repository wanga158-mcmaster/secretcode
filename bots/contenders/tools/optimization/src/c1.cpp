#include "opt/program.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>

namespace opt::program {
namespace {
double unit(std::mt19937_64& rng) { return static_cast<double>(rng() >> 11) * 0x1.0p-53; }
std::size_t index(std::mt19937_64& rng, std::size_t n) {
    if (!n) throw std::runtime_error("cannot choose from an empty population");
    return static_cast<std::size_t>(rng() % n);
}
double normal(std::mt19937_64& rng) {
    const double u = std::max(unit(rng), std::numeric_limits<double>::min());
    return std::sqrt(-2.0 * std::log(u)) * std::cos(6.2831853071795864769 * unit(rng));
}
double f32(double x) { return static_cast<double>(static_cast<float>(x)); }
Json leaf(double v) { return Json{{"factor", f32(std::clamp(v, 0.125, 8.0))}}; }
Json generate(std::mt19937_64& rng, int remaining, bool force_branch = false) {
    if (remaining <= 0 || (!force_branch && unit(rng) > 0.56))
        return leaf(std::exp2(-3.0 + 6.0 * unit(rng)));
    // Initial immigrants must distinguish jobs, rather than spend a native game
    // evaluating only a common positive multiplier on every candidate score.
    const int feature = force_branch ? 6 + static_cast<int>(index(rng, 6)) : static_cast<int>(index(rng, feature_count));
    double threshold = unit(rng);
    if (feature == 7 || feature == 8) threshold *= threshold;
    return Json{{"feature", feature},
                {"threshold", f32(threshold)},
                {"left", generate(rng, remaining - 1)},
                {"right", generate(rng, remaining - 1)}};
}
using Path = std::vector<bool>;
void paths(const Json& tree, Path path, std::vector<Path>& out) {
    out.push_back(path);
    if (tree.contains("factor")) return;
    path.push_back(false); paths(tree.at("left"), path, out);
    path.back() = true; paths(tree.at("right"), path, out);
}
Json& at_path(Json& tree, const Path& path) {
    Json* node = &tree;
    for (bool right : path) node = &node->at(right ? "right" : "left");
    return *node;
}
const Json& at_path(const Json& tree, const Path& path) {
    const Json* node = &tree;
    for (bool right : path) node = &node->at(right ? "right" : "left");
    return *node;
}
void validate_node(const Json& tree, int remaining, std::size_t& count) {
    if (!tree.is_object() || ++count > 127) throw std::runtime_error("program must have at most 127 object nodes");
    if (tree.contains("factor")) {
        if (tree.size() != 1 || !tree.at("factor").is_number()) throw std::runtime_error("leaf requires only numeric factor");
        double v = tree.at("factor").get<double>();
        if (!std::isfinite(v) || v < 0.125 || v > 8) throw std::runtime_error("leaf factor must be in [0.125,8]");
        return;
    }
    if (remaining <= 0 || tree.size() != 4 || !tree.contains("feature") || !tree.contains("threshold") ||
        !tree.contains("left") || !tree.contains("right")) throw std::runtime_error("invalid or too-deep conditional node");
    if (!tree.at("feature").is_number_integer()) throw std::runtime_error("feature must be an integer");
    int feature = tree.at("feature").get<int>();
    double threshold = tree.at("threshold").get<double>();
    if (feature < 0 || feature >= feature_count || !std::isfinite(threshold) || threshold < 0 || threshold > 1)
        throw std::runtime_error("invalid feature index or threshold");
    validate_node(tree.at("left"), remaining - 1, count);
    validate_node(tree.at("right"), remaining - 1, count);
}
void emit(const Json& tree, std::ostringstream& out, int indent) {
    const std::string pad(static_cast<std::size_t>(indent), ' ');
    auto literal = [](double value) {
        std::ostringstream stream;
        stream << std::scientific << std::setprecision(9) << static_cast<float>(value) << 'f';
        return stream.str();
    };
    if (tree.contains("factor")) { out << pad << "return " << literal(tree.at("factor").get<double>()) << ";\n"; return; }
    out << pad << "if (features[" << tree.at("feature").get<int>() << "] <= "
        << literal(tree.at("threshold").get<double>()) << ") {\n";
    emit(tree.at("left"), out, indent + 4);
    out << pad << "} else {\n"; emit(tree.at("right"), out, indent + 4); out << pad << "}\n";
}
}
const std::vector<std::string>& feature_names() {
    static const std::vector<std::string> names{
        "roundFraction", "hpFraction", "bagFraction", "goldFraction", "enemyVisibleFraction",
        "alliesVisibleFraction", "travelFraction", "groupGemFraction", "groupGoldFraction", "groupHpFraction", "groupHarmless", "homeDistanceFraction"};
    return names;
}
void validate(const Json& tree, int max_depth) {
    if (max_depth < 0 || max_depth > 6) throw std::runtime_error("max_depth must be in [0,6]");
    std::size_t count = 0; validate_node(tree, max_depth, count);
}
std::size_t nodes(const Json& tree) { return tree.contains("factor") ? 1 : 1 + nodes(tree.at("left")) + nodes(tree.at("right")); }
int depth(const Json& tree) { return tree.contains("factor") ? 0 : 1 + std::max(depth(tree.at("left")), depth(tree.at("right"))); }
float factor(const Json& tree, const std::vector<float>& features) {
    if (features.size() != feature_count) throw std::runtime_error("program needs twelve features");
    const Json* cursor = &tree;
    for (int guard = 0; guard <= 6; ++guard) {
        if (cursor->contains("factor")) return cursor->at("factor").get<float>();
        const float x = features[static_cast<std::size_t>(cursor->at("feature").get<int>())];
        cursor = &cursor->at(x <= cursor->at("threshold").get<float>() ? "left" : "right");
    }
    throw std::runtime_error("program exceeded bounded evaluation depth");
}
Json random_tree(std::mt19937_64& rng, int max_depth) { auto result = generate(rng, max_depth, max_depth > 0); validate(result, max_depth); return result; }
Json mutate(const Json& tree, std::mt19937_64& rng, int max_depth, double sigma) {
    validate(tree, max_depth);
    if (!std::isfinite(sigma) || sigma <= 0) throw std::runtime_error("mutation_sigma must be positive");
    Json result = tree;
    std::vector<Path> all; paths(result, {}, all);
    const Path selected = all[index(rng, all.size())];
    Json& node = at_path(result, selected);
    const int remaining = max_depth - static_cast<int>(selected.size());
    const int operation = static_cast<int>(index(rng, 5));
    if (operation == 0) node = generate(rng, remaining);
    else if (node.contains("factor")) {
        if (operation == 1 && remaining > 0) {
            Json old = node;
            node = Json{{"feature", static_cast<int>(index(rng, feature_count))}, {"threshold", f32(unit(rng))},
                        {"left", old}, {"right", generate(rng, remaining - 1)}};
        } else node = leaf(std::exp2(std::log2(node.at("factor").get<double>()) + sigma * normal(rng)));
    } else if (operation == 1) node["feature"] = static_cast<int>(index(rng, feature_count));
    else if (operation == 2) node["threshold"] = f32(std::clamp(node.at("threshold").get<double>() + 0.2 * normal(rng), 0.0, 1.0));
    else if (operation == 3) { Json left = node.at("left"); node["left"] = node.at("right"); node["right"] = left; }
    else { Json child = node.at(unit(rng) < 0.5 ? "left" : "right"); node = child; }
    validate(result, max_depth); return result;
}
Json crossover(const Json& a, const Json& b, std::mt19937_64& rng, int max_depth) {
    Json result = a;
    std::vector<Path> dest, source; paths(result, {}, dest); paths(b, {}, source);
    const auto& to = dest[index(rng, dest.size())];
    const auto& from = source[index(rng, source.size())];
    const Json& donor = at_path(b, from);
    if (static_cast<int>(to.size()) + depth(donor) <= max_depth) at_path(result, to) = donor;
    validate(result, max_depth); return result;
}
std::string cpp_header(const Json& tree) {
    validate(tree);
    std::ostringstream out;
    out << "#pragma once\n// Generated bounded job-priority program. Input features must follow contenders-job-context/v1.\n"
        << "namespace contender_program {\ninline float factor(const float features[12]) {\n";
    emit(tree, out, 4); out << "}\n} // namespace contender_program\n"; return out.str();
}
std::string ranking_signature(const Json& tree) {
    validate(tree);std::string ranks;
    for(int context=0;context<8;++context) {
        std::vector<float> scores;scores.reserve(12);
        for(int job=0;job<12;++job) {
            std::vector<float> x(feature_count);
            for(int f=0;f<6;++f)x[f]=static_cast<float>(((context+1)*(f*2+3))%17)/16.f;
            for(int f=6;f<feature_count;++f)x[f]=static_cast<float>(((job+1)*(f*2+1))%19)/18.f;
            x[10]=static_cast<float>(job&1);
            const float base=static_cast<float>(1+(job*7)%13)/4.f;
            scores.push_back(base*factor(tree,x));
        }
        for(std::size_t a=0;a<scores.size();++a)for(std::size_t b=a+1;b<scores.size();++b)
            ranks.push_back(scores[a]<scores[b]?'0':scores[a]>scores[b]?'2':'1');
    }
    return sha256(ranks);
}
} // namespace opt::program

namespace opt {
namespace {
Json strings(const std::vector<std::string>& values) { Json result = Json::array(); for (const auto& value : values) result.push_back(value); return result; }
std::string case_key(const Json& item) {
    if (!item.is_object() || !item.contains("map") || !item.contains("opponent") || !item.contains("side"))
        throw std::runtime_error("each evaluation case needs map, side and opponent");
    const std::string side = item.at("side").get<std::string>();
    if (side != "A" && side != "B") throw std::runtime_error("case side must be A or B");
    return item.at("map").get<std::string>() + "|" + side + "|" + item.at("opponent").get<std::string>();
}
Json cases_for(const Json& config, const std::string& role) {
    if (!config.contains(role)) return Json::array();
    const Json& section = config.at(role);
    if (section.is_array()) return section;
    if (!section.is_object()) throw std::runtime_error(role + " must be an array or object");
    if (section.contains("cases")) return section.at("cases");
    Json result = Json::array();
    if (!section.contains("maps")) return result;
    const Json opponents = section.contains("opponents") ? section.at("opponents") :
        (config.contains("opponents") ? config.at("opponents") : Json::array({"anchors/main-v42"}));
    const Json sides = section.contains("sides") ? section.at("sides") : Json::array({"A", "B"});
    for (const auto& map : section.at("maps").as_array()) for (const auto& opponent : opponents.as_array()) for (const auto& side : sides.as_array())
        result.push_back(Json{{"map", map}, {"opponent", opponent}, {"side", side}});
    return result;
}
void check_splits(const Json& config,const Json& train, const Json& validation, const Json& holdout) {
    std::set<std::string> used;
    std::map<std::string,int> map_roles,family_roles;int role=0;
    for (const Json* cases : {&train, &validation, &holdout}) {
        for (const auto& item : cases->as_array()) if (!used.insert(case_key(item)).second)
            throw std::runtime_error("training/validation/holdout have duplicate or overlapping full-game cases");
        for(const auto& item:cases->as_array()) {
            const auto map=item.at("map").get<std::string>();
            auto family=item.value("family",map);
            if(config.contains("map_families")&&config.at("map_families").contains(map))family=config.at("map_families").at(map).get<std::string>();
            for(auto* roles:{&map_roles,&family_roles}) {
                const auto key=roles==&map_roles?map:family;const auto found=roles->find(key);
                if(found!=roles->end()&&found->second!=role)throw std::runtime_error("C1 map/family overlaps training/validation/holdout: "+key);
                (*roles)[key]=role;
            }
        }
        ++role;
    }
    if (train.empty()) throw std::runtime_error("C1 requires nonempty training cases");
}
Json request_for(const Json& config, const Json& cases, const Json& tree, const std::string& role) {
    Json request{{"schema", "contenders-eval/v1"}, {"kind", "program"},
                 {"candidate", Json{{"tree", tree}}}, {"cases", cases}, {"role", role}};
    request["controller"] = config.value("controller", "anchors/main-v42");
    request["baseline"] = config.value("baseline", config.value("controller", "anchors/main-v42"));
    if (config.contains("evaluation")) request["evaluation"] = config.at("evaluation");
    return request;
}
Json verify_response(const Json& response,const Json& context,const std::string& objective) {
    if(!response.contains("identity")||response.at("identity")!=context)throw std::runtime_error("C1 evaluator changed frozen case identity");
    auto resolved_key=[](const std::string& map,const std::string& side,const std::string& opponent){return map+"|"+side+"|"+opponent;};
    std::set<std::string> expected,observed;
    for(const auto& item:context.at("cases").as_array()) {
        const auto key=resolved_key(item.at("map_sha256").get<std::string>(),item.at("side").get<std::string>(),item.at("opponent").at("key").get<std::string>());
        if(!expected.insert(key).second)throw std::runtime_error("C1 frozen context duplicates a case");
    }
    double total=0;bool raw_healthy=true;std::string outcome;
    const auto& rows=response.at("cases");
    for(const auto& item:rows.as_array()) {
        const auto& identity=item.at("identity");const auto opponent=item.at("opponent").get<std::string>();
        if(identity.at("opponent_key").get<std::string>()!=opponent)throw std::runtime_error("C1 row opponent identity disagrees");
        const auto key=resolved_key(identity.at("map_sha256").get<std::string>(),item.at("side").get<std::string>(),opponent);
        if(!expected.contains(key)||!observed.insert(key).second)throw std::runtime_error("C1 evaluator returned missing/duplicate/foreign case coverage");
        if(response.contains("subject_key")&&identity.at("subject_key")!=response.at("subject_key"))throw std::runtime_error("C1 native candidate identity changed between rows");
        if(identity.at("baseline_key")!=context.at("baseline").at("key"))throw std::runtime_error("C1 paired baseline identity changed");
        bool clean=item.at("complete").get<bool>();
        for(const auto* name:{"health","opponent_health","baseline_health","baseline_opponent_health"}) {
            if(!item.at(name).is_array())throw std::runtime_error("C1 raw paired health must be arrays");
            clean=clean&&item.at(name).empty();
        }
        raw_healthy=raw_healthy&&clean;
        const auto value=item.at("payoff").get<double>(),baseline=item.at("baseline_payoff").get<double>();
        if(!std::isfinite(value)||!std::isfinite(baseline)||std::abs(value)>1||std::abs(baseline)>1)throw std::runtime_error("C1 payoff outside [-1,1]");
        total+=objective=="paired_objective"?value-baseline:value;
        outcome+=key+":"+item.at("payoff").dump()+":"+item.at("margin").dump()+":"+item.at("rounds").dump()+"\n";
    }
    if(observed!=expected||rows.empty())throw std::runtime_error("C1 evaluator omitted requested case coverage");
    const double actual=total/static_cast<double>(rows.size()),claimed=response.at(objective).get<double>();
    if(!std::isfinite(claimed)||std::abs(actual-claimed)>1e-10)throw std::runtime_error("C1 evaluator fitness disagrees with raw paired rows");
    return Json{{"score",actual},{"eligible",raw_healthy&&response.value("healthy",false)&&response.value("selection_eligible",false)},
        {"raw_healthy",raw_healthy},{"outcome_signature",sha256(outcome)},{"verified_cases",rows.size()}};
}
Json evaluate_tree(Run& run, const Json& tree, const Json& cases, const std::string& role, const std::string& objective) {
    const std::string key = sha256(tree.dump() + "\n" + cases.dump() + "\n" + role);
    const std::string expected_environment = run.state.at("environment_identities").at(role).get<std::string>();
    auto& cache = run.state["fitness_cache"];
    if (cache.contains(key)) {
        Json cached = cache.at(key);
        const fs::path response_path = run.options.out / cached.at("response_file").get<std::string>();
        if (!fs::is_regular_file(response_path) || file_sha256(response_path) != cached.at("response_sha256").get<std::string>())
            throw std::runtime_error("C1 cached evaluator result is missing or corrupt: " + response_path.string());
        const Json response = read_json(response_path);
        if (response.value("environment_identity", "") != expected_environment)
            throw std::runtime_error("cached C1 evaluator environment changed");
        const auto verified=verify_response(response,run.state.at("environment_contexts").at(role),objective);
        if(verified.at("score")!=cached.at("score")||verified.at("eligible")!=cached.at("eligible"))throw std::runtime_error("C1 cached fitness disagrees with raw rows");
        cached["cache_hit"] = true;
        return cached;
    }
    const fs::path relative = fs::path("evaluations") / role / key;
    const fs::path work = run.options.out / relative;
    Json response = evaluate(run.options, request_for(run.config, cases, tree, role), work);
    if (response.value("environment_identity", "") != expected_environment)
        throw std::runtime_error("C1 evaluator environment changed during campaign");
    const fs::path response_file = relative / "evaluation.json";
    write_json(run.options.out / response_file, response);
    const auto verified=verify_response(response,run.state.at("environment_contexts").at(role),objective);
    Json item{{"id", key}, {"tree", tree}, {"score", verified.at("score")}, {"eligible", verified.at("eligible")},
              {"raw_healthy",verified.at("raw_healthy")},{"verified_cases",verified.at("verified_cases")},
              {"outcome_signature",verified.at("outcome_signature")},{"ranking_signature",program::ranking_signature(tree)},
              {"response_file", response_file.generic_string()}, {"response_sha256", file_sha256(run.options.out / response_file)},
              {"cache_hit", false}, {"nodes", program::nodes(tree)}, {"depth", program::depth(tree)}};
    cache[key] = item;
    run.state["candidate_evaluations"] = run.state.value<std::uint64_t>("candidate_evaluations", 0) + 1;
    return item;
}
bool better(const Json& a, const Json& b) {
    if (a.value("eligible", false) != b.value("eligible", false)) return a.value("eligible", false);
    if (!a.value("eligible", false)) return false;
    const double av = a.at("score").get<double>(), bv = b.at("score").get<double>();
    return av > bv + 1e-12 || (std::abs(av - bv) <= 1e-12 && a.at("nodes").get<int>() < b.at("nodes").get<int>());
}
std::size_t rng_index(Run& run, std::size_t n) { if (!n) throw std::runtime_error("empty C1 population"); return static_cast<std::size_t>(run.rng() % n); }
double rng_unit(Run& run) { return static_cast<double>(run.rng() >> 11) * 0x1.0p-53; }
const Json& tournament(Run& run, const Json& population, int rounds) {
    std::size_t selected = rng_index(run, population.size());
    for (int i = 1; i < rounds; ++i) {
        const std::size_t candidate = rng_index(run, population.size());
        if (better(population.at(candidate), population.at(selected))) selected = candidate;
    }
    return population.at(selected);
}
void verify_active_results(const Run& run) {
    auto verify = [&](const Json& entry) {
        const fs::path path = run.options.out / entry.at("response_file").get<std::string>();
        if (!fs::is_regular_file(path) || file_sha256(path) != entry.at("response_sha256").get<std::string>())
            throw std::runtime_error("cannot resume C1: active evaluator result changed or disappeared");
        program::validate(entry.at("tree"));
    };
    for (const auto& entry : run.state.at("population").as_array()) verify(entry);
    if (!run.state.at("best").is_null()) verify(run.state.at("best"));
    for(const auto* name:{"archive","diversity_archive"})if(run.state.contains(name))
        for(const auto& [cell,entry]:run.state.at(name).as_object()){(void)cell;verify(entry);}
    for(const auto* role:{"validation","holdout"})if(run.state.contains(role))verify(run.state.at(role));
}
std::string archive_cell(const Json& tree) {
    const int root = tree.contains("factor") ? -1 : tree.at("feature").get<int>();
    return "depth-" + std::to_string(program::depth(tree)) + "_root-" + std::to_string(root);
}
Json preserve_artifacts(const Run& run, const Json& response, const std::string& role,
                        Json& manifest, std::map<std::string, std::string>& paths) {
    if (response.contains("artifacts")) for (const auto& artifact : response.at("artifacts").as_array()) {
        if (!artifact.is_object() || !artifact.contains("path") || !artifact.contains("sha256"))
            throw std::runtime_error("C1 native artifact requires path and sha256");
        const fs::path source = artifact.at("path").get<std::string>();
        const std::string hash = artifact.at("sha256").get<std::string>();
        if (hash.size() != 64 || hash.find_first_not_of("0123456789abcdef") != std::string::npos)
            throw std::runtime_error("C1 native artifact has invalid sha256");
        const fs::path relative = fs::path("artifacts") / (hash + "-" + source.filename().string());
        const fs::path destination = run.options.out / relative;
        if (!fs::is_regular_file(destination) || file_sha256(destination) != hash) {
            if (fs::exists(destination)) throw std::runtime_error("C1 durable artifact changed: " + destination.string());
            if (!fs::is_regular_file(source) || file_sha256(source) != hash)
                throw std::runtime_error("C1 selected native artifact missing/changed: " + source.string());
            fs::create_directories(destination.parent_path());
            atomic_write(destination, read_text(source));
            if (file_sha256(destination) != hash) throw std::runtime_error("C1 artifact copy hash mismatch");
        }
        Json saved = artifact;
        saved["path"] = relative.generic_string();
        saved["evaluation_role"] = role;
        manifest.push_back(saved);
        paths[source.string()] = relative.generic_string();
    }
    // Native export nests WASM/source/specification descriptors. Resolve those
    // paths relative to best_program.json, independently of regenerable caches.
    auto rewrite = [&](auto&& self, Json& value) -> void {
        if (value.is_object()) {
            if (value.contains("path") && value.at("path").is_string()) {
                auto found = paths.find(value.at("path").get<std::string>());
                if (found != paths.end()) value["path"] = found->second;
            }
            for (auto& item : value.as_object()) self(self, item.second);
        } else if (value.is_array()) for (auto& item : value.as_array()) self(self, item);
    };
    Json exported = response.contains("export") ? response.at("export") : Json::object();
    rewrite(rewrite, exported);
    return exported;
}
void export_best(Run& run) {
    const Json& best = run.state.at("best");
    if (best.is_null()) return;
    Json response = read_json(run.options.out / best.at("response_file").get<std::string>());
    Json manifest = Json::array();
    std::map<std::string, std::string> artifact_paths;
    const Json native_export = preserve_artifacts(run, response, "training", manifest, artifact_paths);
    for (const auto& role : {"validation", "holdout"}) if (run.state.contains(role)) {
        const auto& entry = run.state.at(role);
        const Json evidence = read_json(run.options.out / entry.at("response_file").get<std::string>());
        preserve_artifacts(run, evidence, role, manifest, artifact_paths);
    }
    write_json(run.options.out / "artifacts.json", manifest);
    Json exported{{"schema", "contenders-program/v1"}, {"candidate", Json{{"tree", best.at("tree")}}},
                  {"features", strings(program::feature_names())}, {"feature_semantics", "contenders-job-context/v1"},
                  {"controller", run.config.value("controller", "anchors/main-v42")},
                  {"training", best}, {"environment_identity", run.state.at("environment_identity")},
                  {"qualification", "unqualified_search_output"}, {"native_export", native_export},
                  {"artifacts_manifest", "artifacts.json"}, {"artifact_paths_relative_to", "best_program.json"}};
    if (run.state.contains("validation")) exported["validation"] = run.state.at("validation");
    if (run.state.contains("holdout")) exported["holdout"] = run.state.at("holdout");
    write_json(run.options.out / "best_program.json", exported);
    atomic_write(run.options.out / "best_program.hpp", program::cpp_header(best.at("tree")));
    Json reproduction = request_for(run.config, run.state.at("training_cases"), best.at("tree"), "training");
    const auto& context=run.state.at("environment_contexts").at("training");
    reproduction["controller"]=context.at("controller").at("key");
    reproduction["baseline"]=context.at("baseline").at("key");
    reproduction["expected_context_identity"]=run.state.at("environment_identities").at("training");
    Json frozen_cases=Json::array();for(const auto& item:context.at("cases").as_array())
        frozen_cases.push_back(Json{{"map",item.at("map")},{"map_sha256",item.at("map_sha256")},{"side",item.at("side")},{"opponent",item.at("opponent").at("key")}});
    reproduction["cases"]=frozen_cases;
    write_json(run.options.out / "reproduce_best.json", reproduction);
}
}

int run_c1(const Options& options) {
    Run run(options, "c1", "2");
    const Json train = cases_for(run.config, "training");
    const Json validation = cases_for(run.config, "validation");
    const Json holdout = cases_for(run.config, "holdout");
    check_splits(run.config,train, validation, holdout);
    const int population_size = run.config.value("population_size", 32);
    const int max_depth = run.config.value("max_depth", 4);
    const int tournament_size = run.config.value("tournament_size", 3);
    const int validation_every = run.config.value("validation_every", 25);
    const int archive_capacity=run.config.value("archive_capacity",128);
    const double immigration = run.config.value("immigration", 0.12);
    const double crossover_rate = run.config.value("crossover_rate", 0.25);
    const double sigma = run.config.value("mutation_sigma", 0.35);
    const std::string objective = run.config.value("fitness", "objective");
    if (population_size < 2 || population_size > 4096 || archive_capacity<1||archive_capacity>4096||max_depth < 0 || max_depth > 6 || tournament_size < 1 ||
        tournament_size > 100 || validation_every < 0 || !std::isfinite(immigration) || immigration < 0 || immigration > 1 ||
        !std::isfinite(crossover_rate) || crossover_rate < 0 || crossover_rate > 1 || !std::isfinite(sigma) || sigma <= 0 ||
        (objective != "objective" && objective != "paired_objective")) throw std::runtime_error("invalid C1 search configuration");
    Json environments = Json::object(),contexts=Json::object();std::map<std::string,std::string> map_roles;
    for (const auto& role : {"training", "validation", "holdout"}) {
        const Json& cases = std::string(role) == "training" ? train : std::string(role) == "validation" ? validation : holdout;
        if (cases.empty()) continue;
        Json identity_request = request_for(run.config, cases, Json{{"factor", 1.0}}, role);
        identity_request["kind"] = "identity";
        const Json identity_response = evaluate(options, identity_request, options.out / "environment" / role);
        const std::string role_identity = identity_response.value("environment_identity", "");
        if (role_identity.empty()) throw std::runtime_error("C1 evaluator must provide environment_identity for safe continuation");
        const auto& context=identity_response.at("identity");
        if(context.at("cases").size()!=cases.size())throw std::runtime_error("C1 identity does not cover every requested case");
        for(const auto& item:context.at("cases").as_array()) {
            const auto hash=item.at("map_sha256").get<std::string>();
            if(map_roles.contains(hash)&&map_roles.at(hash)!=role)throw std::runtime_error("C1 native map bytes overlap evaluation roles");
            map_roles[hash]=role;
        }
        environments[role] = role_identity;
        contexts[role]=context;
    }
    const std::string environment = sha256(environments.dump());
    if (run.resumed) {
        if (run.state.at("environment_identity").get<std::string>() != environment)
            throw std::runtime_error("C1 resume rejected: evaluator/controller/opponents/maps/toolchain identity changed");
        if(run.state.at("environment_contexts")!=contexts)throw std::runtime_error("C1 frozen native context changed on resume");
        verify_active_results(run);
    } else {
        run.state = Json{{"environment_identity", environment}, {"environment_identities", environments},{"environment_contexts",contexts}, {"population", Json::array()}, {"best", nullptr},
                         {"fitness_cache", Json::object()}, {"archive", Json::object()}, {"candidate_evaluations", 0},
                         {"diversity_archive",Json::object()},{"duplicate_proposals", 0},{"duplicate_retries",0}, {"training_cases", train}, {"validation_cases", validation}, {"holdout_cases", holdout}};
        run.checkpoint();
    }
    const auto checkpoint_limit=run.config.value<std::uint64_t>("max_checkpoint_mb",384)*1024*1024;
    if(checkpoint_limit<8ULL*1024*1024||checkpoint_limit>768ULL*1024*1024)throw std::runtime_error("C1 checkpoint limit must be8..768MiB");
    bool resource_limited=false;
    while (!run.stop()) {
        if(run.state.dump().size()+4*1024*1024>checkpoint_limit){resource_limited=true;break;}
        Json tree;
        if (run.state.contains("pending")) tree = run.state.at("pending");
        else {
            const auto& population = run.state.at("population");
            if (population.empty()) tree = Json{{"factor", 1.0}};
            else if (population.size() < static_cast<std::size_t>(population_size) || rng_unit(run) < immigration)
                tree = program::random_tree(run.rng, max_depth);
            else {
                tree = tournament(run, population, tournament_size).at("tree");
                if (rng_unit(run) < crossover_rate)
                    tree = program::crossover(tree, tournament(run, population, tournament_size).at("tree"), run.rng, max_depth);
                tree = program::mutate(tree, run.rng, max_depth, sigma);
            }
            // Retry exact already-evaluated genotypes before spending an
            // iteration; the bounded fallback still cannot duplicate parents.
            for(int retry=0;retry<64;++retry) {
                const auto key=sha256(tree.dump()+"\n"+train.dump()+"\ntraining");
                if(!run.state.at("fitness_cache").contains(key))break;
                run.state["duplicate_retries"]=run.state.value<std::uint64_t>("duplicate_retries",0)+1;
                tree=program::random_tree(run.rng,max_depth);
            }
            run.state["pending"] = tree;
            run.checkpoint(); // The proposed tree and post-proposal RNG survive an interrupted evaluator.
        }
        program::validate(tree, max_depth);
        Json evaluated = evaluate_tree(run, tree, train, "training", objective);
        if (evaluated.at("cache_hit").get<bool>()) run.state["duplicate_proposals"] = run.state.value<std::uint64_t>("duplicate_proposals", 0) + 1;
        Json& population = run.state["population"];
        bool exact_duplicate=false;for(const auto& parent:population.as_array())if(parent.at("tree")==tree){exact_duplicate=true;break;}
        if (!exact_duplicate&&population.size() < static_cast<std::size_t>(population_size)) population.push_back(evaluated);
        else if(!exact_duplicate) {
            std::size_t worst = rng_index(run, population.size());
            for (std::size_t i = 0; i < population.size(); ++i) if (better(population.at(worst), population.at(i))) worst = i;
            if (better(evaluated, population.at(worst)) || !better(population.at(worst), evaluated) || rng_unit(run) < immigration)
                population.at(worst) = evaluated;
        }
        Json& best = run.state["best"];
        bool improved = false;
        if (evaluated.at("eligible").get<bool>() && (best.is_null() || better(evaluated, best))) {
            best = evaluated; improved = true;
            run.state.as_object().erase("validation");
            run.state.as_object().erase("holdout");
        }
        const std::string cell = archive_cell(tree);
        auto& archive = run.state["archive"];
        if (evaluated.at("eligible").get<bool>() && (!archive.contains(cell) || better(evaluated, archive.at(cell)))) archive[cell] = evaluated;
        auto& diversity=run.state["diversity_archive"];
        const auto niche=evaluated.at("outcome_signature").get<std::string>()+"_"+evaluated.at("ranking_signature").get<std::string>();
        if(evaluated.at("eligible").get<bool>()&&(!diversity.contains(niche)||better(evaluated,diversity.at(niche))))diversity[niche]=evaluated;
        if(diversity.size()>static_cast<std::size_t>(archive_capacity)) {
            auto worst=diversity.as_object().begin();
            for(auto it=diversity.as_object().begin();it!=diversity.as_object().end();++it)if(better(worst->second,it->second))worst=it;
            diversity.as_object().erase(worst);
        }
        run.state.as_object().erase("pending");
        run.advance();
        run.event(Json{{"kind", "candidate"}, {"candidate", evaluated.at("id")}, {"eligible", evaluated.at("eligible")},
                       {"score", evaluated.at("score")}, {"nodes", evaluated.at("nodes")}, {"cache_hit", evaluated.at("cache_hit")}, {"improved", improved}});
        if (!best.is_null() && !validation.empty() && validation_every > 0 && run.iteration % static_cast<std::uint64_t>(validation_every) == 0)
            run.state["validation"] = evaluate_tree(run, best.at("tree"), validation, "validation", objective);
        run.progress(Json{{"best_training", best.is_null() ? Json(nullptr) : best.at("score")},
                          {"eligible_incumbent", !best.is_null()}, {"candidate_evaluations", run.state.at("candidate_evaluations")},
                          {"archive_cells", archive.size()}, {"duplicate_proposals", run.state.at("duplicate_proposals")}});
        run.checkpoint();
        if (improved || run.iteration % 10 == 0) export_best(run);
    }
    if (!run.state.at("best").is_null() && !interrupted()&&!time_limit_reached()&&run.remaining_seconds()>.5) {
        const Json tree = run.state.at("best").at("tree");
        if (!validation.empty()) run.state["validation"] = evaluate_tree(run, tree, validation, "validation", objective);
        if (!holdout.empty() && run.config.value("evaluate_holdout_on_finish", false))
            run.state["holdout"] = evaluate_tree(run, tree, holdout, "holdout", objective);
    }
    export_best(run);
    write_json(options.out / "program_archive.json", run.state.at("archive"));
    write_json(options.out/"diversity_archive.json",Json{{"schema","contenders-program-diversity/v1"},{"entries",run.state.at("diversity_archive")},
        {"scope","Bounded synthetic job-ranking and actual native terminal-outcome niches. Neither is a certificate of native behavioral equivalence or trajectory diversity."}});
    Json result{{"schema", "contenders-c1-result/v1"}, {"engine", "c1"}, {"search", "steady_state_genetic_programming"},
                {"domain", "conditional_job_priority_programs_over_twelve_permitted_features"},
                {"iterations", run.iteration}, {"candidate_evaluations", run.state.at("candidate_evaluations")},
                {"population_turnover_equivalent",static_cast<double>(run.iteration)/population_size},
                {"duplicate_proposals", run.state.at("duplicate_proposals")},
                {"duplicate_retries",run.state.at("duplicate_retries")},{"status",resource_limited?"resource_limited":"checkpointed_anytime"},
                {"checkpoint_state_bytes",run.state.dump().size()},{"checkpoint_limit_bytes",checkpoint_limit},
                {"diversity_archive_cells",run.state.at("diversity_archive").size()},{"population_size",run.state.at("population").size()},
                {"best", run.state.at("best")}, {"archive_cells", run.state.at("archive").size()},
                {"training_cases", train.size()}, {"validation_cases", validation.size()}, {"holdout_cases", holdout.size()},
                {"environment_identity", environment}, {"qualification", "unqualified_search_output"},
                {"reproduction_scope","Frozen entrant keys and map hashes are required. Selected native artifacts are retained; restoring other source populations requires their original snapshots or recipes."},
                {"limitations", Json::array({"Search grammar changes job priorities, not arbitrary champion micro or the full bot.",
                    "Structural, synthetic ranking and native terminal-outcome niches are diversity proxies, not proof of native trajectory diversity.",
                    "Validation is reported and does not select parents; repeated deterministic keys are cached, not new samples.",
                    "A requested final holdout is not an unseen historical map set or automatic release qualification."})}};
    if (run.state.contains("validation")) result["validation"] = run.state.at("validation");
    if (run.state.contains("holdout")) result["holdout"] = run.state.at("holdout");
    result["final_roles_deferred_by_deadline"]=time_limit_reached()||run.remaining_seconds()<=.5;
    run.checkpoint(); run.finish(result); return 0;
}
} // namespace opt
