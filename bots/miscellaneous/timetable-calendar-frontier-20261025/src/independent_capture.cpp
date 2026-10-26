// Fresh one-input capture refinement, authored from SDK lifecycle/point APIs.
// World member geometry and base strength/range are a caller-owned immutable
// complete upper envelope. Reward removes only the selected group's sources for
// this invocation. No timer, receipt, future quietness or full-cycle proof follows.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace tt::capture_detail {
enum class Status { Ready, NeedCompute, NeedObservation, Stale };
enum class Life { Unknown, Alive, Reward, Respawning };
struct Flags { bool alive{},channeling{},can_move{}; };
struct Bag { std::uint32_t gold{},gems{}; };
template<class Point> struct Result
{
    Status status{Status::NeedObservation};
    int epoch{},group{-1};
    Point pos;
    float hp{};
    std::uint32_t gold{},gems{};
    Life availability{Life::Unknown};
    double current_damage{},next_damage{};
    bool can_move{},safe_input{},holding{},moved{},visible_enemy{},zero_external{};
};
template<class A,class B> bool same(A a,B b) { return a.x==b.x && a.y==b.y; }
template<class A,class B> std::int64_t distance(A a,B b)
{
    const auto dx=static_cast<std::int64_t>(a.x)-b.x,dy=static_cast<std::int64_t>(a.y)-b.y;
    return std::max(dx<0 ? -dx : dx,dy<0 ? -dy : dy);
}
template<class Group,class Point> bool member_spawn(const Group& group,Point point)
{
    for (const auto& member : group.members) if (same(member.pos,point)) return true;
    return false;
}
inline double saturated_add(double value,double amount,double cap)
{ return amount>=cap-value ? cap : value+amount; }
struct Damage
{
    Status status{Status::NeedObservation};
    double current{},next{};
};
// Exact saturation of the static external-source sums at HP is enough to decide
// zero versus positive exposure. Sleep, current life, shields and CC grant no
// omission or reduction. Only a fresh selected-group Reward permits its exclusion.
template<class Groups,class Point,class Headroom>
Damage external_damage(const Groups& groups,int selected,Point current,Point next,
                       double cap,Headroom headroom)
{
    Damage result;
    if (selected<0 || static_cast<std::size_t>(selected)>=groups.size() ||
        !std::isfinite(cap) || cap<=0.0) return result;
    std::size_t visited=0;
    for (std::size_t g=0;g<groups.size();++g)
    {
        if (static_cast<int>(g)==selected) continue;
        for (const auto& member : groups[g].members)
        {
            if ((visited++&15u)==0 && !headroom())
            { result.status=Status::NeedCompute; return result; }
            if (!std::isfinite(member.strength) || member.strength<0.0f ||
                (member.strength>0.0f && member.range<0)) return result;
            if (member.strength<=0.0f) continue;
            if (distance(member.pos,current)<=member.range)
                result.current=saturated_add(result.current,member.strength,cap);
            if (distance(member.pos,next)<=member.range)
                result.next=saturated_add(result.next,member.strength,cap);
            if (result.current==cap && result.next==cap)
            { result.status=Status::Ready; return result; }
        }
    }
    result.status=Status::Ready;
    return result;
}

// The caller dates the CaptureWord and selects its next waypoint. That provenance
// is not encoded by destination alone. This shared adapter establishes only fresh
// one-input physical authority; each later capture input must be observed again.
template<class Groups,class Point,class Actor,class Clock,class GroupState,
         class Enemies,class Legal,class Headroom>
