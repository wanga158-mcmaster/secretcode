#include "../src/recovery.h"
#include <array>
#include <cassert>

int main() {
    using namespace h12_active_surveyor;
    const std::array<RecoveryThreat,2> opponents{{{{33,53},9,150},{{32,53},9,150}}};
    // The native two-tile plateau gives no strict clearance improvement.
    const std::array<RoutePoint,2> plateau{{{26,52},{26,53}}};
    assert(!recovery_step({26,53},plateau,opponents));
    assert(!recovery_step({26,52},plateau,opponents));
    // A real clearance improvement remains admissible, with stable ties.
    const std::array<RoutePoint,4> exits{{{26,52},{26,53},{25,52},{25,53}}};
    assert((recovery_step({26,53},exits,opponents)==RoutePoint{25,52}));
    const std::array<RoutePoint,1> approach{{{27,53}}};
    assert(!recovery_step({26,53},approach,opponents));
    const std::array<RoutePoint,1> nonlocal{{{24,53}}};
    assert(!recovery_step({26,53},nonlocal,opponents));
    assert(!recovery_step({26,53},exits,{}));
    const std::array<RecoveryThreat,1> saturated{{{{40,53},6,45}}};
    assert(!recovery_step({26,53},exits,saturated));
}
