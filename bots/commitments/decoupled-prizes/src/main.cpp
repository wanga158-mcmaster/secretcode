#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { return p9::live::run<p9::decoupled_prizes::Policy>(p9::decoupled_prizes::profile,p9::live::Arm::active); }
