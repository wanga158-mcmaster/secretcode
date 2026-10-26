// Banking boundary scenarios; SDK action/channel behavior is checked by the
// legal diagnostic and release match logs rather than engine mocks.
#include "../src/policy.h"
using fireflies::BankReason;
using fireflies::bank_reason;

static_assert(bank_reason(11, 100, 2000, 5, 12) == BankReason::none);
static_assert(bank_reason(12, 100, 2000, 5, 12) == BankReason::threshold);
static_assert(bank_reason(23, 100, 2000, 5, 12) == BankReason::threshold);
static_assert(bank_reason(12, 100, 2000, 5, 24) == BankReason::none);
static_assert(bank_reason(23, 100, 2000, 5, 24) == BankReason::none);
static_assert(bank_reason(24, 100, 2000, 5, 24) == BankReason::threshold);
static_assert(bank_reason(1, 100, 2000, 4, 12) == BankReason::nearby);
static_assert(bank_reason(1, 100, 2000, 4, 24) == BankReason::nearby);
static_assert(bank_reason(1, 1899, 2000, 5, 12) == BankReason::none);
static_assert(bank_reason(1, 1900, 2000, 5, 12) == BankReason::late);
static_assert(bank_reason(1, 1900, 2000, 5, 24) == BankReason::late);
static_assert(bank_reason(1, 900, 1000, 5, 12) == BankReason::late);
static_assert(bank_reason(1, 0, 50, 5, 12) == BankReason::late);
static_assert(bank_reason(0, 1999, 2000, 1, 12) == BankReason::none);
int main() {}
