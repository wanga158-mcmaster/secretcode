// Contract regression: completing Treasure replaces the old Monster stage.
#include "../src/policy.h"
using greenhouse::Stage;
static_assert(greenhouse::stage(0, 5, 0, 5) == Stage::first);
static_assert(greenhouse::stage(4, 5, 5, 5) == Stage::first);
static_assert(greenhouse::stage(5, 5, 0, 5) == Stage::jungle);
static_assert(greenhouse::stage(5, 5, 4, 5) == Stage::jungle);
static_assert(greenhouse::stage(5, 5, 5, 5) == Stage::power);
// Runtime combine counts govern stage completion rather than copied fives.
static_assert(greenhouse::stage(5, 6, 5, 5) == Stage::first);
static_assert(greenhouse::stage(6, 6, 3, 3) == Stage::power);
// Reward evidence must separate a complete upkeep window from capture gold.
static_assert(greenhouse::reward_only(43, 1, 1) == 42);
static_assert(greenhouse::reward_only(36, 1, 1) == 35);
static_assert(greenhouse::reward_only(45, 3, 1) == 42);
// This item experiment retains B0 banking rather than inheriting core01.
static_assert(!greenhouse::bank(12, 50, 2000, 5));
static_assert(greenhouse::bank(24, 50, 2000, 5));
static_assert(greenhouse::bank(1, 50, 2000, 4));
static_assert(!greenhouse::bank(1, 1899, 2000, 5));
static_assert(greenhouse::bank(1, 1900, 2000, 5));
static_assert(greenhouse::bank(1, 100, 200, 5));
static_assert(!greenhouse::bank(0, 2000, 2000, 1));
int main() {}
