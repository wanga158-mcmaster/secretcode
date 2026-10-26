#pragma once
#include "../../tempo/src/policy.h"
#include "itinerary.h"

namespace p11_itinerary {
struct Policy : p11_tempo::Policy {
    static constexpr const char* name="p11-itinerary";
    static constexpr bool bounded_scan=true;
    static constexpr int window_size=3;
    static constexpr bool trace_scan=true;
    static constexpr bool route_select=true;
    using RouteJob=p11_itinerary::RouteJob;

    // Preview and partial-progress fallback retain Tempo's single-job score.
    // Only the completed-frontier final decision uses the short route model.
    static double screen(const p11::State&s,const p11::Candidate&c) {
        return p11_tempo::Policy::score(s,c);
    }
    static int select_route(const p11::State&s,std::span<const RouteJob>jobs) {
        return p11_itinerary::select_route(s,jobs);
    }
};
} // namespace p11_itinerary
