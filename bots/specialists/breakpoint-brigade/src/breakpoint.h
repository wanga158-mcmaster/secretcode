#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace breakpoint {
inline constexpr float epsilon=0.0001f;
inline constexpr unsigned unbounded=1000000;
inline unsigned hits(const std::vector<float>& health,float damage) {
    if (!(damage>epsilon)) return unbounded;
    unsigned sum=0;
    for (float hp:health) {
        if (hp<=epsilon) continue;
        const float count=std::ceil((hp-epsilon)/damage);
        if (count>=unbounded-sum) return unbounded;
        sum+=static_cast<unsigned>(std::max(1.0f,count));
    }
    return sum;
}
// Current Strength already contains the existing ingredient points. A new
// emblem replaces those ingredients: subtract them before adding its points.
constexpr float point_delta(unsigned owned,unsigned count,unsigned boundary,
                            int shard_points,int result_points) {
    return owned+count==boundary ? result_points-static_cast<int>(owned)*shard_points
                                 : static_cast<int>(count)*shard_points;
}
struct Bundle {
    unsigned family=0,count=0,cost=0,after=unbounded;
    float strength=0,multiplier=1;
    bool valid=false;
};
constexpr bool better(const Bundle& candidate,const Bundle& incumbent,unsigned before) {
    if (!candidate.valid || candidate.after>=before) return false;
    if (!incumbent.valid) return true;
    if (candidate.cost!=incumbent.cost) return candidate.cost<incumbent.cost;
    if (candidate.after!=incumbent.after) return candidate.after<incumbent.after;
    if (candidate.family!=incumbent.family) return candidate.family<incumbent.family;
    return candidate.count<incumbent.count;
}
}
