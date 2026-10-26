#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { return p9::live::run<p9::dividend_redeployment::Policy>(p9::dividend_redeployment::profile,p9::live::Arm::active); }
