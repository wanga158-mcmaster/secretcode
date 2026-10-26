#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { return p9::live::run<p9::capture_fork::Policy>(p9::capture_fork::profile,p9::live::Arm::active); }
