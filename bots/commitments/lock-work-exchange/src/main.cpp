#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { p9::live::run<p9::lock_work_exchange::Policy>(p9::lock_work_exchange::profile, p9::live::Arm::active); }
