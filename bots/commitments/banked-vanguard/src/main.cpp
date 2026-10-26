#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { return p9::live::run<p9::banked_vanguard::Policy>(p9::banked_vanguard::profile,p9::live::Arm::active); }
