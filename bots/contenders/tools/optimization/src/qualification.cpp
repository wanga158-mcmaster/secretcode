#include "opt/qualification.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <set>
#include <sstream>

namespace opt::qualification {
namespace {
fs::path resolve(const fs::path& path, const fs::path& base, const fs::path& repo) {
    if(path.is_absolute())return path;
    if(fs::is_regular_file(base/path))return fs::absolute(base/path);
    if(fs::is_regular_file(repo/path))return fs::absolute(repo/path);
    throw std::runtime_error("qualification input missing: "+path.string());
}
std::vector<std::string> map_names(const Json& selector,const fs::path& repo) {
    std::vector<std::string> names;
    if(selector.is_string()) {
        const auto value=selector.get<std::string>();
        if(value=="pool") {
            const auto pool=read_json(repo/".cambc/map-pool.json");
            for(const auto& row:pool.at("maps").as_array()) {
                const auto file=row.at("file").get<std::string>();
                const auto path=repo/"maps"/file;
                if(!fs::is_regular_file(path)||file_sha256(path)!=row.at("sha256").get<std::string>())
                    throw std::runtime_error("qualification pool map missing or changed: "+file);
                names.push_back(fs::path(file).stem().string());
            }
        } else if(value=="legacy") {
            for(const auto& entry:fs::recursive_directory_iterator(repo/"maps/legacy"))
                if(entry.is_regular_file()&&entry.path().extension()==".map26a")
                    names.push_back(fs::relative(entry.path(),repo/"maps").generic_string());
        } else names.push_back(value);
    } else if(selector.is_array())for(const auto& item:selector.as_array())names.push_back(item.get<std::string>());
    else throw std::runtime_error("qualification maps must be pool, legacy, or an explicit array");
    if(names.empty())throw std::runtime_error("qualification stage has no maps");
    std::sort(names.begin(),names.end());
    if(std::adjacent_find(names.begin(),names.end())!=names.end())throw std::runtime_error("duplicate qualification map");
    return names;
}
fs::path map_path(const fs::path& repo,const std::string& name) {
    const fs::path path(name);
    for(const auto& candidate:{path,repo/path,repo/"maps"/path,repo/"maps"/(name+".map26a")})
        if(fs::is_regular_file(candidate)&&candidate.extension()==".map26a")return fs::absolute(candidate);
    throw std::runtime_error("qualification map not found: "+name);
}
bool clean(const Json& row) {
    if(!row.value("complete",false))return false;
    for(const auto* field:{"health","baseline_health","opponent_health","baseline_opponent_health"})
        if(!row.contains(field)||!row.at(field).is_array()||!row.at(field).empty())return false;
    return true;
}
double exact_mcnemar(std::uint64_t gains,std::uint64_t losses) {
    const auto n=gains+losses,k=std::min(gains,losses);
    if(!n)return 1.;
    // Log probabilities avoid underflow when many discordant keys have one sign.
    double total=0.;
    for(std::uint64_t i=0;i<=k;++i)
        total+=std::exp(std::lgamma(static_cast<double>(n)+1)-std::lgamma(static_cast<double>(i)+1)
            -std::lgamma(static_cast<double>(n-i)+1)-static_cast<double>(n)*std::log(2.));
    return std::min(1.,2*total);
}
Json statistics(const Json& rows) {
    std::uint64_t wins=0,base_wins=0,draws=0,gains=0,losses=0,unhealthy=0,incomplete=0;
    double payoff=0,base=0,margin=0;
    for(const auto& row:rows.as_array()) {
        const double a=row.at("payoff").get<double>(),b=row.at("baseline_payoff").get<double>();
        if((a!=-1&&a!=0&&a!=1)||(b!=-1&&b!=0&&b!=1))throw std::runtime_error("nonterminal native payoff");
        wins+=a>0;base_wins+=b>0;draws+=a==0;gains+=(a>0&&b<=0);losses+=(a<=0&&b>0);
        payoff+=a;base+=b;margin+=row.at("margin").get<double>()-row.at("baseline_margin").get<double>();
        unhealthy+=!clean(row);incomplete+=!row.value("complete",false);
    }
    const double n=static_cast<double>(rows.size());
    return Json{{"keys",rows.size()},{"candidate_wins",wins},{"baseline_wins",base_wins},{"candidate_draws",draws},
        {"paired_win_gains",gains},{"paired_win_losses",losses},{"mean_payoff",n?payoff/n:0.},
        {"baseline_mean_payoff",n?base/n:0.},{"paired_mean_payoff",n?(payoff-base)/n:0.},
        {"paired_mean_gem_margin",n?margin/n:0.},{"unhealthy_pairs",unhealthy},{"incomplete_pairs",incomplete},
        {"raw_key_mcnemar_p",exact_mcnemar(gains,losses)}};
}
std::string csv(std::string text) {
    std::string out="\"";for(char c:text){if(c=='\"')out+='\"';out+=c;}return out+'\"';
}
void verify_artifacts(const fs::path& out,const Json& artifacts) {
    for(const auto& item:artifacts.as_array()) {
        const auto relative=item.at("path").get<std::string>();
        const fs::path path(relative);
        if(path.is_absolute()||relative.find("..")!=std::string::npos||!fs::is_regular_file(out/path)
            ||file_sha256(out/path)!=item.at("sha256").get<std::string>()
            ||fs::file_size(out/path)!=item.at("size").get<std::uint64_t>())
            throw std::runtime_error("qualification retained artifact missing or changed: "+relative);
    }
}
void preserve(Run& run,const Json& response) {
    auto& retained=run.state["artifacts"];
    for(const auto& item:response.value("artifacts",Json::array()).as_array()) {
        const auto source=fs::path(item.at("path").get<std::string>());
        const auto hash=item.at("sha256").get<std::string>();
        if(!fs::is_regular_file(source)||file_sha256(source)!=hash)throw std::runtime_error("native evidence changed before retention");
        // Native filenames may already contain one or two SHA256 digests.
        // Repeating them here overflows normal Windows output paths; the
        // content hash plus format extension is a sufficient portable key.
        const auto relative=fs::path("artifacts")/(hash+source.extension().string());
        const auto destination=run.options.out/relative;
        if(!fs::exists(destination))atomic_write(destination,read_text(source));
        if(file_sha256(destination)!=hash)throw std::runtime_error("retained native evidence checksum mismatch");
        bool seen=false;for(const auto& old:retained.as_array())seen|=old.at("path").get<std::string>()==relative.generic_string();
        if(!seen){Json copied=item;copied["path"]=relative.generic_string();copied["path_base"]="run_output";copied["size"]=fs::file_size(destination);retained.push_back(std::move(copied));}
    }
    write_json(run.options.out/"artifacts.json",retained);
}
Json raw_request(const Run& run) {
    Json request;
    if(run.config.contains("request_file")) {
        auto file=resolve(run.config.at("request_file").get<std::string>(),run.options.input.parent_path(),run.options.repo);
        request=read_json(file);
    } else request=run.config.contains("request")?run.config.at("request"):run.config;
    if(request.contains("request"))request=request.at("request");
    if(request.value("schema","")!="contenders-eval/v1"||!request.contains("candidate"))
        throw std::runtime_error("qualify requires an exported contenders-eval/v1 candidate request");
    if(request.value("kind","")!="config"&&request.value("kind","")!="program"&&request.value("kind","")!="policy")
        throw std::runtime_error("qualify supports native config/program/policy candidates");
    for(const auto* field:{"cases","context_cases","context_identity","environment_identity","expected_context_identity","request_id","request_sha256","campaign_identity"})request.as_object().erase(field);
    request["parallel_cases"]=run.config.value<unsigned>("parallel_cases",4);
    if(run.config.contains("baseline"))request["baseline"]=run.config.at("baseline");
    return request;
}
}

Json case_plan(const Json& config,const fs::path& repo) {
    const auto stages=config.value("stages",Json::array({Json{{"name","pool"},{"maps","pool"},
        {"opponents",Json::array({"anchors/main-v42","anchors/main-hunter7"})}}}));
    if(!stages.is_array()||stages.empty())throw std::runtime_error("qualification needs stages");
    Json plan=Json::array();std::set<std::string> keys,stage_names;
    for(const auto& stage:stages.as_array()) {
        const auto name=stage.at("name").get<std::string>();
        if(name.empty()||!stage_names.insert(name).second)throw std::runtime_error("duplicate/empty qualification stage name");
        const auto maps=map_names(stage.at("maps"),repo);
        const auto opponents=stage.at("opponents");
        if(!opponents.is_array()||opponents.empty())throw std::runtime_error("qualification needs opponents");
        std::set<std::string> seen_opponents;
        for(const auto& opponent:opponents.as_array())if(!seen_opponents.insert(opponent.get<std::string>()).second)throw std::runtime_error("duplicate qualification opponent");
        const auto sides=stage.value("sides",Json::array({"A","B"}));
        if(!sides.is_array()||sides.empty())throw std::runtime_error("qualification stage needs sides");
        for(const auto& map:maps) {
            const auto path=map_path(repo,map);
            const auto hash=file_sha256(path);
            for(const auto& side:sides.as_array()) {
                const auto s=side.get<std::string>();if(s!="A"&&s!="B")throw std::runtime_error("invalid qualification side");
                for(const auto& opponent:opponents.as_array()) {
                    const auto relative=fs::relative(path,repo/"maps").generic_string();
                    const auto key=hash+"|"+s+"|"+opponent.get<std::string>();
                    if(!keys.insert(key).second)throw std::runtime_error("duplicate physical qualification key across stages");
                    plan.push_back(Json{{"stage",name},{"role",stage.value("role","qualification")},
                        {"map",relative},{"map_name",path.stem().string()},{"map_sha256",hash},{"side",s},{"opponent",opponent},
                        {"family",config.value("families",Json::object()).value(path.stem().string(),path.stem().string())},
                        {"historical_use",stage.value("historical_use","Previously used maps; withheld here does not mean unseen.")}});
                }
            }
        }
    }
    return plan;
}

Json summarize(const Json& rows,std::uint64_t seed,std::size_t samples) {
    if(!rows.is_array())throw std::runtime_error("qualification rows must be an array");
    Json result=statistics(rows),groups=Json::object(),healthy=Json::array();
    std::map<std::string,std::vector<double>> clusters;
    for(const auto& row:rows.as_array()) {
        for(const auto& [kind,key]:std::vector<std::pair<std::string,std::string>>{{"stage",row.at("stage").get<std::string>()},
            {"map",row.at("map").get<std::string>()},{"opponent",row.at("opponent").get<std::string>()},
            {"side",row.at("side").get<std::string>()},{"family",row.at("family").get<std::string>()}}) {
            if(!groups.contains(kind))groups[kind]=Json::object();
            if(!groups[kind].contains(key))groups[kind][key]=Json::array();groups[kind][key].push_back(row);
        }
        if(clean(row))healthy.push_back(row);
        // Copies or aliases of one map are one resampling unit, even when
        // different opponents made those distinct legitimate game keys.
        const auto cluster=row.contains("identity")&&row.at("identity").contains("map_sha256")
            ?row.at("identity").at("map_sha256").get<std::string>():row.at("map").get<std::string>();
        clusters[cluster].push_back(row.at("payoff").get<double>()-row.at("baseline_payoff").get<double>());
    }
    Json grouped=Json::object();
    for(const auto& [kind,items]:groups.as_object()) {
        grouped[kind]=Json::object();for(const auto& [key,values]:items.as_object())grouped[kind][key]=statistics(values);
    }
    result["breakdown"]=grouped;result["healthy_pair_sensitivity"]=statistics(healthy);
    result["healthy_pair_sensitivity_scope"]="Secondary only: any contaminated key is removed from both arms, never used to override raw health failure.";
    Json interval{{"unit","entire physical map content, preserving both sides and opponents"},{"map_clusters",clusters.size()},
        {"samples",samples},{"independent_new_games",false},{"sufficient_clusters",clusters.size()>=2},
        {"sufficient_resamples",samples>=1000}};
    if(!clusters.empty()&&samples) {
        std::vector<std::vector<double>> data;for(const auto& [map,values]:clusters){(void)map;data.push_back(values);}
        std::mt19937_64 rng(seed^0xa475b891c0ffeeULL);std::vector<double> means;means.reserve(samples);
        for(std::size_t b=0;b<samples;++b) {
            double sum=0;std::size_t count=0;
            for(std::size_t i=0;i<data.size();++i){const auto& draw=data[static_cast<std::size_t>(rng()%data.size())];sum+=std::accumulate(draw.begin(),draw.end(),0.);count+=draw.size();}
            means.push_back(sum/static_cast<double>(count));
        }
        std::sort(means.begin(),means.end());interval["lower95"]=means[static_cast<std::size_t>(.025*static_cast<double>(samples-1))];
        interval["upper95"]=means[static_cast<std::size_t>(.975*static_cast<double>(samples-1))];
    }
    result["paired_payoff_interval"]=interval;
    result["uncertainty_scope"]="Map-cluster bootstrap conditional on this fixed opponent population. Raw-key McNemar assumes independent discordant keys; correlated keys are not new independent samples.";
    return result;
}

} // namespace opt::qualification

