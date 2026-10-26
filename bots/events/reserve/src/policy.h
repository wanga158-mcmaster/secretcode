#pragma once
#include "../../tempo/src/policy.h"
#include "reserve.h"

namespace p11_reserve {
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-reserve";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;

    static double score(const p11::State&s,const p11::Candidate&c) {
        const int tax=recovery_ticks(s,c);
        if(tax<0||c.clear>std::numeric_limits<int>::max()-tax)
            return -std::numeric_limits<double>::infinity();
        auto charged=c;charged.clear+=tax;
        return p11_tempo::Policy::score(s,charged);
    }
    static double screen(const p11::State&s,const p11::Candidate&c) {
        return score(s,c);
    }
};
} // namespace p11_reserve
