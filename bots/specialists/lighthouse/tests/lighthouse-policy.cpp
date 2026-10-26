#include "../src/lighthouse-policy.h"
static_assert(lighthouse_policy::fork(4.f, 3.f, true));
static_assert(!lighthouse_policy::fork(4.f, 2.999f, true));
static_assert(!lighthouse_policy::fork(4.f, 3.f, false));
static_assert(!lighthouse_policy::fork(0.f, 0.f, true));
static_assert(lighthouse_policy::ward_allowed(0, false, false, 1));
static_assert(!lighthouse_policy::ward_allowed(1, false, false, 1));
static_assert(!lighthouse_policy::ward_allowed(0, true, false, 1));
static_assert(!lighthouse_policy::ward_allowed(0, false, true, 1));
static_assert(!lighthouse_policy::ward_allowed(0, false, false, 0));
static_assert(lighthouse_policy::hold(25, 10, true));
static_assert(!lighthouse_policy::hold(26, 10, true));
static_assert(!lighthouse_policy::hold(11, 10, false));
static_assert(lighthouse_policy::better(3, 8, 2, 1));
static_assert(lighthouse_policy::better(3, 8, 3, 9));
static_assert(!lighthouse_policy::better(3, 10, 3, 9));
static_assert(!lighthouse_policy::better(0, 1, 0, 9));
int main() {}
