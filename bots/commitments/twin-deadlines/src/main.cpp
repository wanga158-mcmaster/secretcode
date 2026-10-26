#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { return p9::live::run<p9::twin_deadlines::Policy>(p9::twin_deadlines::profile,p9::live::Arm::active); }
