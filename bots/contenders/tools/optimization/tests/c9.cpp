#include "opt/endgame.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace opt;
using namespace opt::endgame;
namespace {
int checks=0;
void check(bool ok,const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
Json sample() {
    return Json{{"id","bank-test"},{"width",7},{"height",5},{"round",1999},{"altars",Json::array({Json::array({2,2})})},
        {"actors",Json::array({Json{{"pos",Json::array({1,2})},{"spawn",Json::array({0,0})},{"gems",10},{"locks",0}},
                             Json{{"pos",Json::array({3,2})},{"spawn",Json::array({6,4})},{"gems",0},{"locks",1},{"attack_range",3},{"ranged",true}}})}};
}
void to_owner(const Domain& d,State& s,int side) {
    do { s=transition(d,s,{"end"}); } while (!terminal(d,s)&&s.current_team()!=side);
}
}
int main() {
    try {
        auto c=parse_case(sample()); auto s=transition(c.domain,c.state,{"tribute",0,0,0});
        check(s.actors[0].channel_timer==1&&s.score[0]==0,"one-tick tribute must not bank immediately");
        s=transition(c.domain,s,{"end"});
        check(s.actors[0].channel_timer==0&&s.score[0]==0,"owner end only decrements channel");
        to_owner(c.domain,s,0);
        check(s.score[0]==10&&s.actors[0].gems==0,"next owner start completes whole bag");

        s=transition(c.domain,c.state,{"tribute",0,0,0}); s=transition(c.domain,s,{"end"});
        s=transition(c.domain,s,{"basic"});
        check(s.actors[0].hp==1155&&s.actors[0].channel_altar==0,"damage must not cancel tribute");

        s=transition(c.domain,c.state,{"tribute",0,0,0}); s=transition(c.domain,s,{"end"});
        s=transition(c.domain,s,{"lock",0,0,0});
        check(s.actors[0].channel_altar==-1&&s.actors[0].gems==10,"enemy lock cancels pending tribute without losing bag");
        check(s.altar_locks[0][1]==5&&s.actors[1].can_cast&&s.actors[1].can_attack,"lock consumes inventory, not cast/basic");

        auto lethal=sample(); lethal["actors"][1]["strength"]=1200;
        c=parse_case(lethal); s=transition(c.domain,c.state,{"tribute",0,0,0}); s=transition(c.domain,s,{"end"});
        s=transition(c.domain,s,{"basic"});
        check(!s.actors[0].alive()&&s.actors[1].gems==10&&s.score[0]==0,"lethal before completion denies bank and transfers bag");
        check(s.kills[1]==1&&s.earned_gold[1]==100,"kill pays actual lifetime earned gold");

        c=parse_case(sample()); s=transition(c.domain,c.state,{"tribute",0,0,0}); s=transition(c.domain,s,{"end"});
        s=transition(c.domain,s,{"cast"});
        check(s.actors[0].stun==1&&s.actors[0].channel_altar==-1,"applied stun immediately cancels tribute");
        check(s.actors[1].cooldown[0]==8&&!s.actors[1].can_cast,"cast spends one cast and starts cooldown");
        s=transition(c.domain,s,{"end"});
        check(s.actors[1].cooldown[0]==7,"new cooldown decrements at same owner end");
        to_owner(c.domain,s,0);
        check(legal_actions(c.domain,s).size()==1,"stun denies target next owner opportunity");

        auto ranged=sample(); ranged["actors"][0]["ranged"]=true; ranged["actors"][0]["attack_range"]=3;
        c=parse_case(ranged); s=transition(c.domain,c.state,{"move",0,2});
        check(!s.actors[0].can_move&&!s.actors[0].can_attack,"ranged movement spends attack");
        s=transition(c.domain,s,{"reset"});
        check(!s.actors[0].can_move&&s.actors[0].can_attack&&!s.actors[0].can_cast,"reset restores attack only and spends cast");
        check(state_from_json(state_json(s)).actors[0].can_attack&&state_key(s)!=state_key(c.state),"state serialization preserves allowances");
        c=parse_case(ranged);
        auto actions=legal_actions(c.domain,c.state);
        check(std::none_of(actions.begin(),actions.end(),[](const Action& a){return a.kind=="reset";}),"reset of an already-ready attack is illegal");
        s=transition(c.domain,c.state,{"move",0,2}); actions=planning_actions(c.domain,s);
        check(std::any_of(actions.begin(),actions.end(),[](const Action& a){return a.kind=="reset";}),"search retains productive movement/reset continuation");

        auto ties=sample(); ties["round"]=2001; ties["earned_gold"]=Json::array({100,100});
        c=parse_case(ties); check(terminal_payoff(c.domain,c.state)==-1,"exact terminal ties favor B");
        ties["earned_gold"]=Json::array({101,100}); c=parse_case(ties);
        check(terminal_payoff(c.domain,c.state)==1,"lifetime gold breaks equal-gem terminal tie");
        ties["earned_gold"]=Json::array({100,100}); ties["kills"]=Json::array({1,0}); c=parse_case(ties);
        check(terminal_payoff(c.domain,c.state)==1,"kills break equal gems and lifetime gold");
        auto respawn=sample(); respawn["actors"][0]["hp"]=0; respawn["actors"][0]["respawn"]=0;
        respawn["actors"][0]["cooldown"]=Json::array({1000,900}); c=parse_case(respawn);
        check(c.state.actors[0].hp==1200&&c.state.actors[0].pos==c.state.actors[0].spawn
            &&c.state.actors[0].cooldown[0]==0&&c.state.actors[0].cooldown[1]==0,"respawn restores health, spawn and both abilities");

        auto locks=sample(); locks["altar_locks"]=Json::array({Json::array({0,5})}); c=parse_case(locks); s=c.state;
        for (int hit=0;hit<5;++hit) {
            s=transition(c.domain,s,{"lock_hit",0,0,0});
            check(s.altar_locks[0][1]==4-hit,"each lock attack removes exactly one of five hits");
            if (hit<4) {
                // Extend this isolated mechanism case rather than confuse a short
                // terminal horizon with persistent lock state.
                c.domain.settings.max_rounds=2010;
                to_owner(c.domain,s,0);
            }
        }
        s=transition(c.domain,s,{"tribute",0,0,0});
        check(s.actors[0].channel_timer==1,"tribute starts after fifth lock hit, despite attack already spent");
        bool rejected=false; auto invalid=sample(); invalid["actors"][0]["hp"]=12.5;
        try { parse_case(invalid); } catch (const std::exception&) { rejected=true; }
        check(rejected,"fractional HP outside integer domain is rejected");
        rejected=false; invalid=sample(); invalid["actors"][0]["passive"]="retaliation";
        try { parse_case(invalid); } catch (const std::exception&) { rejected=true; }
        check(rejected,"unsupported passives are not silently modeled");
        rejected=false;invalid=sample();invalid["altars"]=Json::array();
        for(int altar=0;altar<17;++altar)invalid["altars"].push_back(Json::array({altar%7,altar/7}));
        try { parse_case(invalid); } catch (const std::exception& e) { rejected=std::string(e.what()).find("16 altars")!=std::string::npos; }
        check(rejected,"upfront altar cap bounds copied DFS frontier before allocation");

        // These exercise the real iterative search and its serialized DFS stack,
        // not a separate recursive reference solver.
        auto smoke=sample(); smoke["settings"]=Json{{"winning_gems",10}};
        smoke["max_depth"]=12; smoke["checkpoint_nodes"]=50; smoke["max_transpositions"]=10000;
        for (int t=0;t<2;++t) {
            auto& a=smoke["actors"][t]; a["strength"]=0; a["spell_damage"]=0; a["spell_range"]=0;
            a["spell_stun"]=0; a["locks"]=0; a["cooldown"]=Json::array({100,100});
        }
        smoke["actors"][1]["pos"]=Json::array({6,2});
        const auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
        const auto base=fs::current_path()/".cache"/"contenders"/"optimization"/("c9-checks-"+std::to_string(stamp));
        fs::create_directories(base); const auto input=base/"input.json"; write_json(input,smoke);
        Options options; options.mode="c9"; options.repo=fs::current_path(); options.input=input;
        options.out=base/"complete"; options.iterations=20000;
        check(run_c9(options)==0,"tiny exact search executes");
        auto complete=read_json(options.out/"bank-test"/"solution.json");
        check(complete.at("proven_in_domain").get<bool>()&&complete.at("lower_bound").get<int>()==1,
            "tiny exact endgame proves Team A's final bank");
        check(complete.at("principal_actions").at(0).at("kind").get<std::string>()=="tribute",
            "solved policy chooses actual bank start");
        options.out=base/"resumed"; options.iterations=13; options.resume=false;
        check(run_c9(options)==0,"interrupted search writes checkpoint");
        const auto checkpoint=read_json(options.out/"checkpoint.json");
        check(!checkpoint.at("state").at("searches").at(0).at("stack").empty(),"checkpoint preserves nonempty DFS frontier");
        options.iterations=20000; options.resume=true;
        check(run_c9(options)==0,"search resumes serialized frontier");
        const auto resumed=read_json(options.out/"bank-test"/"solution.json");
        check(resumed==complete,"split and uninterrupted searches produce identical proof, nodes, table and trace");
        auto request=read_json(options.out/"bank-test"/"native-validation-request.json");
        check(request.at("predicted_states").size()==request.at("actions").size()+1,"native qualification request binds every ordered transition");
        const auto digest=request.at("request_sha256").get<std::string>();request.as_object().erase("request_sha256");
        check(digest==sha256(request.dump()),"native trace request hash binds setup actions and full predictions");
        auto policy=read_json(options.out/"bank-test"/"policy.json");bool root_found=false;
        for(const auto& row:policy.at("rows").as_array()) if(row.at("state_key")==policy.at("root_key")) {
            root_found=true;check(row.at("action")==resumed.at("principal_actions").at(0),"policy root uses latest principal action rather than a stale cache entry");
        }
        check(root_found,"bounded deployment table retains root state");

        auto other=smoke;other["id"]="other-bank";
        Json family{{"cases",Json::array({smoke,other})},{"case_node_quantum",7},{"checkpoint_nodes",25},{"max_transposition_bytes",65536}};
        const auto family_input=base/"family.json";write_json(family_input,family);options.input=family_input;options.resume=false;
        options.out=base/"family-complete";options.iterations=5000;
        check(run_c9(options)==0,"bounded-cache multi-case search executes");
        const auto full_state=read_json(options.out/"checkpoint.json").at("state");
        const auto family_report=read_json(options.out/"report.json");
        std::uint64_t payload=0,limited=0;
        for(const auto& result:family_report.at("cases").as_array()) {payload+=result.at("transposition_payload_bytes").get<std::uint64_t>();limited+=result.at("cache_limit_events").get<std::uint64_t>();}
        check(payload<=65536&&limited>0,"total family transposition payload cap is respected and reported");
        options.out=base/"family-resumed";options.iterations=20;options.resume=false;
        check(run_c9(options)==0,"family can stop within its second case quantum");
        const auto partial=read_json(options.out/"checkpoint.json").at("state");
        check(partial.at("searches").at(0).at("expanded").get<int>()>0&&partial.at("searches").at(1).at("expanded").get<int>()>0,"round-robin prevents hard first case starving later families");
        options.iterations=5000;options.resume=true;
        check(run_c9(options)==0,"partial family quantum resumes");
        check(read_json(options.out/"checkpoint.json").at("state")==full_state,"full and split family cursor frontier tables bounds and counters match exactly");

        std::cout<<"C9 domain checks passed: "<<checks<<"\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<"C9 test error: "<<e.what()<<"\n"; return 1; }
}
