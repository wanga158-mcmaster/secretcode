#pragma once
namespace reservoir {
constexpr int lead_required = 48;
constexpr unsigned maximum_approach = 8, maximum_hold = 32;
constexpr bool due_window(unsigned now, unsigned due, int travel) {
    return travel >= 0 && travel <= 8 && due > now
        && due-now >= static_cast<unsigned>(travel+1)
        && due-now <= static_cast<unsigned>(travel+2);
}
constexpr bool enabled_by_score(long long lead, unsigned bag) { return lead >= lead_required && bag == 0; }
constexpr unsigned eligibility_start(unsigned due, unsigned occupied) { return due > occupied ? due : occupied; }
// Early occupancy precedes start; unsigned subtraction must be guarded.
constexpr bool elapsed_hold_limit(unsigned now, unsigned start) {
    return now >= start && now-start >= maximum_hold;
}
constexpr bool eligible_phase(unsigned now, unsigned lastOccupied, unsigned start, int timer) {
    return now >= start && lastOccupied+1 == now && timer == 0;
}
}
