#pragma once
#include "../../common/src/observe.h"
namespace p5::runoff {
enum class Arm { finite_harvest, bank_each_control };
struct Profile {
    Arm arm=Arm::finite_harvest;
    static DraftRequest draft_request(const DraftContext&){
        DraftRequest r;r.required_anchor="marcella";
        r.roles={{{1,.02,.05,1,1},{1,.02,.05,1,1},{.8,.01,.2,2,1},
                  {1.1,.01,.05,2,2},{1.1,.01,.05,2,2}}};return r;
    }
};
} // namespace p5::runoff
