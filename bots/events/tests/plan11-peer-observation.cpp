#include <cassert>
#include <utility>
#include "../matching/src/policy.h"

namespace {
using P=p11_matching::Policy;
using p11_matching::Worker;

// The pinned SDK champion_info field names and types are deliberately mirrored
// without linking a host or including the SDK in this pure caller fixture.
struct Record {
    int type,x,y;
    float health,max_health,strength;
    int state;
};
Record generation(int value) {
    return {11,10+value,20+value,900.f-value,1000.f+value,45.f+value,100+value};
}
void equal(const Worker&a,const Worker&b) {
    assert(a.id==b.id&&a.x==b.x&&a.y==b.y);
    assert(a.hp==b.hp&&a.maximum==b.maximum&&a.strength==b.strength);
    assert(a.available==b.available&&a.score_identity==b.score_identity);
}
}

int main() {
    static_assert(P::reserve_action_budget);
    int calls=0,last_handle=-1;
    auto changing=[&](int handle)->std::optional<Record> {
        last_handle=handle;return generation(++calls);
    };
    const auto first=P::observe_worker(41,changing);
    assert(first&&calls==1&&last_handle==41);
    equal(first->worker,Worker{41,11,21,899,1001,46,true,11});
    assert(first->state==101);
    // A second invocation observes a new generation, not cached live fields;
    // each invocation still has one internally coherent source record.
    const auto second=P::observe_worker(41,changing);
    assert(second&&calls==2);
    equal(second->worker,Worker{41,12,22,898,1002,47,true,11});
    assert(second->state==102);
    assert(first->worker.x==11&&first->worker.hp==899&&first->state==101);
    // Physical ownership and canonical design identity cannot be conflated.
    assert(first->worker.id==41&&first->worker.score_identity==11);

    int hidden_calls=0;
    const auto hidden=P::observe_worker(7,[&](int handle)->std::optional<Record> {
        assert(handle==7);++hidden_calls;return std::nullopt;
    });
    assert(!hidden&&hidden_calls==1);

    const Record fixed{10,3,8,1000,1100,65,37};
    int snapshot_calls=0;
    const auto snapshot=P::observe_worker(3,[&](int handle)->std::optional<Record> {
        assert(handle==3);++snapshot_calls;return fixed;
    });
    assert(snapshot&&snapshot_calls==1);
    // Independent public-style getters agree when the world is fixed. The
    // snapshot has the same semantics while avoiding their repeated reads.
    int getter_reads=0;
    auto read_fixed=[&] {++getter_reads;return fixed;};
    auto position=[&] {const auto row=read_fixed();return std::pair{row.x,row.y};};
    const auto p=position();
    const float hp=read_fixed().health,maximum=read_fixed().max_health;
    const float strength=read_fixed().strength;
    const int type=read_fixed().type,state=read_fixed().state;
    equal(snapshot->worker,Worker{3,p.first,p.second,hp,maximum,strength,true,type});
    assert(snapshot->state==state&&getter_reads==6);
}
