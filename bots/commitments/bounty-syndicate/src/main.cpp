#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { return p9::live::run<p9::bounty_syndicate::Policy>(p9::bounty_syndicate::profile,p9::live::Arm::active); }
