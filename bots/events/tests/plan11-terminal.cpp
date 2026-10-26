#include <cassert>
#include <type_traits>
#include "../terminal/src/policy.h"

namespace {
using p11::Candidate;
using p11::State;
using p11_terminal::RouteJob;
using p11_terminal::Policy;

RouteJob job(int id,int gems=0,int travel=0,int gold=0) {
    RouteJob result;auto&c=result.candidate;
    c.id=id;c.gems=gems;c.travel=travel;c.gold=gold;c.capture=1;
    return result;
}

int actual_bank(const State&s,const Candidate&c) {
    const int bag=s.bag+c.gems;
    const int ticks=bag>0?std::max(s.tribute_minimum,
        s.tribute_base+int(std::ceil(bag*s.tribute_multiplier))):0;
    return c.travel+c.clear+c.capture+c.altar+ticks;
}

double value(const State&s,const Candidate&c) {
    const int bank=actual_bank(s,c);
    if(c.gems>0&&bank>s.limit-s.round)return -std::numeric_limits<double>::infinity();
    const int remaining=s.needed_gems-s.bag;
    const int reward=remaining<=0?0:std::min(c.gems,remaining);
    const int spread=(3*c.id+11*s.identity)%7;
    const int capture=c.travel+c.clear+c.capture;
    const int shop=s.field_shop?0:s.recall+1;
    return (3.*reward/(1+bank+12*c.allies+spread)
        +(s.mature?.04:3.)*c.gold/(1+capture+shop+12*c.allies+spread))
        *(c.committed?1.10:1.);
}

struct Oracle {int terminal=-1,selected=-1;};
// Independently enumerate each supplied single-job completion. No production
// feasibility, time, tribute, score, finishing helper or chooser is consulted.
Oracle enumerate(const State&s,std::span<const RouteJob>jobs) {
    Oracle result;int earliest=std::numeric_limits<int>::max();
    double best=-std::numeric_limits<double>::infinity();
    for(int i=0;i<int(jobs.size())&&i<3;++i) {
        const auto&c=jobs[i].candidate;
        const bool feasible=c.ready || (c.clear<10000 && s.hp-c.loss>=s.maximum*.25f);
        if(!feasible)continue;
        const int completion=actual_bank(s,c);
        if(c.id>=0&&s.bag+c.gems>=s.needed_gems&&completion<=s.limit-s.round
            &&(completion<earliest||(completion==earliest&&(result.terminal<0||c.id<result.terminal)))) {
            earliest=completion;result.terminal=c.id;
        }
        const double score=value(s,c);
        if(std::isfinite(score)&&score>best) {best=score;result.selected=c.id;}
    }
    if(result.terminal>=0)result.selected=result.terminal;
    return result;
}

void verify(const State&s,std::span<const RouteJob>jobs,int expected) {
    const auto oracle=enumerate(s,jobs);
    assert(oracle.selected==expected);
    assert(p11_terminal::finishing_job(s,jobs)==oracle.terminal);
    // This is the Policy caller used by the runtime route-select adapter.
    assert(Policy::select_route(s,jobs)==expected);
}
} // namespace

