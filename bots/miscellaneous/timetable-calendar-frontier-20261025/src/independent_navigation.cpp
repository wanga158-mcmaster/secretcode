// Independent route planning and command refinement from the pinned SDK contract.
// The pure section is shared with the fixture; the production adapter is not mocked.
#include <algorithm>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace tt::navigation_detail {
enum class MoveResult { Applied, Refused, Unavailable, Illegal, Stale };

enum class RouteStage { Walls,Roots,Native,Export,Published,Withdrawn };
enum class GeometryResult { NeedCompute,Ready,NeedObservation,Unreachable,Stale };
struct RouteProgress
{
    RouteStage stage{RouteStage::Walls};
    std::size_t group{},member{},root{};
    bool begun{};
};
inline constexpr std::size_t route_units=256,route_explicit_roots=128,route_known_groups=4096;

template<class Point> struct RouteKey
{
    int kind{},group{-1};
    std::uint64_t graph{};
    Point origin;
    const void* goal_owner{};
    const void* empty_owner{};
    std::vector<Point> roots;
    std::vector<int> known;
};
template<class Point>
bool same_route_key(const RouteKey<Point>& a,const RouteKey<Point>& b)
{
    return a.kind==b.kind && a.group==b.group && a.graph==b.graph && a.origin==b.origin &&
        a.goal_owner==b.goal_owner && a.empty_owner==b.empty_owner && a.roots==b.roots && a.known==b.known;
}

// This is the actual retained phase dispatcher used below. One completed phase
// owns an input; in particular native completion NEVER publishes Ready or starts
// export. Cursors change only after their bounded unit completes. Masks and the
// native field belong to Backend's retained owner, not to this invocation.
// fresh() dates every invocation separately: later quiet owner inputs may resume,
// but suspension/displacement inside any invocation withdraws the whole owner.
template<class Backend>
GeometryResult advance_route_geometry(RouteProgress& progress,Backend& backend)
{
    const auto withdraw=[&](GeometryResult result) {
        progress.stage=RouteStage::Withdrawn;return result;
    };
    if(!backend.fresh())return withdraw(GeometryResult::Stale);
    if(progress.stage==RouteStage::Published)return GeometryResult::Ready;
    if(progress.stage==RouteStage::Withdrawn)return GeometryResult::NeedObservation;
    if(!backend.headroom(progress.stage))return GeometryResult::NeedCompute;
    if(progress.stage==RouteStage::Walls || progress.stage==RouteStage::Roots)
    {
        const bool walls=progress.stage==RouteStage::Walls;
        if(!progress.begun)
        {
            if(walls)backend.begin_walls();else backend.begin_roots();
            if(!backend.fresh())return withdraw(GeometryResult::Stale);
            progress.begun=true;
        }
        std::size_t units=0;
        while(!(walls?backend.walls_done(progress):backend.roots_done(progress)))
        {
            if(units>=route_units)return GeometryResult::NeedCompute;
            if((units&15u)==0)
            {
                if(!backend.fresh())return withdraw(GeometryResult::Stale);
                if(!backend.headroom(progress.stage))return GeometryResult::NeedCompute;
            }
            if(walls)backend.wall_unit(progress);else backend.root_unit(progress);
            ++units;
        }
        if(!backend.fresh())return withdraw(GeometryResult::Stale);
        if(!backend.headroom(progress.stage))return GeometryResult::NeedCompute;
        const bool roots_present=walls || backend.finish_roots();
        if(!backend.fresh())return withdraw(GeometryResult::Stale);
        if(!roots_present)return withdraw(GeometryResult::Unreachable);
        progress.stage=walls?RouteStage::Roots:RouteStage::Native;
        progress.begun=false;
        return GeometryResult::NeedCompute;
    }
    const bool native=progress.stage==RouteStage::Native;
    const auto result=native?backend.compute():backend.export_field();
    if(!backend.fresh())return withdraw(GeometryResult::Stale);
    if(result==GeometryResult::NeedCompute)return result;
    if(result!=GeometryResult::Ready)return withdraw(result);
    progress.stage=native?RouteStage::Export:RouteStage::Published;
    return native?GeometryResult::NeedCompute:GeometryResult::Ready;
}

template<class FrameStatus>
bool accepts_movement_frame(FrameStatus value,FrameStatus ready,FrameStatus needs_observation)
{
    // A stamped post-action frame can provide only its epoch/origin. Fresh physical
    // queries below establish movement authority without promoting global freshness.
    return value==ready || value==needs_observation;
}

inline bool planning_headroom(std::uint64_t remaining)
{
    // Scheduling threshold only: this is not a theorem about native search cost.
    // It is shared with the two-turn scheduling fixture so observation work cannot
    // silently be assumed free before a planning phase.
    return remaining>14000u+64000u;
}

