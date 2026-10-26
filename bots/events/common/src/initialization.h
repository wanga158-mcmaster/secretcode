#pragma once

namespace p11::initialization {
inline constexpr int setup_reserve = 60000;
inline constexpr int group_reserve = 30000;
inline constexpr int member_reserve = 15000;
inline constexpr int close_reserve = 5000;
inline constexpr int finish_reserve = 25000;

// Callbacks own public metadata and its cursors. Each callback is atomic with
// respect to a voluntary yield, and must fit the reserve at its boundary.
// In particular, public group enumeration and spawn-mask construction remain
// SDK operations whose costs need native map coverage; this driver cannot
// suspend inside them. No callback may execute a gameplay action.
template<class Fuel, class Complete, class Started, class Start, class GroupPending,
         class GroupOpen, class OpenGroup, class MemberPending, class AddMember,
         class CloseGroup, class Finish>
struct Work {
    Fuel fuel;
    Complete complete;
    Started started;
    Start start;
    GroupPending group_pending;
    GroupOpen group_open;
    OpenGroup open_group;
    MemberPending member_pending;
    AddMember add_member;
    CloseGroup close_group;
    Finish finish;
};

template<class Work> bool resume(Work& work) {
    if (work.complete()) return true;
    if (!work.started()) {
        if (work.fuel() < setup_reserve) return false;
        work.start();
    }
    while (work.group_pending()) {
        if (!work.group_open()) {
            if (work.fuel() < group_reserve) return false;
            work.open_group();
        }
        while (work.member_pending()) {
            if (work.fuel() < member_reserve) return false;
            work.add_member();
        }
        // A final member may use its entire allowance. Preserve the open
        // group at member-end and close it on a later turn when necessary.
        if (work.fuel() < close_reserve) return false;
        work.close_group();
    }
    if (work.fuel() < finish_reserve) return false;
    work.finish();
    return true;
}
} // namespace p11::initialization
