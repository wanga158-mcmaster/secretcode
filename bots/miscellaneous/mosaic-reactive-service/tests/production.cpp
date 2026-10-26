#include "../src/production_model.h"
#include <cassert>
#include <cmath>
using namespace mosaic::model;
int main() {
    CaptureExpectation capture;
    capture.begin(2,25);
    assert(!capture.needs_observation(30,5)&&capture.needs_observation(31,5));
    capture.begin(2,29); // Repeated occupancy is not a new entry/receipt.
    assert(capture.needs_observation(31,5));
    capture.clear();capture.begin(2,32);
    assert(!capture.needs_observation(32,5));
    // Splash includes the primary exactly once, and never reaches a non-neighbor.
    std::vector<Member> group{{2,2,100,20,3},{3,2,100,20,3},{7,7,100,20,3}};
    basic(group,0,45,1);
    assert(group[0].hp==55 && group[1].hp==55 && group[2].hp==100);
    // Geometry changes useful labor even though total HP, stats, and member count match.
    std::vector<Member> dense{{2,2,180,20,3},{3,2,180,20,3},{2,3,180,20,3},{3,3,180,20,3}};
    const auto splash=forecast(dense,2,1,1,45,1);
    const auto single=forecast(dense,2,1,1,45,0);
    assert(splash.complete && splash.rounds==4 && !single.complete);
    // A ready reset packs two basics into the first owner turn; a cooling one cannot.
    const std::vector<Member> one{{2,2,90,20,3}};
    const auto ready=forecast(one,2,1,1,45,0,0);
    const auto cooling=forecast(one,2,1,1,45,0,1);
    assert(ready.complete && ready.rounds==1 && ready.damage==0);
    assert(cooling.complete && cooling.rounds==2 && cooling.damage==20);
    // Distant damaging members wake with the group but only reachable volleys count.
    std::vector<Member> distant{{2,2,90,20,3},{7,7,90,200,1}};
    const auto partial=forecast(distant,2,1,1,45,0);
    assert(!partial.complete && partial.damage==20);
}