template<class Point>
bool capture_key_matches(int saved_group,std::uint64_t saved_revision,Point saved_origin,
                          int requested_group,std::uint64_t requested_revision,Point requested_origin)
{
    return saved_group==requested_group && saved_revision==requested_revision && saved_origin==requested_origin;
}

enum class CapturePhase { Native, Export, Published };
inline CapturePhase capture_phase(bool same_key,bool export_pending,bool published)
{
    if (!same_key) return CapturePhase::Native;
    if (export_pending) return CapturePhase::Export;
    return published?CapturePhase::Published:CapturePhase::Native;
}
enum class CaptureExport { Ready, MissingOwner, Stale, ShapeMismatch };
// The native field is the immutable dated source of further representatives.
// Exporting or moving a derived buffer never consumes that source owner.
template<class Native,class Fresh>
CaptureExport export_capture_buffer(const Native& native,std::vector<std::uint16_t>& published,
                                    std::size_t expected,Fresh fresh)
{
    if (!native) return CaptureExport::MissingOwner;
    auto distances=native->export_linear_buffer();
    if (!fresh()) return CaptureExport::Stale;
    if (distances.size()!=expected) return CaptureExport::ShapeMismatch;
    published=std::move(distances);
    return CaptureExport::Ready;
}

template<class Point>
bool adjacent(Point a,Point b)
{
    const auto dx=static_cast<std::int64_t>(a.x)-b.x;
    const auto dy=static_cast<std::int64_t>(a.y)-b.y;
    return dx>=-1 && dx<=1 && dy>=-1 && dy<=1 && (dx!=0 || dy!=0);
}

// Caller and fixture exercise this actual command composition. The final physical
// call is absolute; no interpretation of a destination as a relative delta is used.
// These guards withdraw authority on a detected prefix change. Numeric fuel safety
// of the imports between guards remains a separate native premise.
template<class Actor,class Point,class Clock,class Legal>
MoveResult apply_destination(Actor& actor,Point origin,Point destination,int epoch,
                             Clock clock,Legal legal)
{
    if (clock()!=epoch) return MoveResult::Stale;
    if (actor.position()!=origin) return MoveResult::Stale;
    if (!actor.can_move()) return MoveResult::Unavailable;
    if (!adjacent(origin,destination)) return MoveResult::Illegal;
    if (!legal(destination)) return MoveResult::Illegal;
    // A position query can resume in a later owner epoch at the same tile.
    // Query the epoch after that last observation, immediately before the command.
    if (actor.position()!=origin || clock()!=epoch) return MoveResult::Stale;
    const bool applied=actor.move_absolute(destination);
    if (clock()!=epoch) return MoveResult::Stale;
    return applied ? MoveResult::Applied : MoveResult::Refused;
}

inline std::uint16_t distance_at(const std::vector<std::uint16_t>& distances,int width,int height,
                                 int x,int y)
{
    constexpr auto unreachable=std::numeric_limits<std::uint16_t>::max();
    if (width<=0 || height<=0 || x<0 || y<0 || x>=width || y>=height) return unreachable;
    const auto expected=static_cast<std::size_t>(width)*static_cast<std::size_t>(height);
    if (distances.size()!=expected) return unreachable;
    return distances[static_cast<std::size_t>(y)*static_cast<std::size_t>(width)+static_cast<std::size_t>(x)];
}

inline bool descends(std::uint16_t from,std::uint16_t to)
{
    constexpr auto unreachable=std::numeric_limits<std::uint16_t>::max();
    return from!=unreachable && from>0 && to!=unreachable && to==from-1;
}

enum class CaptureCoverage { Ready, NeedCompute, Unknown };
// Staged pure verification of the exported gradient at the selected FUTURE
// frontier. Reaching the old compute_to destination does not establish coverage
// here. Native roots are checked against actual immutable selected-group members.
template<class Point,class Root,class Guard>
CaptureCoverage advance_capture_gradient(const std::vector<std::uint16_t>& field,int width,int height,
    Point frontier,Point& retained_frontier,Point& position,std::uint16_t& remaining,bool& started,
    Root is_root,std::size_t units,Guard guard)
{
    constexpr auto uncovered=std::numeric_limits<std::uint16_t>::max();
    if (width<=0 || height<=0) return CaptureCoverage::Unknown;
    const auto cells=static_cast<std::uint64_t>(width)*static_cast<std::uint64_t>(height);
    if (cells>std::numeric_limits<std::size_t>::max() || field.size()!=cells) return CaptureCoverage::Unknown;
    if (!started || retained_frontier!=frontier)
    {
        remaining=distance_at(field,width,height,frontier.x,frontier.y);
        if (remaining==uncovered || remaining>=cells) return CaptureCoverage::Unknown;
        retained_frontier=position=frontier; started=true;
    }
    constexpr int directions[][2]={{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}};
    for (std::size_t unit=0;unit<units;++unit)
    {
        if (!guard()) return CaptureCoverage::NeedCompute;
        if (remaining==0) return is_root(position)?CaptureCoverage::Ready:CaptureCoverage::Unknown;
        bool found=false;
        for (const auto& direction:directions)
        {
            const Point next{position.x+direction[0],position.y+direction[1]};
            if (descends(remaining,distance_at(field,width,height,next.x,next.y)))
            { position=next; --remaining; found=true; break; }
        }
        if (!found) return CaptureCoverage::Unknown;
    }
    return CaptureCoverage::NeedCompute;
}

