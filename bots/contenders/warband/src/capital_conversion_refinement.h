#pragma once
#include "capital_conversion_native.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

// One admitted physical refinement, not a bank forecast or a tactical ranker.
// Runtime supplies ordered landing nominations and an ACTUAL paid-ingress bit.
// Prepare BEFORE Adapter::fund_fallback: that adapter claims on invocation.
// NoPlan must never be used to invoke it. Adapter::snapshot remains the sole
// observation/receipt writer; this helper neither observes nor changes MAC.
namespace wb::capital::conversion_refinement {
namespace n=conversion_native;
namespace t=tribute;
inline constexpr std::size_t landing_limit=8;
enum class Kind {NoPlan,Move,ActualClosedOrPaidPrefix};
enum class ClosureScope {None,MovementWord,ActualPhysicalState};
enum class Closure {None,PaidMovementSpent,Dead,Stunned,Channel,CompletedTurn};
enum class Reason {
    None,MissingObservation,Stale,Changed,NoGeometry,InvalidBudget,NeedFuel,
    NoAdmittedLanding,TooManyLandings,MovementRestricted,NoMovementUnpaid,
    ReleasedCapital,NoLegalLanding,Consumed,PermissionChanged,Refused
};
struct Options {
    n::Options reserves{};
    // Borrowed for PREPARE only. No ranking, enemy fact or span enters Plan.
    std::span<const t::Point> admitted_destinations{};
};
struct Plan {
    Kind kind{Kind::NoPlan};Reason reason{Reason::None};
    ClosureScope closure_scope{ClosureScope::None};Closure closure{Closure::None};
    n::Epoch source{};std::uint64_t sample{};t::Point position{},destination{};
    bool paid_ingress{},remaining_attack{},remaining_cast{},consumed{};
    // Copy SELF dependencies, not Geometry's borrowed altars/IDs or foe state.
    t::Observation own{};t::Context context{};
    float hp{},max_hp{};int move_speed{};unsigned restrictions{};bool completed_turn{};
    n::Options reserves{};
};
enum class Status {NoPlan,Closed,Moved,Refused,Stale,Changed,Consumed,NeedFuel,PermissionChanged};
struct Execution {
    Status status{Status::NoPlan};Reason reason{Reason::None};
    bool attempted{},moved{},closed{},fresh{};
    ClosureScope closure_scope{ClosureScope::None};
    // These are the supplied ACTUAL pre-command allowances, never fabricated
    // post-Move values. A/C may remain true for a MovementWord closure.
    bool remaining_attack{},remaining_cast{};
};
inline bool playable(const Plan& plan){return plan.kind!=Kind::NoPlan&&!plan.consumed;}
namespace detail {
inline std::uint64_t full_tail(const n::Options& o){return std::uint64_t(o.fallback_reserve)+o.observation_reserve;}
inline std::uint64_t move_tail(const n::Options& o){return std::uint64_t(o.approach_reserve)+o.observation_reserve;}
inline bool budget_valid(const n::Options& o){
    return o.observation_reserve&&o.approach_reserve&&o.fallback_reserve>=o.approach_reserve&&
        full_tail(o)<=std::numeric_limits<unsigned>::max();
}
inline bool adjacent(t::Point a,t::Point b){
    const auto dx=std::int64_t(a.x)-b.x,dy=std::int64_t(a.y)-b.y;
    return dx>=-1&&dx<=1&&dy>=-1&&dy<=1&&(dx||dy);
}
inline bool bound(const n::Packet& packet,const t::Context& context,const n::Geometry& geometry,n::Epoch epoch){
    return epoch.owner>=0&&epoch.round>=0&&epoch.input&&n::same(geometry.source,epoch)&&
        t::detail::valid(packet.own)&&packet.own.owner==epoch.owner&&packet.own.round==epoch.round&&
        packet.own.input==epoch.input&&packet.own.channel!=t::Channel::Unknown&&t::detail::current(context,packet.own)&&
        std::isfinite(packet.hp)&&std::isfinite(packet.max_hp)&&packet.hp>=0&&packet.max_hp>0&&(!packet.own.alive||packet.hp>0);
}
template<class Host> bool current(Host& host,n::Epoch epoch){return host.owner()==epoch.owner&&host.round()==epoch.round;}
template<class Host> bool funded(Host& host,n::Epoch epoch,std::uint64_t reserve){
    return current(host,epoch)&&host.fuel()>=reserve&&current(host,epoch);
}
inline bool unchanged(const Plan& plan,const n::Packet& packet,const t::Context& context){
    return plan.sample==packet.own.sample&&plan.position==packet.own.position&&
        t::detail::same(plan.own,packet.own)&&t::detail::same(plan.context,context)&&
        plan.hp==packet.hp&&plan.max_hp==packet.max_hp&&plan.move_speed==packet.move_speed&&
        plan.restrictions==packet.restrictions&&plan.completed_turn==packet.completed_turn;
}
inline Closure actual_closed(const n::Packet& packet){
    if(!packet.own.alive)return Closure::Dead;
    if(packet.own.stunned)return Closure::Stunned;
    if(packet.own.channel!=t::Channel::None)return Closure::Channel;
    if(packet.completed_turn)return Closure::CompletedTurn;
    return Closure::None;
}
}
// At most eight read-only legality checks. The tail declarations are empirical
// caller obligations shared with the native adapter, not a WASM fuel theorem.
template<class Host>
Plan prepare(const n::Packet& packet,const t::Context& context,const n::Geometry& geometry,
             n::Epoch epoch,bool paid_ingress,Host& host,Options options={}){
    Plan plan;plan.source=epoch;plan.sample=packet.own.sample;plan.position=packet.own.position;
    plan.paid_ingress=paid_ingress;plan.remaining_attack=packet.own.can_attack;plan.remaining_cast=packet.own.can_cast;
    plan.own=packet.own;plan.context=context;plan.hp=packet.hp;plan.max_hp=packet.max_hp;
    plan.move_speed=packet.move_speed;plan.restrictions=packet.restrictions;plan.completed_turn=packet.completed_turn;
    plan.reserves=options.reserves;
    const auto no=[&](Reason reason){plan.kind=Kind::NoPlan;plan.reason=reason;return plan;};
    if(!detail::bound(packet,context,geometry,epoch))return no(Reason::MissingObservation);
    if(!detail::current(host,epoch))return no(Reason::Stale);
    const auto closed=detail::actual_closed(packet);
    if(closed!=Closure::None){
        plan.kind=Kind::ActualClosedOrPaidPrefix;plan.closure_scope=ClosureScope::ActualPhysicalState;plan.closure=closed;return plan;
    }
    if(!packet.own.can_move){
        if(!paid_ingress)return no(Reason::NoMovementUnpaid);
        plan.kind=Kind::ActualClosedOrPaidPrefix;plan.closure_scope=ClosureScope::MovementWord;
        plan.closure=Closure::PaidMovementSpent;return plan;
    }
    if(!packet.own.bag)return no(Reason::ReleasedCapital);
    if(packet.restrictions&1u)return no(Reason::MovementRestricted);
    if(!geometry.root_history_known||geometry.exclusive_cast_used||!geometry.static_walkable||packet.move_speed<1)
        return no(Reason::NoGeometry);
    if(!detail::budget_valid(options.reserves))return no(Reason::InvalidBudget);
    if(options.admitted_destinations.empty())return no(Reason::NoAdmittedLanding);
    if(options.admitted_destinations.size()>landing_limit)return no(Reason::TooManyLandings);
    for(std::size_t index=0;index<options.admitted_destinations.size();++index){
        const auto landing=options.admitted_destinations[index];bool duplicate=false;
        for(std::size_t earlier=0;earlier<index;++earlier)duplicate|=landing==options.admitted_destinations[earlier];
        if(duplicate||!detail::adjacent(packet.own.position,landing)||!t::detail::point(landing))continue;
        if(!detail::funded(host,epoch,detail::full_tail(options.reserves)))
            return no(detail::current(host,epoch)?Reason::NeedFuel:Reason::Stale);
        const bool walkable=geometry.static_walkable(geometry.context,landing);
        if(!detail::current(host,epoch))return no(Reason::Stale);
        if(!walkable)continue;
        const bool legal=host.move_legal(epoch,packet,landing,geometry);
        if(!detail::current(host,epoch))return no(Reason::Stale);
        if(!legal)continue;
        if(!detail::funded(host,epoch,detail::full_tail(options.reserves)))
            return no(detail::current(host,epoch)?Reason::NeedFuel:Reason::Stale);
        plan.kind=Kind::Move;plan.destination=landing;return plan;
    }
    return no(Reason::NoLegalLanding);
}
// Invoke inside the already funded native callback with THAT callback's fresh
// Packet/Context and the current caller Epoch. No intervening own host action
// is permitted between prepare and this call. move_legal does not reread MAC.
template<class Host>
Execution execute(Plan& plan,const n::Packet& packet,const t::Context& context,
                  const n::Geometry& geometry,n::Epoch epoch,Host& host){
    Execution out;out.remaining_attack=packet.own.can_attack;out.remaining_cast=packet.own.can_cast;
    const auto result=[&](Status status,Reason reason){out.status=status;out.reason=reason;return out;};
    if(plan.kind==Kind::NoPlan)return result(Status::NoPlan,plan.reason);
    if(plan.consumed)return result(Status::Consumed,Reason::Consumed);
    if(!n::same(plan.source,epoch)||!detail::bound(packet,context,geometry,epoch)||!detail::current(host,epoch))
        return result(Status::Stale,Reason::Stale);
    if(!detail::unchanged(plan,packet,context))return result(Status::Changed,Reason::Changed);
    out.fresh=true;
    if(plan.kind==Kind::ActualClosedOrPaidPrefix){
        const bool physical=plan.closure_scope==ClosureScope::ActualPhysicalState&&detail::actual_closed(packet)==plan.closure;
        const bool paid=plan.closure_scope==ClosureScope::MovementWord&&plan.paid_ingress&&!packet.own.can_move&&
            plan.closure==Closure::PaidMovementSpent;
        if(!physical&&!paid)return result(Status::Changed,Reason::Changed);
        plan.consumed=true;out.closed=true;out.closure_scope=plan.closure_scope;
        return result(Status::Closed,Reason::None);
    }
    // Consume before any command (including exceptions). A failed final check
    // cannot retry this prepared authority with another landing or callback.
    plan.consumed=true;
    if(!packet.own.can_move||(packet.restrictions&1u)||detail::actual_closed(packet)!=Closure::None||
        !geometry.root_history_known||geometry.exclusive_cast_used||!geometry.static_walkable||packet.move_speed<1||
        !detail::adjacent(packet.own.position,plan.destination)||!t::detail::point(plan.destination))
        return result(Status::PermissionChanged,Reason::PermissionChanged);
    if(!detail::budget_valid(plan.reserves)||!detail::funded(host,epoch,detail::full_tail(plan.reserves))){
        if(!detail::current(host,epoch)){out.fresh=false;return result(Status::Stale,Reason::Stale);}
        return result(Status::NeedFuel,Reason::NeedFuel);
    }
    const bool walkable=geometry.static_walkable(geometry.context,plan.destination);
    if(!detail::current(host,epoch)){out.fresh=false;return result(Status::Stale,Reason::Stale);}
    if(!walkable)return result(Status::PermissionChanged,Reason::PermissionChanged);
    const bool legal=host.move_legal(epoch,packet,plan.destination,geometry);
    if(!detail::current(host,epoch)){out.fresh=false;return result(Status::Stale,Reason::Stale);}
    if(!legal)return result(Status::PermissionChanged,Reason::PermissionChanged);
    if(!detail::funded(host,epoch,detail::move_tail(plan.reserves))){
        if(!detail::current(host,epoch)){out.fresh=false;return result(Status::Stale,Reason::Stale);}
        return result(Status::NeedFuel,Reason::NeedFuel);
    }
    out.attempted=true;out.fresh=false;out.moved=host.move(plan.destination);
    // Even accepted movement is not a stamped post-command packet. Native
    // Adapter's paid snapshot must confirm endpoint/MAC and update the monitor.
    if(!detail::current(host,epoch))return result(Status::Stale,Reason::Stale);
    return result(out.moved?Status::Moved:Status::Refused,out.moved?Reason::None:Reason::Refused);
}
}
