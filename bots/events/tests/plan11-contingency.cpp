#include <cassert>
#include <cstdio>
#include <type_traits>
#include "../contingency/src/policy.h"

namespace {
using p11::Candidate;
using p11::State;
using p11_contingency::RouteJob;

RouteJob job(int id,int gems=0,int gold=0) {
    RouteJob result;result.candidate.id=id;
    result.candidate.gems=gems;result.candidate.gold=gold;return result;
}

// Reconstruct Tempo's formula, queried tribute timing and health gate without
// calling its score or production utility/scenario helpers.
double oracle_utility(const State&s,const Candidate&c) {
    const bool admissible=c.ready||(c.clear<10000&&s.hp-c.loss>=s.maximum*.25f);
    if(!admissible)return 0;
    const int elapsed=c.travel+c.clear+c.capture;
    const int bag=s.bag+c.gems;
    const int ticks=bag>0?std::max(s.tribute_minimum,
        s.tribute_base+int(std::ceil(bag*s.tribute_multiplier))):0;
    const int bank=elapsed+c.altar+ticks;
    if(c.gems>0&&bank>s.limit-s.round)return 0;
    const int spread=(c.id*3+s.identity*11)%7;
    const double gems=3.*c.gems/(1+bank+12*c.allies+spread);
    const double gold=(s.mature?.04:3.)*c.gold
        /(1+elapsed+(s.field_shop?0:s.recall+1)+12*c.allies+spread);
    const double value=(gems+gold)*(c.committed?1.10:1.);
    return std::isfinite(value)&&value>0?value:0;
}

int enumerate(const State&s,std::span<const RouteJob>jobs) {
    if(jobs.size()>3)return -1;
    std::vector<int> ids{-1};
    for(const auto&entry:jobs) {
        const int id=entry.candidate.id;
        if(id<0||std::find(ids.begin(),ids.end(),id)!=ids.end())return -1;
        ids.push_back(id);
    }
    std::vector<std::vector<double>> scenarios;
    // Materialize each whole menu independently. Exactly one job changes in
    // scenarios 1..N; wait is included even when every other action is unsafe.
    for(int delayed=-1;delayed<int(jobs.size());++delayed) {
        std::vector<double> utilities{0};
        for(int index=0;index<int(jobs.size());++index) {
            Candidate c=jobs[index].candidate;
            if(index==delayed) {
                if(c.ready||c.clear<=0) c.capture=c.capture+1;
                else {
                    c.clear=c.clear+1;
                    const float phase=c.incoming*(s.jungle_shards<5?1.f:.7f);
                    c.loss=c.loss+phase;
                }
            }
            utilities.push_back(oracle_utility(s,c));
        }
        scenarios.push_back(utilities);
    }
    int winner=0;double smallest=std::numeric_limits<double>::infinity();
    for(int action=0;action<int(ids.size());++action) {
        double largest=0;
        for(const auto&scenario:scenarios) {
            const double optimum=*std::max_element(scenario.begin(),scenario.end());
            largest=std::max(largest,optimum-scenario[action]);
        }
        if(largest<smallest||(largest==smallest
                &&(scenarios[0][action]>scenarios[0][winner]
                ||(scenarios[0][action]==scenarios[0][winner]&&ids[action]<ids[winner])))) {
            winner=action;smallest=largest;
        }
    }
    return ids[winner];
}

void verify(const State&s,std::span<const RouteJob>jobs,int expected) {
    const int oracle=enumerate(s,jobs);
    if(oracle!=expected)std::fprintf(stderr,"contingency expected=%d oracle=%d jobs=%zu horizon=%d first_loss=%.2f first_clear=%d\n",
        expected,oracle,jobs.size(),s.limit-s.round,jobs.empty()?0:jobs[0].candidate.loss,
        jobs.empty()?0:jobs[0].candidate.clear);
    assert(oracle==expected);
    assert(p11_contingency::Policy::select_route(s,jobs)==expected);
}
} // namespace

