#define CAPACITY_MEMBER_CLOCK 1
#include "capacity.h"
#include <cassert>
using namespace mb;
int main() {
    assert(capacity::member_clock(0,0,0,0)==0);
    assert(capacity::member_clock(100,100,0,10)==1<<20);
    // Frozen native g3 model: old aggregate clock six, member clock eight, actual clear longer.
    assert(capacity::member_clock(3180,1350,548.4375f,178.125f)==8);
    // Focused99 plus area1 can clear HP[100,1] in one fixed-rate unit. Capping only the area
    // component by101/100 would incorrectly demand two. Whole-member capability preserves it.
    assert(capacity::member_clock(101,100,101,100)==1);
    capacity::Job j;j.group=3;j.value=134;j.fight={3180,0,0};j.largest_hp=1350;
    j.dps[0]=174.375f;j.dps[1]=374.0625f;j.member_dps[0]=71.25f;j.member_dps[1]=106.875f;
    std::array<capacity::Body,8> bodies;
    bodies[0]={30,500,1,-1,true};bodies[1]={95,750,1,-1,true};
    auto quoted=capacity::quote(j,bodies,2,3);
    assert(quoted.group==3 && quoted.clear==8 && quoted.finish==13);
    j.fight={};j.largest_hp=0;
    assert(capacity::quote(j,bodies,2,1).clear==0);
}
