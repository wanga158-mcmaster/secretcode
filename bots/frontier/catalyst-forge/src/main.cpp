#include "c-policy.h"
#include "policy.h"
#include "../../common/src/runtime.h"
template class p10::Runtime<p10_c_forge::CPolicy>;
int main(){return p10::run<p10_c_forge::CPolicy>(catalyst_forge::profile,p10::Arm::active);}
