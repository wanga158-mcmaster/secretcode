#include "opt/oracle.hpp"
#include "opt/sim.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace opt::oracle {
namespace {
fs::path home_path() {
    const char* p=std::getenv(
#ifdef _WIN32
        "USERPROFILE"
#else
        "HOME"
#endif
    );
    return p?fs::path(p):fs::path{};
}
std::string python_path(const Options& o) {
    if (o.has("python")) return o.get("python");
    if (const char* p=std::getenv("OPT_PYTHON")) return p;
    const auto base=o.repo/"bots/contenders/tools/optimization";
    for (const auto& p: {
        base/"runtime/python/python.exe", base/"runtime/python/bin/python3",
        o.repo/"runtime/python/python.exe", o.repo/"runtime/python/bin/python3",
        o.executable.parent_path()/"runtime/python/python.exe",
        home_path()/".cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe"})
        if (fs::is_regular_file(p)) return p.string();
#ifdef _WIN32
    throw std::runtime_error("Native oracle needs --python PATH or a packet Python runtime");
#else
    return "python3";
#endif
}
}
Json invoke(const Options& o,const Json& request,const fs::path& directory) {
    fs::create_directories(directory);
    fs::path call;
    for (std::uint64_t suffix=0;;++suffix) {
        call=directory/("call-"+std::to_string(suffix));
        if (fs::create_directory(call)) break;
    }
    const auto input=call/"request.json", output=call/"response.json";
    write_json(input,request);
    std::vector<std::string> command;
    if (o.has("oracle")) {
        command.push_back(o.get("oracle"));
        const auto extra=o.values("oracle-arg"); command.insert(command.end(),extra.begin(),extra.end());
    } else {
        command={python_path(o),"-B","-I","-X","utf8",
            (o.repo/"bots/contenders/tools/optimization/adapters/native_oracle.py").string()};
    }
    command.insert(command.end(),{"--request",fs::absolute(input).string(),"--response",fs::absolute(output).string(),
        "--repo",fs::absolute(o.repo).string()});
    if (o.has("node")) command.insert(command.end(),{"--node",o.get("node")});
    if (o.has("oracle-engine")) command.insert(command.end(),{"--engine",o.get("oracle-engine")});
    if (o.has("oracle-seed")) command.insert(command.end(),{"--seed-replay",o.get("oracle-seed")});
    if (o.has("oracle-driver")) command.insert(command.end(),{"--driver",o.get("oracle-driver")});
    const auto status=run_process(command,o.repo,call/"launcher.stdout.log",call/"launcher.stderr.log",
        o.real("oracle-timeout",240));
    if (status.exit_code!=0 || status.timed_out)
        throw std::runtime_error("Native oracle subprocess failed; inspect "+(call/"launcher.stderr.log").string());
    if (!fs::is_regular_file(output)) throw std::runtime_error("Native oracle returned success without a fresh response");
    const auto response=read_json(output);
    if (response.value("schema","")!="contenders-native-oracle/v1")
        throw std::runtime_error("Native oracle response schema mismatch");
    if (response.value("request_sha256","")!=file_sha256(input))
        throw std::runtime_error("Native oracle response is not bound to this request");
    return response;
}
} // namespace opt::oracle

