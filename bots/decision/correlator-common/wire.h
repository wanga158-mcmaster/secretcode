#pragma once
#include "bundles.h"
#include "../assignment-common/wire.h"
#include "../common/allocation-wire.h"

namespace plan3_correlator {
using Bytes=plan3_assignment::DraftBytes;
struct Seal {
    Policy policy;
    unsigned width=0,height=0;
    std::uint32_t matrixFingerprint=0,ticket=0;
    bool control=false;
};
inline std::uint32_t table_fingerprint(const Policy& p) {
    plan3_assignment::Fingerprint f;f.text("P3CR-T1");f.byte(unsigned(p.actors.size()));
    for(unsigned a=0;a<p.actors.size();++a) {
        f.u32(unsigned(p.actors[a]));f.byte(p.actionCount[a]);
        for(const auto& role:p.roles[a]) {f.u32(unsigned(role.group));f.u32(role.utility);}
    }
    for(unsigned n=0;n<p.tupleCount;++n) {
        f.byte(p.tuples[n].bits);f.u32(std::uint32_t(p.tuples[n].output));f.u32(std::uint32_t(p.tuples[n].output>>32));
    }
    f.byte(p.left);f.byte(p.right);return f.value;
}
inline std::optional<Bytes> encode(const Seal& seal) {
    const auto& p=seal.policy;
    if(p.state!=State::known || p.actors.empty() || p.actors.size()>5 || p.groups.size()>64 ||
       !seal.width || !seal.height || seal.width>100 || seal.height>100 || p.tupleCount<2 || p.tupleCount>32)return std::nullopt;
    for(unsigned a=0;a<p.actors.size();++a)
        if(p.actors[a]<0 || p.actors[a]>=255 || (a && p.actors[a-1]>=p.actors[a]))return std::nullopt;
    for(unsigned g=0;g<p.groups.size();++g)
        if(p.groups[g]<0 || unsigned(p.groups[g])>=seal.width*seal.height || (g && p.groups[g-1]>=p.groups[g]))return std::nullopt;
    Bytes bytes{};
    const auto put=[&](unsigned at,unsigned value){bytes[at]=std::byte(value&255u);};
    const auto put16=[&](unsigned at,unsigned value){put(at,value);put(at+1,value>>8);};
    const auto put32=[&](unsigned at,std::uint32_t value){for(unsigned k=0;k<4;++k)put(at+k,value>>(8*k));};
    const auto put64=[&](unsigned at,std::uint64_t value){put32(at,std::uint32_t(value));put32(at+4,std::uint32_t(value>>32));};
    put(0,'P');put(1,'3');put(2,'C');put(3,'R');put(4,1);put(5,1);put(6,unsigned(seal.control));put(7,unsigned(p.actors.size()));
    put16(8,seal.width);put16(10,seal.height);put16(12,unsigned(p.groups.size()));put16(14,128);
    put32(16,seal.matrixFingerprint);put32(20,table_fingerprint(p));put32(24,seal.ticket);
    for(unsigned a=0;a<5;++a)put(32+a,255);
    for(unsigned a=0;a<p.actors.size();++a) {
        put(32+a,unsigned(p.actors[a]));put(218+a,p.actionCount[a]);
        for(unsigned action=0;action<2;++action) {
            const auto& role=p.roles[a][action];unsigned index=255;
            if(role.group>=0) {
                const auto at=std::lower_bound(p.groups.begin(),p.groups.end(),role.group);
                if(at==p.groups.end() || *at!=role.group || !role.utility)return std::nullopt;
                index=unsigned(at-p.groups.begin());
            } else if(role.group!=-1 || role.utility)return std::nullopt;
            const unsigned offset=168+a*10+action*5;put(offset,index);put32(offset+1,role.utility);
        }
    }
    for(unsigned g=0;g<p.groups.size();++g)put16(40+g*2,unsigned(p.groups[g]));
    put(224,p.left);put(225,p.right);put(226,p.tupleCount);put(227,p.variableMask);
    put64(228,p.jointNumerator);put64(236,p.independentNumerator);put32(244,p.tupleCount);
    put32(28,plan3_assignment::checksum(bytes));return bytes;
}
inline Seal decode(std::span<const std::byte> bytes,unsigned width,unsigned height,bool control) {
    Seal invalid;if(bytes.size()!=1024)return invalid;
    const auto get=[&](unsigned at){return std::to_integer<unsigned>(bytes[at]);};
    const auto get16=[&](unsigned at){return get(at)|(get(at+1)<<8);};
    const auto get32=[&](unsigned at){std::uint32_t value=0;for(unsigned k=0;k<4;++k)value|=std::uint32_t(get(at+k))<<(8*k);return value;};
    if(get(0)!='P' || get(1)!='3' || get(2)!='C' || get(3)!='R' || get(4)!=1 || get(5)!=1 ||
       get(6)!=unsigned(control) || get(7)<1 || get(7)>5 || get16(8)!=width || get16(10)!=height ||
       !width || !height || width>100 || height>100 || get16(12)>64 || get16(14)!=128 ||
       get32(28)!=plan3_assignment::checksum(bytes))return invalid;
    Matrix matrix;
    for(unsigned a=0;a<get(7);++a)matrix.actors.push_back(int(get(32+a)));
    for(unsigned g=0;g<get16(12);++g)matrix.groups.push_back(int(get16(40+g*2)));
    matrix.cells.assign(matrix.actors.size()*matrix.groups.size(),{CellState::unavailable,0});
    for(unsigned a=0;a<matrix.actors.size();++a)for(unsigned action=0;action<2;++action) {
        const unsigned offset=168+a*10+action*5,index=get(offset);const auto utility=get32(offset+1);
        if(index==255) {if(utility)return invalid;continue;}
        if(index>=matrix.groups.size() || !utility || matrix.cells[a*matrix.groups.size()+index].state==CellState::known)return invalid;
        matrix.cells[a*matrix.groups.size()+index]={CellState::known,utility};
    }
    Seal result;result.policy=build(matrix);result.width=width;result.height=height;
    result.control=control;result.ticket=get32(24);result.matrixFingerprint=get32(16);
    const auto canonical=encode(result);
    if(!canonical || !std::equal(bytes.begin(),bytes.end(),canonical->begin()))return invalid;
    return result;
}
// Actor-local ownership preference, not a single collision-free team allocation.
// Each actor projects its own sampled intent; collisions remain possible/legal.
inline plan3_runtime::MarketPlan actor_plan(const Seal& seal,int actor) {
    plan3_runtime::MarketPlan plan;const auto& p=seal.policy;
    if(p.state!=State::known || !encode(seal))return plan;
    const auto found=std::lower_bound(p.actors.begin(),p.actors.end(),actor);
    if(found==p.actors.end() || *found!=actor)return plan;
    const unsigned index=unsigned(found-p.actors.begin());
    const int selected=p.roles[index][(sample(p,seal.ticket,seal.control)>>index)&1u].group;
    plan.complete=true;plan.width=seal.width;plan.height=seal.height;plan.actorOrdinals=p.actors;plan.groupIds=p.groups;
    plan.ownerOrdinals.assign(p.groups.size(),-1);
    if(selected>=0)plan.ownerOrdinals[std::lower_bound(p.groups.begin(),p.groups.end(),selected)-p.groups.begin()]=actor;
    return plan;
}
} // namespace plan3_correlator
