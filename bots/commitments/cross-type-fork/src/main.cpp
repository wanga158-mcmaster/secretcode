#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { return p9::live::run<p9::cross_type_fork::Policy>(p9::cross_type_fork::profile,p9::live::Arm::active); }
