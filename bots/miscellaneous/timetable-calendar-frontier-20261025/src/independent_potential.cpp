// Limited refinement of a completed static potential and an actual walk suffix.
// Source completeness/fixed strength, historical path provenance, one interval per
// exit step and numeric SDK fuel costs remain separate caller/native premises.
#include "production_potential.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace tt::potential_detail {
enum class Status { Ready, NeedCompute, NeedObservation, Stale };
struct Flags { bool alive{},channeling{},can_move{}; };
template<class Point> struct Result
{
    Status status{Status::NeedObservation};
    int epoch{};
    Point pos;
    float hp{};
    bool can_move{},safe_exit{},safe_wait{},safe_return{},safe_service{},moved{};
};
template<class Point> bool adjacent(Point a,Point b)
{
    const auto dx=static_cast<std::int64_t>(a.x)-b.x,dy=static_cast<std::int64_t>(a.y)-b.y;
    return dx>=-1 && dx<=1 && dy>=-1 && dy<=1 && (dx!=0 || dy!=0);
}

// required_hp is only a retained, caller-validated whole-word bound. It carries
// no own-state, owner-epoch, movement or target authority. This operator refines
// only its next ordinary leg, without replaying the field/path computation.
template<class Actor,class Point,class Clock,class Legal,class Headroom>
Result<Point> transit_authority(double required_hp,Point expected_origin,Point destination,
                                Actor& actor,Clock clock,Legal legal,Headroom headroom)
{
    Result<Point> result;
    if (!std::isfinite(required_hp) || required_hp<0.0 ||
        !adjacent(expected_origin,destination)) return result;
    if (!headroom()) { result.status=Status::NeedCompute; return result; }
    result.epoch=clock(); // stamp before every own-state import
    result.pos=actor.position();
    result.hp=actor.health();
    const auto flags=actor.flags();
    result.can_move=flags.alive && !flags.channeling && flags.can_move;
    if (actor.position()!=result.pos || clock()!=result.epoch)
    { result.status=Status::Stale; return result; }
    if (result.pos!=expected_origin || !std::isfinite(result.hp) || result.hp<0.0f ||
        !flags.alive || flags.channeling) return result;
    result.safe_return=static_cast<double>(result.hp)>required_hp;
    // These flags describe different represented operators. The scalar supplied
    // to this seam does not invent wait/service/exit certificates for them.
    result.status=Status::Ready;
    if (!result.safe_return || !result.can_move) return result;
    const bool allowed=legal(destination); // fresh point observations, no field
    const bool enough=headroom();
    // Position can itself suspend; the last epoch read must follow it. A native
    // fuel bound for the final check-to-command tail remains a separate premise.
    if (actor.position()!=result.pos || clock()!=result.epoch)
    { result.status=Status::Stale; return result; }
    if (!enough) { result.status=Status::NeedCompute; return result; }
    if (!allowed) { result.status=Status::NeedObservation; return result; }
    const bool applied=actor.move_absolute(destination);
    if (clock()!=result.epoch) { result.status=Status::Stale; return result; }
    if (!applied) { result.status=Status::NeedObservation; return result; }
    result.pos=destination;
    result.can_move=false;
    result.moved=true;
    return result; // HP was sampled before this move, never a new work frame
}

