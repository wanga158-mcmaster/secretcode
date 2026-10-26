#include "live-policy.h"
#include "../../common/src/runtime.h"
int main() { p9::live::run<p9::critical_mass_conversion::Policy>(p9::critical_mass_conversion::profile, p9::live::Arm::active); }
