#include "config.h"
#if HEURISTICS_ENABLED
#include "policy.h"
#if SATISFICER_ROUTE_REFUSAL
#include "runtime.h"
#else
#include "../../common/src/runtime.h"
#endif
int main(){return heuristics_runtime::run<h12_satisficer::Policy>();}
#else
#include "../../common/src/control.h"
#endif
