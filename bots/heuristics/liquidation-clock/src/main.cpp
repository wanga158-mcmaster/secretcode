#include "config.h"
#if HEURISTICS_ENABLED
#include "runtime.h"
int main(){return h12_liquidation_clock::run();}
#else
#include "../../common/src/control.h"
#endif