template<class Point,class Refused>
std::vector<Point> field_proposals(const std::vector<std::uint16_t>& distances,int width,int height,
                                   Point origin,Point actual_origin,int stamped_epoch,int actual_epoch,
                                   bool field_ready,bool frame_eligible,Refused refused)
{
    std::vector<Point> result;
    if (!field_ready || !frame_eligible || stamped_epoch!=actual_epoch || origin!=actual_origin) return result;
    const auto here=distance_at(distances,width,height,origin.x,origin.y);
    if (here==0 || here==std::numeric_limits<std::uint16_t>::max()) return result;
    // Pinned SDK compass order: north is (0,+1). Values are destinations, never deltas.
    constexpr int directions[][2]={{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}};
    for (const auto& direction:directions)
    {
        const Point destination{origin.x+direction[0],origin.y+direction[1]};
        if (destination.x<0 || destination.y<0 || destination.x>=width || destination.y>=height) continue;
        if (descends(here,distance_at(distances,width,height,destination.x,destination.y)) && !refused(destination))
            result.push_back(destination);
    }
    return result;
}

// Complete frozen-graph ingress, its geometric reversal, then actual historical
// return. This is a candidate physical word, not evidence of future occupancy,
// allowances, timing, exposure or quietness. The caller charges its work phase
// and guards every actual primitive separately. Empty means Unknown.
template<class Point>
std::vector<Point> approach_word(const std::vector<std::uint16_t>& distances,int width,int height,
                                 bool ready,Point origin,Point first,std::span<const Point> actual_return,
                                 std::size_t max_nodes)
{
    if (!ready || width<=0 || height<=0 || actual_return.empty() || actual_return.front()!=origin) return {};
    const auto cells=static_cast<std::uint64_t>(width)*static_cast<std::uint64_t>(height);
    if (cells>std::numeric_limits<std::size_t>::max() || distances.size()!=cells) return {};
    const auto inside=[&](Point point) {return point.x>=0 && point.y>=0 && point.x<width && point.y<height;};
    const auto initial=distance_at(distances,width,height,origin.x,origin.y);
    if (initial==0 || initial==std::numeric_limits<std::uint16_t>::max() || initial>=cells ||
        !inside(first) || !adjacent(origin,first) ||
        !descends(initial,distance_at(distances,width,height,first.x,first.y))) return {};
    // The exact final size is 2*ingress_edges + historical_nodes. Check it before
    // any traversal/allocation; no capped partial witness may escape this helper.
    if (actual_return.size()>max_nodes || initial>(max_nodes-actual_return.size())/2) return {};
    const auto total=2*static_cast<std::size_t>(initial)+actual_return.size();
    for (std::size_t i=0;i<actual_return.size();++i)
        if (!inside(actual_return[i]) || (i && !adjacent(actual_return[i-1],actual_return[i]))) return {};
    std::vector<Point> result;
    try { result.reserve(total); }
    catch (const std::bad_alloc&) { return {}; }
    catch (const std::length_error&) { return {}; }
    result.push_back(origin); result.push_back(first);
    auto here=static_cast<std::uint16_t>(initial-1);
    constexpr int directions[][2]={{0,1},{1,1},{1,0},{1,-1},{0,-1},{-1,-1},{-1,0},{-1,1}};
    while (here>0)
    {
        const auto from=result.back(); bool found=false;
        for (const auto& direction:directions)
        {
            const Point next{from.x+direction[0],from.y+direction[1]};
            if (inside(next) && descends(here,distance_at(distances,width,height,next.x,next.y)))
            { result.push_back(next); --here; found=true; break; }
        }
        if (!found) return {}; // malformed/truncated gradient is not unreachable
    }
    const auto ingress_nodes=result.size();
    for (std::size_t i=ingress_nodes-1;i>0;--i) result.push_back(result[i-1]);
    for (std::size_t i=1;i<actual_return.size();++i) result.push_back(actual_return[i]);
    return result;
}
} // namespace tt::navigation_detail

#ifndef TT_NAVIGATION_FIXTURE
#include "runtime.h"
#include "config.h"