// This is the actual production caller composition, also compiled by the spy
// fixture. Every published Ready result has a position-first final epoch fence.
template<class Actor,class Point,class Clock,class Legal,class Headroom>
Result<Point> authority(const mb::production::potential::Field& field,std::span<const Point> path,
                        bool execute_exit,Actor& actor,Clock clock,Legal legal,Headroom headroom,
                        bool require_refusal_cover=false,std::optional<Point> service_point={},
                        unsigned service_waits=0)
{
    namespace potential=mb::production::potential;
    Result<Point> result;
    if (field.status()==potential::Status::NeedCompute)
    { result.status=Status::NeedCompute; return result; }
    if (field.status()!=potential::Status::Ready || path.empty() || service_waits>2 ||
        (service_waits!=0 && !service_point)) return result;
    const auto width=static_cast<std::uint64_t>(field.width()),height=static_cast<std::uint64_t>(field.height());
    if (width==0 || height==0 || path.size()>width*height) return result;
    if (!headroom()) { result.status=Status::NeedCompute; return result; }
    result.epoch=clock(); // before all own-state imports
    result.pos=actor.position(); result.hp=actor.health();
    const auto flags=actor.flags();
    result.can_move=flags.alive && !flags.channeling && flags.can_move;
    if (actor.position()!=result.pos || clock()!=result.epoch)
    { result.status=Status::Stale; return result; }
    if (path.front()!=result.pos || !std::isfinite(result.hp) || result.hp<0) return result;
    potential::PathBound bound;
    double terminal{},maximum_exposure{};
    for (std::size_t i=0;i<path.size();++i)
    {
        if ((i&15u)==0 && !headroom()) { result.status=Status::NeedCompute; return result; }
        if (i && !adjacent(path[i-1],path[i])) return result;
        const auto value=field.at({path[i].x,path[i].y});
        if (value.status!=potential::Status::Ready) return result;
        if (i && !potential::detail::add_upper(bound.exit_before_yield,value.upper,bound.exit_before_yield)) return result;
        if (!potential::detail::add_upper(bound.wait_then_exit,value.upper,bound.wait_then_exit)) return result;
        maximum_exposure=std::max(maximum_exposure,value.upper);
        terminal=value.upper;
    }
    // A complete approach word can carry up to two separately declared service
    // phases at its future frontier. They are physical waits, not free planning.
    // Future occupancy and completing actual work inside this allocation remain
    // conditional; failure must withdraw to the represented return operator.
    if (service_waits!=0)
    {
        if (std::find(path.begin(),path.end(),*service_point)==path.end()) return result;
        const auto charge=field.at({service_point->x,service_point->y});
        if (charge.status!=potential::Status::Ready) return result;
        for (unsigned phase=0;phase<service_waits;++phase)
            if (!potential::detail::add_upper(bound.exit_before_yield,charge.upper,bound.exit_before_yield) ||
                !potential::detail::add_upper(bound.wait_then_exit,charge.upper,bound.wait_then_exit)) return result;
        maximum_exposure=std::max(maximum_exposure,charge.upper);
    }
    if (terminal!=0) return result; // actual suffix must terminate at a model-quiet point
    result.safe_exit=static_cast<double>(result.hp)>bound.exit_before_yield;
    result.safe_wait=static_cast<double>(result.hp)>bound.wait_then_exit;
    double return_bound{},service_bound{};
    if (!potential::detail::add_upper(bound.exit_before_yield,maximum_exposure,return_bound) ||
        !potential::detail::add_upper(bound.wait_then_exit,maximum_exposure,service_bound))
        return result;
    // One refusal can occur at ANY represented origin, including a later
    // higher-damage landing. Repeated future blocking is not bounded.
    result.safe_return=static_cast<double>(result.hp)>return_bound;
    result.safe_service=static_cast<double>(result.hp)>service_bound;
    if (!headroom()) { result.status=Status::NeedCompute; return result; }
    if (actor.position()!=result.pos || clock()!=result.epoch)
    { result.status=Status::Stale; return result; }
    result.status=Status::Ready;
    if (!execute_exit || !result.safe_exit || (require_refusal_cover && !result.safe_return) ||
        !result.can_move || path.size()<2) return result;
    const auto destination=path[1];
    const bool allowed=legal(destination); // cached terrain + fresh point queries only
    if (!headroom()) { result.status=Status::NeedCompute; return result; }
    // A query can resume in a later epoch at the same position. The final clock
    // must follow the final position query, immediately before the command.
    if (actor.position()!=result.pos || clock()!=result.epoch)
    { result.status=Status::Stale; return result; }
    if (!allowed) { result.status=Status::NeedObservation; return result; }
    const bool applied=actor.move_absolute(destination);
    if (clock()!=result.epoch) { result.status=Status::Stale; return result; }
    if (!applied) { result.status=Status::NeedObservation; return result; }
    result.pos=destination; result.can_move=false; result.moved=true;
    return result; // pre-move HP; no authority for another service command
}
} // namespace tt::potential_detail

