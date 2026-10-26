#include "config.h"
#if HEURISTICS_ENABLED
#include "policy.h"
#include "runtime.h"
int main(){return h12_reset_runtime::run<h12_reset_pursuit::Policy>();}
#else
#include "../../common/src/control.h"
#endif
