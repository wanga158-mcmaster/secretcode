#pragma once
// One bounded quiet movement operator. A completed dated root field nominates a
// landing; it never carries current action authority. The caller owns matching
// Route.graph_revision to its retained geometry revision before invoking this.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

namespace tt { struct Route; }
namespace mb::production::potential { class Field; }
namespace tt::dash_service {
struct Point { int x{},y{}; friend bool operator==(Point,Point)=default; };
enum class Purpose { Work, Capture, Bank, Unsupported };
enum class Status { Applied, NoProgress, Refused, NeedCompute, NeedObservation, Stale, Unsupported };
struct Outcome {
    Status status{Status::NeedObservation};
    int epoch{-1},geometry_epoch{-1},slot{-1};
    Point from{-1,-1},to{-1,-1};
    bool attempted{},accepted{},allowances_queried{},can_move{},can_attack{},can_cast{};
    std::uint16_t ordinary_distance{65535},landing_distance{65535};
};
struct RouteView {
    bool completed{};
    Purpose purpose{Purpose::Unsupported};
    int group{-1},geometry_epoch{-1},width{},height{};
    std::span<const std::uint16_t> distances;
};
struct Flags { bool alive{},channeling{},can_move{},can_attack{},can_cast{}; };
struct Dash {
    bool dash{},ground{},ready{},exclusive{};
    float range{},modifier{};
    int duration{};
};
struct Tile { bool path_blocked{},landing_blocked{}; };
enum class Scope { Clear, HasTarget, Unsupported };
constexpr int max_range=2,side=2*max_range+1,max_tiles=side*side,spell_slots=2;
constexpr auto unreachable=std::numeric_limits<std::uint16_t>::max();
inline int distance(Point a,Point b) { return std::max(std::abs(a.x-b.x),std::abs(a.y-b.y)); }
inline bool inside(Point p,int w,int h) { return p.x>=0 && p.y>=0 && p.x<w && p.y<h; }
inline std::uint16_t at(const RouteView& route,Point p) {
    return inside(p,route.width,route.height)?route.distances[static_cast<std::size_t>(p.y)*route.width+p.x]:unreachable;
}
inline int local_index(Point origin,Point p) { return (p.y-origin.y+max_range)*side+p.x-origin.x+max_range; }
// Official Dash metadata admits a legal <=range eight-step path. Current native
// opposed (+2,+1) corridors accept either free middle around the blocked other
// middle; there is no fixed direction interpolation to reproduce. This bounded
// existential search nominates a known-legal path, never a free hidden landing.
// Holes may occur inside the path, but its final tile must be ordinary ground.
inline bool path_supported(Point from,Point to,int range,int width,int height,const std::array<Tile,max_tiles>& tiles) {
    if (!inside(to,width,height) || to==from || range<1 || range>max_range ||
        distance(from,to)>range || tiles[local_index(from,to)].landing_blocked) return false;
    if (tiles[local_index(from,to)].path_blocked) return false;
    if (distance(from,to)==1) return true;
    // This declared class has range<=2. A length-two witness needs exactly one
    // of at most eight adjacent middle tiles; enumerate all, never interpolate.
    if (range<2) return false;
    for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
        const Point middle{from.x+dx,from.y+dy};
        if ((!dx && !dy) || !inside(middle,width,height) || distance(middle,to)>1) continue;
        if (!tiles[local_index(from,middle)].path_blocked) return true;
    }
    return false;
}
// Backend supplies exactly the same production composition exercised by spies:
// epoch, position, flags, budget, quiet(point), prepare(protect_work), tile(point),
// dash(slot), use(slot,absolute_destination). It may query, never yield on purpose.
// Empirical native import/body fuel remains a separate qualification requirement.
template<class Backend>
Outcome try_progress(const RouteView& route,bool walked_this_turn,Backend& backend) {
    Outcome out; out.geometry_epoch=route.geometry_epoch;
    const auto cells=static_cast<std::uint64_t>(std::max(route.width,0))*std::max(route.height,0);
    if (!route.completed || cells==0 || route.distances.size()!=cells ||
        route.purpose==Purpose::Unsupported ||
        ((route.purpose==Purpose::Work || route.purpose==Purpose::Capture) && route.group<0)) {
        out.status=Status::Unsupported; return out;
    }
    if (!backend.budget()) { out.status=Status::NeedCompute; return out; }
    out.epoch=backend.epoch(); out.from=out.to=backend.position();
    const auto fresh=[&] { return backend.position()==out.from && backend.epoch()==out.epoch; };
    auto flags=backend.flags();
    if (!fresh()) { out.status=Status::Stale; return out; }
    if (!inside(out.from,route.width,route.height) || !flags.alive || flags.channeling || !flags.can_cast)
        return out;
    if (!backend.quiet(out.from)) { out.status=Status::Unsupported; return out; }
    const auto scope=backend.prepare(route.purpose==Purpose::Work);
    if (!fresh()) { out.status=Status::Stale; return out; }
    if (scope!=Scope::Clear) { out.status=scope==Scope::Unsupported?Status::Unsupported:Status::NoProgress; return out; }
    std::array<Tile,max_tiles> tiles;
    for (int dy=-max_range;dy<=max_range;++dy) {
        if (!backend.budget()) { out.status=Status::NeedCompute; return out; }
        for (int dx=-max_range;dx<=max_range;++dx) {
            const Point p{out.from.x+dx,out.from.y+dy};
            tiles[local_index(out.from,p)]=inside(p,route.width,route.height)?backend.tile(p):Tile{true,true};
        }
    }
    if (!fresh()) { out.status=Status::Stale; return out; }
    const auto here=at(route,out.from);
    auto ordinary=here;
    if (flags.can_move) for (int dy=-1;dy<=1;++dy) for (int dx=-1;dx<=1;++dx) {
        const Point p{out.from.x+dx,out.from.y+dy};
        // Compare only alternatives admitted by THIS quiet operator. A legal
        // ordinary root covered by a harmful source is not a safe capture/route
        // continuation and cannot preempt an otherwise supported P0 dash.
        if ((dx || dy) && !tiles[local_index(out.from,p)].landing_blocked && backend.quiet(p))
            ordinary=std::min(ordinary,at(route,p));
    }
    out.ordinary_distance=ordinary;
    bool unsupported=false;
    auto best=ordinary;
    Dash selected;
    Point destination=out.from;
    for (int slot=0;slot<spell_slots;++slot) {
        if (!backend.budget()) { out.status=Status::NeedCompute; return out; }
        const auto dash=backend.dash(slot);
        if (!dash.dash || !dash.ground || !dash.ready || (dash.exclusive && walked_this_turn)) continue;
        // Initial class excludes landing payloads: collateral/wake/retaliation
        // is a separate work operator, not silently a quiet movement effect.
        if (!std::isfinite(dash.range) || dash.range<1 || dash.range>max_range ||
            !std::isfinite(dash.modifier) || dash.modifier!=0 || dash.duration!=0) { unsupported=true; continue; }
        const int range=static_cast<int>(std::floor(dash.range));
        for (int dy=-range;dy<=range;++dy) for (int dx=-range;dx<=range;++dx) {
            const Point p{out.from.x+dx,out.from.y+dy};
            if ((!dx && !dy) || !inside(p,route.width,route.height)) continue;
            const auto value=at(route,p);
            if (value==unreachable || value>=best || (here!=unreachable && value>=here) ||
                !backend.quiet(p) || !path_supported(out.from,p,range,route.width,route.height,tiles)) continue;
            best=value; destination=p; out.slot=slot; selected=dash;
        }
    }
    if (!fresh()) { out.status=Status::Stale; return out; }
    if (out.slot<0) { out.status=unsupported?Status::Unsupported:Status::NoProgress; return out; }
    if (!backend.budget()) { out.status=Status::NeedCompute; return out; }
    flags=backend.flags();
    const auto final_dash=backend.dash(out.slot);
    if (!flags.alive || flags.channeling || !flags.can_cast || !final_dash.dash ||
        !final_dash.ground || !final_dash.ready || (final_dash.exclusive && walked_this_turn) ||
        final_dash.range!=selected.range || final_dash.modifier!=selected.modifier ||
        final_dash.duration!=selected.duration || final_dash.exclusive!=selected.exclusive) return out;
    // Position FIRST, owner epoch LAST, immediately before the native command.
    if (!fresh()) { out.status=Status::Stale; return out; }
    out.attempted=true; out.landing_distance=best;
    out.accepted=backend.use(out.slot,destination);
    out.to=backend.position();
    if (backend.epoch()!=out.epoch) { out.status=Status::Stale; return out; }
    const auto after=backend.flags();
    if (backend.position()!=out.to || backend.epoch()!=out.epoch) { out.status=Status::Stale; return out; }
    out.allowances_queried=true; out.can_move=after.can_move; out.can_attack=after.can_attack; out.can_cast=after.can_cast;
    if ((out.accepted && out.to!=destination) || (!out.accepted && out.to!=out.from)) return out;
    out.status=out.accepted?Status::Applied:Status::Refused;
    // No synthetic allowances or stationary packet publication after this point.
    return out;
}
} // namespace tt::dash_service
namespace tt {
dash_service::Outcome try_dash_progress(const Route&,const mb::production::potential::Field&,bool walked_this_turn);
}
