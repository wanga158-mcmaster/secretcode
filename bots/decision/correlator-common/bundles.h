#pragma once
#include "../assignment-common/matching.h"
#include <algorithm>

namespace plan3_correlator {
using Matrix=plan3_matchmaker::Matrix;
using CellState=plan3_matchmaker::CellState;
enum class State {known,unavailable,invalid};
struct Role {int group=-1;std::uint32_t utility=0;};
struct Bundle {unsigned bits=0;std::uint64_t output=0;};
struct Policy {
    State state=State::invalid;
    std::vector<int> actors,groups;
    std::array<std::array<Role,2>,5> roles{};
    std::array<unsigned,5> actionCount{};
    std::array<Bundle,32> tuples{};
    unsigned tupleCount=0,variableMask=0,left=0,right=0;
    std::uint64_t jointNumerator=0,independentNumerator=0;
    // Joint denominator2; independent denominator tupleCount. Intended roles,
    // not realised excursions: common Market recovery/fallback stays enabled.
    std::array<std::array<unsigned,2>,5> jointCounts{},independentCounts{};
};
inline bool role_less(const Role& a,const Role& b) {
    return a.utility==b.utility?a.group<b.group:a.utility>b.utility;
}
inline bool bundle_less(const Policy& p,unsigned a,unsigned b) {
    for(unsigned i=0;i<p.actors.size();++i) {
        const int x=p.roles[i][(a>>i)&1u].group,y=p.roles[i][(b>>i)&1u].group;
        if(x!=y)return x<y;
    }
    return false;
}
// Assumed joint output: each physical group contributes the maximum solo
// H128 converted output among its assigned actors ONCE. This ignores delays,
// split-rounding, interference and opponents; it is not a measured joint table.
inline std::uint64_t team_output(const Policy& p,unsigned bits) {
    std::uint64_t value=0;
    for(unsigned a=0;a<p.actors.size();++a) {
        const auto role=p.roles[a][(bits>>a)&1u];if(role.group<0)continue;
        bool earlier=false;
        for(unsigned b=0;b<a;++b)earlier|=p.roles[b][(bits>>b)&1u].group==role.group;
        if(earlier)continue;
        auto best=role.utility;
        for(unsigned b=a+1;b<p.actors.size();++b) {
            const auto other=p.roles[b][(bits>>b)&1u];
            if(other.group==role.group)best=std::max(best,other.utility);
        }
        value+=best;
    }
    return value;
}
inline Policy build(const Matrix& matrix) {
    Policy p;const auto validation=plan3_matchmaker::matching_detail::validate(matrix);
    if(validation!=plan3_matchmaker::SolverStatus::pending) {
        p.state=validation==plan3_matchmaker::SolverStatus::unknown?State::unavailable:State::invalid;return p;
    }
    p.actors=matrix.actors;p.groups=matrix.groups;
    for(unsigned a=0;a<p.actors.size();++a) {
        std::array<Role,2> best{};unsigned count=0;
        for(unsigned g=0;g<matrix.groups.size();++g) {
            const auto& cell=matrix.cells[a*matrix.groups.size()+g];
            if(cell.state!=CellState::known || !cell.utility)continue;
            const Role candidate{matrix.groups[g],cell.utility};
            if(!count || role_less(candidate,best[0])) {
                best[1]=best[0];best[0]=candidate;count=std::min(2u,count+1);
            } else if(count==1 || role_less(candidate,best[1])) {best[1]=candidate;count=2;}
        }
        if(!count) {p.actionCount[a]=1;continue;}
        p.actionCount[a]=2;p.variableMask|=1u<<a;
        if(count==1)p.roles[a]={Role{},best[0]};
        else {if(best[1].group<best[0].group)std::swap(best[0],best[1]);p.roles[a]=best;}
    }
    if(!p.variableMask) {p.state=State::unavailable;return p;}
    p.tupleCount=1;
    for(unsigned a=0;a<p.actors.size();++a)if(p.actionCount[a]==2)p.tupleCount*=2;
    for(unsigned n=0;n<p.tupleCount;++n) {
        unsigned bits=0,cursor=0;
        for(unsigned a=0;a<p.actors.size();++a)if(p.actionCount[a]==2)bits|=((n>>cursor++)&1u)<<a;
        p.tuples[n]={bits,team_output(p,bits)};p.independentNumerator+=p.tuples[n].output;
        for(unsigned a=0;a<p.actors.size();++a)++p.independentCounts[a][(bits>>a)&1u];
    }
    bool selected=false;
    for(unsigned n=0;n<p.tupleCount/2;++n) {
        auto a=p.tuples[n],b=p.tuples[(p.tupleCount-1)^n];
        if(bundle_less(p,b.bits,a.bits))std::swap(a,b);
        const auto value=a.output+b.output;
        if(!selected || value>p.jointNumerator ||
           (value==p.jointNumerator && (bundle_less(p,a.bits,p.left) ||
            (a.bits==p.left && bundle_less(p,b.bits,p.right))))) {
            selected=true;p.left=a.bits;p.right=b.bits;p.jointNumerator=value;
        }
    }
    for(unsigned a=0;a<p.actors.size();++a) {
        ++p.jointCounts[a][(p.left>>a)&1u];++p.jointCounts[a][(p.right>>a)&1u];
    }
    p.state=State::known;return p;
}
// A uniform source word gives equal intended-role marginals EXACTLY: the joint
// arm shares bit0; control uses disjoint bits per physical sorted actor slot.
// Source uniformity/availability is a separate runtime assumption.
inline unsigned sample(const Policy& p,std::uint32_t ticket,bool control) {
    if(p.state!=State::known)return 0;
    return control?(ticket&p.variableMask):((ticket&1u)?p.right:p.left);
}
} // namespace plan3_correlator
