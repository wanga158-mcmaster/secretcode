#include <array>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstdio>
#define MOSAIC_EXECUTABLE_CONTROLLER_PURE_ONLY
#include "../src/executable_controller.h"
int main(){
    assert(executable_owner_selection(true,true)==ExecutableOwnerSelection::pending_feedback);
    assert(executable_owner_selection(true,false)==ExecutableOwnerSelection::pending_feedback);
    assert(executable_owner_selection(false,true)==ExecutableOwnerSelection::retained_legacy);
    assert(executable_owner_selection(false,false)==ExecutableOwnerSelection::fresh_policy);
    ExecutableCaptureLeases<4> leases{};
    assert(leases.reward(0,4,true));assert(leases.waited(0,true));assert(leases.waited(0,true));
    assert(leases.remaining(0)==2);assert(leases.reward(1,3,true));assert(leases.reward(0,4,true));
    assert(leases.remaining(0)==2);assert(leases.waited(0,true));assert(leases.waited(0,true));
    assert(!leases.waited(0,true));assert(leases.reward(1,3,true));assert(leases.reward(0,4,true));
    assert(leases.remaining(0)==0);assert(!leases.alive(0,false));assert(leases.remaining(0)==0);
    assert(leases.alive(0,true));assert(leases.reward(0,4,true));assert(leases.remaining(0)==4);
    assert(!leases.reward(4,4,true));assert(!leases.alive(4,true));assert(!leases.waited(4,true));
    assert(!leases.reward(2,4,false));assert(!leases.waited(2,true));assert(leases.remaining(2)==0);
    assert(!leases.reward(2,0,true));assert(leases.reward(2,2,true));assert(!leases.waited(2,false));
    assert(leases.remaining(2)==2);assert(leases.reward(0,9,true));assert(leases.remaining(0)==4);
    ExecutableGroupProbes<3> probes{};
    assert(probes.begin(0,2,7,3,4,false)==-1);
    assert(probes.begin(0,2,7,3,4,true)==0);
    assert(!probes.observed(0,1,true));assert(!probes.observed(0,0,false));
    assert(probes.observed(0,0,true));assert(probes.begin(0,2,7,3,4,true)==1);
    assert(probes.begin(1,1,7,3,4,true)==0);assert(probes.observed(1,0,true));
    assert(probes.observed(0,1,true));assert(probes.begin(0,2,7,3,4,true)==-1);
    assert(probes.begin(1,1,7,3,4,true)==-1);assert(!probes.observed(0,1,true));
    // Renomination/group churn and repeated Alive do not reset a paid pass.
    assert(probes.begin(0,2,7,3,4,true)==-1);probes.changed_lifecycle(0,false);
    assert(probes.begin(0,2,7,3,4,true)==-1);
    // An actual new pose/episode/lifecycle supplies a new ingress basis.
    assert(probes.begin(0,2,7,4,4,true)==0);assert(probes.observed(0,0,true));
    assert(probes.begin(0,2,8,4,4,true)==0);probes.changed_lifecycle(0,true);
    assert(probes.begin(0,2,8,4,4,true)==0);assert(probes.begin(3,1,1,0,0,true)==-1);
    std::puts("executable-controller: capture lease and bounded actual group/member continuation passed");
}
