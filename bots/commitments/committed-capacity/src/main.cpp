#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { p9::live::run<p9::committed_capacity::Policy>(p9::committed_capacity::profile, p9::live::Arm::active); }
