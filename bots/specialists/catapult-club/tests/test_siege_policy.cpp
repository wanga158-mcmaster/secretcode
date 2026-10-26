#include "../src/siege-policy.h"

// Reservation boundaries and capture phase timing matter more than a build:
// a large route must never underflow the end-of-match horizon.
static_assert(siege_policy::fits(1900, 36, 2000));
static_assert(!siege_policy::fits(1900, 37, 2000));
static_assert(!siege_policy::fits(2001, 0, 2000));
static_assert(!siege_policy::fits(0, 0, 63));
static_assert(!siege_policy::fits(100, 0xffffffffu, 2000));
static_assert(siege_policy::casts(500, 220) == 3);
static_assert(siege_policy::casts(440, 220) == 2);
static_assert(siege_policy::casts(0, 220) == 0);
static_assert(siege_policy::casts(5, 0) == 5);
static_assert(siege_policy::work(3, 8, 4, 4, 5, 10, 3) == 51);
static_assert(siege_policy::work(3, 0, 4, 4, 5, 10, 3) == 30);
int main() {}
