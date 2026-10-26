#include "config.h"
#if HEURISTICS_ENABLED
#include "policy.h"
#include "runtime.h"
int main(){return h12_act_first::run();}
#else
#include "../../common/src/control.h"
#endif
