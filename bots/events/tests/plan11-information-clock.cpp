#include <cassert>
#include "../surveyor/src/policy.h"

namespace {
using p11::Candidate;
using p11::State;
using P=p11_surveyor::Policy;
using Clock=P::InformationClock;
using Menu=std::array<Candidate,20>;

Menu menu() {
    Menu result{};
    for(int i=0;i<int(result.size());++i) {
        auto&c=result[i];c.id=i;c.travel=30;c.clear=10;c.gems=1;
        c.altar=3;c.hp=1000;c.incoming=10;c.loss=120;
    }
    auto set=[&](int id,int travel,int clear,int gems,float hp,float incoming,float loss) {
        auto&c=result[id];c.travel=travel;c.clear=clear;c.gems=gems;
        c.hp=hp;c.incoming=incoming;c.loss=loss;
    };
    set(0,0,5,10,500,10,70);
    set(1,1,5,30,500,30,210);result[1].committed=true;result[1].altar=4;
    set(2,0,20,45,2000,50,1100);
    set(3,1,20,24,2000,50,1100);
    return result;
}

std::vector<Candidate> complete(const Menu&public_menu,std::span<const int> order) {
    std::vector<Candidate> result;
    for(const int id:order) {
        auto c=public_menu[id];c.known=true;
        if(id==3){c.ready=true;c.clear=0;c.loss=0;}
        result.push_back(c);
    }
    return result;
}

void counterexample() {
    State state;state.mature=true;state.round=184;
    const auto public_menu=menu();
    int fallback=-1,best_fallback=100000;
    for(const auto&c:public_menu)
        if(p11::feasible(state,c)&&c.travel+c.clear<best_fallback) {
            best_fallback=c.travel+c.clear;fallback=c.id;
        }
    assert(fallback==0);
    const int target=1,identity=0;
    for(int round=160;round<320;++round) {
        // The previous actual runtime scheduling path substitutes fallback for
        // explore whenever those two protected jobs differ. Its exact current
        // shortlist and detailed selector receive the complete alternatives.
        const int explore=(round/8+identity)%public_menu.size();
        const int owned=target>=0?target:fallback;
        const int alternative=fallback>=0&&fallback!=owned?fallback:explore;
        auto order=p11::shortlist<P>(state,public_menu,P::window_size,owned,alternative);
        auto promote=[&](int id) {
            const auto found=std::find(order.begin(),order.end(),id);
            if(found!=order.end())std::rotate(order.begin(),found,found+1);
        };
        promote(target);promote(fallback);
        assert((order==std::vector<int>{0,1,2}));
        const auto detailed=complete(public_menu,order);
        assert(p11::choose<P>(state,detailed)==1);
    }
    std::array<int,20> every{};for(int i=0;i<20;++i)every[i]=i;
    const auto all_details=complete(public_menu,every);
    assert(p11::choose<P>(state,all_details)==3);

    Clock clock;
    for(int probe=0;probe<=3;++probe) {
        const auto order=clock.frontier<P>(state,public_menu,3,target,fallback);
        assert(order.size()==3&&order[0]==fallback&&order[1]==target);
        assert(std::find(order.begin(),order.end(),probe)!=order.end());
        const auto detailed=complete(public_menu,order);
        assert(p11::choose<P>(state,detailed)==(probe==3?3:1));
        for(const auto&c:detailed)clock.observed(c.id);
    }
}

void fairness_and_interruptions() {
    State state;state.mature=true;
    const auto public_menu=menu();Clock clock;
    for(int cycle=0;cycle<3;++cycle)for(int probe=0;probe<20;++probe) {
        const auto order=clock.frontier<P>(state,public_menu,3,1,0);
        assert(order.size()==3&&order[0]==0&&order[1]==1);
        auto sorted=order;std::sort(sorted.begin(),sorted.end());
        assert(std::adjacent_find(sorted.begin(),sorted.end())==sorted.end());
        assert(std::find(order.begin(),order.end(),probe)!=order.end());
        // A fuel cut or stale plan causes no observed call. Replanning the
        // same eligible state must retain this probe rather than skip its ID.
        assert(clock.frontier<P>(state,public_menu,3,1,0)==order);
        clock.observed(1000);
        assert(clock.frontier<P>(state,public_menu,3,1,0)==order);
        for(const int id:order)clock.observed(id);
    }
    clock.reset();
    assert((clock.frontier<P>(state,public_menu,3,-1,-1)==std::vector<int>{0,2,1}));
    clock.observed(0);
    assert((clock.frontier<P>(state,public_menu,3,1,1)==std::vector<int>{1,2,3}));
}

void eligibility_and_extreme_ids() {
    State state;state.mature=true;const auto full=menu();Clock clock;
    for(int id=0;id<2;++id){clock.frontier<P>(state,full,3,1,0);clock.observed(id);}
    const auto pending=clock.frontier<P>(state,full,3,1,0);
    assert(pending.back()==2);
    // A respawning group is absent from the caller's eligible preview. The
    // unconsumed cursor next proposes its successor; reentry is found on wrap.
    std::vector<Candidate> reduced;
    for(const auto&c:full)if(c.id!=2)reduced.push_back(c);
    auto order=clock.frontier<P>(state,reduced,3,1,0);
    assert(order.back()==3);clock.observed(2);
    assert(clock.frontier<P>(state,reduced,3,1,0)==order);
    clock.observed(3);
    for(int id=4;id<20;++id) {
        order=clock.frontier<P>(state,full,3,1,0);
        assert(order.back()==id);clock.observed(id);
    }
    for(int id=0;id<3;++id) {
        order=clock.frontier<P>(state,full,3,1,0);
        assert(std::find(order.begin(),order.end(),id)!=order.end());clock.observed(id);
    }
    clock.reset();
    auto invalid=full;invalid[0].gems=0;invalid[1].id=-1;invalid[2].clear=10000;
    order=clock.frontier<P>(state,invalid,3,1,0);
    assert(order.front()==3);
    clock.reset();
    assert(clock.frontier<P>(state,std::span<const Candidate>{},3,1,0).empty());
    clock.observed(0);
    Candidate extreme=full[0];extreme.id=std::numeric_limits<int>::max();
    order=clock.frontier<P>(state,std::span(&extreme,1),3,-1,-1);
    assert(order==std::vector<int>{extreme.id});clock.observed(extreme.id);
    assert(clock.frontier<P>(state,full,3,-1,-1).front()==0);
    clock.reset();
    assert(clock.frontier<P>(state,full,0,1,0).empty());
}
}

int main() {
    static_assert(P::window_size==3);
    counterexample();fairness_and_interruptions();eligibility_and_extreme_ids();
}
