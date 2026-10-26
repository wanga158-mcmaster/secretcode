#include "opt/scenarios.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using opt::Json;
using namespace opt::scenarios;
namespace {
void require(bool yes,const char* why) { if (!yes) throw std::runtime_error(why); }
template<class F> void rejects(F f,const char* why) {
    bool rejected=false; try { f(); } catch (const std::exception&) { rejected=true; }
    require(rejected,why);
}
}
int main() {
    try {
        auto seed=default_case();
        require(validate(seed)==seed,"default seed must be valid");
        auto queried_catalog=opt::read_json("docs/reference/runtime-catalog.json");
        queried_catalog["result"]["settings"]["capture_time"]=17;
        auto queried=default_case(queried_catalog);
        require(queried.at("settings").value("capture_time",0)==17,"copied settings overrode queried catalog");
        for (const auto& champion:queried_catalog.at("result").at("champions").as_array())
            if (champion.value("name","")=="ryth")
                require(queried.at("actors").at(0).at("spells").at(0).at("max_cooldown")
                    ==champion.at("abilities").at(0).at("cooldown"),"queried kit cooldown was not imported");
        auto disconnected=seed;
        disconnected["camps"].at(1)["x"]=5;
        rejects([&]{validate(disconnected);},"disconnected same-group camp members accepted");
        auto touching=seed;
        touching["camps"].at(1)["group"]=1;
        rejects([&]{validate(touching);},"cardinally touching different groups accepted");
        auto blocked=seed;
        blocked["blocked"]=Json::array({Json::array({2,2})});
        rejects([&]{validate(blocked);},"obstacle on actor accepted");
        auto divided=seed;
        divided["blocked"]=Json::array();
        for (int row=0;row<7;++row) divided["blocked"].push_back(Json::array({4,row}));
        rejects([&]{validate(divided);},"disconnected terrain accepted");
        auto bad_response=seed; bad_response["opponent_policy"]="omniscient-native";
        rejects([&]{validate(bad_response);},"unknown response program accepted");
        auto other_family=seed; other_family["family"]="independent-geometry";
        require(descriptor(other_family)!=descriptor(seed),"independent families share one archive cell");
        auto contested=seed;
        contested["opponent_policy"]="pressure";
        auto enemy=seed.at("actors").at(0); enemy["id"]="enemy"; enemy["team"]=1;
        enemy["x"]=2; enemy["y"]=3; enemy["spawn"]=Json::array({6,6});
        contested["actors"].push_back(enemy); contested["active_actor"]=1;
        auto enemy_state=opt::sim::initial(contested);
        const auto reply=opponent_action(enemy_state,"pressure");
        require(reply.value("type","")=="cast" || reply.value("target_kind","")=="actor",
            "pressure response ignored an in-range opposing actor");
        const auto legal_reply=opt::sim::legal_actions(enemy_state);
        require(std::find(legal_reply.begin(),legal_reply.end(),reply)!=legal_reply.end(),"response program generated illegal action");
        Limits bounds;
        bounds.oracle_nodes=160;
        bounds.beam_width=8;
        bounds.max_actions=512;
        bounds.baseline="basic_first";
        bounds.minimization_trials=4;
        auto response_case=contested;
        response_case["goal"]=Json{{"kind","survive"},{"team",0},{"rounds",3}};
        response_case["settings"]["max_rounds"]=15;
        auto response_bounds=bounds; response_bounds.oracle_nodes=8; response_bounds.max_actions=128;
        const auto response_comparison=evaluate_case(response_case,response_bounds);
        std::size_t response_inputs=0;
        for (const auto* which:{"baseline","oracle"}) {
            auto replay=opt::sim::initial(response_case);
            for (const auto& action:response_comparison.at(which).at("actions").as_array()) {
                if (replay.at("actors").at(static_cast<std::size_t>(opt::sim::active_actor(replay))).value("team",0)==1) {
                    require(action==opponent_action(replay,"pressure"),"paired traces used inconsistent opponent programs");
                    ++response_inputs;
                }
                replay=opt::sim::step(replay,action);
            }
        }
        require(response_inputs>0,"opponent response comparison had no opposing input");
        std::mt19937_64 a(913),b(913);
        auto parent_a=seed,parent_b=seed;
        for (int iteration=0;iteration<40;++iteration) {
            parent_a=mutate(parent_a,a,bounds); parent_b=mutate(parent_b,b,bounds);
            require(parent_a==parent_b,"mutation is not reproducible");
            validate(parent_a);
            require(parent_a.at("actors").at(0).at("strength")==seed.at("actors").at(0).at("strength"),"mutation changed immutable kit strength");
            require(parent_a.at("camps").at(1).at("max_hp")==seed.at("camps").at(1).at("max_hp"),"mutation changed immutable camp design");
        }
        auto planted=seed;
        // At 145 HP cast-first damage headroom can be canceled by its lost entry
        // step. At 235 HP the extra basic crosses a whole-cycle breakpoint.
        planted["camps"].at(1)["hp"]=235;
        auto evaluation=evaluate_case(planted,bounds);
        if (!evaluation.value("witness",false)) {
            opt::write_json(".cache/contenders/optimization/c8/failed-planted.json",evaluation);
            std::cerr << Json{{"baseline_utility",evaluation.at("baseline").at("utility")},
                {"oracle_utility",evaluation.at("oracle").at("utility")},
                {"baseline_summary",evaluation.at("baseline").at("summary")},
                {"oracle_expanded",evaluation.at("oracle").at("expanded")},
                {"generated",evaluation.at("oracle").at("states_generated")}}.dump(2) << '\n';
        }
        require(evaluation.at("oracle").value("status","")=="success","bounded oracle failed to produce complete alternative");
        require(evaluation.value("witness",false),"cast-order planted regret not discovered");
        require(evaluation.value("heuristic_model_regret",0.0)>0,"nonpositive planted regret");
        const auto request=validation_request(evaluation,"oracle");
        require(request.value("supported",false),"complete trace validation request not exported");
        require(request.at("predicted_states").size()==evaluation.at("oracle").at("actions").size(),"validation omitted ordered predictions");
        require(request.at("predicted_states").as_array().back()==evaluation.at("oracle").at("final_state"),"validation final prediction differs from witness");
        require(request.at("prediction_sha256").get<std::string>()==opt::sha256(request.at("predicted_states").dump()),"prediction digest is not bound");
        require(!validation_request(evaluation,"oracle",1).value("supported",true),"validation limit silently truncated a trace");
        require(!evaluation.at("native_validation").value("native_validated",false),"model comparison mislabeled native");
        auto canceled_headroom=planted;
        canceled_headroom["camps"].at(1)["hp"]=145;
        auto same_cycle=evaluate_case(canceled_headroom,bounds);
        require(!same_cycle.value("witness",false),"extra damage with no completed-cycle benefit became a witness");
        auto unfinished=bounds; unfinished.max_actions=1; unfinished.oracle_nodes=1;
        auto prefix=evaluate_case(seed,unfinished);
        require(!prefix.value("witness",false),"unfinished alternative counted as a witness");
        auto limited_reference=seed;
        limited_reference["goal"]["kind"]="clear";
        limited_reference["camps"].at(1)["hp"]=100;
        auto one_action=bounds; one_action.max_actions=1;
        auto reference_prefix=evaluate_case(limited_reference,one_action);
        require(reference_prefix.value("feasible_alternative",false),"one-cast complete alternative missing");
        require(!reference_prefix.value("reference_completed",true),"reference prefix not recognized");
        require(!reference_prefix.value("witness",false),"truncated reference prefix became task regret");
        auto simplify=planted;
        simplify["blocked"].push_back(Json::array({6,6}));
        auto with_wall=evaluate_case(simplify,bounds);
        require(with_wall.value("witness",false),"walled planted witness missing");
        auto smaller=minimize(with_wall,planted,bounds);
        require(smaller.value("witness",false),"minimization lost witness");
        require(smaller.at("scenario").at("blocked").size()<simplify.at("blocked").size(),"minimization failed to remove irrelevant obstacle");
        auto cancel=evaluate_case(seed,bounds,[]{return true;});
        require(cancel.value("interrupted",false),"cancellation did not stop a case");
        std::cout << "C8 legality, mutation, feasible-regret, minimization and cancellation checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "C8 test failure: " << e.what() << '\n';
        return 1;
    }
}
