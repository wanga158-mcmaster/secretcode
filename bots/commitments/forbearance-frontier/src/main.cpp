#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { p9::live::run<p9::forbearance_frontier::Policy>(p9::forbearance_frontier::profile, p9::live::Arm::active); }