int main() {
    using p11_contingency::Policy;
    static_assert(std::is_base_of_v<p11_tempo::Policy,Policy>);
    static_assert(Policy::bounded_scan&&Policy::window_size==3&&Policy::trace_scan
        &&Policy::route_select&&Policy::private_wait);
    State state;state.field_shop=true;state.limit=10;
    std::array<RouteJob,2> deadline{{job(0,20),job(7,15)}};
    for(auto&entry:deadline)entry.candidate.travel=1;
    deadline[0].candidate.altar=1;
    std::array<Candidate,2> singles{{deadline[0].candidate,deadline[1].candidate}};
    assert(p11::choose<p11_tempo::Policy>(state,singles)==0);
    verify(state,deadline,7); // The higher baseline job loses its bank deadline.
    std::reverse(deadline.begin(),deadline.end());verify(state,deadline,7);
    const auto unchanged=deadline;
    assert(Policy::select_route(state,deadline)==7);
    for(int i=0;i<2;++i) {
        assert(deadline[i].candidate.clear==unchanged[i].candidate.clear);
        assert(deadline[i].candidate.capture==unchanged[i].candidate.capture);
        assert(deadline[i].candidate.loss==unchanged[i].candidate.loss);
    }
    const auto&c=deadline[0].candidate;
    assert(Policy::score(state,c)==p11_tempo::Policy::score(state,c));
    assert(Policy::screen(state,c)==p11_tempo::Policy::score(state,c));
    assert(Policy::bank(state)==p11_tempo::Policy::bank(state));
    assert(Policy::retreat(400,1000,50)==p11_tempo::Policy::retreat(400,1000,50));

    State health;health.field_shop=true;
    std::array<RouteJob,2> exposure{{job(0,0,20),job(7,0,12)}};
    auto&fight=exposure[0].candidate;
    fight.clear=1;fight.loss=749;fight.incoming=1;
    verify(health,exposure,0); // Exactly 25% HP after the extra phase is feasible.
    fight.loss=750;verify(health,exposure,7);
    fight.loss=748.5f;fight.incoming=2;
    verify(health,exposure,7);
    health.jungle_shards=5;verify(health,exposure,0); // 30% camp mitigation.
    health.jungle_shards=4;verify(health,exposure,7);
    fight.ready=true;fight.loss=999;fight.incoming=999;
    verify(health,exposure,0); // Ready jobs delay capture, never add camp exposure.
    fight.ready=false;fight.clear=0;
    verify(health,exposure,7); // Baseline health gate still applies to clear-zero.
    fight.loss=750;fight.incoming=999;
    verify(health,exposure,0); // Clear-zero delay adds no damage.

    fight.clear=1;fight.loss=750;fight.incoming=1;fight.gold=15;
    exposure[1].candidate.gold=7;
    verify(health,exposure,0); // Equal worst regret (3); higher baseline wins.
    std::reverse(exposure.begin(),exposure.end());verify(health,exposure,0);

    std::array<RouteJob,1> one{{job(0,10)}};
    State custom;custom.field_shop=true;custom.limit=9;
    verify(custom,one,0); // Baseline banks at 7; delayed capture banks at 8.
    custom.bag=11;custom.tribute_multiplier=.25;custom.tribute_base=2;custom.tribute_minimum=9;
    custom.limit=14;verify(custom,one,-1); // Baseline bank at 15 includes the bag.
    custom.limit=15;verify(custom,one,0);
    custom.limit=16;verify(custom,one,0);
    one[0].candidate.gold=10;one[0].candidate.gems=0;one[0].candidate.altar=10000;
    verify(custom,one,0); // Tempo's gold payout has no fictitious altar journey.
    one[0].candidate.loss=751;verify(custom,one,-1);
    one[0].candidate.loss=0;one[0].candidate.clear=10000;verify(custom,one,-1);
    one[0]=job(0);verify(custom,one,-1); // Wait wins the complete zero tie.
    verify(custom,std::span<const RouteJob>{},-1);
    one[0].candidate.gold=-1;verify(custom,one,-1); // Non-positive utility is zero.
    one[0]=job(0,1,1);one[0].candidate.travel=-7;
    assert(!std::isfinite(p11_tempo::Policy::score(custom,one[0].candidate)));
    verify(custom,one,0); // Zero baseline utility; the delayed scenario is positive.
    one[0].candidate.gold=-1;verify(custom,one,-1); // All scenario utilities are zero.

    std::array<RouteJob,3> ties{{job(14,0,10),job(7,0,10),job(0,0,10)}};
    State equal;equal.field_shop=true;
    verify(equal,ties,0);std::reverse(ties.begin(),ties.end());verify(equal,ties,0);
    ties[0].candidate.id=ties[1].candidate.id;verify(equal,ties,-1);
    ties[0].candidate.id=-1;verify(equal,ties,-1);
    std::array<RouteJob,4> too_many{{job(0,0,1),job(7,0,1),job(14,0,1),job(21,0,100)}};
    verify(equal,too_many,-1);

    // 384 menus exercise the actual Policy route entry point against complete
    // independent scenario/action enumeration, including both health branches,
    // deadline cliffs, custom queried settings, commitment and shop maturity.
    int cases=0;
    for(int mode=0;mode<8;++mode)for(int code=0;code<48;++code) {
        State s;s.identity=(mode&4)?5:0;s.mature=mode&1;s.field_shop=mode&2;
        s.round=100;s.limit=s.round+8+code%39;s.bag=code*3%23;s.recall=2+code%6;
        s.hp=1000-code%4*50;s.jungle_shards=code%7;
        s.tribute_multiplier=code%2?.25:.1;s.tribute_base=code%3;s.tribute_minimum=1+code%3;
        std::array<RouteJob,3> menu{};
        for(int j=0;j<3;++j) {
            auto&entry=menu[j];auto&candidate=entry.candidate;
            candidate.id=code%2?2+j*3:j*7;entry.x=code+j;entry.y=code-j;
            candidate.travel=(code/(j+1)+j*3)%13;candidate.clear=(code+2*j)%5;
            candidate.capture=1+(code+3*j)%6;candidate.altar=(code*3+j*5)%15;
            candidate.gems=(code*(j+2)+j*7)%31;candidate.gold=(code*(j+3)+j*11)%41;
            candidate.allies=(code+j)%3;candidate.loss=float((code+j*5)%10*80);
            candidate.incoming=float((code+j*3)%9*10);candidate.ready=code%4==0;
            candidate.committed=code&(1<<j);candidate.known=code%2;
        }
        const auto bounded=std::span<const RouteJob>(menu.data(),std::size_t(code%4));
        const int expected=enumerate(s,bounded);
        assert(Policy::select_route(s,bounded)==expected);
        std::reverse(menu.begin(),menu.begin()+code%4);
        assert(Policy::select_route(s,bounded)==expected);++cases;
    }
    assert(cases==384);
}
