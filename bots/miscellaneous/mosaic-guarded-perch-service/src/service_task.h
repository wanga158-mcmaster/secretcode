#pragma once
// Proposal geometry only. A reader may use dated work estimates and bounded
// negative intervals; no returned target or frontier grants live authority.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

namespace mosaic::service_task {
struct Point {int x{},y{};};
struct Member {Point pos;float estimated_hp{};bool proposed{};};
inline int distance(Point a,Point b){return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y));}
// Work goals follow proposal eligibility; capture goals retain every physical
// member spawn, including members excluded by bounded negative work evidence.
// Reward membership does not require a work-reader query or grant live authority.
template<class Index,class Reader>
bool goal_member(Index index,bool reward,Reader read) {
    return reward||read(index).proposed;
}
template<class Indices,class Reader>
std::optional<std::size_t> nominate_basic(const Indices& indices,Point origin,int range,float hit,Reader read) {
    std::optional<std::size_t> best;float value=-1;
    for(const auto index:indices) {
        const auto member=read(index);
        if(!member.proposed||distance(origin,member.pos)>range)continue;
        const float useful=std::min(member.estimated_hp,std::max(0.0f,hit));
        if(useful>value){value=useful;best=index;}
    }
    return best;
}
template<class Indices,class Reader>
bool in_frontier(const Indices& indices,Point origin,int range,Reader read) {
    return std::any_of(indices.begin(),indices.end(),[&](auto index){
        const auto member=read(index);return member.proposed&&distance(origin,member.pos)<=range;
    });
}
} // namespace mosaic::service_task
