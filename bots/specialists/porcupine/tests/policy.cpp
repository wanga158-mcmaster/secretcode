#include "../src/policy.h"
#include <cassert>
int main() {
    using namespace porcupine;
    Estimate e{2,60,40,100,false,true};
    assert(qualifies(e,900,900,40,150));
    assert(!qualifies(Estimate{1,30,20,10,false,true},900,900,40,150));
    assert(!qualifies(Estimate{2,10,40,10,false,true},900,900,40,150));
    assert(!qualifies(Estimate{2,60,40,10,true,true},900,900,40,150));
    assert(!qualifies(Estimate{2,60,40,10,false,false},900,900,40,150));
    assert(!qualifies(Estimate{3,60,60,4050,false,true},900,900,40,100));
    assert(qualifies(Estimate{2,20,40,0,false,true},305,900,40,150)); // ratio=.5, health reserve exactly225
    assert(!qualifies(Estimate{2,20,40,0,false,true},304.9f,900,40,150));
    assert(!qualifies(Estimate{2,19.99f,40,0,false,true},900,900,40,150));
    const Estimate a{2,60,40,100,false,true}, b{2,60,40,100,false,true};
    assert(better_group(a,2,4,b,1,1));
    assert(better_group(a,2,4,b,2,5));
    assert(!better_group(a,2,5,b,2,4));
    assert(better_tile(Estimate{3,60,60,1,false,true},10,Estimate{2,60,40,1,false,true},9));
    assert(better_tile(a,10,Estimate{2,60,50,100,false,true},1));
    assert(better_tile(a,1,b,2));
    assert(!fundamentals::should_bank(23,300,2000,5));
    assert(fundamentals::should_bank(24,300,2000,100));
    assert(fundamentals::should_bank(1,300,2000,4));
    assert(fundamentals::should_bank(1,1900,2000,100));
    assert(!fundamentals::should_bank(0,2000,2000,0));
    assert(porcupine::close(30,30.01f));
    assert(!porcupine::close(30,33));
    assert(fundamentals::should_retreat(315,900,60));
    assert(!fundamentals::should_retreat(720,900,60));
    return 0;
}
