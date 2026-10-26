#pragma once
// One bounded, provenance-carrying active-group observation. Ready means collection
// completed in one owner epoch; it is not an exact combat/safety-model certificate.
#include "service_model.h"
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace mb { struct Ctx; }

namespace mb::service {

enum class FrameStatus { Ready, NeedFuel, Stale, Incomplete };
enum class Availability { Unknown, Alive, Reward, Respawning };
enum ProjectionLimit : std::uint32_t
{
    HiddenMembers = 1u << 0,
    BasicOutgoingPassive = 1u << 1,
    EnhancedAttackEffect = 1u << 2,
    UnsupportedSpell = 1u << 3,
    PersistentDamage = 1u << 4,
    ExternalCampThreats = 1u << 5,
};

struct WorkFrame
{
    FrameStatus status{FrameStatus::Incomplete};
    State state;
    int epoch{-1}, group{-1};
    Availability availability{Availability::Unknown};
    int respawn_timer{-1};
    bool all_members_visible{};
    std::uint32_t projection_limits{};
    std::array<int,spell_slots> cooldowns{};
};

WorkFrame observe_work(Ctx& ctx, int group, bool moved_this_turn=false);
// Incremental refinement is valid only immediately after this applied primitive,
// with no intervening action. Refused commands or unrelated events require a new
// observe_work; unchanged HP entries are reused only under that caller contract.
FrameStatus refresh_work(Ctx& ctx, WorkFrame& frame, const Primitive& primitive,
                         bool moved_this_turn=false);

namespace observation_detail {

struct AvailabilityObservation
{
    Availability value{Availability::Unknown};
    int respawn_timer{-1};
};

// The production SDK adapter and fixture doubles enter this same collection path.
// Query exceptions deliberately propagate. No failed query is converted to zero HP.
template<class Backend>
void collect_member(Backend& backend, WorkFrame& frame, std::size_t i, bool refresh_hp)
{
    auto& member=frame.state.members[i];
    if (!backend.visible(i))
    {
        // Last personally observed residual is not an upper bound after sight is
        // lost. Preserve the design bound and explicitly withdraw exactness.
        member.hp=backend.design(i).hp;
        member.hp_exact=false;
        member.awake=false;
        member.stun_remaining=0;
        frame.all_members_visible=false;
        frame.projection_limits|=HiddenMembers;
        return;
    }
    if (!backend.living(i))
    {
        member.hp=0.0f;
        member.hp_exact=true;
        member.awake=false;
        member.stun_remaining=0;
        return;
    }
    member.awake=backend.awake(i);
    if (refresh_hp || !member.hp_exact)
    {
        const float actual=backend.health(i);
        if (!std::isfinite(actual) || actual<0.0f)
            throw std::runtime_error("error invalid service member health");
        member.hp=actual;
        member.hp_exact=true;
        // Only a source that can hit the stationary body needs a live stun query for
        // this next-volley projection. Zero elsewhere is conservative, not observed.
        member.stun_remaining=member.strength>0.0f &&
            distance(member.pos,frame.state.body.pos)<=member.range ? backend.stun(i) : 0;
    }
}

template<class Backend>
WorkFrame collect_work_frame(Backend& backend, int expected_epoch, int group, bool moved_this_turn)
{
    WorkFrame frame;
    frame.group=group;
    frame.epoch=backend.round();
    if (frame.epoch!=expected_epoch) { frame.status=FrameStatus::Stale; return frame; }
    const auto count=backend.member_count();
    if (count==0 || count>max_members) return frame;
    if (!backend.enough_fuel(count,false)) { frame.status=FrameStatus::NeedFuel; return frame; }
    backend.prepare();
    backend.body(frame,moved_this_turn);
    frame.state.member_count=count;
    frame.all_members_visible=true;
    bool any_visible=false;
    for (std::size_t i=0;i<count;++i)
    {
        frame.state.members[i]=backend.design(i);
        any_visible|=backend.visible(i);
        collect_member(backend,frame,i,true);
    }
    if (any_visible)
    {
        const auto observed=backend.availability();
        frame.availability=observed.value;
        frame.respawn_timer=observed.respawn_timer;
    }
    const bool other_sources_complete=backend.incoming_complete(frame);
    frame.state.incoming_complete=frame.all_members_visible && other_sources_complete;
    if (backend.round()!=frame.epoch) { frame.status=FrameStatus::Stale; return frame; }
    frame.status=FrameStatus::Ready;
    backend.commit(frame);
    return frame;
}

template<class Backend>
FrameStatus refresh_work_frame(Backend& backend, WorkFrame& frame, int expected_epoch,
                              const Primitive& primitive, bool moved_this_turn)
{
    const int epoch=backend.round();
    if (frame.status!=FrameStatus::Ready || epoch!=expected_epoch || frame.epoch!=epoch)
        return frame.status=FrameStatus::Stale;
    const auto count=backend.member_count();
    if (count==0 || count>max_members || count!=frame.state.member_count)
        return frame.status=FrameStatus::Incomplete;
    std::array<bool,max_members> affected{};
    if (primitive.kind==PrimitiveKind::basic)
    {
        const Member* primary=nullptr;
        for (std::size_t i=0;i<count;++i)
            if (frame.state.members[i].id==primitive.member_id) primary=&frame.state.members[i];
        if (!primary) return frame.status=FrameStatus::Incomplete;
        for (std::size_t i=0;i<count;++i)
            affected[i]=frame.state.members[i].id==primitive.member_id ||
                (frame.state.body.splash && distance(frame.state.members[i].pos,primary->pos)<=1);
    }
    else
    {
        if (primitive.slot<0 || static_cast<std::size_t>(primitive.slot)>=spell_slots)
            return frame.status=FrameStatus::Incomplete;
        const auto& spell=frame.state.spells[static_cast<std::size_t>(primitive.slot)];
        if (spell.kind==SpellKind::none) return frame.status=FrameStatus::Incomplete;
        for (std::size_t i=0;i<count;++i)
        {
            const auto& member=frame.state.members[i];
            affected[i]=spell.kind==SpellKind::aoe ? distance(member.pos,primitive.aim)<=spell.radius :
                ((spell.kind==SpellKind::damage || spell.kind==SpellKind::cc) &&
                 (primitive.member_id>=0 ? member.id==primitive.member_id : member.pos==primitive.aim));
        }
    }
    std::size_t query_bound=0;
    for (std::size_t i=0;i<count;++i) query_bound+=affected[i] || !frame.state.members[i].hp_exact;
    if (!backend.enough_fuel(query_bound,true)) return frame.status=FrameStatus::NeedFuel;
    WorkFrame fresh=frame;
    backend.prepare();
    backend.body(fresh,moved_this_turn);
    if (fresh.state.body.pos!=frame.state.body.pos) return frame.status=FrameStatus::Stale;
    fresh.all_members_visible=true;
    bool any_visible=false;
    for (std::size_t i=0;i<count;++i)
    {
        any_visible|=backend.visible(i);
        collect_member(backend,fresh,i,affected[i]);
    }
    fresh.availability=Availability::Unknown;
    fresh.respawn_timer=-1;
    if (any_visible)
    {
        const auto observed=backend.availability();
        fresh.availability=observed.value;
        fresh.respawn_timer=observed.respawn_timer;
    }
    const bool other_sources_complete=backend.incoming_complete(fresh);
    fresh.state.incoming_complete=fresh.all_members_visible && other_sources_complete;
    if (backend.round()!=epoch) return frame.status=FrameStatus::Stale;
    fresh.status=FrameStatus::Ready;
    backend.commit(fresh);
    frame=std::move(fresh);
    return frame.status;
}

} // namespace observation_detail
} // namespace mb::service
