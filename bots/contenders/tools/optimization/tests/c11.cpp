#include "opt/oracle.hpp"
#include <iostream>
#include <stdexcept>
using opt::Json;
static void check(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static Json actor(int id,int team,float hp) {
    return Json{{"id",id},{"team",team},{"x",id==7?35:34},{"y",18},{"hp",hp},{"max_hp",1200},
        {"strength",47},{"focus",100},{"range",1},{"alive",true},{"move",true},{"attack",true},
        {"cast",true},{"stunned",false},{"shield",0},{"cooldowns",Json::array({0,0})},
        {"abilities",Json::array({Json{{"maxCooldown",8},{"payload",Json{{"range",4},{"modifier",1}}}},
            Json{{"maxCooldown",8},{"payload",Json::object()}}})}};
}
int main(int argc,char** argv) {
    try {
        Json before{{"owner",7},{"actors",Json::array({actor(7,1,500),actor(2,0,300)})}};
        before["actors"][1]["shield"]=25;
        Json after=before;
        after["actors"][0]["attack"]=false;
        after["actors"][1]["hp"]=278;
        after["actors"][1]["shield"]=0;
        const Json t{{"action",Json{{"type","basic"}}},{"before",before},{"after",after},{"applied",true}};
        auto result=opt::oracle::compare_transition(t,opt::oracle::Model{});
        check(result.at("classification").get<std::string>()=="transition_match","Shield-first basic must match");
        auto wrong=opt::oracle::Model{}; wrong.basic_scale=2;
        check(opt::oracle::compare_transition(t,wrong).at("loss").get<double>()>0,"Bad damage prediction must reject");
        wrong=opt::oracle::Model{}; wrong.shield_first=false;
        check(opt::oracle::compare_transition(t,wrong).at("loss").get<double>()>0,"Bad shield order must reject");
        Json illegal=t; illegal["applied"]=false; illegal["before"]["actors"][0]["attack"]=false;
        illegal["after"]=illegal["before"];
        check(opt::oracle::compare_transition(illegal,opt::oracle::Model{}).at("classification").get<std::string>()==
            "invalid_action_generation","Refused spent attack must not be called a transition mismatch");
        Json reset=t; reset["action"]=Json{{"type","reset"}};
        reset["applied"]=false; reset["after"]=reset["before"];
        check(opt::oracle::compare_transition(reset,opt::oracle::Model{}).at("loss").get<double>()==0,
            "Reset must require an already-spent attack");
        wrong=opt::oracle::Model{}; wrong.reset_requires_spent_attack=false;
        check(opt::oracle::compare_transition(reset,wrong).at("loss").get<double>()>0,
            "Native reset counterexample must reject missing legality guard");
        Json boundary=t; boundary["action"]=Json{{"type","next_owner"},{"owner","target"}};
        check(!opt::oracle::compare_transition(boundary,opt::oracle::Model{}).at("compared").get<bool>(),
            "Hidden world owner transitions must not be claimed as predicted");
        Json movement=t; movement["action"]=Json{{"type","move"},{"x",35},{"y",19}};
        check(opt::oracle::compare_transition(movement,opt::oracle::Model{}).at("classification").get<std::string>()==
            "insufficient_observation","Occupancy-free projection cannot certify movement");
        bool rejected=false;
        try { opt::oracle::Model::from(Json{{"basic_scale",-1}}); } catch (const std::exception&) { rejected=true; }
        check(rejected,"Negative model scales must reject");
        if (argc>1) {
            const auto native=opt::read_json(argv[1]);
            check(native.at("independent_source_prefixes").get<int>()==1,"Fresh branch prefix must match");
            check(native.at("original_seed_unchanged").get<bool>(),"Seed must remain unchanged");
            for (const auto& item:native.at("results").as_array()) {
                if (!item.at("supported").get<bool>()) continue;
                const auto comparison=opt::oracle::compare_case(item,opt::oracle::Model{});
                if (comparison.at("mismatches").get<int>()!=0) {
                    std::cerr<<item.at("case_id").dump()<<": "<<comparison.dump(2)<<'\n';
                    throw std::runtime_error("Native projection mismatch");
                }
            }
        }
        std::cout<<"C11 projection/triage tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"C11 test error: "<<error.what()<<'\n'; return 1;
    }
}
