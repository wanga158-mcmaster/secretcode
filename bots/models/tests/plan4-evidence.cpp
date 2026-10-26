#include "../common/evidence.h"
#include <cassert>
#include <iostream>
using namespace plan4;
using namespace plan4::runtime;
int main() {
    GroupBoundary end{0,10,42,30,Lifecycle::cleared,{10,Source::personal_vision,Status::exact_now},true,true};
    GroupBoundary start{0,11,42,30,Lifecycle::respawning,{11,Source::personal_vision,Status::exact_now},true,true};
    const auto event=capture_between_owner_turns(end,start);
    assert(event.supported&&event.receipt.round==11&&event.receipt.respawn_rounds==30);
    assert(event.event_status==Status::derived&&!event.reward_attribution_known);
    assert(event.receipt.origin==CaptureOrigin::lifecycle_boundaries);
    assert(event.receipt.event_status==Status::derived&&!event.receipt.actor_attribution_known);
    int cases=1;
    for(int change=0;change<9;++change) {
        auto bad=start;
        switch(change) {
            case 0:bad.round=12;bad.provenance.observed_round=12;break;
            case 1:bad.personally_visible=false;break;
            case 2:bad.provenance.status=Status::predicted;break;
            case 3:bad.provenance.observed_round=10;break;
            case 4:bad.group_id=43;break;
            case 5:bad.respawn_rounds=31;break;
            case 6:bad.actor_alive=false;break;
            case 7:bad.actor=1;break;
            case 8:bad.lifecycle=Lifecycle::living;break;
        }
        assert(!capture_between_owner_turns(end,bad).supported);++cases;
    }
    // An off-spawn observer and a zero-gem capture use the same lifecycle
    // witness. No amount of bag change can repair a missing lifecycle premise.
    end.lifecycle=Lifecycle::living;
    assert(!capture_between_owner_turns(end,start).supported);++cases;
    std::cout<<"{\"schema\":\"plan4-capture-evidence-tests/1\",\"passed\":true,\"cases\":"<<cases<<"}\n";
}
