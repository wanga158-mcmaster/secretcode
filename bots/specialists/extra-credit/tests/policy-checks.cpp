#include "../src/extra-credit-policy.h"
static_assert(extra_credit::owns(0,true));
static_assert(!extra_credit::owns(0,false));
static_assert(extra_credit::owns(1,false));
static_assert(!extra_credit::owns(1,true));
static_assert(!extra_credit::owns(-1,true));
static_assert(extra_credit::owns(999,false));
static_assert(extra_credit::may_enter(70,100,10,20));
static_assert(!extra_credit::may_enter(69.99f,100,10,20));
static_assert(!extra_credit::may_enter(100,100,10,20.01f));
static_assert(extra_credit::may_enter(1,100,0,10000));
static_assert(!extra_credit::may_enter(100,0,10,0));
static_assert(extra_credit::retreat(65,100,false));
static_assert(!extra_credit::retreat(65.01f,100,false));
static_assert(extra_credit::retreat(100,100,true));
static_assert(!extra_credit::retreat(0,0,false));
int main() {}
