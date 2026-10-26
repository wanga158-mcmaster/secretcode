#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace wb::capital::exit {
struct Point {int x{},y{};};
struct Source {
    int id{-1},x{},y{},range{};
    float damage{};
    bool alive{},awake{},will_wake{};
};
struct Quote {
    bool supported{},affordable{},survives{},ends_quiet{};
    float damage{},hp_after{},shield_after{};
    unsigned response_intervals{},hits{};
};
inline constexpr std::size_t max_sources=500,max_occupancy=129;
inline constexpr unsigned max_refusals=8;
namespace detail {
inline std::int64_t distance(Point a,Point b) {
    auto x=std::int64_t(a.x)-b.x,y=std::int64_t(a.y)-b.y;
    return std::max(x<0?-x:x,y<0?-y:y);
}
inline bool hits(const Source& s,Point p) {
    return distance({s.x,s.y},p)<=s.range;
}
}
// Conditional cost of an already proposed basic and finite ordinary egress.
// occupancy[0] is the attack origin at the NEXT camp round-start; subsequent
// points are actual one-step waypoints at later round-starts, final point quiet.
// All selected/collaterally woken group members must be supplied will_wake.
// Already-awake outside sources are included; sleeping outside sources are
// deliberately excluded under this limited scenario, not proved harmless.
// Source order is the caller's actual camp actor order. damage is the already
// modified per-hit amount. Shield must remain valid until used; pass zero when
// expiry/ordering is not represented. No future shields or healing are assumed.
// The extra refusal allowance is ONLY at occupancy[0], not an arbitrary future
// waypoint. The live cursor must re-quote a different actual refused origin.
// No map bounds, blockers, champion replies, action readiness or freshness are
// supplied by this pure quote. Its result grants no native command authority.
inline Quote quote(std::span<const Source> sources,std::span<const Point> occupancy,
                   float hp,float shield,unsigned refusal_reserve=1) {
    Quote out;
    if(sources.size()>max_sources||occupancy.empty()||occupancy.size()>max_occupancy
       ||refusal_reserve>max_refusals||!std::isfinite(hp)||hp<=0
       ||!std::isfinite(shield)||shield<0)return out;
    for(std::size_t i=0;i<occupancy.size();++i) {
        if(occupancy[i].x<0||occupancy[i].y<0)return out;
        if(i&&detail::distance(occupancy[i-1],occupancy[i])>1)return out;
    }
    std::array<int,max_sources> identities{};
    std::array<const Source*,max_sources> active{};
    std::size_t count=0;
    bool ordered=true;
    for(std::size_t i=0;i<sources.size();++i) {
        const auto& s=sources[i];
        if(s.id<0||s.x<0||s.y<0||s.range<0||!std::isfinite(s.damage)||s.damage<0)return out;
        identities[i]=s.id;
        if(i) {
            if(sources[i-1].id==s.id)return out;
            if(sources[i-1].id>s.id)ordered=false;
        }
        if(s.alive&&(s.awake||s.will_wake)&&s.damage>0)active[count++]=&s;
    }
    // The live producer already supplies native actor order by ascending ID.
    // Validate that common path in O(N); unordered callers retain the same
    // O(N log N) identity-only fallback. Never reorder the active hit sequence.
    if(!ordered) {
        std::sort(identities.begin(),identities.begin()+sources.size());
        for(std::size_t i=1;i<sources.size();++i)if(identities[i]==identities[i-1])return out;
    }
    out.supported=true;out.hp_after=hp;out.shield_after=shield;out.ends_quiet=true;
    for(std::size_t i=0;i<count;++i)if(detail::hits(*active[i],occupancy.back()))out.ends_quiet=false;
    auto response=[&](Point point) {
        ++out.response_intervals;
        for(std::size_t i=0;i<count;++i) {
            const auto& s=*active[i];if(!detail::hits(s,point))continue;
            ++out.hits;out.damage+=s.damage;
            if(!std::isfinite(out.damage))return false;
            // One native-domain float32 subtraction per SOURCE in caller order;
            // aggregating sources first is observably a different HP arithmetic.
            float remaining=s.damage;
            if(out.shield_after>0) {
                const float before=out.shield_after;
                out.shield_after=std::max(0.f,before-remaining);
                remaining=std::max(0.f,remaining-before);
            }
            if(remaining>0)out.hp_after=std::max(0.f,out.hp_after-remaining);
        }
        return true;
    };
    for(unsigned i=0;i<refusal_reserve;++i)if(!response(occupancy.front()))return Quote{};
    for(const auto p:occupancy)if(!response(p))return Quote{};
    out.survives=out.hp_after>0;
    out.affordable=out.survives&&out.ends_quiet;
    return out;
}
}
