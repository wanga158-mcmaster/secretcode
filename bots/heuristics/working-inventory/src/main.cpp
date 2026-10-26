#include "config.h"
#if HEURISTICS_ENABLED
#include "policy.h"
#include "runtime.h"
int main(){return h12_inventory_runtime::run<h12_working_inventory::Policy>();}
#else
#include "../../common/src/control.h"
#endif
