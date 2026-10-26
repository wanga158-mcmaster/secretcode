#pragma once
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <span>

namespace h12_active_surveyor {
struct RoutePoint {
    int x=0,y=0;
    friend bool operator==(const RoutePoint&,const RoutePoint&)=default;
};

// A bounded movement continuation when a full distance field is unaffordable.
// Inputs are already-fresh legal neighbors and goals; no perception, path
// search or sideways/away step is performed by this selector.
inline std::optional<RoutePoint> choose_step(int fuel,RoutePoint origin,
        std::span<const RoutePoint> goals,std::span<const RoutePoint> legal) {
    if(fuel<12000||goals.empty()||goals.size()>128||legal.empty()||legal.size()>9)
        return std::nullopt;
    const auto distance=[](RoutePoint a,RoutePoint b) {
        return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));
    };
    auto goal=goals.front();
    int best_distance=distance(origin,goal);
    for(const auto candidate:goals) {
        const int d=distance(origin,candidate);
        if(d<best_distance){goal=candidate;best_distance=d;}
    }
    std::optional<RoutePoint> step;
    for(const auto candidate:legal) {
        const int stride=distance(origin,candidate);
        if(stride!=1)continue;
        const int d=distance(candidate,goal);
        if(d<best_distance){step=candidate;best_distance=d;}
    }
    return step;
}
}
