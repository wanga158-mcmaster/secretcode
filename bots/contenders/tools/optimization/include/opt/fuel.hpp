#pragma once
#include "common.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace opt::fuel {

// Only these integer kernel families are emitted. The valid deployment domain is
// n in [0,256], coordinates in [0,99], and stable first-index tie breaking.
struct Candidate {
    std::string layout{"aos32"}, distance{"ternary"}, reduction{"branch"};
    int unroll{1};
    bool early_zero{false}, axis_prune{false};
};

inline Json encode(const Candidate& c) {
    return Json{{"layout",c.layout},{"distance",c.distance},{"reduction",c.reduction},
                {"unroll",c.unroll},{"early_zero",c.early_zero},{"axis_prune",c.axis_prune}};
}

inline Candidate decode(const Json& j) {
    Candidate c;
    c.layout=j.value("layout","aos32"); c.distance=j.value("distance","ternary");
    c.reduction=j.value("reduction","branch"); c.unroll=j.value("unroll",1);
    c.early_zero=j.value("early_zero",false); c.axis_prune=j.value("axis_prune",false);
    const std::array<std::string,5> layouts{"aos32","aos16","soa32","soa16","packed16"};
    const std::array<std::string,4> distances{"ternary","stdabs","lut","rotated"};
    if (std::find(layouts.begin(),layouts.end(),c.layout)==layouts.end()
        || std::find(distances.begin(),distances.end(),c.distance)==distances.end()
        || (c.reduction!="branch" && c.reduction!="mask")
        || (c.unroll!=1 && c.unroll!=2 && c.unroll!=4 && c.unroll!=8))
        throw std::runtime_error("C10 candidate is outside the verified implementation grammar");
    return c;
}

inline std::vector<Json> family(const Json& restrictions=Json::object()) {
    if(!restrictions.is_object()) throw std::runtime_error("C10 search must be an object");
    for(const auto& [key,value]:restrictions.as_object()) {
        (void)value;
        if(key!="layouts" && key!="distances" && key!="reductions" && key!="unrolls"
            && key!="early_zero" && key!="axis_prune")
            throw std::runtime_error("Unknown C10 search axis: "+key);
    }
    const auto strings=[&](const std::string& key,std::initializer_list<const char*> defaults) {
        std::vector<std::string> out;
        if (restrictions.contains(key)) for (const auto& x:restrictions.at(key).as_array()) out.push_back(x.get<std::string>());
        else for (auto x:defaults) out.emplace_back(x);
        if (out.empty()) throw std::runtime_error("C10 search axis must not be empty: "+key);
        return out;
    };
    const auto layouts=strings("layouts",{"aos32","aos16","soa32","soa16","packed16"});
    const auto distances=strings("distances",{"ternary","stdabs","lut","rotated"});
    const auto reductions=strings("reductions",{"branch","mask"});
    std::vector<int> unrolls{1,2,4,8};
    if (restrictions.contains("unrolls")) { unrolls.clear(); for (const auto& x:restrictions.at("unrolls").as_array()) unrolls.push_back(x.get<int>()); }
    if (unrolls.empty()) throw std::runtime_error("C10 unroll axis is empty");
    std::vector<bool> early{false,true}, prune{false,true};
    if (restrictions.contains("early_zero")) { early.clear(); for(const auto& x:restrictions.at("early_zero").as_array()) early.push_back(x.get<bool>()); }
    if (restrictions.contains("axis_prune")) { prune.clear(); for(const auto& x:restrictions.at("axis_prune").as_array()) prune.push_back(x.get<bool>()); }
    if (early.empty()||prune.empty()) throw std::runtime_error("C10 boolean axis is empty");
    std::vector<Json> out;
    for(const auto& l:layouts) for(const auto& d:distances) for(const auto& r:reductions)
        for(int u:unrolls) for(bool z:early) for(bool p:prune) {
            Candidate c{l,d,r,u,z,p}; const auto j=encode(c); (void)decode(j);
            if (std::find(out.begin(),out.end(),j)==out.end()) out.push_back(j);
        }
    return out;
}

inline Json default_benchmark() {
    return Json{{"map","01big-world"},{"sides",Json::array({"A","B"})},
                {"point_counts",Json::array({16,64,128})},
                {"patterns",Json::array({"uniform","clustered","ties","zero_front","zero_back","axis_stripes"})},
                {"samples_per_case",2},{"seed",17}};
}

} // namespace opt::fuel