int main() {
    static_assert(std::is_base_of_v<p11_tempo::Policy,Policy>);
    static_assert(std::is_same_v<Policy::RouteJob,RouteJob>);
    static_assert(Policy::bounded_scan&&Policy::window_size==3&&Policy::trace_scan
        &&Policy::terminal_goal&&Policy::route_select);
    State s;s.mature=true;s.field_shop=true;s.needed_gems=15;s.altar=30;
    std::array<RouteJob,2> menu{{job(0,14),job(7,15,10)}};
    std::array<Candidate,2> candidates{{menu[0].candidate,menu[1].candidate}};
    assert(p11::choose<p11_tempo::Policy>(s,candidates)==0);
    assert(p11::choose<Policy>(s,candidates)==0);
    verify(s,menu,7); // A slower local rate still completes the winning target.
    assert(!Policy::bank(s));s.bag=15;assert(Policy::bank(s));
    assert(!p11_tempo::Policy::bank(s)); // The target can be below Tempo's threshold.
    assert(Policy::retreat(400,1000,50)==p11_tempo::Policy::retreat(400,1000,50));

    // The overshoot takes ten tribute ticks even though only ten gems matter.
    // Clipping the physical bag would falsely select the nearer hundred-gem job.
    s=State{};s.mature=true;s.field_shop=true;s.needed_gems=10;s.limit=4;
    menu={{job(0,100),job(7,10,2)}};verify(s,menu,7);
    assert(menu[0].candidate.bank_time(s)==11);
    assert(!std::isfinite(Policy::score(s,menu[0].candidate)));
    s.limit=2000;
    assert(Policy::score(s,menu[0].candidate)==30./12);
    assert(Policy::score(s,menu[1].candidate)==30./5);
    verify(s,menu,7);
    s.bag=21;s.needed_gems=25;s.limit=3;
    std::array<RouteJob,1> one{{job(0,4)}};verify(s,one,-1);
    s.limit=4;verify(s,one,0); // Existing bag is also an actual tribute obligation.

    s=State{};s.mature=true;s.field_shop=true;s.needed_gems=10;
    menu={{job(0,10),job(7,9)}};menu[0].candidate.loss=751;
    verify(s,menu,7); // An unhealthy finishing job cannot displace safe fallback.
    assert(p11_terminal::finishing_job(s,menu)==-1);
    menu[0].candidate.loss=750;verify(s,menu,0);
    menu[0].candidate.clear=10000;verify(s,menu,7);
    menu[0].candidate.clear=0;menu[0].candidate.loss=900;
    menu[0].candidate.ready=true;verify(s,menu,0); // Shared ready-job health exemption.

    s=State{};s.needed_gems=10;s.tribute_multiplier=.25;
    s.tribute_base=2;s.tribute_minimum=7;
    one={{job(0,10)}};one[0].candidate.altar=2;
    s.limit=9;verify(s,one,-1);s.limit=10;verify(s,one,0);
    s.bag=11;s.limit=10;verify(s,one,-1);s.limit=11;verify(s,one,0);
    s.round=12;verify(s,one,-1); // No time remains even for an otherwise sufficient job.

    s=State{};s.needed_gems=10;
    std::array<RouteJob,4> ties{{job(14,10,1),job(7,10,1),job(0,10,1),job(1,10)}};
    verify(s,ties,0); // Ignore a fourth job and break equal actual times by ID.
    std::reverse(ties.begin(),ties.begin()+3);verify(s,ties,0);
    ties[0].candidate.id=-1;verify(s,ties,7);
    ties[0].candidate.id=7;verify(s,ties,7); // Duplicate IDs do not create more work.
    verify(s,std::span<const RouteJob>{},-1);

    s=State{};s.mature=true;s.field_shop=true;s.needed_gems=0;
    menu={{job(0,0),job(7,10,3,100)}};verify(s,menu,0);
    assert(Policy::bank(s));assert(Policy::score(s,menu[0].candidate)==0);
    assert(Policy::score(s,menu[1].candidate)==.04*100/5);
    s.bag=30;assert(Policy::score(s,menu[0].candidate)==0);
    s.needed_gems=1000;assert(Policy::bank(s)==p11_tempo::Policy::bank(s));
    one={{job(0,0,3,100)}};
    assert(Policy::score(s,one[0].candidate)==p11_tempo::Policy::score(s,one[0].candidate));
    s.bag=0;s.needed_gems=1000;menu={{job(0,14),job(7,15,10)}};
    verify(s,menu,0); // With no finishing job, the caller uses its clipped chooser.
    candidates={{menu[0].candidate,menu[1].candidate}};
    assert(Policy::select_route(s,menu)==p11::choose<Policy>(s,candidates));

    // 512 bounded menus independently cover terminal/fallback paths, remaining
    // goals, health, readiness, actual bags, custom channels and financial terms.
    int cases=0,terminal=0,fallback=0;
    for(int mode=0;mode<8;++mode)for(int code=0;code<64;++code) {
        State state;state.identity=(mode&4)?5:0;state.mature=mode&1;state.field_shop=mode&2;
        state.round=100;state.limit=100+8+code%43;state.bag=code*3%29;
        state.needed_gems=1+(code*7+mode*11)%61;state.recall=2+code%6;
        state.hp=1000-code%4*50;state.tribute_multiplier=code%2?.15:.1;
        state.tribute_base=code%3;state.tribute_minimum=1+code%2;
        std::array<RouteJob,3> jobs{};
        for(int j=0;j<3;++j) {
            auto&c=jobs[j].candidate;c.id=code%2?2+j*3:j*7;
            jobs[j].x=code+j;jobs[j].y=code-j; // Coordinates do not affect one-job costs.
            c.travel=(code/(j+1)+j*3)%13;c.clear=(code+2*j)%5;c.capture=1+(code+3*j)%6;
            c.altar=(code*3+j*5)%15;c.gems=(code*(j+2)+j*7)%36;
            c.gold=(code*(j+3)+j*11)%41;c.allies=(code+j)%3;
            c.loss=float((code+j*5)%12*80);c.ready=code%4==0;c.committed=code&(1<<j);
            if(code%17==0&&j==2)c.clear=10000;
            const double actual=Policy::score(state,c),expected=value(state,c);
            assert(actual==expected);
        }
        if(code%11==0)jobs[1].candidate.id=jobs[0].candidate.id;
        const auto supplied=std::span<const RouteJob>(jobs.data(),std::size_t(code%4));
        const auto oracle=enumerate(state,supplied);
        verify(state,supplied,oracle.selected);
        if(oracle.terminal>=0)++terminal;else ++fallback;
        ++cases;
    }
    assert(cases==512&&terminal>0&&fallback>0);
}
