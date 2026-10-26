#pragma once
#include <cambc.h>
#include "spatial_support.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mosaic::domain {
struct Stamp {
    int epoch{-1},actor{-1};std::uint64_t revision{};
    bool matches(const Stamp& other) const {
        return epoch>=0&&actor>=0&&epoch==other.epoch&&actor==other.actor&&revision==other.revision;
    }
};
struct Geometry {
    cambc::vec2 origin{};
    int move_speed{},attack_range{},splash_radius{},passive{};
    float effect_reach{};
    bool complete{true};
    bool operator==(const Geometry&) const=default;
};
inline bool same_geometry(const Geometry& a,const Geometry& b){return a.complete&&b.complete&&a==b;}
// Every ability's geometric payload contributes, even when cooling or currently
// untargetable: a kill/reset may change readiness inside this owner input. Ground
// effects need not carry the camp unit-target bit. This envelope is conservative.
template<class Self,class Abilities> Geometry geometry(const Self& self,const Abilities& abilities) {
    Geometry out;out.origin=self.pos;out.move_speed=self.move_speed;out.attack_range=self.range;
    out.splash_radius=self.splash_radius;out.passive=static_cast<int>(self.passive);
    if(self.pos.x<0||self.pos.y<0||self.move_speed<0||self.range<0||self.splash_radius<0){out.complete=false;return out;}
    out.effect_reach=float(self.range)+float(self.splash_radius);
    for(const auto& ability:abilities) {
        if(!std::isfinite(ability.range)||!std::isfinite(ability.radius)||ability.range<0||ability.radius<0){out.complete=false;return out;}
        out.effect_reach=std::max(out.effect_reach,ability.range+ability.radius);
    }
    if(!std::isfinite(out.effect_reach))out.complete=false;
    return out;
}
inline bool same_layout(spatial::Token a,spatial::Token b) {
    return a.index==b.index&&a.generation==b.generation&&a.count==b.count;
}
inline std::int64_t distance(cambc::vec2 a,cambc::vec2 b) {
    const auto dx=std::int64_t(a.x)-b.x,dy=std::int64_t(a.y)-b.y;
    return std::max(dx<0?-dx:dx,dy<0?-dy:dy);
}
struct ObservationDomain {
    // Default fixtures/full collectors observe the existing complete field family.
    // Runtime may choose this only BEFORE performing a full dynamic member scan.
    bool limited{},complete{true};
    Stamp stamp;
    spatial::Token layout;
    Geometry geometry;
    std::array<std::uint64_t,(spatial::capacity+63)/64> members{};
    bool contains(std::size_t index) const {
        if(!limited)return complete;
        return complete&&index<spatial::capacity&&index<layout.count&&(members[index/64]&(std::uint64_t{1}<<(index%64)))!=0;
    }
    bool matches(Stamp expected,spatial::Token current,std::size_t count,const Geometry& actual) const {
        if(!limited)return complete;
        return complete&&stamp.matches(expected)&&same_layout(layout,current)&&layout.valid(count)&&same_geometry(geometry,actual);
    }
    bool rebind(Stamp next,spatial::Token current,std::size_t count,const Geometry& actual) {
        if(!limited){stamp=next;layout=current;geometry=actual;return complete;}
        if(!complete||next.epoch!=stamp.epoch||next.actor!=stamp.actor||next.revision<=stamp.revision||
            !same_layout(layout,current)||!layout.valid(count)||!same_geometry(geometry,actual)){complete=false;return false;}
        stamp=next;return true;
    }
    bool covers(const spatial::Selection& selection) const {
        if(!limited)return complete;
        if(!complete||!selection.complete||!layout.valid(layout.count))return false;
        for(auto i:selection)if(!contains(i))return false;
        return true;
    }
    bool covers_hits(cambc::vec2 center,int radius) const {
        if(!limited)return complete;
        if(!complete||!geometry.complete||radius<0||!layout.valid(layout.count)||
            double(distance(center,geometry.origin))+double(radius)>double(geometry.effect_reach))return false;
        return covers(layout.index->hits(center.x,center.y,radius));
    }
    bool covers_threats(cambc::vec2 after_position) const {
        if(!limited)return complete;
        if(!complete||!geometry.complete||!layout.valid(layout.count)||distance(after_position,geometry.origin)>geometry.move_speed)return false;
        return covers(layout.index->threats(after_position.x,after_position.y));
    }
};
// Called before choosing which dynamic rows to query. Failure remains a LIMITED,
// incomplete domain; caller may explicitly select a full scan before collection.
// Revocation after partial local reads must discard the packet, never relabel it.
inline ObservationDomain make_limited(Stamp stamp,spatial::Token layout,std::size_t count,const Geometry& geometry) {
    ObservationDomain out;out.limited=true;out.complete=false;out.stamp=stamp;out.layout=layout;out.geometry=geometry;
    if(!geometry.complete||!stamp.matches(stamp)||!layout.valid(count))return out;
    const double radius=std::ceil(double(geometry.effect_reach));
    if(!std::isfinite(radius)||radius<0||radius>double(std::numeric_limits<int>::max()))return out;
    const auto hits=layout.index->hits(geometry.origin.x,geometry.origin.y,static_cast<int>(radius));
    const auto threats=layout.index->threats(geometry.origin.x,geometry.origin.y,geometry.move_speed);
    if(!hits.complete||!threats.complete)return out;
    for(auto i:hits)out.members[i/64]|=std::uint64_t{1}<<(i%64);
    for(auto i:threats)out.members[i/64]|=std::uint64_t{1}<<(i%64);
    out.complete=true;return out;
}
} // namespace mosaic::domain
