#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { return p9::live::run<p9::spent_response_exchange::Policy>(p9::spent_response_exchange::profile,p9::live::Arm::active); }