Result<Point> authority(const Groups& groups,int selected,Point expected,Point destination,
                        Actor& actor,Clock clock,GroupState group_state,Enemies enemies,
                        Legal legal,Headroom headroom,bool allow_staging=false)
{
    Result<Point> result;
    result.group=selected;
    if (selected<0 || static_cast<std::size_t>(selected)>=groups.size()) return result;
    const bool hold=same(expected,destination);
    if ((!hold && distance(expected,destination)!=1) ||
        (hold && !allow_staging && !member_spawn(groups[static_cast<std::size_t>(selected)],expected))) return result;
    if (!headroom()) { result.status=Status::NeedCompute; return result; }
    result.epoch=clock(); // before all own position/HP/bag/flag observations
    result.pos=actor.position();
    result.hp=actor.health();
    const auto bag=actor.bag();
    result.gold=bag.gold; result.gems=bag.gems;
    const auto flags=actor.flags();
    result.can_move=flags.alive && !flags.channeling && flags.can_move;
    const auto fresh=[&] { return same(actor.position(),result.pos) && clock()==result.epoch; };
    if (!fresh()) { result.status=Status::Stale; return result; }
    if (!same(result.pos,expected) || !std::isfinite(result.hp) || result.hp<=0.0f ||
        !flags.alive || flags.channeling) return result;
    result.availability=group_state(selected);
    if (!fresh()) { result.status=Status::Stale; return result; }
    if (result.availability!=Life::Reward)
    {
        // Fresh body/bag/lifecycle remain observations even after capture ends.
        // This Ready result grants no capture or movement authority and does not
        // attribute a changed bag to a particular group or a timer transition.
        if (result.availability==Life::Alive || result.availability==Life::Respawning)
            result.status=Status::Ready;
        return result;
    }
    result.visible_enemy=enemies(); // fresh personal perspective, never cached allies/world
    if (!fresh()) { result.status=Status::Stale; return result; }
    if (result.visible_enemy) return result;
    const auto damage=external_damage(groups,selected,result.pos,destination,result.hp,headroom);
    result.current_damage=damage.current; result.next_damage=damage.next;
    if (!fresh()) { result.status=Status::Stale; return result; }
    if (damage.status!=Status::Ready) { result.status=damage.status; return result; }
    result.zero_external=damage.current==0.0 && damage.next==0.0;
    // Positive external exposure is unsupported in this first bridge. This is
    // neither an infeasibility claim nor admission to consume a finite HP balance.
    if (!result.zero_external) return result;
    const bool prefix_headroom=headroom();
    if (!fresh()) { result.status=Status::Stale; return result; }
    if (!prefix_headroom) { result.status=Status::NeedCompute; return result; }
    result.safe_input=true;
    result.status=Status::Ready;
    if (hold) { result.holding=member_spawn(groups[static_cast<std::size_t>(selected)],expected); return result; }
    if (!result.can_move) return result;
    const bool allowed=legal(destination);
    const bool enough=headroom();
    if (!fresh()) { result.status=Status::Stale; return result; }
    if (!enough) { result.status=Status::NeedCompute; return result; }
    if (!allowed) { result.status=Status::NeedObservation; return result; }
    const bool applied=actor.move_absolute(destination);
    if (clock()!=result.epoch) { result.status=Status::Stale; return result; }
    if (!applied) { result.status=Status::NeedObservation; return result; }
    result.pos=destination; result.can_move=false; result.moved=true;
    return result; // bag/HP precede movement and do not acknowledge a payout
}
} // namespace tt::capture_detail

#ifndef TT_CAPTURE_FIXTURE
#include "runtime.h"
#include "ordinary_step.h"
namespace tt {
namespace {
struct CaptureActor
{
    cambc::champion& actor;
    vec2 position() const { return actor.position(); }
    float health() const { return actor.health(); }
    capture_detail::Bag bag() const { return {actor.gold_count(),actor.gem_count()}; }
    capture_detail::Flags flags() const
    {
        const auto state=actor.state();
        return {state.contains(cambc::champion_state::alive),
            state.contains(cambc::champion_state::recalling) ||
            state.contains(cambc::champion_state::submitting_gems) ||
            state.contains(cambc::champion_state::teleporting),
            state.contains(cambc::champion_state::can_move)};
    }
    bool move_absolute(vec2 point) { return actor.move_absolute(point); }
};
}
CaptureAuthority capture_authority(int selected,vec2 expected_origin,vec2 destination,bool allow_staging)
{
    const auto& world=World::get(); // immutable source root initialized by caller
    CaptureActor actor{cambc::self()};
    const auto guarded=capture_detail::authority(world.groups,selected,expected_origin,destination,actor,
        [] { return static_cast<int>(cambc::round_number()); },
        [&](int group) {
            // The SDK itself checks current visibility. Losing it is an expected
            // observation withdrawal; every other engine error remains unexpected.
            try
            {
                const auto state=world.handles[static_cast<std::size_t>(group)].state();
                if (std::holds_alternative<cambc::map::camp_group_alive>(state)) return capture_detail::Life::Alive;
                if (std::holds_alternative<cambc::map::camp_group_reward_available>(state)) return capture_detail::Life::Reward;
                return capture_detail::Life::Respawning;
            }
            catch (const cambc::error& error)
            {
                if (error.code()==cambc::error_code::api_not_in_vision) return capture_detail::Life::Unknown;
                throw;
            }
        },[] { return !cambc::vision::champions(cambc::team::enemy).empty(); },
        [](vec2 point) {
            return ordinary_step_may_attempt(point);
        },[] { return budget(6000); },allow_staging);
    CaptureAuthority result;
    switch (guarded.status)
    {
    case capture_detail::Status::Ready: result.status=Status::Ready; break;
    case capture_detail::Status::NeedCompute: result.status=Status::NeedCompute; break;
    case capture_detail::Status::NeedObservation: result.status=Status::NeedObservation; break;
    case capture_detail::Status::Stale: result.status=Status::Stale; break;
    }
    switch (guarded.availability)
    {
    case capture_detail::Life::Unknown: result.availability=Availability::Unknown; break;
    case capture_detail::Life::Alive: result.availability=Availability::Alive; break;
    case capture_detail::Life::Reward: result.availability=Availability::Reward; break;
    case capture_detail::Life::Respawning: result.availability=Availability::Respawning; break;
    }
    result.epoch=guarded.epoch; result.group=guarded.group; result.pos=guarded.pos; result.hp=guarded.hp;
    result.gold=guarded.gold; result.gems=guarded.gems; result.current_damage=guarded.current_damage;
    result.next_damage=guarded.next_damage; result.can_move=guarded.can_move; result.safe_input=guarded.safe_input;
    result.holding=guarded.holding; result.moved=guarded.moved; result.visible_enemy=guarded.visible_enemy;
    result.zero_external=guarded.zero_external;
    return result;
}
} // namespace tt
#endif
