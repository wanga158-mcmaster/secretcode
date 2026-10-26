#pragma once
#include "solver.h"
#include <algorithm>
#include <optional>
#include <span>
#include <string_view>

namespace plan3_assignment {
inline constexpr unsigned horizon=128,maximum_groups=64,maximum_actors=5;
using DraftBytes=std::array<std::byte,1024>;
struct Fingerprint {
    std::uint32_t value=2166136261u;
    void byte(unsigned v){value=(value^(v&255u))*16777619u;}
    void u16(unsigned v){byte(v);byte(v>>8);}
    void u32(std::uint32_t v){for(unsigned k=0;k<4;++k)byte(v>>(8*k));}
    void text(std::string_view v){for(unsigned char c:v)byte(c);}
};
struct DraftPlan {
    bool complete=false,feasible=false;
    Policy policy=Policy::matchmaker_exact;
    unsigned width=0,height=0;
    std::uint32_t matrixFingerprint=0;
    std::vector<int> actors,groups,owners;
    std::vector<std::uint32_t> disagreement;
};
inline bool valid_plan(const DraftPlan& p) {
    if(!p.complete || !valid_policy(p.policy) || !p.width || !p.height || p.width>100 || p.height>100 ||
       p.actors.size()<3 || p.actors.size()>maximum_actors || p.groups.size()>maximum_groups || p.groups.size()!=p.owners.size())return false;
    if(bargaining(p.policy)) {
        if(p.disagreement.size()!=p.actors.size())return false;
        for(const auto d:p.disagreement)if(d)return false; // Version1 explicitly uses the idle d=0 outside option.
    } else if(!p.feasible || !p.disagreement.empty())return false;
    for(unsigned a=0;a<p.actors.size();++a)
        if(p.actors[a]<0 || p.actors[a]>=12 || (a && p.actors[a-1]>=p.actors[a]))return false;
    std::array<bool,12> used{};unsigned assigned=0;
    for(unsigned g=0;g<p.groups.size();++g) {
        if(p.groups[g]<0 || unsigned(p.groups[g])>=p.width*p.height || (g && p.groups[g-1]>=p.groups[g]))return false;
        const int owner=p.owners[g];if(owner==-1)continue;
        if(!p.feasible || owner<0 || owner>=12 || used[unsigned(owner)] || !std::binary_search(p.actors.begin(),p.actors.end(),owner))return false;
        used[unsigned(owner)]=true;++assigned;
    }
    return !bargaining(p.policy) || !p.feasible || assigned==p.actors.size();
}
inline std::uint32_t assignment_fingerprint(const DraftPlan& p) {
    Fingerprint f;f.text("P3AS-A1");f.byte(unsigned(p.policy));f.byte(unsigned(p.feasible));
    for(const auto d:p.disagreement)f.u32(d);
    for(unsigned g=0;g<p.groups.size();++g){f.u16(unsigned(p.groups[g]));f.byte(p.owners[g]<0?255u:unsigned(p.owners[g]));}
    return f.value;
}
inline std::uint32_t checksum(std::span<const std::byte> bytes) {
    Fingerprint f;for(unsigned i=0;i<bytes.size();++i)f.byte(i>=28 && i<32?0u:std::to_integer<unsigned>(bytes[i]));return f.value;
}
inline std::optional<DraftBytes> encode_plan(const DraftPlan& p) {
    if(!valid_plan(p))return std::nullopt;
    DraftBytes bytes{};
    const auto put=[&](unsigned at,unsigned v){bytes[at]=std::byte(v&255u);};
    const auto put16=[&](unsigned at,unsigned v){put(at,v);put(at+1,v>>8);};
    const auto put32=[&](unsigned at,std::uint32_t v){for(unsigned k=0;k<4;++k)put(at+k,v>>(8*k));};
    put(0,'P');put(1,'3');put(2,'A');put(3,'S');put(4,1);put(5,1);put(6,unsigned(p.feasible));put(7,unsigned(p.policy));
    put16(8,horizon);put16(10,p.width);put16(12,p.height);put(14,unsigned(p.actors.size()));put16(16,unsigned(p.groups.size()));put16(18,1024);
    put32(20,p.matrixFingerprint);put32(24,assignment_fingerprint(p));
    for(unsigned a=0;a<maximum_actors;++a)put(32+a,255);
    for(unsigned g=0;g<maximum_groups;++g)put(168+g,255);
    for(unsigned a=0;a<p.actors.size();++a)put(32+a,unsigned(p.actors[a]));
    for(unsigned g=0;g<p.groups.size();++g){put16(40+g*2,unsigned(p.groups[g]));put(168+g,p.owners[g]<0?255u:unsigned(p.owners[g]));}
    for(unsigned a=0;a<p.disagreement.size();++a)put32(232+a*4,p.disagreement[a]);
    put32(28,checksum(bytes));return bytes;
}
inline DraftPlan decode_plan(std::span<const std::byte> bytes,unsigned width,unsigned height,Policy policy) {
    if(bytes.size()!=1024 || !valid_policy(policy))return {};
    const auto get=[&](unsigned at){return std::to_integer<unsigned>(bytes[at]);};
    const auto get16=[&](unsigned at){return get(at)|(get(at+1)<<8);};
    const auto get32=[&](unsigned at){std::uint32_t v=0;for(unsigned k=0;k<4;++k)v|=std::uint32_t(get(at+k))<<(8*k);return v;};
    if(get(0)!='P' || get(1)!='3' || get(2)!='A' || get(3)!='S' || get(4)!=1 || get(5)!=1 || get(6)>1 || get(7)!=unsigned(policy) ||
       get16(8)!=horizon || get16(10)!=width || get16(12)!=height || get(14)<3 || get(14)>maximum_actors ||
       get16(16)>maximum_groups || get16(18)!=1024 || get32(28)!=checksum(bytes))return {};
    DraftPlan p;p.complete=true;p.feasible=get(6)!=0;p.policy=policy;p.width=width;p.height=height;p.matrixFingerprint=get32(20);
    for(unsigned a=0;a<get(14);++a){p.actors.push_back(int(get(32+a)));if(bargaining(policy))p.disagreement.push_back(get32(232+a*4));}
    for(unsigned g=0;g<get16(16);++g){p.groups.push_back(int(get16(40+g*2)));p.owners.push_back(get(168+g)==255?-1:int(get(168+g)));}
    if(!valid_plan(p) || get32(24)!=assignment_fingerprint(p))return {};
    const auto canonical=encode_plan(p);if(!canonical || !std::equal(bytes.begin(),bytes.end(),canonical->begin()))return {};
    return p;
}
} // namespace plan3_assignment
