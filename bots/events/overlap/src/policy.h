#pragma once
#include "../../tempo/src/policy.h"
#include "clock.h"

namespace p11_overlap {
// Keep Tempo's financial objective, bank rule and retreat rule. Only the
// candidate's capture cost carries the conditional action/phase correction;
// all reward, risk and travel observations retain their original values.
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-overlap";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static constexpr bool packed_clock=true;
    static constexpr bool capture_tribute=true;

    static p11::Candidate pack(const p11::State&,p11::Candidate candidate,
                               int attack_range,int origin_spawn_distance,
                               int members,bool fresh_allowances) {
        if(!fresh_allowances||members!=1||candidate.clear>=10000)return candidate;
        candidate.capture=std::max(0,candidate.capture+capture_phase_correction(
            candidate.ready,candidate.travel,candidate.clear,attack_range,origin_spawn_distance));
        return candidate;
    }
    static double score(const p11::State&s,const p11::Candidate&c) {
        // After a capture pays, the first altar-bound walk can occur in that
        // round's action phase. The adapter also starts tribute on arrival;
        // one travel action therefore adds no extra elapsed round. Keep gold
        // payout unchanged: it does not take the altar/tribute pipeline.
        auto bank_clock=c;
        if(bank_clock.gems>0)bank_clock.altar=std::max(0,bank_clock.altar-1);
        return p11_tempo::Policy::score(s,bank_clock);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {
        return score(s,c);
    }
    static bool can_overlap_capture_tribute(bool on_spawn,bool reward,bool contested,
                                           bool channel,int bag,int altar_distance,
                                           int submit_range,bool locked) {
        return p11_overlap::can_overlap_capture_tribute(on_spawn,reward,contested,
            channel,bag,altar_distance,submit_range,locked);
    }
#ifndef P11_OVERLAP_INLINE_SHORTLIST
#define P11_OVERLAP_INLINE_SHORTLIST 0
#endif
#if P11_OVERLAP_INLINE_SHORTLIST
    [[gnu::always_inline]] static inline std::vector<int> detail_frontier(
            const p11::State&s,std::span<const p11::Candidate>preview,int limit,
            int target,int fallback,int explore) {
        const int owned=target>=0?target:fallback;
        const int alternative=fallback>=0&&fallback!=owned?fallback:explore;
        [[clang::always_inline]]
        return p11::shortlist<Policy>(s,preview,limit,owned,alternative);
    }
#endif
};
} // namespace p11_overlap
