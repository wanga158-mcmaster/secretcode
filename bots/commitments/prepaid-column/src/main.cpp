#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { return p9::live::run<p9::prepaid_column::Policy>(p9::prepaid_column::profile,p9::live::Arm::active); }
