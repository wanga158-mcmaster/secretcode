// Production group admission must examine every member, including groups whose
// first (or chosen anchor) spawn is across a terrain boundary.
#include <array>
#include "../src/policy.h"

constexpr std::array split_group{false, true, false};
constexpr std::array unreachable_group{false, false, false};
constexpr std::array empty_group = std::array<bool, 0>{};
static_assert(fireflies::any_member_reachable(split_group, [](bool reachable) { return reachable; }));
static_assert(!fireflies::any_member_reachable(unreachable_group, [](bool reachable) { return reachable; }));
static_assert(!fireflies::any_member_reachable(empty_group, [](bool reachable) { return reachable; }));
int main() {}
