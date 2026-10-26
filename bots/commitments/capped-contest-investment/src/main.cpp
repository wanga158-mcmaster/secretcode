#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { p9::live::run<p9::capped_contest_investment::Policy>(p9::capped_contest_investment::profile, p9::live::Arm::active); }
