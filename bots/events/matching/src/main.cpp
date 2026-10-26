#include "policy.h"
#define P11_COHERENT_FARM 1
#include "../../common/src/runtime.h"
int main(){return p11_runtime::run<p11_matching::Policy>();}