namespace opt {
int run_qualification(const Options& options) {
    Run run(options,"qualify","1");
    const auto plan=qualification::case_plan(run.config,options.repo);
    const auto plan_hash=sha256(plan.dump());
    const auto request=qualification::raw_request(run);
    const auto request_hash=sha256(request.dump());
    if(run.resumed) {
        if(run.state.at("plan_sha256")!=Json(plan_hash)||run.state.at("request_sha256")!=Json(request_hash))throw std::runtime_error("qualification plan/candidate changed on resume");
        qualification::verify_artifacts(options.out,run.state.at("artifacts"));
    } else {
        run.state={{"plan_sha256",plan_hash},{"request_sha256",request_hash},{"next",0},
            {"rows",Json::array()},{"artifacts",Json::array()},{"batches",Json::array()}};
        write_json(options.out/"case-plan.json",plan);write_json(options.out/"candidate-request.json",request);run.checkpoint();
    }
    Json context_cases=Json::array();for(const auto& row:plan.as_array())context_cases.push_back(Json{{"map",row.at("map")},{"side",row.at("side")},{"opponent",row.at("opponent")}});
    Json identity_request=request;identity_request["target_kind"]=request.at("kind");identity_request["kind"]="identity";
    identity_request["cases"]=context_cases;identity_request["context_cases"]=context_cases;
    if(!run.stop()) {
        const auto identity=evaluate(options,identity_request,options.out/"identity");
        if(!identity.value("healthy",false)||!identity.contains("context_identity"))throw std::runtime_error("qualification native identity unavailable");
        if(run.state.contains("context_identity")&&run.state.at("context_identity")!=identity.at("context_identity"))throw std::runtime_error("qualification executable/opponent/tool context changed");
        run.state["context_identity"]=identity.at("context_identity");run.state["frozen_environment"]=identity.at("identity");run.checkpoint();
    }
    const auto batch_size=run.config.value<std::size_t>("batch_cases",8);
    if(!batch_size||batch_size>256)throw std::runtime_error("batch_cases must be1..256");
    while(!run.stop()&&run.state.at("next").get<std::size_t>()<plan.size()) {
        const auto next=run.state.at("next").get<std::size_t>();
        Json batch=Json::array();std::map<std::string,Json> expected;
        const auto remaining=options.iterations-run.iteration;
        const auto capacity=std::min<std::uint64_t>(batch_size,remaining);
        for(std::size_t i=next;i<std::min<std::uint64_t>(plan.size(),next+capacity);++i) {
            const auto& row=plan.at(i);batch.push_back(Json{{"map",row.at("map")},{"side",row.at("side")},{"opponent",row.at("opponent")}});
            const auto key=row.at("map_name").get<std::string>()+"|"+row.at("side").get<std::string>()+"|"+row.at("opponent").get<std::string>();
            if(!expected.emplace(key,row).second)throw std::runtime_error("ambiguous qualification map aliases");
        }
        Json call=request;call["cases"]=batch;call["context_cases"]=context_cases;call["expected_context_identity"]=run.state.at("context_identity");
        const auto directory=options.out/"evaluations"/std::to_string(next);
        const auto response=evaluate(options,call,directory);
        if(response.at("context_identity")!=run.state.at("context_identity"))throw std::runtime_error("qualification response context differs");
        if(!response.contains("candidate_identity"))throw std::runtime_error("qualification candidate identity missing");
        if(run.state.contains("candidate_identity")&&run.state.at("candidate_identity")!=response.at("candidate_identity"))throw std::runtime_error("qualification candidate changed between batches");
        Json rows=Json::array();
        for(const auto& native:response.at("cases").as_array()) {
            const auto key=native.at("map").get<std::string>()+"|"+native.at("side").get<std::string>()+"|"+native.value("requested_opponent",native.at("opponent").get<std::string>());
            const auto found=expected.find(key);if(found==expected.end())throw std::runtime_error("qualification missing, duplicate or unexpected game key");
            Json row=native;
            if(row.at("identity").at("map_sha256")!=found->second.at("map_sha256"))throw std::runtime_error("qualification map identity mismatch");
            for(const auto* field:{"stage","role","family","historical_use"})row[field]=found->second.at(field);
            rows.push_back(std::move(row));expected.erase(found);
        }
        if(!expected.empty())throw std::runtime_error("qualification evaluator omitted cases");
        qualification::preserve(run,response);
        write_json(directory/"response.json",response);write_json(directory/"request.json",call);
        for(auto& row:rows.as_array())run.state["rows"].push_back(std::move(row));
        run.state["candidate_identity"]=response.at("candidate_identity");
        run.state["batches"].push_back(Json{{"start",next},{"cases",batch.size()},{"response",fs::relative(directory/"response.json",options.out).generic_string()},
            {"sha256",file_sha256(directory/"response.json")},{"games_played",response.value<std::uint64_t>("games_played",0)},
            {"cache_hits",response.value<std::uint64_t>("cache_hits",0)}});
        run.state["next"]=next+batch.size();run.advance(batch.size());run.checkpoint();
        run.progress(Json{{"completed_keys",run.iteration},{"planned_keys",plan.size()},{"batch_health",response.value("healthy",false)}});
    }
    const auto stats=qualification::summarize(run.state.at("rows"),options.seed,run.config.value<std::size_t>("bootstrap_samples",2000));
    const bool complete=run.state.at("next").get<std::size_t>()==plan.size();
    const bool healthy=stats.at("unhealthy_pairs").get<std::uint64_t>()==0;
    const auto threshold=run.config.value<double>("minimum_paired_gain",0.);
    bool positive=stats.at("paired_mean_payoff").get<double>()>threshold;
    const auto interval=stats.at("paired_payoff_interval");
    const bool supported=interval.value("sufficient_clusters",false)&&interval.value("sufficient_resamples",false)
        &&interval.contains("lower95")&&interval.at("lower95").get<double>()>threshold;
    const std::string decision=!complete?"incomplete":!healthy?"rejected_health":!positive?"no_demonstrated_gain":!supported?"positive_but_uncertain":"supported_gain_in_requested_distribution";
    std::ostringstream table;table<<"stage,map,side,opponent,payoff,baseline_payoff,paired_margin,healthy\n";
    for(const auto& row:run.state.at("rows").as_array())table<<qualification::csv(row.at("stage").get<std::string>())<<','<<qualification::csv(row.at("map").get<std::string>())<<','
        <<row.at("side").get<std::string>()<<','<<qualification::csv(row.at("opponent").get<std::string>())<<','<<row.at("payoff").get<double>()<<','<<row.at("baseline_payoff").get<double>()<<','
        <<row.at("margin").get<double>()-row.at("baseline_margin").get<double>()<<','<<(qualification::clean(row)?"true":"false")<<'\n';
    atomic_write(options.out/"paired-games.csv",table.str());
    Json result{{"status",complete?"completed":"checkpointed"},{"decision",decision},{"completed_keys",run.state.at("rows").size()},{"planned_keys",plan.size()},
        {"statistics",stats},{"candidate_identity",run.state.value("candidate_identity",Json::object())},
        {"context_identity",run.state.value("context_identity",Json())},{"coverage_complete_for_requested_distribution",complete},
        {"artifacts",Json{{"cases","case-plan.json"},{"candidate","candidate-request.json"},{"paired_results","paired-games.csv"},{"native_artifacts","artifacts.json"}}},
        {"scope","Native paired complete-game qualification of one frozen exported candidate; no automatic promotion or online action."},
        {"limitations",Json::array({"Known pool and legacy maps are historically used; this utility does not manufacture unseen holdouts.",
            "A supported empirical gain is conditional on declared stages/opponents; it is not external superiority or automatic release approval.",
            "Raw unhealthy pairs block qualification. Healthy-only paired sensitivity is secondary.",
            "Map-cluster bootstrap does not establish generalization to new opponent families."})}};
    write_json(options.out/"decision.json",Json{{"decision",decision},{"identity",run.identity},{"candidate",run.state.value("candidate_identity",Json::object())}});
    run.finish(std::move(result));return 0;
}
} // namespace opt
