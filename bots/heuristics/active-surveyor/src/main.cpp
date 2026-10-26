#include "config.h"
#if HEURISTICS_ENABLED
#include "policy.h"
#include "runtime.h"
int main(){return heuristics_runtime::run<h12_active_surveyor::Policy>();}
#else
#include "../../common/src/control.h"
#endif
