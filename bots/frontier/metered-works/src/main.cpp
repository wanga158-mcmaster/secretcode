#include "policy.h"
#include "c-policy.h"
#include "../../common/src/runtime.h"
int main(){return p10::run<p10_c_metered::CPolicy>(metered_works::profile,p10::Arm::active);}
