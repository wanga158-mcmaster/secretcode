// Exercise the same native response guards and matrix admission used by the engine.
#include "../src/c7.cpp"
#include <iostream>

int main(){
    using opt::Json;
    unsigned checks=0;
    auto check=[&](bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);};
    auto rejected=[&](auto action){try{action();return false;}catch(const std::exception&){return true;}};
    Json request{{"cases",Json::array({Json{{"map","case"},{"side","A"}},Json{{"map","case"},{"side","B"}}})}};
    Json response{{"subject_healthy",true},{"opponent_healthy",true},{"baseline_healthy",true},{"healthy",true},{"selection_eligible",true},{"cases",Json::array()}};
    for(const auto* side:{"A","B"})response["cases"].push_back({{"map","case"},{"side",side},{"complete",true},{"opponent","test/opponent@000000000000"},
        {"payoff",side[0]=='A'?1:-1},{"baseline_payoff",0},{"health",Json::array()},{"opponent_health",Json::array()},
        {"baseline_health",Json::array()},{"baseline_opponent_health",Json::array()},
        {"identity",Json{{"subject_key","test/policy@000000000000"},{"opponent_key","test/opponent@000000000000"},
            {"baseline_key","test/base@000000000000"},{"map_sha256","test-map"},{"engine","test"},{"toolchain","test"}}}});
    const auto clean=opt::checked_cell(response,request);
    check(clean.subject_healthy && clean.infrastructure_healthy,"clean native cell remains eligible");
    check(clean.value.at("payoff").get<double>()==0,"payoff is recomputed from exact covered cases");
    auto bad=response;
    bad["subject_healthy"]=false;bad["healthy"]=false;bad["selection_eligible"]=false;
    bad["cases"].at(0)["health"]=Json::array({"fuel_exhausted"});
    check(!opt::checked_cell(bad,request).subject_healthy,"subject health cannot select a policy");
    bad=response;bad["opponent_healthy"]=false;bad["healthy"]=false;bad["selection_eligible"]=false;
    bad["cases"].at(0)["opponent_health"]=Json::array({"stall"});
    check(!opt::checked_cell(bad,request).infrastructure_healthy,"opponent faults cannot become ordinary superiority");
    bad=response;bad["cases"].as_array().pop_back();
    check(rejected([&]{opt::checked_cell(bad,request);}),"missing native case must reject cell");
    bad=response;bad["cases"].at(0)["complete"]=false;
    check(rejected([&]{opt::checked_cell(bad,request);}),"nonterminal native case must reject cell");
    bad=response;bad["cases"].at(1)["side"]="A";
    check(rejected([&]{opt::checked_cell(bad,request);}),"duplicate native case must reject cell");
    bad=response;bad["cases"].at(0)["payoff"]=1.5;
    check(rejected([&]{opt::checked_cell(bad,request);}),"invalid terminal payoff must reject cell");
    bad=response;bad["cases"].at(0).as_object().erase("identity");
    check(rejected([&]{opt::checked_cell(bad,request);}),"missing immutable native identity must reject cell");
    bad=response;bad["cases"].at(1)["identity"]["opponent_key"]="test/changed@000000000000";
    check(rejected([&]{opt::checked_cell(bad,request);}),"changing opponent inside cell must reject");
    bad=response;bad["selection_eligible"]=false;
    check(rejected([&]{opt::checked_cell(bad,request);}),"unexplained selection rejection is not ignored");
    bad=response;bad["candidate_identity"]={{"key","test/changed@000000000000"}};
    check(rejected([&]{opt::checked_cell(bad,request);}),"recipe identity must equal the executable actually evaluated");
    auto with_recipe=response;
    with_recipe["candidate_identity"]={{"key","test/policy@000000000000"},{"source_sha256","source"},
        {"modules",Json{{"raw","raw"},{"injected","injected"}}}};
    with_recipe["identity"]={{"controller",Json{{"key","test/controller@000000000000"}}}};
    const auto recipe_cell=opt::checked_cell(with_recipe,request);
    check(recipe_cell.value.at("candidate_identity")==with_recipe.at("candidate_identity"),"native reconstruction identity retained verbatim");
    Json row=opt::new_row(Json::object());row["cells"]["column"]=clean.value;
    Json state{{"rows",Json::array({row})},{"columns",Json::array({Json{{"id","column"}}})}};
    check(opt::complete_rows(state).size()==1,"complete eligible row enters finite matrix");
    state["rows"].at(0)["cells"]["column"]["infrastructure_healthy"]=false;
    check(opt::complete_rows(state).empty(),"contaminated matrix cell never becomes a payoff");
    state["rows"].at(0)["cells"]=Json::object();
    check(opt::complete_rows(state).empty(),"missing cell remains missing, not zero");
    auto& archived=state["rows"].at(0);
    archived["candidate_key"]="test/policy@000000000000";
    archived["candidate_identity"]=with_recipe.at("candidate_identity");
    archived["candidate_controller"]="test/controller@000000000000";
    Json exported=Json::object();opt::population_recipes(exported,state);
    check(exported.at("population_configs").size()==1 && exported.at("population_configs").at(0).at("knobs")==row.at("knobs"),
        "population request carries exact original configuration");
    check(exported.at("population_configs").at(0).at("identity")==with_recipe.at("candidate_identity")
        && exported.at("population_configs").at(0).at("controller").get<std::string>()=="test/controller@000000000000",
        "population request freezes original controller and executable identities");
    opt::c2::ConfigSpace seed_space(Json{{"space",Json::array({Json{{"name","x"},{"values",Json::array({0,1})},{"default",0}}})}});
    check(opt::legal_seed(seed_space,Json{{"x",1}}).at("x")==Json(1),"legal warm start retains exact configuration");
    check(rejected([&]{opt::legal_seed(seed_space,Json{{"unknown",1}});}),"unknown warm-start knob cannot silently disappear");
    check(rejected([&]{opt::legal_seed(seed_space,Json{{"x",7}});}),"illegal warm-start value cannot silently change");
    Json validation_state{{"rows",Json::array({Json{{"id","p"},{"validation",Json{{"v",clean.value}}}}})},
        {"validation_columns",Json::array({Json{{"id","v"},{"opponent","op"},{"family","held-out"},{"maps",Json::array({"case"})}}})}};
    Json solution{{"policies",Json::array({Json{{"id","p"}},Json{{"id","unobserved"}}})},{"policy",Json::array({0.75,0.25})}};
    const auto held=opt::validation_summary(validation_state,solution).at(0);
    check(!held.at("complete").get<bool>() && held.at("payoff").is_null(),"missing validation mass is not a zero payoff");
    check(held.at("lower").get<double>()==-0.25 && held.at("upper").get<double>()==0.25,"unobserved mixture mass gets sound payoff bounds");
    opt::Options transferred;transferred.repo=opt::fs::current_path();
    transferred.out=transferred.repo/".cache/contenders/optimization/c7/portable-artifact-test";
    const auto artifact_hash=opt::sha256("native-artifact");
    opt::fs::create_directories(transferred.out/"artifacts");
    opt::atomic_write(transferred.out/"artifacts"/(artifact_hash+".wasm"),"native-artifact");
    Json transferred_row{{"cells",Json{{"c",Json{{"artifact_sources",Json::array({Json{{"sha256",artifact_hash},
        {"path","Z:/old-worker/.cache/contenders/optimization/native/payload.wasm"},{"role","injected_wasm"}}})}}}}}};
    check(opt::preserve_artifacts(transferred_row,transferred).size()==1,"transferred verified exports do not require the old worker cache");
    std::cout<<"C7 native-cell guards: "<<checks<<" checks passed\n";
}