#ifndef TT_POTENTIAL_FIXTURE
#include "runtime.h"
#include "ordinary_step.h"
namespace tt {
namespace {
struct PotentialActor
{
    cambc::champion& actor;
    vec2 position() const { return actor.position(); }
    float health() const { return actor.health(); }
    potential_detail::Flags flags() const
    {
        const auto state=actor.state();
        return {state.contains(cambc::champion_state::alive),
            state.contains(cambc::champion_state::recalling) ||
            state.contains(cambc::champion_state::submitting_gems) ||
            state.contains(cambc::champion_state::teleporting),
            state.contains(cambc::champion_state::can_move)};
    }
    bool move_absolute(vec2 destination) { return actor.move_absolute(destination); }
};
}

PotentialAuthority potential_authority(const mb::production::potential::Field& field,
                                      std::span<const vec2> path,PotentialUse use,
                                      vec2 service_point,unsigned service_waits)
{
    PotentialActor actor{cambc::self()};
    const auto guarded=potential_detail::authority(field,path,use!=PotentialUse::Inspect,actor,
        [] { return static_cast<int>(cambc::round_number()); },
        [](vec2 destination) {
            return ordinary_step_may_attempt(destination);
        },[] { return budget(6000); },use==PotentialUse::CertifiedEntry,
        service_waits ? std::optional<vec2>{service_point} : std::nullopt,service_waits);
    PotentialAuthority result;
    switch (guarded.status)
    {
    case potential_detail::Status::Ready: result.status=Status::Ready; break;
    case potential_detail::Status::NeedCompute: result.status=Status::NeedCompute; break;
    case potential_detail::Status::NeedObservation: result.status=Status::NeedObservation; break;
    case potential_detail::Status::Stale: result.status=Status::Stale; break;
    }
    result.epoch=guarded.epoch; result.pos=guarded.pos; result.hp=guarded.hp;
    result.can_move=guarded.can_move; result.safe_exit=guarded.safe_exit;
    result.safe_wait=guarded.safe_wait; result.safe_return=guarded.safe_return;
    result.safe_service=guarded.safe_service; result.moved=guarded.moved;
    return result;
}

PotentialAuthority transit_authority(double required_hp,vec2 expected_origin,vec2 destination)
{
    PotentialActor actor{cambc::self()};
    const auto guarded=potential_detail::transit_authority(required_hp,expected_origin,destination,actor,
        [] { return static_cast<int>(cambc::round_number()); },
        [](vec2 point) {
            return ordinary_step_may_attempt(point);
        },[] { return budget(6000); });
    PotentialAuthority result;
    switch (guarded.status)
    {
    case potential_detail::Status::Ready: result.status=Status::Ready; break;
    case potential_detail::Status::NeedCompute: result.status=Status::NeedCompute; break;
    case potential_detail::Status::NeedObservation: result.status=Status::NeedObservation; break;
    case potential_detail::Status::Stale: result.status=Status::Stale; break;
    }
    result.epoch=guarded.epoch; result.pos=guarded.pos; result.hp=guarded.hp;
    result.can_move=guarded.can_move; result.safe_exit=guarded.safe_exit;
    result.safe_wait=guarded.safe_wait; result.safe_return=guarded.safe_return;
    result.safe_service=guarded.safe_service; result.moved=guarded.moved;
    return result;
}
} // namespace tt
#endif
