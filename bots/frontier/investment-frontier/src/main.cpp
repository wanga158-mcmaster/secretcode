#include "c-policy.h"
#include "../../common/src/runtime.h"
#ifndef P10_INVESTMENT_CONTROL
#define P10_INVESTMENT_CONTROL 0
#endif
int main(){return p10::run<p10_c11::CPolicy>(p10::investment::profile,P10_INVESTMENT_CONTROL?p10::Arm::control:p10::Arm::active);}
