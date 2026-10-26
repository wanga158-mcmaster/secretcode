#pragma once
#include "../../tempo/src/policy.h"
#include "triage.h"
#include "recovery.h"

#ifndef P11_TRIAGE_POSTCAST_BASIC
#define P11_TRIAGE_POSTCAST_BASIC 0
#endif

namespace p11_triage {
// Keep Tempo's financial, bank and retreat policies. The runtime may first
// exercise the narrow sufficient-tribute option during full-health recovery.
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-triage";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static constexpr bool target_horizon=true;
#if P11_TRIAGE_POSTCAST_BASIC
    static constexpr bool triage_postcast_basic=true;
#endif
    using RecoveryBankInput=p11_triage::RecoveryBankInput;
    static constexpr bool can_bank_before_recall(const RecoveryBankInput& input) {
        return p11_triage::can_bank_before_recall(input);
    }
    using FocusJob=p11_triage::FocusJob;
    static int choose_focus(std::span<const FocusJob> jobs) {
        return p11_triage::choose_focus(jobs);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {
        return p11_tempo::Policy::score(s,c);
    }
};
} // namespace p11_triage