namespace opt {
namespace {
using oracle::Model;
std::uint64_t draw(Run& run,std::uint64_t n) { return std::uniform_int_distribution<std::uint64_t>(0,n-1)(run.rng); }
Json actions_for(const std::string& family) {
    const auto basic=Json{{"type","basic"},{"target","target"}};
    const auto cast=Json{{"type","cast"},{"slot",0},{"target","target"}};
    if (family=="basic") return Json::array({basic});
    if (family=="poke") return Json::array({cast});
    if (family=="poke_basic") return Json::array({cast,basic});
    if (family=="reset") return Json::array({basic,Json{{"type","reset"}},basic});
    if (family=="move_basic") return Json::array({Json{{"type","move"},{"x",35},{"y",19}},basic});
    const auto next=Json{{"type","next_owner"},{"owner","target"}};
    const auto reply=Json{{"type","basic"},{"owner","target"},{"target","actor"}};
    if (family=="cc_cycle") return Json::array({cast,next,reply,next,reply});
    if (family=="lethal_reply") return Json::array({basic,next,reply});
    throw std::runtime_error("Unknown C11 case family: "+family);
}
const std::vector<std::string> families={"basic","poke","poke_basic","reset","move_basic","cc_cycle","lethal_reply"};
Json stamp(Json spec) {
    spec.as_object().erase("id");
    spec["id"]=sha256(spec.dump()).substr(0,24);
    return spec;
}
Json fresh_case(Run& run,const Json& archive) {
    Json spec;
    if (!archive.empty() && draw(run,3)==0) {
        spec=archive.at(static_cast<std::size_t>(draw(run,archive.size()))).at("specification");
        spec.as_object().erase("id");
    } else {
        const auto family=families[draw(run,families.size())];
        spec=Json{{"family",family},{"actor_hp",500},{"target_hp",300},{"target_shield",0},
            {"actor_position",Json::array({35,18})},{"target_position",Json::array({34,18})},
            {"actions",actions_for(family)}};
    }
    // Include breakpoints, fractional float32 inputs, and broad random states. This
    // is a constructed-domain search, not an estimate of natural case frequency.
    const std::vector<double> actor_hp={1,46,47,48,99,100,101,500,850};
    const std::vector<double> target_hp={1,46,47,48,99,100,101,146,147,148,300,873};
    const std::vector<double> shields={0,1,25,46,47,48,99,100,101,147,200};
    const auto pick=[&](const std::vector<double>& choices,int maximum) {
        return draw(run,2)==0?choices[draw(run,choices.size())]:
            static_cast<double>(1+draw(run,static_cast<std::uint64_t>(maximum*4)))/4.0;
    };
    spec["actor_hp"]=std::min(850.0,pick(actor_hp,850));
    spec["target_hp"]=std::min(873.0,pick(target_hp,873));
    spec["target_shield"]=draw(run,2)==0?shields[draw(run,shields.size())]:static_cast<double>(draw(run,1001))/4;
    // Do not teleport onto source owner or unknown walls by pretending success.
    // The real oracle rejects any unsupported setup and it is reported separately.
    const int d=static_cast<int>(1+draw(run,6));
    spec["target_position"]=Json::array({35-d,18});
    if (spec.value("family","")=="lethal_reply" && draw(run,2)==0) spec["target_hp"]=1;
    return stamp(std::move(spec));
}
std::vector<Model> candidate_models(const Json& config) {
    std::vector<Model> models;
    if (config.contains("candidate_models")) {
        for (const auto& item:config.at("candidate_models").as_array()) models.push_back(Model::from(item));
    } else {
        for (double basic:{0.5,1.0,1.5}) for (double cast:{0.5,1.0,1.5})
            for (int mask=0;mask<64;++mask) {
                Model m;
                m.basic_scale=basic; m.cast_scale=cast; m.shield_first=(mask&1)!=0;
                m.reset_restores_attack=(mask&2)!=0; m.cast_consumes_attack=(mask&4)!=0;
                m.cast_blocks_move=(mask&8)!=0; m.basic_blocks_move=(mask&16)!=0;
                m.reset_requires_spent_attack=(mask&32)!=0;
                models.push_back(m);
            }
    }
    if (models.empty() || models.size()>4096) throw std::runtime_error("C11 supports 1..4096 candidate projection models");
    return models;
}
struct Loss { double total{}; int cases{},transitions{},mismatches{}; double average() const { return cases?total/cases:0; } };
Loss loss_on(const Json& reservoir,const Model& model,bool heldout,double tolerance) {
    Loss result;
    for (const auto& row:reservoir.as_array()) {
        if (row.at("heldout").get<bool>()!=heldout) continue;
        const auto comparison=oracle::compare_case(row.at("native_case"),model,tolerance);
        if (comparison.at("compared").get<int>()==0) continue;
        ++result.cases; result.total+=comparison.at("loss").get<double>();
        result.transitions+=comparison.at("compared").get<int>();
        result.mismatches+=comparison.at("mismatches").get<int>();
    }
    return result;
}
Json loss_json(const Loss& loss) {
    return Json{{"cases",loss.cases},{"compared_transitions",loss.transitions},
        {"mismatches",loss.mismatches},{"mean_projection_loss",loss.average()}};
}
bool heldout_family(const Json& config,const std::string& family) {
    if (config.contains("holdout_families"))
        for (const auto& item:config.at("holdout_families").as_array())
            if (item.get<std::string>()==family) return true;
    return false;
}
Json first_failure(const Json& native_case,const Model& model,double tolerance) {
    Json spec=native_case.at("specification"), prefix=Json::array();
    for (const auto& t:native_case.at("transitions").as_array()) {
        prefix.push_back(t.at("action"));
        const auto compared=oracle::compare_transition(t,model,tolerance);
        if (compared.value("classification","")=="prediction_error") {
            spec["actions"]=prefix;
            spec=stamp(spec);
            return Json{{"specification",spec},{"comparison",compared},{"native_before",t.at("before")},
                {"native_after",t.at("after")},{"native_applied",t.at("applied")},
                {"minimization","Shortest observed failing prefix of this explicit action list; not global delta-minimality"}};
        }
    }
    return Json{};
}
Json shared_model_comparison(const Json& fixture,double tolerance) {
    try {
        auto state=sim::initial(fixture.at("scenario"));
        const auto legal=sim::legal_actions(state);
        const bool predicted=std::find(legal.begin(),legal.end(),fixture.at("action"))!=legal.end();
        const bool actual=fixture.at("native_action_applied").get<bool>();
        if (predicted!=actual)
            return Json{{"classification","prediction_error"},{"loss",1.0},{"reason","Shared model legality differs"},
                {"predicted_legal",predicted},{"native_applied",actual}};
        if (!actual)
            return Json{{"classification","invalid_action_generation"},{"loss",0.0}};
        const auto after=sim::summary(sim::step(state,fixture.at("action")));
        const auto& expected=fixture.at("expected_native_projection");
        Json differences=Json::array(); double loss=0;
        for (std::size_t i=0;i<expected.at("actors").size();++i) {
            const auto& actor=expected.at("actors").at(i);
            for (const auto* field:{"hp","shield"}) {
                const double predicted_value=after.at(field).at(i).get<double>(), native=actor.at(field).get<double>();
                if (std::abs(predicted_value-native)>tolerance) {
                    loss+=std::abs(predicted_value-native)/std::max(1.0,std::abs(native));
                    differences.push_back(Json{{"actor_index",static_cast<int>(i)},{"field",field},
                        {"predicted",predicted_value},{"native",native}});
                }
            }
            if (actor.at("id").get<int>()==expected.at("owner").get<int>()) {
                const auto permissions=Json::array({actor.at("move"),actor.at("attack"),actor.at("cast")});
                if (after.at("permissions").at(i)!=permissions) {
                    ++loss; differences.push_back(Json{{"actor_index",static_cast<int>(i)},{"field","permissions"},
                        {"predicted",after.at("permissions").at(i)},{"native",permissions}});
                }
                if (after.at("cooldowns").at(i)!=actor.at("cooldowns")) {
                    ++loss; differences.push_back(Json{{"actor_index",static_cast<int>(i)},{"field","cooldowns"},
                        {"predicted",after.at("cooldowns").at(i)},{"native",actor.at("cooldowns")}});
                }
            }
        }
        return Json{{"classification",loss>0?"prediction_error":"transition_match"},{"loss",loss},
            {"differences",differences},{"model",sim::version},
            {"scope","One immediate constructed pair command; full world and owner-boundary fidelity excluded"}};
    } catch (const std::exception& error) {
        return Json{{"classification","insufficient_observation"},{"loss",0.0},{"reason",error.what()},
            {"model",sim::version}};
    }
}

int run_c11_camp(Run& run,const Options& options) {
    Options native=options;
    native.extra["oracle-driver"]={(options.repo/"bots/contenders/tools/optimization/adapters/native_camp_oracle.mjs").string()};
    const auto inventory_path=options.repo/run.config.value("inventory_file",std::string("results/contenders/optimization/c11/camp-domain/inventory.json"));
    const auto inventory=read_json(inventory_path);
    const auto inventory_hash=file_sha256(inventory_path);
    Json sources=Json::array();
    for(const auto& source:inventory.at("sources").as_array()) {
        bool use=!run.config.contains("source_inputs");
        if(!use)for(const auto& requested:run.config.at("source_inputs").as_array())
            if(requested==source.at("source_input"))use=true;
        if(use)sources.push_back(source);
    }
    if(sources.empty())throw std::runtime_error("C11 native-camp inventory has no requested actual source prefixes");
    const auto identity_response=oracle::invoke(native,Json{{"kind","identity"}},options.out/"oracle-identity");
    if(!identity_response.value("native",false)||!identity_response.value("supported",false))throw std::runtime_error("C11 camp requires actual native oracle");
    const auto identity=identity_response.at("identity");
    if(inventory.at("identity")!=identity)throw std::runtime_error("C11 native-camp inventory predates this oracle; regenerate camp_inventory before search");
    const int max_steps=run.config.value("max_trace_actions",192);
    const double mutation=run.config.value("random_action_probability",0.12);
    if(max_steps<1||max_steps>2048||!std::isfinite(mutation)||mutation<0||mutation>1)throw std::runtime_error("Invalid native-camp trace bounds");
    if(!run.resumed)run.state=Json{{"oracle_identity",identity},{"inventory_sha256",inventory_hash},{"model",sim::version},
        {"coverage",Json::object()},{"action_coverage",Json::object()},
        {"native_cases",0},{"cache_hits",0},{"prediction_errors",0},{"trace_matches",0},
        {"clears",0},{"captures",0},{"departures",0},{"unsupported",0},{"model_falsified",false},{"archive",Json::array()}};
    if(run.state.at("oracle_identity")!=identity||run.state.at("inventory_sha256").get<std::string>()!=inventory_hash||run.state.at("model").get<std::string>()!=sim::version)
        throw std::runtime_error("C11 native-camp source/model identity changed");
    auto save=[&]{run.checkpoint();};
    if(!run.resumed)save();
    fs::create_directories(options.out/"native-cache");
    while(!run.stop()&&!run.state.value("model_falsified",false)) {
        const std::size_t source_index=run.iteration<sources.size()?static_cast<std::size_t>(run.iteration):static_cast<std::size_t>(draw(run,sources.size()));
        const auto& source=sources.at(source_index);const auto& scenario=source.at("scenario");
        auto state=sim::initial(scenario);Json actions=Json::array(),states=Json::array({state});
        for(int step=0;step<max_steps&&!sim::terminal(state);++step) {
            auto legal=sim::legal_actions(state);
            legal.erase(std::remove_if(legal.begin(),legal.end(),[](const Json& action){
                const auto type=action.value("type","");return type!="basic"&&type!="cast"&&type!="move"&&type!="end";
            }),legal.end());
            if(legal.empty())break;
            auto action=sim::baseline_action(state);
            if(std::find(legal.begin(),legal.end(),action)==legal.end())action=legal.front();
            if(std::generate_canonical<double,53>(run.rng)<mutation)action=legal[static_cast<std::size_t>(draw(run,legal.size()))];
            actions.push_back(action);state=sim::step(state,action);states.push_back(state);
        }
        const Json request{{"kind","scenario_validation"},{"source_input",source.at("source_input")},{"scenario",scenario},
            {"actions",actions},{"expected_states",states},{"scenario_sha256",sha256(scenario.dump())},{"trace_sha256",sha256(actions.dump())},
            {"prediction_sha256",sha256(states.dump())},{"comparison_projection","restricted-model-after-every-action"}};
        const auto key=sha256(Json{{"request",request},{"oracle_identity",identity},{"model",sim::version}}.dump());
        const auto cache=options.out/"native-cache"/(key+".json");Json response;
        std::uint64_t first_iteration=run.iteration;
        if(fs::is_regular_file(cache)) {
            const auto stored=read_json(cache);if(stored.at("key").get<std::string>()!=key||stored.at("request")!=request||stored.at("response").at("identity")!=identity)
                throw std::runtime_error("C11 camp native cache content mismatch");
            if(stored.at("response_sha256").get<std::string>()!=sha256(stored.at("response").dump()))throw std::runtime_error("C11 camp cached native response checksum mismatch");
            response=stored.at("response");first_iteration=stored.at("first_iteration").get<std::uint64_t>();
            if(first_iteration>run.iteration)throw std::runtime_error("C11 native cache belongs to an ungenerated future iteration");
            if(first_iteration<run.iteration)run.state["cache_hits"]=run.state.at("cache_hits").get<std::uint64_t>()+1;
        } else {
            response=oracle::invoke(native,request,options.out/"oracle-work"/("case-"+std::to_string(run.iteration)));
            if(response.at("identity")!=identity||!response.value("original_seed_unchanged",false))throw std::runtime_error("C11 native-camp identity or replay preservation failure");
            write_json(cache,Json{{"key",key},{"first_iteration",run.iteration},{"request",request},{"response",response},{"response_sha256",sha256(response.dump())}});
        }
        for(const auto* field:{"scenario_sha256","trace_sha256","prediction_sha256"})
            if(response.value(field,std::string{})!=request.at(field).get<std::string>())throw std::runtime_error(std::string("C11 native-camp response binding differs: ")+field);
        if(response.value("comparison_projection",std::string{})!="restricted-model-after-every-action")throw std::runtime_error("C11 native-camp projection mismatch");
        // Cache creation's iteration is an idempotent commit marker: replay after
        // interruption counts an uncommitted case once, while later repetitions
        // are hits. The checkpoint need not grow with every prior trace hash.
        if(first_iteration==run.iteration) {
            run.state["native_cases"]=run.state.at("native_cases").get<std::uint64_t>()+1;
            const auto source_key=source.at("source_input").dump();
            auto& coverage=run.state["coverage"];coverage[source_key]=coverage.value(source_key,std::uint64_t{0})+1;
            for(const auto& action:actions.as_array()) {
                const auto label=action.value("type","")+(action.value("type","")=="cast"?std::to_string(action.at("slot").get<int>()):"");
                auto& counts=run.state["action_coverage"];counts[label]=counts.value(label,std::uint64_t{0})+1;
            }
            if(!response.value("native",false)||!response.value("supported",false))run.state["unsupported"]=run.state.at("unsupported").get<std::uint64_t>()+1;
            else {
                const bool passed=response.value("passed",false)&&response.value("scope","")=="entire-trace";
                const std::string counter=passed?"trace_matches":"prediction_errors";
                run.state[counter]=run.state.at(counter).get<std::uint64_t>()+1;
                const auto& after=response.at("after");bool clear=true,on_tile=false;
                for(const auto& camp:after.at("camps").as_array()) {
                    clear=clear&&camp.at("hp").get<double>()==0;
                    on_tile=on_tile||(camp.at("x")==after.at("actor").at("x")&&camp.at("y")==after.at("actor").at("y"));
                }
                const bool captured=after.at("actor").at("gems").get<int>()>response.at("before").at("actor").at("gems").get<int>();
                for(const auto& [name,condition]:std::vector<std::pair<std::string,bool>>{{"clears",clear},{"captures",captured},{"departures",captured&&!on_tile}})
                    if(condition)run.state[name]=run.state.at(name).get<std::uint64_t>()+1;
                if(!passed) {
                    if(run.config.value("stop_on_first_counterexample",true))run.state["model_falsified"]=true;
                    int first=static_cast<int>(actions.size())-1;
                    for(const auto& difference:response.at("differences").as_array())if(difference.contains("step"))first=std::min(first,difference.at("step").get<int>());
                    Json prefix=Json::array();for(int i=0;i<=first&&i<static_cast<int>(actions.size());++i)prefix.push_back(actions.at(static_cast<std::size_t>(i)));
                    write_json(options.out/"counterexamples"/(key+".json"),Json{{"key",key},{"oracle_identity",identity},{"scenario",scenario},{"actions",prefix},
                        {"differences",response.at("differences")},{"minimization","Shortest observed failing action prefix, not global delta-minimum"},{"native_case",fs::relative(cache,options.out).generic_string()}});
                    auto& archive=run.state["archive"].as_array();archive.push_back(Json{{"key",key},{"source_input",source.at("source_input")},{"first_failure_step",first}});
                    if(archive.size()>128)archive.erase(archive.begin());
                }
            }
        }
        run.advance();save();run.progress(Json{{"native_cases",run.state.at("native_cases")},{"trace_matches",run.state.at("trace_matches")},
            {"prediction_errors",run.state.at("prediction_errors")},{"captures",run.state.at("captures")},{"source_prefixes",run.state.at("coverage").size()}});
    }
    save();
    std::ofstream corpus(options.out/"native-camp-corpus.jsonl",std::ios::binary|std::ios::trunc);
    if(!corpus)throw std::runtime_error("Cannot export native camp corpus");
    std::vector<fs::path> cache_files;
    for(const auto& entry:fs::directory_iterator(options.out/"native-cache"))if(entry.is_regular_file()&&entry.path().extension()==".json")cache_files.push_back(entry.path());
    std::sort(cache_files.begin(),cache_files.end());
    for(const auto& file:cache_files) {
        const auto stored=read_json(file);
        if(stored.at("first_iteration").get<std::uint64_t>()<run.iteration)corpus<<stored.dump()<<'\n';
    }
    corpus.flush();if(!corpus)throw std::runtime_error("Cannot flush native camp corpus");
    Json report=run.state;report["domain"]=identity.at("domain");report["available_actual_source_prefixes"]=sources.size();
    report["status"]=run.state.value("model_falsified",false)?"model_falsified":run.iteration>=options.iterations?"completed":"checkpointed";
    report["qualification"]="Actual native replay branch differential tests over complete declared local traces; each mismatch is a model falsification, not a strength result";
    report["products"]=Json::array({"native-camp-corpus.jsonl","native-cache/","counterexamples/","checkpoint.json"});
    report["limits"]=Json::array({"One native Varg camp group and no-items actual prefix distribution", "Other guest policies yield; no unseen enemy-response claim",
        "Only local declared observable state is compared; hidden capture countdown is not directly read", "Random/model-guided traces search falsification; matching coverage is not universal model fidelity",
        "Default stops on the first native counterexample; repair the model before a longer campaign. Disable only for explicit coverage diagnostics."});
    run.finish(report);return 0;
}
}

int run_c11(const Options& options) {
    Run run(options,"c11-native-identification","2");
    if(run.config.value("domain",std::string{})=="retained-varg-camp18-v1")return run_c11_camp(run,options);
    const auto batch=run.config.value("batch_size",8);
    const auto capacity=run.config.value("reservoir_size",96);
    const auto tolerance=run.config.value("tolerance",1e-5);
    if (batch<1 || batch>64 || capacity<8 || capacity>2048 || !std::isfinite(tolerance) || tolerance<0)
        throw std::runtime_error("Invalid C11 batch/reservoir/tolerance");
    const auto identity_response=oracle::invoke(options,Json{{"kind","identity"}},options.out/"oracle-identity");
    if (!identity_response.value("native",false) || !identity_response.value("supported",false))
        throw std::runtime_error("C11 requires the actual pinned native oracle");
    const auto identity=identity_response.at("identity");
    Json reservoir=Json::array(), archive=Json::array(), coverage=Json::object(), comparison_counts=Json::object();
    Json shared_counts=Json::object();
    std::set<std::string> processed;
    std::uint64_t unique=0,hits=0,unsupported=0,seen=0;
    Model model=Model::from(run.config.value("initial_model",Json::object()));
    if (run.resumed) {
        if (run.state.at("oracle_identity")!=identity)
            throw std::runtime_error("Native oracle assets/source differ from checkpoint");
        reservoir=run.state.at("reservoir"); archive=run.state.at("archive"); coverage=run.state.at("coverage");
        comparison_counts=run.state.at("comparison_counts");
        shared_counts=run.state.value("shared_sim_comparison_counts",Json::object());
        for (const auto& key:run.state.at("processed_case_ids").as_array()) processed.insert(key.get<std::string>());
        unique=run.state.at("unique_native_cases").get<std::uint64_t>();
        hits=run.state.at("cache_hits").get<std::uint64_t>(); unsupported=run.state.at("unsupported").get<std::uint64_t>();
        seen=run.state.at("seen_supported").get<std::uint64_t>(); model=Model::from(run.state.at("model"));
    }
    const auto models=candidate_models(run.config);
    fs::create_directories(options.out/"native-cache");
    fs::create_directories(options.out/"case-evidence");
    std::size_t tied=0;
    auto checkpoint=[&] {
        Json processed_ids=Json::array();
        for (const auto& key:processed) processed_ids.push_back(key);
        run.checkpoint(Json{{"oracle_identity",identity},{"reservoir",reservoir},{"archive",archive},{"coverage",coverage},
            {"comparison_counts",comparison_counts},{"unique_native_cases",unique},{"cache_hits",hits},
            {"unsupported",unsupported},{"seen_supported",seen},{"model",model.json()},
            {"shared_sim_comparison_counts",shared_counts},{"processed_case_ids",processed_ids}});
    };
    // The initial RNG/state must be recoverable even if the first oracle batch
    // is interrupted after writing some immutable cache entries.
    if (!run.resumed) checkpoint();
    while (!run.stop()) {
        Json specifications=Json::array();
        std::map<std::string,Json> completed;
        std::set<std::string> batch_keys;
        for (int n=0;n<batch;++n) {
            const auto spec=fresh_case(run,archive);
            const auto key=spec.at("id").get<std::string>();
            if (!batch_keys.insert(key).second) { ++hits; continue; }
            const auto file=options.out/"native-cache"/(key+".json");
            if (fs::is_regular_file(file)) {
                const auto cached=read_json(file);
                if (cached.at("oracle_identity")!=identity || cached.at("native_case").at("specification")!=spec)
                    throw std::runtime_error("C11 cached native case identity mismatch");
                completed[key]=cached.at("native_case");
                if (processed.count(key)) ++hits;
            } else specifications.push_back(spec);
        }
        if (!specifications.empty()) {
            const auto directory=options.out/"oracle-work"/("batch-"+std::to_string(run.iteration));
            const auto response=oracle::invoke(options,Json{{"kind","native_transition_batch"},{"cases",specifications}},directory);
            if (response.at("identity")!=identity || !response.value("original_seed_unchanged",false))
                throw std::runtime_error("Native batch identity/seed preservation failure");
            if (response.at("results").size()!=specifications.size())
                throw std::runtime_error("Native batch dropped cases");
            for (std::size_t i=0;i<specifications.size();++i) {
                const auto& spec=specifications.at(i); const auto& item=response.at("results").at(i);
                if (item.at("specification")!=spec) throw std::runtime_error("Native case order/content mismatch");
                const auto key=spec.at("id").get<std::string>();
                completed[key]=item;
                const auto stored=Json{{"oracle_identity",identity},{"native_case",item}};
                write_json(options.out/"native-cache"/(key+".json"),stored);
            }
        }
        for (const auto& [key,item]:completed) {
            if (processed.insert(key).second) {
                ++unique;
                Json evidence=Json::array();
                if (item.value("supported",false))
                    for (const auto& transition:item.at("transitions").as_array()) {
                        const auto fixture=oracle::simulator_fixture(transition);
                        if (fixture.is_null()) continue;
                        Json row{{"oracle_identity",identity},{"case_id",key},{"fixture",fixture}};
                        if (run.config.value("compare_shared_sim",true)) {
                            row["comparison"]=shared_model_comparison(fixture,tolerance);
                            const auto classification=row.at("comparison").at("classification").get<std::string>();
                            shared_counts[classification]=shared_counts.value(classification,std::uint64_t{0})+1;
                        }
                        evidence.push_back(std::move(row));
                    }
                // A crash before checkpoint simply overwrites this derived file
                // on replay; counters and final JSONL products remain identical.
                write_json(options.out/"case-evidence"/(key+".json"),evidence);
            }
            const auto family=item.at("specification").value("family","unknown");
            coverage[family]=coverage.value(family,std::uint64_t{0})+1;
            if (!item.value("supported",false)) { ++unsupported; continue; }
            const auto comparison=oracle::compare_case(item,model,tolerance);
            for (const auto& t:comparison.at("transitions").as_array()) {
                const auto classification=t.at("classification").get<std::string>();
                comparison_counts[classification]=comparison_counts.value(classification,std::uint64_t{0})+1;
            }
            const bool heldout=heldout_family(run.config,family);
            const Json row{{"case_id",key},{"heldout",heldout},{"native_case",item}};
            bool in_reservoir=false;
            for (const auto& previous:reservoir.as_array())
                if (previous.at("case_id").get<std::string>()==key) { in_reservoir=true; break; }
            if (!in_reservoir) {
                ++seen;
                if (reservoir.size()<static_cast<std::size_t>(capacity)) reservoir.push_back(row);
                else {
                    const auto replacement=draw(run,seen);
                    if (replacement<static_cast<std::uint64_t>(capacity))
                        reservoir[static_cast<std::size_t>(replacement)]=row;
                }
            }
            if (!heldout) {
                auto& existing=archive.as_array();
                existing.erase(std::remove_if(existing.begin(),existing.end(),[&](const Json& row) {
                    return row.at("key").get<std::string>()==key;
                }),existing.end());
                archive.push_back(Json{{"key",key},{"specification",item.at("specification")},
                    {"loss",comparison.at("loss")},{"family",family}});
                auto& values=archive.as_array();
                std::stable_sort(values.begin(),values.end(),[](const Json& a,const Json& b) {
                    return a.at("loss").get<double>()>b.at("loss").get<double>();
                });
                if (values.size()>128) values.resize(128);
                const auto failure=first_failure(item,model,tolerance);
                if (!failure.is_null())
                    write_json(options.out/"counterexamples"/(key+"-"+sha256(model.json().dump()).substr(0,12)+".json"),
                        Json{{"oracle_identity",identity},{"model",model.json()},{"failure",failure}});
            }
        }
        const double old_loss=loss_on(reservoir,model,false,tolerance).average();
        double best_loss=old_loss; Model best=model; tied=0;
        for (const auto& candidate:models) {
            const auto loss=loss_on(reservoir,candidate,false,tolerance);
            if (!loss.cases) continue;
            if (loss.average()<best_loss-1e-12) { best=candidate; best_loss=loss.average(); tied=1; }
            else if (std::abs(loss.average()-best_loss)<=1e-12) ++tied;
        }
        if (best.json()!=model.json()) {
            run.event(Json{{"type","model_improvement"},{"from_loss",old_loss},{"to_loss",best_loss},
                {"model",best.json()},{"selection","Training reservoir only"}});
            model=best;
        }
        run.advance();
        const auto train=loss_on(reservoir,model,false,tolerance), validation=loss_on(reservoir,model,true,tolerance);
        run.progress(Json{{"unique_native_cases",unique},{"cache_hits",hits},{"unsupported",unsupported},
            {"training",loss_json(train)},{"heldout",loss_json(validation)}});
        write_json(options.out/"best-projection-model.json",Json{{"model",model.json()},{"oracle_identity",identity},
            {"domain",oracle::domain},{"training",loss_json(train)},{"heldout",loss_json(validation)},
            {"tied_grid_models",static_cast<std::uint64_t>(tied)},
            {"qualification","Immediate observed two-Ryth projection only; not a full world model or universal fidelity proof"}});
        checkpoint();
    }
    checkpoint();
    // Reconstruct aggregate exports from checkpointed content-addressed cases.
    // Their contents do not depend on an interruption between cache and state.
    {
        std::ofstream corpus(options.out/"transition-corpus.jsonl",std::ios::binary|std::ios::trunc);
        std::ofstream fixtures(options.out/"simulator-fixtures.jsonl",std::ios::binary|std::ios::trunc);
        std::ofstream comparisons(options.out/"shared-sim-comparisons.jsonl",std::ios::binary|std::ios::trunc);
        if (!corpus || !fixtures || !comparisons) throw std::runtime_error("Cannot create C11 corpus exports");
        for (const auto& key:processed) {
            corpus<<read_json(options.out/"native-cache"/(key+".json")).dump()<<'\n';
            const auto evidence=read_json(options.out/"case-evidence"/(key+".json"));
            for (const auto& row:evidence.as_array()) {
                Json fixture=row; fixture.as_object().erase("comparison");
                fixtures<<fixture.dump()<<'\n';
                if (row.contains("comparison")) comparisons<<row.dump()<<'\n';
            }
        }
        corpus.flush(); fixtures.flush(); comparisons.flush();
        if (!corpus || !fixtures || !comparisons) throw std::runtime_error("Cannot flush C11 corpus exports");
    }
    const auto train=loss_on(reservoir,model,false,tolerance), validation=loss_on(reservoir,model,true,tolerance);
    std::ostringstream header;
    header<<"#pragma once\n// C11 immediate Ryth projection parameters; native domain "<<oracle::domain<<"\n";
    header<<"namespace contender_projection {\ninline constexpr double basic_scale="<<model.basic_scale
        <<", cast_scale="<<model.cast_scale<<";\n";
    const auto exported_model=model.json();
    for (const auto& [key,value]:exported_model.as_object())
        if (value.is_boolean()) header<<"inline constexpr bool "<<key<<"="<<(value.get<bool>()?"true":"false")<<";\n";
    header<<"}\n";
    atomic_write(options.out/"projection_model.hpp",header.str());
    run.finish(Json{{"status",run.iteration>=options.iterations?"completed":"checkpointed"},{"domain",oracle::domain},
        {"iterations",run.iteration},{"oracle_identity",identity},{"unique_native_cases",unique},{"cache_hits",hits},
        {"unsupported_setups",unsupported},{"coverage",coverage},{"comparison_counts",comparison_counts},
        {"shared_sim_comparison_counts",shared_counts},
        {"model",model.json()},{"training",loss_json(train)},{"heldout",loss_json(validation)},
        {"tied_grid_models",static_cast<std::uint64_t>(tied)},
        {"products",Json::array({"best-projection-model.json","projection_model.hpp","transition-corpus.jsonl","simulator-fixtures.jsonl","counterexamples/","native-cache/","checkpoint.json"})},
        {"limits",Json::array({"One retained Ryth mirror boundary and constructed suffixes",
            "Hidden world/owner transitions and movement occupancy are not predicted",
            "Heldout means explicit suffix-family split, not unseen champions/maps/opponents",
            "Finite candidate parameter family and bounded reservoir do not prove model identification uniqueness",
            "No native certification of arbitrary atlas/curriculum states or competitive policy strength"})}});
    return 0;
}
} // namespace opt
