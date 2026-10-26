#ifndef P11_EARLY_FALLBACK_DETAIL
#define P11_EARLY_FALLBACK_DETAIL 0
#endif
#include "policy.h"
#include "../../common/src/runtime.h"
int main(){return p11_runtime::run<p11_overlap::Policy>();}
