#include "native_profile.h"
#include "../../common/src/runtime.h"

#ifndef P7_CONTROL
#define P7_CONTROL 0
#endif

int main() {
    return p7::live::run(p7::profile::design, P7_CONTROL != 0,
                         p7::profile::draft_order);
}

