#pragma once
#include "contingency.h"

namespace p11_contingency {
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-contingency";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static constexpr bool route_select=true;
    static constexpr bool private_wait=true;
    using RouteJob=p11_contingency::RouteJob;

    static double screen(const p11::State&s,const p11::Candidate&c) {
        return p11_tempo::Policy::score(s,c);
    }
    static int select_route(const p11::State&s,std::span<const RouteJob>jobs) {
        return p11_contingency::select_route(s,jobs);
    }
};
} // namespace p11_contingency
