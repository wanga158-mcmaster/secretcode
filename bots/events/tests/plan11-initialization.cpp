#include <array>
#include <cassert>
#include <cstddef>
#include <utility>
#include <vector>
#include "../common/src/initialization.h"

namespace {
struct Metadata {
    std::vector<std::vector<int>> groups{{2,3,5},{7}};
    int fuel=0, starts=0, opens=0, finishes=0, member_cost=5000;
    bool started=false, open=false, complete=false;
    std::size_t group=0, member=0;
    int partial=0;
    std::vector<std::vector<int>> cache;
    std::vector<int> closed;
    std::vector<std::pair<std::size_t,int>> tile_index;

    void spend(int amount) { assert(fuel>=amount);fuel-=amount; }
    auto callbacks() {
        // These stand in for atomic immutable SDK metadata operations. The
        // production driver's actual guards, order and resumption are tested;
        // no independent copy of its loop appears in this fixture.
        return p11::initialization::Work{
            [&]{return fuel;}, [&]{return complete;}, [&]{return started;},
            [&]{assert(!started&&!complete);spend(40000);started=true;++starts;},
            [&]{return group<groups.size();}, [&]{return open;},
            [&]{assert(started&&!open&&!complete);spend(16000);open=true;member=0;
                partial=0;cache.emplace_back();++opens;},
            [&]{return member<groups[group].size();},
            [&]{assert(open&&!complete);spend(member_cost);const int value=groups[group][member];
                tile_index.push_back({group,value});cache.back().push_back(value);
                partial+=value;++member;},
            [&]{assert(open&&member==groups[group].size()&&!complete);spend(1000);
                closed.push_back(partial);open=false;++group;},
            [&]{assert(group==groups.size()&&!open&&!complete);spend(7000);
                complete=true;++finishes;}
        };
    }
};
}

int main() {
    Metadata phased;
    auto work=phased.callbacks();
    const auto resume=[&](int fuel){phased.fuel=fuel;return p11::initialization::resume(work);};

    // Refusal before setup must not clear or publish partially built state.
    assert(!resume(59999)&&!phased.started&&phased.starts==0);
    assert(!resume(60000)&&phased.started&&phased.starts==1);
    assert(phased.group==0&&!phased.open&&phased.cache.empty());
    assert(!resume(29999)&&phased.opens==0);

    // Yield after opening and within a group; an open group is not reopened,
    // its public-model row is not duplicated, and its index stays stable.
    assert(!resume(30000)&&phased.open&&phased.opens==1&&phased.member==0);
    assert(!resume(14999)&&phased.tile_index.empty()&&phased.cache.size()==1);
    assert(!resume(19000)&&phased.member==1&&phased.cache[0]==std::vector<int>{2});
    assert(!resume(20000)&&phased.group==1&&!phased.open);
    assert(phased.closed==std::vector<int>{10}&&phased.opens==1);
    assert(!phased.complete&&phased.finishes==0);

    // Resume at the next group, finish its last member, then refuse final
    // readiness until its separate reserve is available.
    assert(!resume(30000)&&phased.group==1&&phased.member==0&&phased.opens==2);
    assert(!resume(20000)&&phased.group==2&&!phased.open);
    assert(!resume(24999)&&!phased.complete&&phased.finishes==0);
    assert(resume(25000)&&phased.complete&&phased.finishes==1);

    Metadata uninterrupted;
    uninterrupted.fuel=1000000;
    auto whole=uninterrupted.callbacks();
    assert(p11::initialization::resume(whole));
    assert(phased.cache==uninterrupted.cache&&phased.closed==uninterrupted.closed);
    assert(phased.tile_index==uninterrupted.tile_index);
    assert((phased.tile_index==std::vector<std::pair<std::size_t,int>>{{0,2},{0,3},{0,5},{1,7}}));
    assert(phased.starts==1&&phased.opens==2&&phased.finishes==1);

    // A completed initializer is idempotent even at zero fuel. Readiness can
    // never make a later call repeat a metadata write or consume the reserve.
    const auto before=phased.tile_index;
    assert(resume(0)&&phased.fuel==0&&phased.tile_index==before);
    assert(phased.starts==1&&phased.opens==2&&phased.finishes==1);

    // Empty group lists still require setup and finalization; they must not
    // fabricate a group/member operation or skip the final readiness reserve.
    Metadata empty;empty.groups.clear();empty.fuel=60000;
    auto empty_work=empty.callbacks();
    assert(!p11::initialization::resume(empty_work)&&empty.started&&!empty.complete);
    empty.fuel=25000;
    assert(p11::initialization::resume(empty_work)&&empty.opens==0&&empty.finishes==1);

    // A legal final-member callback can consume nearly its whole reserve.
    // Closing has its own guard: keep the completed open group, then close
    // later without repeating its member write or opening another cache row.
    Metadata expensive;expensive.groups={{7}};expensive.member_cost=14500;
    auto expensive_work=expensive.callbacks();
    expensive.fuel=60000;
    assert(!p11::initialization::resume(expensive_work));
    expensive.fuel=30000;
    assert(!p11::initialization::resume(expensive_work)&&expensive.open);
    expensive.fuel=15000;
    assert(!p11::initialization::resume(expensive_work));
    assert(expensive.fuel==500&&expensive.member==1&&expensive.group==0);
    assert(expensive.closed.empty()&&expensive.tile_index.size()==1);
    expensive.fuel=4999;
    assert(!p11::initialization::resume(expensive_work)&&expensive.closed.empty());
    expensive.fuel=5000;
    assert(!p11::initialization::resume(expensive_work)&&expensive.group==1);
    assert(expensive.closed==std::vector<int>{7}&&expensive.cache.size()==1);
    expensive.fuel=25000;
    assert(p11::initialization::resume(expensive_work)&&expensive.finishes==1);
    assert(expensive.tile_index.size()==1&&expensive.opens==1&&expensive.starts==1);
}
