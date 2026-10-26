#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include <span>
#include <tuple>
#include <type_traits>
#include "../triage/src/policy.h"

#ifdef NDEBUG
#error This oracle requires assertions enabled.
#endif

using Policy=p11_triage::Policy;
using Job=Policy::FocusJob;

namespace {
// Independent flat enumeration of every length-three target-index word.
// Each word simulates damage and directly adds integral next-phase damage;
// suffixes after a full clear are ignored. No production search is reused.
int oracle(std::span<const Job> jobs) {
    int best=-1;
    std::tuple<std::int64_t,int,int> best_key{};
    const int count=int(jobs.size());
    for(int a=0;a<count;++a)for(int b=0;b<count;++b)for(int c=0;c<count;++c) {
        const std::array<int,3> targets{a,b,c};
        std::array<int,3> health{};
        for(int i=0;i<count;++i)health[i]=jobs[i].hits;
        std::int64_t exposure=0;
        bool valid=true;
        for(int target:targets) {
            bool alive=false;
            for(int i=0;i<count;++i)alive=alive||health[i]>0;
            if(!alive)break;
            if(health[target]==0){valid=false;break;}
            --health[target];
            for(int i=0;i<count;++i)if(health[i]>0)exposure+=std::int64_t(jobs[i].incoming);
        }
        if(!valid)continue;
        int killed=0;
        for(int i=0;i<count;++i)killed+=health[i]==0;
        const auto key=std::tuple{exposure,-killed,jobs[a].id};
        if(best<0||key<best_key){best=jobs[a].id;best_key=key;}
    }
    return best;
}

std::uint32_t seed=0x74726961;
int draw(int bound){seed=seed*1664525u+1013904223u;return int(seed%std::uint32_t(bound));}
} // namespace

int main() {
    static_assert(Policy::bounded_scan&&Policy::window_size==3&&Policy::trace_scan&&Policy::target_horizon);
    static_assert(std::is_same_v<Policy::FocusJob,p11_triage::FocusJob>);
    static_assert(std::is_base_of_v<p11_tempo::Policy,Policy>);
    static_assert(&Policy::bank==&p11_tempo::Policy::bank);
    static_assert(&Policy::retreat==&p11_tempo::Policy::retreat);
    static_assert(p11_triage::horizon==3&&p11_triage::max_jobs==3);

    // Smith's full-clear ratio prefers six hits at weight eight (6/8<1/1).
    // Its first three hits leave both jobs alive for 27 exposure. Killing the
    // one-hit camp now yields 8+8+8=24 across the three modeled phases.
    std::array<Job,2> counterexample{{{61,1,1},{15,6,8}}};
    assert(double(counterexample[1].hits)/counterexample[1].incoming<
           double(counterexample[0].hits)/counterexample[0].incoming);
    assert(Policy::choose_focus(counterexample)==61);
    assert(oracle(counterexample)==61);
    const std::array<p11::Monster,2> monsters{{{1,1,0},{6,8,0}}};
    const auto full=p11::clear_schedule(monsters,1);
    assert(full.turns==7&&full.loss==64);

    // Native ID, not input position, resolves equal exposure and kill count.
    std::array<Job,3> ties{{{90,1,2},{7,1,2},{35,1,2}}};
    assert(Policy::choose_focus(ties)==7);
    std::reverse(ties.begin(),ties.end());
    assert(Policy::choose_focus(ties)==7);
    // Both best exposures are sixteen: hitting ID 0 three times kills one
    // job; removing IDs 2 and 3 before the last hit kills two. Kills win
    // before the lower first native ID is considered.
    std::array<Job,3> kill_tie{{{0,3,5},{2,1,1},{3,1,1}}};
    assert(Policy::choose_focus(kill_tie)==2);
    assert(oracle(kill_tie)==2);
    std::reverse(kill_tie.begin(),kill_tie.end());
    assert(Policy::choose_focus(kill_tie)==2);
    std::array<Job,3> zero{{{8,4,0},{2,1,0},{9,1,0}}};
    assert(Policy::choose_focus(zero)==2);
    std::reverse(zero.begin(),zero.end());
    assert(Policy::choose_focus(zero)==2);
    std::array<Job,1> one{{{0,1,0}}};
    assert(Policy::choose_focus(one)==0);
    one[0].hits=10000;
    assert(Policy::choose_focus(one)==0);
    one[0].hits=std::numeric_limits<int>::max();
    assert(Policy::choose_focus(one)==0);

    assert(Policy::choose_focus({})==-1);
    std::array<Job,4> too_many{{{0,1,1},{1,1,1},{2,1,1},{3,1,1}}};
    assert(Policy::choose_focus(too_many)==-1);
    for(int hits:{0,-1,std::numeric_limits<int>::min()}) {
        one[0]={0,hits,1};assert(Policy::choose_focus(one)==-1);
    }
    one[0]={-1,1,1};assert(Policy::choose_focus(one)==-1);
    std::array<Job,2> duplicate{{{4,1,1},{4,2,2}}};
    assert(Policy::choose_focus(duplicate)==-1);
    const double nan=std::numeric_limits<double>::quiet_NaN();
    const double inf=std::numeric_limits<double>::infinity();
    for(double weight:{-1.,nan,inf,-inf,std::numeric_limits<double>::max()}) {
        one[0]={0,1,weight};assert(Policy::choose_focus(one)==-1);
    }
    one[0]={0,1,std::numeric_limits<double>::max()/16};
    assert(Policy::choose_focus(one)==0);
    one[0]={0,1,-0.};assert(Policy::choose_focus(one)==0);

    // Few hundred bounded, integral-weight cases exercise the public Policy
    // entry point against a structurally separate oracle, then reverse order.
    for(int trial=0;trial<480;++trial) {
        std::array<Job,3> jobs{};
        const int count=1+draw(3);
        for(int i=0;i<count;++i)jobs[i]={17*i+draw(9),1+draw(7),double(draw(13))};
        auto supplied=std::span<Job>{jobs.data(),std::size_t(count)};
        const int expected=oracle(supplied);
        assert(Policy::choose_focus(supplied)==expected);
        std::reverse(supplied.begin(),supplied.end());
        assert(oracle(supplied)==expected);
        assert(Policy::choose_focus(supplied)==expected);
    }
    p11::State state;
    p11::Candidate candidate;
    assert(Policy::screen(state,candidate)==p11_tempo::Policy::score(state,candidate));
}
