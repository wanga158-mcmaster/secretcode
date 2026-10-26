#include "../correlator-common/wire.h"
#include <cstdio>
#include <cstdlib>

namespace correlator=plan3_correlator;
unsigned checks=0,cases=0;
void require(bool value,const char* expression,int line) {
    ++checks;if(!value) {std::fprintf(stderr,"correlator:%d: %s\n",line,expression);std::exit(1);}
}
#define CHECK(value) require(bool(value),#value,__LINE__)
correlator::Matrix matrix(unsigned actors,unsigned groups) {
    correlator::Matrix m;for(unsigned a=0;a<actors;++a)m.actors.push_back(int(a+1));
    for(unsigned g=0;g<groups;++g)m.groups.push_back(int(10+g));
    m.cells.assign(actors*groups,{correlator::CellState::known,22});return m;
}
std::uint64_t payoff(const correlator::Policy& p,unsigned bits) {
    std::uint64_t result=0;
    for(const int group:p.groups) {
        std::uint32_t best=0;
        for(unsigned a=0;a<p.actors.size();++a) {
            const auto role=p.roles[a][(bits>>a)&1u];if(role.group==group)best=std::max(best,role.utility);
        }
        result+=best;
    }
    return result;
}
bool lex(const correlator::Policy& p,unsigned left,unsigned right) {
    for(unsigned a=0;a<p.actors.size();++a) {
        const int l=p.roles[a][(left>>a)&1u].group,r=p.roles[a][(right>>a)&1u].group;
        if(l!=r)return l<r;
    }
    return false;
}
void verify(const correlator::Matrix& m) {
    const auto p=correlator::build(m);++cases;
    if(p.state!=correlator::State::known) {
        bool positive=false;for(const auto& c:m.cells)positive|=c.state==correlator::CellState::known && c.utility>0;
        CHECK(!positive && p.state==correlator::State::unavailable);return;
    }
    CHECK(p.actors==m.actors && p.groups==m.groups && p.tupleCount<=32 && p.tupleCount>=2);
    unsigned valid=0;std::uint64_t total=0,best=0;unsigned expected_left=0,expected_right=0;bool selected=false;
    for(unsigned bits=0;bits<32;++bits) {
        if(bits&~p.variableMask)continue;++valid;const auto value=payoff(p,bits);total+=value;
        bool present=false;for(unsigned n=0;n<p.tupleCount;++n)
            if(p.tuples[n].bits==bits) {CHECK(p.tuples[n].output==value);present=true;}
        CHECK(present);
        unsigned left=bits,right=bits^p.variableMask;
        if(lex(p,right,left))std::swap(left,right);
        const auto joint=value+payoff(p,right==bits?left:right);
        if(!selected || joint>best || (joint==best && (lex(p,left,expected_left) ||
           (left==expected_left && lex(p,right,expected_right))))) {
            selected=true;best=joint;expected_left=left;expected_right=right;
        }
    }
    CHECK(valid==p.tupleCount && total==p.independentNumerator);
    CHECK(best==p.jointNumerator && expected_left==p.left && expected_right==p.right);
    CHECK(p.jointNumerator*p.tupleCount>=2*p.independentNumerator);
    std::array<std::array<unsigned,2>,5> joint{},independent{};
    for(unsigned ticket=0;ticket<32;++ticket)for(unsigned a=0;a<p.actors.size();++a) {
        ++joint[a][(correlator::sample(p,ticket,false)>>a)&1u];
        ++independent[a][(correlator::sample(p,ticket,true)>>a)&1u];
    }
    for(unsigned a=0;a<p.actors.size();++a)for(unsigned action=0;action<2;++action) {
        CHECK(joint[a][action]==independent[a][action]);
        CHECK(p.jointCounts[a][action]*p.tupleCount==2*p.independentCounts[a][action]);
    }
}
void kernels() {
    auto m=matrix(2,2);const auto p=correlator::build(m);
    CHECK(p.state==correlator::State::known && p.jointNumerator==88 && p.independentNumerator==132 && p.tupleCount==4);
    CHECK(payoff(p,0)==22 && payoff(p,3)==22 && payoff(p,1)==44);verify(m);
    for(unsigned code=0;code<256;++code) {
        auto value=code;for(auto& c:m.cells) {c.utility=value%4;value/=4;}verify(m);
    }
    m=matrix(3,2);
    for(unsigned code=0;code<729;++code) {
        auto value=code;for(auto& c:m.cells) {c.utility=1+value%3;value/=3;}verify(m);
    }
    for(unsigned actors=1;actors<=5;++actors) {
        m=matrix(actors,64);for(auto& c:m.cells)c.utility=UINT32_MAX;verify(m);
        m=matrix(actors,1);verify(m);m.cells[0]={correlator::CellState::unavailable,0};verify(m);
    }
    m=matrix(2,2);m.cells[0]={correlator::CellState::unknown,0};
    CHECK(correlator::build(m).state==correlator::State::unavailable);
    m.cells[3]={correlator::CellState::unavailable,1};CHECK(correlator::build(m).state==correlator::State::invalid);
    m=matrix(2,2);m.actors[1]=m.actors[0];CHECK(correlator::build(m).state==correlator::State::invalid);
    m=matrix(2,2);std::swap(m.groups[0],m.groups[1]);CHECK(correlator::build(m).state==correlator::State::invalid);
    m=matrix(6,2);CHECK(correlator::build(m).state==correlator::State::invalid);
    m=matrix(2,65);CHECK(correlator::build(m).state==correlator::State::invalid);
    m=matrix(2,2);m.cells.pop_back();CHECK(correlator::build(m).state==correlator::State::invalid);
}
void codec() {
    correlator::Seal seal;seal.policy=correlator::build(matrix(3,2));seal.width=seal.height=8;
    seal.matrixFingerprint=0x1234abcd;seal.ticket=0x87654321;
    const auto bytes=correlator::encode(seal);CHECK(bytes.has_value());
    const auto decoded=correlator::decode(*bytes,8,8,false);
    CHECK(decoded.policy.state==correlator::State::known && decoded.ticket==seal.ticket);
    CHECK(correlator::table_fingerprint(decoded.policy)==correlator::table_fingerprint(seal.policy));
    CHECK(correlator::decode(*bytes,9,8,false).policy.state!=correlator::State::known);
    CHECK(correlator::decode(*bytes,8,8,true).policy.state!=correlator::State::known);
    for(const unsigned offset:{0u,4u,7u,16u,24u,224u,255u,1023u}) {
        auto changed=*bytes;changed[offset]^=std::byte{1};
        CHECK(correlator::decode(changed,8,8,false).policy.state!=correlator::State::known);
    }
    auto changed=*bytes;changed[218]=std::byte{3};
    auto hash=plan3_assignment::checksum(changed);for(unsigned k=0;k<4;++k)changed[28+k]=std::byte((hash>>(8*k))&255u);
    CHECK(correlator::decode(changed,8,8,false).policy.state!=correlator::State::known);
    for(unsigned ticket=0;ticket<32;++ticket)for(bool control:{false,true}) {
        seal.ticket=ticket;seal.control=control;
        for(unsigned a=0;a<seal.policy.actors.size();++a) {
            const auto plan=correlator::actor_plan(seal,seal.policy.actors[a]);CHECK(plan3_runtime::valid_market_plan(plan));
            const int role=seal.policy.roles[a][(correlator::sample(seal.policy,ticket,control)>>a)&1u].group;
            for(const int group:plan.groupIds)CHECK(plan.owner(group)==(group==role?seal.policy.actors[a]:-1));
        }
    }
    CHECK(!correlator::actor_plan(seal,99).complete);
}
int main() {
    kernels();codec();std::printf("CORRELATOR_RESULT {\"checks\":%u,\"tables\":%u,\"nativeCalls\":0}\n",checks,cases);
}
