#pragma once

namespace wb {

// One ordinary basic and at most one reset basic. Selection happens after every preceding
// action, because a cast or basic can kill its target without spending the next allowance.
// The callbacks query the live SDK in production; fixtures drive the same scheduler.
template<class Ready, class Choose, class Strike, class Reset>
bool camp_basic_sequence(Ready ready, Choose choose, Strike strike, Reset reset) {
    if (!ready()) return false;
    int target = choose();
    if (target < 0 || !strike(target)) return false;
    if (!ready() && reset()) {
        target = choose();
        if (target >= 0 && ready()) strike(target);
    }
    return true;
}

} // namespace wb
