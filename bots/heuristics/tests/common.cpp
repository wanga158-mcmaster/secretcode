#include "../common/src/decision.h"
#include "../common/src/capture.h"
#include <cassert>
int main(){
    using namespace heuristics;
    // A useful high-index job must not starve behind unreachable or distant IDs.
    std::vector<Detail> jobs;for(int i=0;i<46;++i)jobs.push_back({i,50,false});
    jobs[8]={8,9,false};jobs[44]={44,3,true};jobs[45]={45,12,true};
    auto order=detail_order(jobs,8);assert(order.size()==2&&order[0]==44);
    // Occupancy changes neither permanent admission nor future reachability.
    jobs[45].travel=1;assert(detail_order(jobs,-1)[0]==45);
    // A dense ring of unaffordable fights cannot exclude the first safe income.
    jobs.clear();for(int i=0;i<15;++i)jobs.push_back({i,i+1,true,false});
    jobs.push_back({44,30,true,true});order=detail_order(jobs,-1);assert(order[0]==44&&order.size()==12);
    // A contested cleared camp can never take the unconditional hold branch.
    assert(capture_action(false,false,100)==CaptureAction::hold);
    assert(capture_action(true,true,0)==CaptureAction::engage);
    assert(capture_action(true,true,8)==CaptureAction::leave);
    assert(capture_action(true,false,0)==CaptureAction::leave);
    // Empty personal enemy vision cannot renew a long-row capture hold.
    CaptureWatch watch;
    assert(!watch.expired(4,100,5));
    assert(!watch.expired(4,100,5)); // Both hold paths in one turn share a clock.
    assert(!watch.expired(4,106,5));
    assert(watch.expired(4,107,5));
    assert(watch.expired(4,200,5)); // Gaps in observations do not prove a payout.
    assert(!watch.expired(8,201,5)); // Physically holding a different source.
    watch.reset(); // Verified departure or a new group lifecycle.
    assert(!watch.expired(8,300,5));
    assert(!watch.expired(8,306,5));
    assert(watch.expired(8,307,5));
    assert(!watch.expired(-1,308,5)&&watch.source()==-1);
}
