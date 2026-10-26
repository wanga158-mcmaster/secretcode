#include "../src/breakpoint.h"
#include <cstdio>

using breakpoint::Bundle;
static_assert(breakpoint::point_delta(0,5,5,1,5)==5);
static_assert(breakpoint::point_delta(4,1,5,1,5)==1);
static_assert(breakpoint::point_delta(4,1,5,1,6)==2);
static_assert(breakpoint::point_delta(2,2,5,1,6)==2);
static_assert(breakpoint::point_delta(0,5,5,1,6)==6);
// Least cost wins even when a more expensive bundle removes more basics.
static_assert(breakpoint::better(Bundle{0,1,30,13,40,1,true},Bundle{1,3,90,10,50,1,true},15));
static_assert(breakpoint::better(Bundle{1,1,30,12,45,1,true},Bundle{0,1,30,13,40,1,true},15));
static_assert(breakpoint::better(Bundle{0,1,30,13,40,1,true},Bundle{1,1,30,13,40,1,true},15));
static_assert(breakpoint::better(Bundle{0,1,30,13,40,1,true},Bundle{0,2,30,13,40,1,true},15));
static_assert(!breakpoint::better(Bundle{0,1,30,15,40,1,true},Bundle{},15));

int main() {
    unsigned failures=0;
    const auto check=[&](bool result,const char* label) { if (!result) { std::printf("FAIL %s\n",label); ++failures; } };
    check(breakpoint::hits({500},35)==15,"prepurchase isolated count");
    check(breakpoint::hits({500},40)==13,"one shard count");
    check(breakpoint::hits({500},50)==10,"three shard bulk count");
    check(breakpoint::hits({100,100},50)==4,"sum separate member ceilings");
    check(breakpoint::hits({100.00001f},50)==2,"epsilon integer boundary");
    check(breakpoint::hits({0},50)==0,"dead member contributes zero");
    check(breakpoint::hits({500},0)==breakpoint::unbounded,"zero damage guard");
    // Completing Power from four already-owned shards adds two new points,
    // whereas completing Monster adds one and its first passive exactly once.
    check(breakpoint::hits({500},(55+breakpoint::point_delta(4,1,5,1,6)*5))==8,"Power replacement count");
    check(breakpoint::hits({500},(55+breakpoint::point_delta(4,1,5,1,5)*5)*1.1f)==8,"first Monster passive count");
    std::printf("Breakpoint policy: %u failures\n",failures);
    return failures?1:0;
}
