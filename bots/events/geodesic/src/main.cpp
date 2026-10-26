#ifndef P11_GEODESIC_FRESH_ROUTE_TURN
#define P11_GEODESIC_FRESH_ROUTE_TURN 0
#endif
#include "policy.h"
#include "../../common/src/runtime.h"
int main(){return p11_runtime::run<p11_geodesic::Policy>();}
