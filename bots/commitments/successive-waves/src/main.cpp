#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { p9::live::run<p9::successive_waves::Policy>(p9::successive_waves::profile, p9::live::Arm::active); }
