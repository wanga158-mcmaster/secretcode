#include "config.h"
#if HEURISTICS_ENABLED
#include "policy.h"
#include "runtime.h"
int main(){return h12_capability_portfolio::run();}
#else
#include "../../common/src/control.h"
#endif
