#include "../assignment-common/ownership.h"
#define PLAN3_TELEMETRY_FORMAT_ONLY
#include "../common/telemetry.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

// This WASI fixture exercises pure code, not C++ exception unwinding.
extern "C" void* __cxa_allocate_exception(std::size_t size) {return std::malloc(size);}
extern "C" void __cxa_throw(void*,void*,void(*)(void*)) {std::abort();}

namespace a=plan3_assignment;
using C=plan3_matchmaker::CellState;
static unsigned checks=0,scenarios=0;
static void require(bool condition,int line) {
    ++checks;
    if(!condition){std::fprintf(stderr,"assignment check failed at line %d\n",line);std::exit(1);}
}
#define REQUIRE(...) require(bool(__VA_ARGS__),__LINE__)

static a::Matrix matrix(std::initializer_list<int> utilities,unsigned actors=3,unsigned jobs=3) {
    a::Matrix m;
    for(unsigned i=0;i<actors;++i)m.actors.push_back(int(i));
    for(unsigned i=0;i<jobs;++i)m.groups.push_back(int(10+10*i));
    for(int u:utilities)m.cells.push_back({u<0?C::unavailable:C::known,u<0?0u:unsigned(u)});
    return m;
}
static a::Result solve(a::Policy policy,const a::Matrix& m,unsigned budget) {
    ++scenarios;
    auto solver=std::make_unique<a::Solver>(policy);solver->start(m);
    unsigned calls=0;
    while(solver->status()==a::KernelStatus::pending) {
        REQUIRE(!solver->result().complete && solver->result().owners.empty());
        REQUIRE(++calls<100000);
        solver->step(budget);REQUIRE(solver->last_step_units()<=budget);
    }
    REQUIRE(solver->status()==a::KernelStatus::complete);
    REQUIRE(solver->result().complete);
    return solver->result();
}
static void forks() {
    const auto trap=matrix({10,9,-1,9,0,-1,-1,-1,1});
    for(unsigned b:{1u,8u}) {
        const auto exact=solve(a::Policy::matchmaker_exact,trap,b);
        const auto greedy=solve(a::Policy::matchmaker_greedy,trap,b);
        REQUIRE(exact.owners==std::vector<int>({1,0,2}) && exact.total==19);
        REQUIRE(greedy.owners==std::vector<int>({0,-1,2}) && greedy.total==11);
        // The exact assignment is 9+9+1, independently of greedy's 10+1.
        const auto contrasting=matrix({10,6,-1,6,3,-1,-1,-1,1});
        const auto product=solve(a::Policy::bargainer_product,contrasting,b);
        const auto total=solve(a::Policy::bargainer_total,contrasting,b);
        REQUIRE(product.feasible && product.owners==std::vector<int>({1,0,2}) && product.total==13);
        REQUIRE(product.product.limbs==std::array<std::uint32_t,5>({36,0,0,0,0}));
        REQUIRE(total.feasible && total.owners==std::vector<int>({0,1,2}) && total.total==14);
        REQUIRE(total.product.limbs==std::array<std::uint32_t,5>{});
        const auto tie=matrix({2,1,-1,6,3,-1,-1,-1,1});
        const auto lex=solve(a::Policy::bargainer_product,tie,b);
        const auto sum=solve(a::Policy::bargainer_total,tie,b);
        REQUIRE(lex.owners==std::vector<int>({0,1,2}) && lex.total==6 && lex.product.limbs[0]==6);
        REQUIRE(sum.owners==std::vector<int>({1,0,2}) && sum.total==8);
    }
}
static void states() {
    const auto zero=matrix({0,0,0,0,0,0,0,0,0});
    const auto noGain=solve(a::Policy::bargainer_product,zero,8);
    const auto fillers=solve(a::Policy::bargainer_total,zero,8);
    REQUIRE(!noGain.feasible && noGain.owners.empty() && noGain.total==0);
    REQUIRE(fillers.feasible && fillers.owners==std::vector<int>({0,1,2}) && fillers.total==0);
    const auto shortMatrix=matrix({1,1,1},3,1);
    REQUIRE(!solve(a::Policy::bargainer_product,shortMatrix,8).feasible);
    REQUIRE(!solve(a::Policy::bargainer_total,shortMatrix,8).feasible);
    const auto empty=matrix({},3,0);
    REQUIRE(solve(a::Policy::matchmaker_exact,empty,8).feasible);
    REQUIRE(!solve(a::Policy::bargainer_product,empty,8).feasible);
    const auto good=matrix({3,2,1,2,3,1,1,2,3});
    for(a::Policy p:{a::Policy::matchmaker_exact,a::Policy::matchmaker_greedy,a::Policy::bargainer_product,a::Policy::bargainer_total}) {
        ++scenarios;auto solver=std::make_unique<a::Solver>(p);
        auto unknown=good;unknown.cells[0]={C::unknown,0};solver->start(unknown);
        REQUIRE(solver->status()==a::KernelStatus::unknown && !solver->result().complete && solver->result().owners.empty());
        auto malformed=unknown;malformed.cells[1]={C::unavailable,1};solver->start(malformed);
        REQUIRE(solver->status()==a::KernelStatus::invalid && !solver->result().complete);
        solver->start(good);REQUIRE(solver->step(0)==a::KernelStatus::pending && solver->last_step_units()==0);
        while(solver->status()==a::KernelStatus::pending)solver->step(8);
        REQUIRE(solver->result().complete);
        REQUIRE(solver->step(9)==a::KernelStatus::invalid && !solver->result().complete && solver->result().owners.empty());
        solver->start(good);REQUIRE(solver->status()==a::KernelStatus::pending && !solver->result().complete);
    }
    ++scenarios;a::Solver invalid(static_cast<a::Policy>(255));invalid.start(good);
    REQUIRE(invalid.status()==a::KernelStatus::invalid && !invalid.result().complete);
}
static void wide_product() {
    a::Matrix m;m.actors={0,1,2,3,4};m.groups={10,20,30,40,50};
    for(unsigned i=0;i<5;++i)for(unsigned j=0;j<5;++j)m.cells.push_back({i==j?C::known:C::unavailable,i==j?UINT32_MAX:0});
    const auto result=solve(a::Policy::bargainer_product,m,8);
    REQUIRE(result.feasible && result.owners==std::vector<int>({0,1,2,3,4}));
    // Independent expansion of (2^32-1)^5, in five little-endian limbs.
    REQUIRE(result.product.limbs==std::array<std::uint32_t,5>({UINT32_MAX,4,UINT32_MAX-9,9,UINT32_MAX-4}));
    REQUIRE(result.total==std::uint64_t(UINT32_MAX)*5);
    auto overflow=result.product;const auto old=overflow;
    REQUIRE(!overflow.multiply(2) && overflow.limbs==old.limbs);
}
static a::DraftPlan plan(a::Policy policy) {
    a::DraftPlan p;p.complete=p.feasible=true;p.policy=policy;p.width=p.height=16;
    p.actors={0,1,2};p.groups={10,20,30};p.owners={1,0,2};p.matrixFingerprint=1234567;
    if(a::bargaining(policy))p.disagreement={0,0,0};
    return p;
}
static void reseal(a::DraftBytes& bytes) {
    const auto value=a::checksum(bytes);
    for(unsigned k=0;k<4;++k)bytes[28+k]=std::byte((value>>(k*8))&255u);
}
static void codec_and_ownership() {
    for(a::Policy policy:{a::Policy::matchmaker_exact,a::Policy::matchmaker_greedy,a::Policy::bargainer_product,a::Policy::bargainer_total}) {
        ++scenarios;auto p=plan(policy);const auto bytes=a::encode_plan(p);REQUIRE(bytes.has_value());
        auto decoded=a::decode_plan(*bytes,16,16,policy);
        REQUIRE(decoded.complete && decoded.owners==p.owners && decoded.disagreement==p.disagreement);
        auto owned=a::ownership_plan(decoded);REQUIRE(owned.complete && owned.owner(10)==1 && owned.owner(20)==0 && owned.owner(99)==-1);
        decoded.owners[0]=2;REQUIRE(owned.owner(10)==1); // Owned copy, not a draft-buffer view.
        const auto other=static_cast<a::Policy>((unsigned(policy)+1)%4);
        REQUIRE(!a::decode_plan(*bytes,16,16,other).complete);
        auto corrupt=*bytes;corrupt[7]=std::byte(unsigned(other));reseal(corrupt);
        REQUIRE(!a::decode_plan(corrupt,16,16,policy).complete);
        corrupt=*bytes;corrupt.back()=std::byte{1};reseal(corrupt);
        REQUIRE(!a::decode_plan(corrupt,16,16,policy).complete);
        corrupt=*bytes;corrupt[168]=std::byte{2}; // Duplicate physical owner, even after a valid checksum.
        reseal(corrupt);REQUIRE(!a::decode_plan(corrupt,16,16,policy).complete);
        REQUIRE(!a::decode_plan(std::span<const std::byte>(*bytes).first(1023),16,16,policy).complete);
        REQUIRE(!a::decode_plan(*bytes,15,16,policy).complete);
    }
    ++scenarios;auto infeasible=plan(a::Policy::bargainer_product);infeasible.feasible=false;infeasible.owners={-1,-1,-1};
    const auto absent=a::encode_plan(infeasible);REQUIRE(absent.has_value());
    const auto decoded=a::decode_plan(*absent,16,16,infeasible.policy);
    REQUIRE(decoded.complete && !decoded.feasible && !a::ownership_plan(decoded).complete);
    infeasible.owners[0]=0;REQUIRE(!a::encode_plan(infeasible));
    auto partial=plan(a::Policy::bargainer_product);partial.owners[1]=-1;REQUIRE(!a::encode_plan(partial));
    auto empirical=plan(a::Policy::bargainer_total);empirical.disagreement[0]=1;REQUIRE(!a::encode_plan(empirical));
    // A mutable fixture models fresh-vs-startup bytes only; actual SDK I/O is compile checked separately.
    ++scenarios;const auto p=plan(a::Policy::matchmaker_exact);const auto published=a::encode_plan(p);
    a::DraftBytes shared{};const auto startup=shared;shared=*published;
    REQUIRE(!a::decode_plan(startup,16,16,p.policy).complete);
    auto fresh=a::decode_plan(shared,16,16,p.policy);REQUIRE(fresh.complete);
    auto ownership=a::ownership_plan(fresh);shared.fill(std::byte{});fresh.owners[0]=-1;
    REQUIRE(ownership.complete && ownership.owner(10)==1);
}
static void policy_frames() {
    ++scenarios;plan3_runtime::Telemetry telemetry;
    for(a::Policy policy:{a::Policy::matchmaker_exact,a::Policy::matchmaker_greedy,a::Policy::bargainer_product,a::Policy::bargainer_total}) {
        const auto fields=a::adoption_fields(plan(policy),policy);
        const auto line=telemetry.format_line(7,"tavi","policy",fields);
        REQUIRE(line.valid() && !line.empty() && line.size()<=512);
        std::fwrite(line.view().data(),1,line.size(),stdout);
    }
    const auto line=telemetry.format_line(8,"tavi","policy",a::adoption_fields({},a::Policy::bargainer_product));
    REQUIRE(line.valid() && !line.empty() && line.size()<=512);
    std::fwrite(line.view().data(),1,line.size(),stdout);
}
int main() {
    forks();states();wide_product();codec_and_ownership();policy_frames();
    std::printf("{\"status\":\"pass\",\"checks\":%u,\"scenarios\":%u,\"sdkRuntimeCalls\":0,\"nativeGames\":0}\n",checks,scenarios);
}
