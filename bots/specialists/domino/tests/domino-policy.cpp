#include "../src/policy.h"

// Case-1 cooling kills, case-2 just-used-slot refresh, and ordinary fallback.
static_assert(domino::sequence(true, 7, false, true, true) == 1);
static_assert(domino::sequence(true, 1, false, true, false) == 1);
static_assert(domino::sequence(true, 0, true, true, true) == 2);
static_assert(domino::sequence(true, 0, true, true, false) == 3);
static_assert(domino::sequence(true, 0, true, false, true) == 3);
static_assert(domino::sequence(true, 0, false, true, true) == 3);
static_assert(domino::sequence(true, 3, false, false, true) == 3);
static_assert(domino::sequence(false, 0, true, true, true) == 0);
static_assert(domino::sequence(false, 4, false, true, true) == 0);
static_assert(domino::lethal(75, 75));
static_assert(domino::lethal(75.00005f, 75));
static_assert(!domino::lethal(75.001f, 75));
static_assert(!domino::lethal(0, 75));
static_assert(!domino::lethal(1, 0));
static_assert(!domino::lethal(-1, 75));
static_assert(domino::precedes(120, 900, 121, 800));
static_assert(domino::precedes(120, 800, 120, 900));
static_assert(!domino::precedes(120, 900, 120, 800));
// A refresh cannot earn utility credit once the replaced natural cooldown
// would already have ended, or before the confirmed kill happened.
static_assert(domino::refresh_still_useful(100, 8, 101));
static_assert(domino::refresh_still_useful(100, 8, 100));
static_assert(domino::refresh_still_useful(100, 8, 107));
static_assert(!domino::refresh_still_useful(100, 8, 108));
static_assert(!domino::refresh_still_useful(100, 1, 101));
static_assert(!domino::refresh_still_useful(100, 0, 100));
static_assert(!domino::refresh_still_useful(100, 8, 99));
// The common bank policy remains 24 in both arms, with engine-relative late
// and nearby-altar exceptions.
static_assert(banking::bank_reason(22, 10, 2000, 17, 24) == banking::BankReason::none);
static_assert(banking::bank_reason(24, 10, 2000, 17, 24) == banking::BankReason::threshold);
static_assert(banking::bank_reason(1, 1900, 2000, 17, 24) == banking::BankReason::late);
static_assert(banking::bank_reason(1, 10, 2000, 4, 24) == banking::BankReason::nearby);
int main() {}
