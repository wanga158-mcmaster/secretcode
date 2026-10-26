#pragma once
#include "../../common/src/observe.h"
namespace p5::collateral {
enum class Arm { active, recovery_control };
struct Profile {
    Arm arm = Arm::active;
    static DraftRequest draft_request(const DraftContext&) {
        DraftRequest r; r.required_anchor = "heart_of_ruin";
        r.roles = {{{1,.01,.02,1,1},{1,.01,.02,1,1},{.8,.01,.2,2,1},
                    {1.1,.01,.05,2,2},{1.1,.01,.05,2,2}}};
        return r;
    }
};
} // namespace p5::collateral
