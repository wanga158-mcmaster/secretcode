#include "../src/mechanics.h"
#include "../src/clock.h"
#include <cassert>
namespace lc=h12_liquidation_clock;
int main(){
    lc::Timing timing;
    assert(timing.start_fits(1999,1)&&!timing.start_fits(2000,1));
    assert(timing.gold_fits(1999,0,0,true)&&!timing.gold_fits(2000,0,0,true));
    // Outside range, exclude the whole observed attack footprint. Within an
    // existing exposure, forbid moving closer while retaining an exit ring.
    assert(lc::terminal_exclusion_radius(7,3)==3);
    assert(lc::terminal_exclusion_radius(4,3)==3);
    assert(lc::terminal_exclusion_radius(3,3)==2);
    assert(lc::terminal_exclusion_radius(1,3)==0);
    return 0;
}
