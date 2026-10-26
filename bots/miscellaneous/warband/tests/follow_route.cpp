#include <cassert>
#include <vector>
#include "follow_route.h"

struct Fixture {
    std::vector<int> calls;
    bool anchor_ok=true, group_ok=true;
    bool run(wb::Mode mode,bool follower,int distance,bool objective=true) {
        return wb::dispatch_follow_route(mode,follower,distance,8,objective,
            [&](int route) { calls.push_back(10+route); return anchor_ok; },
            [&](int route) { calls.push_back(20+route); return group_ok; });
    }
};

int main() {
    for(auto mode:{wb::Mode::fight,wb::Mode::retreat}) {
        Fixture f;
        assert(f.run(mode,true,9));
        assert(f.run(mode,true,8));
        assert((f.calls==std::vector<int>{14,14}));
        // A temporarily blocked combat anchor never lends permission to an economic target.
        f.calls.clear(); f.anchor_ok=false;
        assert(!f.run(mode,true,9));
        assert((f.calls==std::vector<int>{14}));
        // When the route becomes legal next turn, retry that same contract.
        f.anchor_ok=true;
        assert(f.run(mode,true,9));
        assert((f.calls==std::vector<int>{14,14}));
    }
    for(auto mode:{wb::Mode::travel,wb::Mode::gather,wb::Mode::clear}) {
        Fixture f;
        assert(f.run(mode,true,9));
        assert((f.calls==std::vector<int>{21}));
        f.calls.clear();
        assert(f.run(mode,true,8));
        assert((f.calls==std::vector<int>{12}));
        f.calls.clear(); f.anchor_ok=false;
        assert(f.run(mode,true,8));
        assert((f.calls==std::vector<int>{12,23}));
    }
    Fixture leader;
    assert(leader.run(wb::Mode::fight,false,9));
    assert((leader.calls==std::vector<int>{21}));
    Fixture no_objective;
    no_objective.anchor_ok=false;
    assert(!no_objective.run(wb::Mode::travel,true,20,false));
    assert((no_objective.calls==std::vector<int>{12}));
}
