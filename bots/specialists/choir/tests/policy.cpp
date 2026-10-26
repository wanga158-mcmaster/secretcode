#include "../src/mission-policy.h"
static_assert(!choir::deadline(48));
static_assert(choir::deadline(49));
static_assert(!choir::stalled(8,1));
static_assert(choir::stalled(9,1));
static_assert(!choir::stalled(1,9));
static_assert(choir::release(true,true,true));
static_assert(!choir::release(false,true,true));
static_assert(!choir::release(true,false,true));
static_assert(!choir::release(true,true,false));
static_assert(!choir::release(false,false,false));
static_assert(choir::complete_reward(0,0,48,0,48,0));
static_assert(choir::complete_reward(12,3,60,38,48,35));
static_assert(!choir::complete_reward(0,0,24,0,48,0));
static_assert(!choir::complete_reward(0,0,48,17,48,35));
static_assert(!choir::complete_reward(12,0,0,0,48,0));
static_assert(!choir::complete_reward(0,35,48,0,48,35));
int main() {}
