#include "opt/config_space.hpp"
#include <iostream>
#include <stdexcept>

using opt::Json;
namespace {
int checks = 0;
void check(bool condition, const char* reason) { ++checks; if (!condition) throw std::runtime_error(reason); }
template<class F> void rejects(F f, const char* reason) { bool rejected=false;try { f(); } catch(const std::exception&) {rejected=true;}check(rejected,reason); }
Json specification() {
    return Json{{"fixed_knobs", Json{{"fixed", true}}}, {"space", Json::array({
        Json{{"name","mode"},{"values",Json::array({0,1})},{"default",0}},
        Json{{"name","draft"},{"type","permutation"},{"values",Json::array({0,1,2,3})},{"default",Json::array({2,1,3,0})},{"active_if",Json{{"mode",Json::array({1})}}}},
        Json{{"name","low"},{"type","integer"},{"min",1},{"max",4},{"default",1}},
        Json{{"name","high"},{"type","integer"},{"min",2},{"max",7},{"default",3}},
        Json{{"name","scale"},{"type","real"},{"min",0.1},{"max",2.0},{"default",1.0}},
        Json{{"name","shop"},{"type","shop_sequence"},{"families",Json::array({0,2,3,4,6})},{"stats",Json::array({2,3})},
            {"milestones",Json::array({5,7})},{"tail_families",Json::array({0,6})},{"min_steps",2},{"max_steps",4},{"length",8},
            {"default",Json::array({335,435,630,-1,-1,-1,-1,-1})},{"active_if",Json{{"mode",Json::array({1})}}}}
    })}, {"constraints",Json::array({Json{{"left","low"},{"op","le"},{"right","high"}}})}};
}
}
int main() {
    try {
        auto spec=specification();opt::c2::ConfigSpace space(spec);std::mt19937_64 rng(17);
        auto base=space.baseline();check(space.valid(base),"baseline valid");check(!base.contains("draft")&&!base.contains("shop"),"inactive removed");
        auto inert=base;inert["draft"]=Json::array({0,0});check(space.canonical(inert)==base,"inactive tails canonicalize away");check(!space.valid(inert),"inactive keys rejected before evaluation");
        auto extra=base;extra["unknown"]=1;check(!space.valid(extra),"unknown knobs rejected");extra=base;extra["fixed"]=false;check(!space.valid(extra),"fixed changes rejected");
        auto active=base;active["mode"]=1;active=space.canonical(active);check(space.valid(active),"activation creates default values");
        auto bad=active;bad["draft"]=Json::array({0,0,1,2});check(!space.valid(bad),"duplicate draft rejected");
        bad=active;bad["shop"]=Json::array({335,325,630,-1,-1,-1,-1,-1});check(!space.valid(bad),"duplicate finite family rejected");
        bad=active;bad["shop"]=Json::array({335,435,635,-1,-1,-1,-1,-1});check(!space.valid(bad),"missing unlimited tail rejected");
        bad=active;bad["shop"]=Json::array({335,430,635,-1,-1,-1,-1,-1});check(!space.valid(bad),"work after unlimited rejected");
        bad=base;bad["low"]=4;bad["high"]=2;check(!space.valid(bad),"cross-variable constraint enforced");
        for(int i=0;i<1000;++i) {
            auto sampled=space.sample(rng);check(space.valid(sampled),"sample legal");
            check(space.valid(space.mutate(sampled,rng,3)),"mutation legal");
            check(space.valid(space.crossover(sampled,active,rng)),"crossover legal");
        }
        auto future=spec;future["space"][0]["active_if"]=Json{{"high",Json::array({2})}};
        rejects([&]{opt::c2::ConfigSpace invalid(future);},"forward conditions rejected");
        auto duplicate=spec;duplicate["space"][1]["name"]="mode";rejects([&]{opt::c2::ConfigSpace invalid(duplicate);},"duplicate names rejected");
        auto invalid_default=spec;invalid_default["space"][0]["default"]=4;rejects([&]{opt::c2::ConfigSpace invalid(invalid_default);},"illegal defaults rejected");
        auto injection=spec;injection["fixed_knobs"]["x;system"]=1;rejects([&]{opt::c2::ConfigSpace invalid(injection);},"bad fixed identifier rejected");
        auto string=spec;string["space"][0]["values"]=Json::array({"code"});rejects([&]{opt::c2::ConfigSpace invalid(string);},"code-like override rejected");
        auto wrong_constraint=spec;wrong_constraint["constraints"][0]["right"]="typo";rejects([&]{opt::c2::ConfigSpace invalid(wrong_constraint);},"unknown constraint rejected");
        check(opt::c2::cpp_literal(Json::array({1,2,3}))=="{ 1, 2, 3 }","array native literal");
        check(opt::c2::configuration_header(active).find("std::array<int, 4> draft")!=std::string::npos,"export compiles array declaration");
        std::cout<<checks<<" C2 configuration-space checks passed\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"C2 test error after "<<checks<<" checks: "<<e.what()<<"\n";return 1;}
}
