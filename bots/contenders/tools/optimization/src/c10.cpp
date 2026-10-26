#include "opt/fuel.hpp"
#include <iostream>
#include <numeric>

namespace opt {
namespace { struct FuelRejected : std::runtime_error { using std::runtime_error::runtime_error; }; }
int run_c10(const Options& options) {
    Run run(options,"C10-fuel-superoptimization","1");
    auto& state=run.state;
    if (!run.resumed) {
        auto candidates=fuel::family(run.config.value("search",Json::object()));
        std::shuffle(candidates.begin(),candidates.end(),run.rng);
        // The ordinary AoS reference is evaluated first and retained as an incumbent.
        const auto reference=fuel::encode(fuel::Candidate{});
        auto it=std::find(candidates.begin(),candidates.end(),reference);
        if(it!=candidates.end()) std::rotate(candidates.begin(),it,it+1);
        state=Json{{"order",Json(candidates)},{"cursor",0},{"archive",Json::array()},
                   {"best",Json()},{"best_objective",Json()},{"failed",0}};
        run.checkpoint();
    }
    auto& order=state.at("order").as_array();
    const auto benchmark=run.config.value("benchmark",fuel::default_benchmark());
    const auto environment_response=evaluate(options,Json{{"schema","contenders-eval/v1"},
        {"kind","fuel"},{"operation","environment"},{"benchmark",benchmark}},options.out/"environment");
    if(!environment_response.value("healthy",false) || !environment_response.contains("environment"))
        throw std::runtime_error("C10 could not establish its fuel measurement environment");
    const auto environment=environment_response.at("environment");
    if(state.contains("environment")) {
        if(state.at("environment")!=environment)
            throw std::runtime_error("C10 measurement environment changed; start a separate output directory instead of mixing fitness");
    } else {
        if(run.iteration!=0 || !state.at("archive").empty())
            throw std::runtime_error("C10 old checkpoint lacks a measurement environment binding");
        state["environment"]=environment; run.checkpoint();
    }
    while (!run.stop() && state.at("cursor").get<std::size_t>()<order.size()) {
        const auto cursor=state.at("cursor").get<std::size_t>();
        const Json candidate=order[cursor]; (void)fuel::decode(candidate);
        const auto id=sha256(candidate.dump()).substr(0,16);
        const fs::path work=options.out/"evaluations"/id;
        const Json request{{"schema","contenders-eval/v1"},{"kind","fuel"},
                           {"candidate",candidate},{"benchmark",benchmark}};
        Json response;
        try {
            response=evaluate(options,request,work);
            if(!response.value("healthy",false) && response.value("failure_kind","")=="candidate_invalid")
                throw FuelRejected(response.value("error","candidate failed semantic or fuel validation"));
            if (!response.value("healthy",false) || !response.contains("objective"))
                throw std::runtime_error(response.value("error","native fuel validation or measurement failed"));
            const bool native=response.value("measurement_backend","")=="native_wasm_fuel";
            if (!native && !options.has("allow-test-evaluator"))
                throw std::runtime_error("C10 requires native_wasm_fuel evidence; diagnostic evaluators require --allow-test-evaluator");
            const double objective=response.at("objective").get<double>();
            if (!std::isfinite(objective)) throw std::runtime_error("nonfinite native fuel objective");
            const Json compact{{"candidate",candidate},{"id",id},{"objective",objective},
                               {"fuel_ratio",response.value("fuel_ratio",1.0)},
                               {"worst_case_ratio",response.value("worst_case_ratio",1.0)},
                               {"native_wasm_fuel",native},
                               {"measurement",response.value("evaluation_io",Json::object()).value("response",(work/"response.json").generic_string())},
                               {"identity",response.value("identity",Json::object())}};
            state["archive"].push_back(compact);
            if (state.at("best").is_null() || objective>state.at("best_objective").get<double>()) {
                state["best"]=compact; state["best_objective"]=objective;
                write_json(options.out/"best_candidate.json",candidate);
                write_json(options.out/"best_measurement.json",response);
                if(response.contains("kernel_header"))
                    atomic_write(options.out/"best_kernel.hpp",response.at("kernel_header").get<std::string>());
                run.event(Json{{"type","incumbent"},{"candidate_id",id},{"objective",objective}});
            }
            run.progress(Json{{"candidate_id",id},{"objective",objective},
                              {"best_objective",state.at("best_objective")},
                              {"covered",state.at("archive").size()},{"family_size",order.size()}});
        } catch(const FuelRejected& error) {
            state["failed"]=state.at("failed").get<std::uint64_t>()+1;
            write_json(work/"failure.json",Json{{"candidate",candidate},{"error",error.what()}});
            run.event(Json{{"type","candidate_rejected"},{"candidate_id",id},{"error",error.what()}});
        } catch(const std::exception& error) {
            if(interrupted()) {
                run.event(Json{{"type","candidate_interrupted"},{"candidate_id",id}});
                run.checkpoint();
                break; // retry the same immutable candidate/cache on resume
            }
            run.event(Json{{"type","infrastructure_failure"},{"candidate_id",id},{"error",error.what()}});
            run.checkpoint();
            throw; // never consume unfinished work after timeout/adapter/build failure
        }
        state["cursor"]=cursor+1; run.advance(); run.checkpoint();
    }
    const bool exhausted=state.at("cursor").get<std::size_t>()==order.size();
    Json frontier=Json::array();
    for(const auto& a:state.at("archive").as_array()) {
        bool dominated=false;
        for(const auto& b:state.at("archive").as_array()) {
            const auto am=a.at("fuel_ratio").get<double>(),aw=a.at("worst_case_ratio").get<double>();
            const auto bm=b.at("fuel_ratio").get<double>(),bw=b.at("worst_case_ratio").get<double>();
            if(bm<=am && bw<=aw && (bm<am || bw<aw)) { dominated=true; break; }
        }
        if(!dominated) frontier.push_back(a);
    }
    write_json(options.out/"fuel_frontier.json",frontier);
    const bool native_best=!state.at("best").is_null() && state.at("best").value("native_wasm_fuel",false);
    run.finish(Json{{"engine","C10"},{"status",exhausted?"family_exhausted":"checkpointed"},
                    {"family_size",order.size()},{"attempted",state.at("cursor")},
                    {"successful",state.at("archive").size()},{"failed",state.at("failed")},
                    {"best",state.at("best")},{"benchmark",benchmark},
                    {"frontier",frontier},{"native_wasm_fuel",native_best},
                    {"scope",native_best?"Best measured WASM fuel within the enumerated integer nearest-target family and benchmark distribution; no whole-bot gain or global superoptimization optimum.":"Diagnostic evaluator results only; no native fuel claim."},
                    {"outputs",Json::array({"best_kernel.hpp","best_candidate.json","best_measurement.json","fuel_frontier.json","checkpoint.json","progress.csv"})}});
    if(state.at("best").is_null()) {
        std::cerr<<"C10 produced no valid native fuel measurement; inspect evaluations/*/failure.json\n";
        return 2;
    }
    return 0;
}
} // namespace opt
