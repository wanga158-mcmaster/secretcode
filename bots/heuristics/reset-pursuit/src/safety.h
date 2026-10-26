#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <tuple>

namespace h12_reset_pursuit {
struct SafetyPoint { int x=0,y=0; };
inline int safety_distance(SafetyPoint a,SafetyPoint b) {
    return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));
}
// One personally observed future enemy action opportunity. Burst is the largest
// ready/next-tick cast, not the sum of mutually exclusive abilities. Raw outgoing
// damage is conservative about our shields; it is not an exact combat forecast.
struct VisibleThreat {
    SafetyPoint position{};
    int basic_range=0,burst_range=0;
    float basic=0,burst=0;
};
// No-contact turns need no observation work. On the first turn after contact
// disappears, clear both the actor-local threat list and its derived route mask
// exactly once before taking the inactive fast path on later turns.
template<class Threats,class ClearMask>
bool begin_visible_observation(bool has_enemies,bool&active,Threats&threats,ClearMask clear_mask) {
    if(!has_enemies&&!active)return false;
    threats.clear();clear_mask();active=false;
    return has_enemies;
}
inline float visible_exposure(std::span<const VisibleThreat> threats,SafetyPoint p) {
    float damage=0;
    for(const auto&t:threats) {
        const int d=safety_distance(p,t.position);
        if(d<=t.basic_range)damage+=t.basic;
        if(d<=t.burst_range)damage+=t.burst;
    }
    return damage;
}
inline int visible_clearance(std::span<const VisibleThreat> threats,SafetyPoint p) {
    int result=10000;
    for(const auto&t:threats)result=std::min(result,safety_distance(p,t.position)
        -std::max(t.basic_range,t.burst_range));
    return result;
}
template<class Unsafe>
bool visible_route_safe(std::span<const VisibleThreat> threats,SafetyPoint p,Unsafe unsafe) {
    return !unsafe(visible_exposure(threats,p));
}
template<class Unsafe,class Draw>
void draw_visible_danger(std::span<const VisibleThreat> threats,Unsafe unsafe,Draw draw) {
    for(const auto&t:threats) {
        if(t.basic>0&&unsafe(t.basic))draw(t.position,t.basic_range);
        if(t.burst>0&&unsafe(t.burst))draw(t.position,t.burst_range);
        if(t.basic>0&&t.burst>0&&unsafe(t.basic+t.burst))
            draw(t.position,std::min(t.basic_range,t.burst_range));
    }
}
// The actual recovery caller supplies only legal currently observed step tiles.
// Minimise total exposure first, then leave the nearest threat envelope; this
// prevents the old sum-of-distances rule escaping one pursuer toward another.
inline SafetyPoint visible_escape(SafetyPoint origin,std::span<const SafetyPoint> legal,
                                  std::span<const VisibleThreat> threats,SafetyPoint home) {
    auto key=[&](SafetyPoint p){return std::tuple{visible_exposure(threats,p),
        -visible_clearance(threats,p),safety_distance(p,home)};};
    auto best=origin;
    for(const auto p:legal)if(key(p)<key(best))best=p;
    return best;
}
} // namespace h12_reset_pursuit