namespace tt {
struct RouteGeometry
{
    navigation_detail::RouteKey<vec2> key;
    navigation_detail::RouteProgress progress;
    std::shared_ptr<cambc::bitboard> goal_owner,empty_owner,walls,roots;
    std::shared_ptr<cambc::distance_field> native;
};
namespace {
struct Refusals
{
    int epoch{-1};
    std::vector<vec2> tiles;
};
Refusals& refusals() { static Refusals state; return state; }

bool inside(const World& world,vec2 tile)
{ return tile.x>=0 && tile.y>=0 && tile.x<world.width && tile.y<world.height; }

void prepare_refusals(int epoch)
{
    auto& state=refusals();
    if (state.epoch!=epoch) { state.epoch=epoch; state.tiles.clear(); }
}

void clear_route(Route& route,Status status)
{
    route.status=status;
    route.distances.clear();
}

void clear_capture(Route& route)
{
    route.capture_status=Status::NeedCompute;
    route.capture_group=-1; route.capture_revision=0; route.capture_origin={-1,-1};
    route.capture_distances.clear();
    route.capture_walls.reset(); route.capture_roots.reset(); route.capture_native.reset();
    route.capture_export_pending=false;
    route.capture_destination=route.capture_frontier=route.capture_check_position={-1,-1};
    route.capture_check_distance=cambc::distance_field::unreachable;
    route.capture_check_started=false;
    route.capture_frontier_status=Status::NeedCompute;
}

bool planning_origin_current(vec2 origin,int epoch)
{ return cambc::self().position()==origin && current(epoch); }

Status export_capture(Route& route,const World& world,vec2 origin,int epoch)
{
    if (!route.capture_export_pending || !route.capture_native) return Status::NeedObservation;
    if (!budget(24000)) return Status::NeedCompute;
    const auto expected=static_cast<std::size_t>(world.width)*static_cast<std::size_t>(world.height);
    const auto exported=navigation_detail::export_capture_buffer(route.capture_native,
        route.capture_distances,expected,[&]{return planning_origin_current(origin,epoch);});
    if (exported==navigation_detail::CaptureExport::Stale) return Status::Stale;
    if (exported!=navigation_detail::CaptureExport::Ready) return Status::NeedObservation;
    route.capture_export_pending=false;
    route.capture_status=Status::Ready;
    route.capture_check_started=false;
    route.capture_frontier_status=Status::NeedCompute;
    return Status::Ready;
}

struct GeometryBackend
{
    RouteGeometry& geometry;
    Route& route;
    const World& world;
    vec2 origin;
    int epoch;
    bool fresh() const {return planning_origin_current(origin,epoch);}
    bool headroom(navigation_detail::RouteStage stage) const
    {
        // Existing empirical entry reserves, now paid in separate fresh inputs.
        return budget(stage==navigation_detail::RouteStage::Native?53000:24000);
    }
    void begin_walls()
    {geometry.walls=std::make_shared<cambc::bitboard>(world.terrain|world.camp_tiles);}
    bool walls_done(const navigation_detail::RouteProgress& p) const
    {return p.group>=world.groups.size();}
    void wall_unit(navigation_detail::RouteProgress& p)
    {
        const auto& members=world.groups[p.group].members;
        const auto known=p.group<geometry.key.known.size()?geometry.key.known[p.group]:-1;
        if((known==static_cast<int>(Availability::Reward) || known==static_cast<int>(Availability::Respawning)) &&
           p.member<members.size())
        {geometry.walls->set(members[p.member].pos,false);++p.member;}
        else {++p.group;p.member=0;}
    }
    void begin_roots()
    {
        if(geometry.empty_owner)*geometry.walls&=~*geometry.empty_owner;
        *geometry.walls|=world.terrain;
        geometry.walls->set(origin,false);
        geometry.roots=geometry.goal_owner?std::make_shared<cambc::bitboard>(*geometry.goal_owner):
            std::make_shared<cambc::bitboard>();
    }
    bool roots_done(const navigation_detail::RouteProgress& p) const
    {return geometry.goal_owner || p.root>=geometry.key.roots.size();}
    void root_unit(navigation_detail::RouteProgress& p)
    {
        const auto point=geometry.key.roots[p.root++];
        if(inside(world,point))geometry.roots->set(point);
    }
    bool finish_roots()
    {*geometry.roots&=~*geometry.walls;return !geometry.roots->is_empty();}
    navigation_detail::GeometryResult compute()
    {
        cambc::bitboard destinations,visited;destinations.set(origin);
        geometry.native=std::make_shared<cambc::distance_field>();
        geometry.native->compute_to(*geometry.walls,*geometry.roots,destinations,visited);
        const auto distance=geometry.native->get(origin);
        if(!cfg::dash_service && distance==cambc::distance_field::unreachable)
            return navigation_detail::GeometryResult::Unreachable;
        // The full returned landing set remains useful to the separate dash
        // operator even when this origin itself has the sentinel distance.
        return navigation_detail::GeometryResult::Ready;
    }
    navigation_detail::GeometryResult export_field()
    {
        if(!geometry.native)return navigation_detail::GeometryResult::NeedObservation;
        auto exported=geometry.native->export_linear_buffer();
        if(!fresh())return navigation_detail::GeometryResult::Stale;
        const auto cells=static_cast<std::size_t>(world.width)*world.height;
        if(exported.size()!=cells)return navigation_detail::GeometryResult::NeedObservation;
        route.distances=std::move(exported);
        return navigation_detail::GeometryResult::Ready;
    }
};

template<class RouteType>
Status plan_route_staged(RouteType& route,const RouteRequest& request,vec2 origin,int epoch,
                         const std::vector<Availability>& known)
{
    using namespace navigation_detail;
    if(!planning_origin_current(origin,epoch))
    {clear_route(route,Status::Stale);route.geometry.reset();return route.status;}
    if(!budget(24000))return route.status=Status::NeedCompute;
    const auto& world=World::get();
    if(!inside(world,origin) || known.size()>route_known_groups || request.roots.size()>route_explicit_roots ||
       (request.kind==RouteKind::Work && (request.group<0 || static_cast<std::size_t>(request.group)>=world.groups.size())) ||
       (request.kind==RouteKind::Capture && (request.group<0 || static_cast<std::size_t>(request.group)>=known.size() ||
                                           known[static_cast<std::size_t>(request.group)]!=Availability::Reward)))
    {clear_route(route,Status::NeedObservation);route.geometry.reset();return route.status;}
    // This finite class owns complete value copies of explicit roots/availability
    // and shared owners of immutable native masks. Mutating a mask in place under
    // the same owner is excluded by RouteRequest's frozen-geometry contract.
    RouteKey<vec2> key;key.kind=static_cast<int>(request.kind);key.group=request.group;
    key.graph=request.graph_revision;key.origin=origin;
    key.goal_owner=request.goal_mask.get();key.empty_owner=request.observed_empty.get();
    key.roots=request.roots;key.known.reserve(known.size());
    for(const auto value:known)key.known.push_back(static_cast<int>(value));
    if(!planning_origin_current(origin,epoch))
    {clear_route(route,Status::Stale);route.geometry.reset();return route.status;}
    if(route.geometry && !same_route_key(route.geometry->key,key))
    {clear_route(route,Status::Stale);route.geometry.reset();return route.status;}
    if(!route.geometry)
    {
        clear_route(route,Status::NeedCompute);clear_capture(route);
        route.kind=request.kind;route.group=request.group;route.graph_revision=request.graph_revision;
        route.origin=origin;
        if(request.kind==RouteKind::Work)
        {route.capture_group=request.group;route.capture_revision=request.graph_revision;route.capture_origin=origin;}
        route.geometry=std::make_shared<RouteGeometry>();route.geometry->key=std::move(key);
        route.geometry->goal_owner=request.goal_mask;route.geometry->empty_owner=request.observed_empty;
    }
    route.epoch=epoch;
    GeometryBackend backend{*route.geometry,route,world,origin,epoch};
    const auto result=advance_route_geometry(route.geometry->progress,backend);
    switch(result)
    {
    case GeometryResult::Ready:route.status=Status::Ready;break;
    case GeometryResult::NeedCompute:route.status=Status::NeedCompute;break;
    case GeometryResult::NeedObservation:clear_route(route,Status::NeedObservation);route.geometry.reset();break;
    case GeometryResult::Unreachable:clear_route(route,Status::Unreachable);route.geometry.reset();break;
    case GeometryResult::Stale:clear_route(route,Status::Stale);route.geometry.reset();break;
    }
    return route.status;
}
} // namespace

Status plan_route(Route& route,const RouteRequest& request,vec2 origin,int epoch,
                  const std::vector<Availability>& known)
{
    if constexpr(cfg::route_service && cfg::prefix_service && cfg::direct_service)
        return plan_route_staged(route,request,origin,epoch,known);
    clear_route(route,Status::NeedCompute);
    route.kind=request.kind;
    route.group=request.group;
    route.graph_revision=request.graph_revision;
    route.epoch=epoch;
    route.origin=origin;
    if (request.kind==RouteKind::Work &&
        !navigation_detail::capture_key_matches(route.capture_group,route.capture_revision,
            route.capture_origin,request.group,request.graph_revision,origin))
    {
        clear_capture(route);
        route.capture_group=request.group; route.capture_revision=request.graph_revision;
        route.capture_origin=origin;
    }
    if (!current(epoch)) return route.status=Status::Stale;
    // This owns a planning phase. The empirical work reserve is intentionally separate
    // from the invariant14000 tail; neither this estimate nor a build is a fuel proof.
    if (!navigation_detail::planning_headroom(cambc::fuel_remaining())) return route.status;
    auto& world=World::get();
    if (!inside(world,origin)) return route.status=Status::Stale;
    if (!planning_origin_current(origin,epoch)) return route.status=Status::Stale;
    if (request.kind==RouteKind::Work &&
        (request.group<0 || static_cast<std::size_t>(request.group)>=world.groups.size()))
        return route.status=Status::NeedObservation;
    if (request.kind==RouteKind::Capture &&
        (request.group<0 || static_cast<std::size_t>(request.group)>=known.size() ||
         known[static_cast<std::size_t>(request.group)]!=Availability::Reward))
        return route.status=Status::NeedObservation;

    if (request.kind==RouteKind::Work && !(cfg::prefix_service && cfg::direct_service))
    {
        if (request.group<0 || static_cast<std::size_t>(request.group)>=world.groups.size())
            return route.status=Status::NeedObservation;
        if (route.capture_status==Status::Unreachable) return route.status=Status::Unreachable;
        const auto phase=navigation_detail::capture_phase(true,route.capture_export_pending,
            route.capture_status==Status::Ready && !route.capture_distances.empty());
        if (phase==navigation_detail::CapturePhase::Export)
        {
            const auto result=export_capture(route,world,origin,epoch);
            // Export publication owns this input; Work field computation begins
            // only on a later same-key quiet planning input.
            return route.status=result==Status::Ready?Status::NeedCompute:result;
        }
    }

    cambc::bitboard walls=world.terrain | world.camp_tiles;
    for (std::size_t g=0;g<world.groups.size() && g<known.size();++g)
        if (known[g]==Availability::Reward || known[g]==Availability::Respawning)
            for (const auto& member : world.groups[g].members) walls.set(member.pos,false);
    if (request.observed_empty) walls &= ~*request.observed_empty;
    walls |= world.terrain;
    // The current tile is the already occupied origin, never a proposed future landing.
    walls.set(origin,false);
    if (request.kind==RouteKind::Work && !(cfg::prefix_service && cfg::direct_service))
    {
        const auto phase=navigation_detail::capture_phase(true,route.capture_export_pending,
            route.capture_status==Status::Ready && !route.capture_distances.empty());
        if (phase!=navigation_detail::CapturePhase::Published)
        {
            route.capture_status=Status::NeedCompute;
            auto capture_walls=walls;
            cambc::bitboard capture_roots;
            for (const auto& member:world.groups[static_cast<std::size_t>(request.group)].members)
            {
                capture_walls.set(member.pos,false);
                capture_roots.set(member.pos);
            }
            capture_walls |= world.terrain;
            capture_walls.set(origin,false);
            capture_roots &= ~capture_walls;
            // Necessary ordinary entry after this group is fully cleared. This is
            // conditional on the same conservative surrounding graph; it establishes
            // neither safe capture execution nor impossibility for a dash-capable kit.
            if (capture_roots.is_empty())
            { route.capture_status=Status::Unreachable; return route.status=Status::Unreachable; }
            if (!planning_origin_current(origin,epoch))
                return route.status=Status::Stale;
            if (!budget(53000)) return route.status=Status::NeedCompute;
            cambc::bitboard capture_destination;
            capture_destination.set(origin);
            cambc::bitboard capture_visited;
            auto capture_field=std::make_shared<cambc::distance_field>();
            capture_field->compute_to(capture_walls,capture_roots,capture_destination,capture_visited);
            const auto distance=capture_field->get(origin);
            if (!planning_origin_current(origin,epoch))
                return route.status=Status::Stale;
            if (distance==cambc::distance_field::unreachable)
            { route.capture_status=Status::Unreachable; return route.status=Status::Unreachable; }
            route.capture_walls=std::make_shared<cambc::bitboard>(std::move(capture_walls));
            route.capture_roots=std::make_shared<cambc::bitboard>(std::move(capture_roots));
            route.capture_native=std::move(capture_field);
            route.capture_destination=origin;
            route.capture_export_pending=true;
            // Native compute, full export, and Work compute own separate quiet
            // inputs. Until export publishes, no capture vector is Ready.
            return route.status=Status::NeedCompute;
        }
    }
    cambc::bitboard roots;
    if (request.goal_mask)
        roots=*request.goal_mask;
    else
    {
        if (request.roots.empty()) return route.status=Status::NeedObservation;
        // Large spatial goal sets should enter through the native mask above. This
        // vector option remains for a bounded explicit list of physical destinations.
        for (auto tile : request.roots) if (inside(world,tile)) roots.set(tile);
    }
    roots &= ~walls;
    // Empty or disconnected roots describe only this exact conservative static
    // wall/root model. Unknown future respawns or different roots can change it.
    if (roots.is_empty()) return route.status=Status::Unreachable;
    if (!current(epoch) || cambc::self().position()!=origin) return route.status=Status::Stale;
    if (!budget(53000)) return route.status=Status::NeedCompute;

    cambc::bitboard destinations;
    destinations.set(origin);
    cambc::bitboard visited;
    cambc::distance_field field;
    // `visited` is output. Only compute_to's explicit destinations stop the search
    // early. A sentinel describes an origin not reached by THIS returned
    // field; it is not a physical disconnection or kit-reachability claim.
    // Dash proposals may use the finite reached landing set without promoting
    // the dated field to current movement authority.
    field.compute_to(walls,roots,destinations,visited);
    const auto origin_distance=field.get(origin);
    if (!current(epoch) || cambc::self().position()!=origin) return route.status=Status::Stale;
    if (origin_distance==cambc::distance_field::unreachable && !cfg::dash_service)
        return route.status=Status::Unreachable;
    if (!budget(6000)) return route.status=Status::NeedCompute;
    auto distances=field.export_linear_buffer();
    if (!current(epoch) || cambc::self().position()!=origin) return route.status=Status::Stale;
    const auto expected=static_cast<std::size_t>(world.width)*static_cast<std::size_t>(world.height);
    if (distances.size()!=expected) return route.status=Status::NeedCompute;
    route.distances=std::move(distances);
    route.status=Status::Ready;
    return route.status;
}

Status resolve_capture_frontier(Route& route,vec2 frontier,vec2 actual_quiet_origin,int epoch)
{
    if (route.kind!=RouteKind::Work || route.status!=Status::Ready) return Status::NeedObservation;
    if (!navigation_detail::capture_key_matches(route.capture_group,route.capture_revision,
            route.capture_origin,route.group,route.graph_revision,route.origin) ||
        actual_quiet_origin!=route.origin)
    {
        clear_capture(route);
        return route.capture_frontier_status=Status::NeedObservation;
    }
    if (!current(epoch) || !navigation_detail::planning_headroom(cambc::fuel_remaining()))
        return current(epoch)?Status::NeedCompute:Status::Stale;
    auto& world=World::get();
    if (!inside(world,frontier) || !inside(world,actual_quiet_origin) || route.group<0 ||
        static_cast<std::size_t>(route.group)>=world.groups.size()) return Status::NeedObservation;
    if (!planning_origin_current(actual_quiet_origin,epoch)) return Status::Stale;
    if (route.capture_frontier!=frontier)
    {
        route.capture_frontier=frontier;
        route.capture_check_started=false;
        route.capture_frontier_status=Status::NeedCompute;
    }
    if (route.capture_export_pending)
    {
        const auto exported=export_capture(route,world,actual_quiet_origin,epoch);
        return exported==Status::Ready?Status::NeedCompute:exported;
    }
    if (route.capture_status!=Status::Ready || !route.capture_walls || !route.capture_roots)
        return route.capture_frontier_status=Status::NeedObservation;
    if (route.capture_frontier_status==Status::NeedObservation) return Status::NeedObservation;
    const auto value=navigation_detail::distance_at(route.capture_distances,world.width,world.height,
                                                   frontier.x,frontier.y);
    if (value==cambc::distance_field::unreachable)
    {
        // The old compute_to stopped at its old destination. Uncovered does not
        // mean unreachable. Resolve this one selected frontier in the SAME graph.
        if (route.capture_destination==frontier)
            return route.capture_frontier_status=Status::NeedObservation;
        if (!budget(53000)) return Status::NeedCompute;
        cambc::bitboard destination; destination.set(frontier);
        cambc::bitboard visited;
        auto field=std::make_shared<cambc::distance_field>();
        field->compute_to(*route.capture_walls,*route.capture_roots,destination,visited);
        const auto resolved=field->get(frontier);
        if (!planning_origin_current(actual_quiet_origin,epoch)) return Status::Stale;
        route.capture_destination=frontier;
        if (resolved==cambc::distance_field::unreachable)
            return route.capture_frontier_status=Status::NeedObservation;
        route.capture_native=std::move(field);
        route.capture_export_pending=true;
        route.capture_status=Status::NeedCompute;
        route.capture_check_started=false;
        return Status::NeedCompute; // never compute and export in one input
    }
    const auto checked=navigation_detail::advance_capture_gradient(route.capture_distances,world.width,world.height,
        frontier,route.capture_frontier,route.capture_check_position,route.capture_check_distance,
        route.capture_check_started,[&](vec2 point) {
            const auto& members=world.groups[static_cast<std::size_t>(route.group)].members;
            return std::any_of(members.begin(),members.end(),[&](const auto& member){return member.pos==point;});
        },64,[] { return budget(24000); });
    if (!planning_origin_current(actual_quiet_origin,epoch)) return Status::Stale;
    switch (checked)
    {
    case navigation_detail::CaptureCoverage::Ready: return route.capture_frontier_status=Status::Ready;
    case navigation_detail::CaptureCoverage::NeedCompute: return route.capture_frontier_status=Status::NeedCompute;
    case navigation_detail::CaptureCoverage::Unknown: return route.capture_frontier_status=Status::NeedObservation;
    }
    return Status::NeedObservation;
}

void refuse(vec2 tile,int epoch)
{
    prepare_refusals(epoch);
    auto& tiles=refusals().tiles;
    if (std::find(tiles.begin(),tiles.end(),tile)==tiles.end()) tiles.push_back(tile);
}

std::vector<vec2> legal_steps(const Observation& observation)
{
    std::vector<vec2> result;
    if (!navigation_detail::accepts_movement_frame(observation.status,Status::Ready,Status::NeedObservation) ||
        !budget(5000)) return result;
    const int epoch=static_cast<int>(cambc::round_number());
    if (epoch!=observation.epoch) return result;
    auto& actor=cambc::self();
    const auto origin=actor.position();
    if (origin!=observation.pos || !actor.can_move()) return result;
    auto& world=World::get();
    if (!inside(world,origin)) return result;
    prepare_refusals(epoch);
    // Dynamic occupancy is reacquired in this epoch, not inferred from a cached route.
    auto walls=world.terrain | cambc::vision::champion_tiles() |
        cambc::vision::camp_tiles() | cambc::vision::teleport_tiles();
    walls.set(origin,false);
    for (auto tile : refusals().tiles) if (inside(world,tile)) walls.set(tile);
    const auto reachable=walls.reachable_tiles(origin,1);
    for (auto tile : reachable.positions())
        if (navigation_detail::adjacent(origin,tile)) result.push_back(tile);
    if (!current(epoch) || actor.position()!=origin) result.clear();
    return result;
}

std::vector<vec2> descent_steps(const Route& route,const Observation& observation)
{
    if (route.status!=Status::Ready ||
        !navigation_detail::accepts_movement_frame(observation.status,Status::Ready,Status::NeedObservation) ||
        !current(observation.epoch)) return {};
    const auto& world=World::get();
    const auto from=navigation_detail::distance_at(route.distances,world.width,world.height,
                                                  observation.pos.x,observation.pos.y);
    if (from==0 || from==cambc::distance_field::unreachable) return {};
    auto options=legal_steps(observation);
    options.erase(std::remove_if(options.begin(),options.end(),[&](vec2 tile) {
        const auto value=navigation_detail::distance_at(route.distances,world.width,world.height,tile.x,tile.y);
        return !navigation_detail::descends(from,value);
    }),options.end());
    // Equal-distance alternatives follow native compass order from reachable.positions.
    // No arbitrary geometric fallback or dynamic-arrival promise is introduced.
    return options;
}

std::vector<vec2> proposed_steps(const Route& route,const Observation& observation)
{
    if (!budget(1000) || !current(observation.epoch)) return {};
    const auto actual_origin=cambc::self().position();
    const auto& world=World::get();
    if (!current(observation.epoch)) return {};
    prepare_refusals(observation.epoch);
    // No dynamic mask or movement allowance is queried here. These candidates only
    // descend the frozen graph from a same-epoch/origin stamp. A separate current
    // safety prefix and move_absolute's native occupancy guard must authorize any move.
    return navigation_detail::field_proposals(route.distances,world.width,world.height,
        observation.pos,actual_origin,observation.epoch,static_cast<int>(cambc::round_number()),
        route.status==Status::Ready,
        navigation_detail::accepts_movement_frame(observation.status,Status::Ready,Status::NeedObservation),
        [](vec2 tile) {
            const auto& tiles=refusals().tiles;
            return std::find(tiles.begin(),tiles.end(),tile)!=tiles.end();
        });
}

std::vector<vec2> approach_witness(const Route& route,vec2 origin,vec2 first,
                                 std::span<const vec2> actual_return,std::size_t max_nodes)
{
    // World must already be initialized by the caller. No native dynamic board,
    // actor query or command is involved in this frozen-geometry construction.
    const auto& world=World::get();
    return navigation_detail::approach_word(route.distances,world.width,world.height,
        route.status==Status::Ready,origin,first,actual_return,max_nodes);
}

bool move_absolute(Observation& observation,vec2 destination)
{
    if (!navigation_detail::accepts_movement_frame(observation.status,Status::Ready,Status::NeedObservation) ||
        !budget(6000)) return false;
    auto& actor=cambc::self();
    const auto result=navigation_detail::apply_destination(actor,observation.pos,destination,observation.epoch,
        [] { return static_cast<int>(cambc::round_number()); },
        [&](vec2 tile) {
            const auto options=legal_steps(observation);
            return std::find(options.begin(),options.end(),tile)!=options.end();
        });
    if (result==navigation_detail::MoveResult::Stale)
    { observation.status=Status::Stale; return false; }
    if (result==navigation_detail::MoveResult::Refused)
    {
        refuse(destination,observation.epoch);
        observation.status=Status::NeedObservation;
        return false;
    }
    if (result!=navigation_detail::MoveResult::Applied) return false;
    observation.pos=destination;
    observation.can_move=false;
    // Movement changes personal vision and attack/ability authority. Reobserve before
    // any follow-up command; this update is not a new complete observation frame.
    observation.status=Status::NeedObservation;
    return true;
}
} // namespace tt
#endif
