#pragma once
#include "capital_service_program.h"

namespace wb::capital::service_native {
struct PreparationStamp {
    int owner{-1},round{-1};std::uint64_t input{},effect_generation{},observed_generation{};
    bool pending{},purchase{};
};
inline bool preparation_rebindable(const PreparationStamp& stamp,const service_program::Frame& f) {
    return stamp.owner>=0&&stamp.round>=0&&stamp.input&&!stamp.pending&&!stamp.purchase
        &&stamp.effect_generation==stamp.observed_generation&&f.observed&&f.fresh
        &&f.owner==stamp.owner&&f.round==stamp.round&&f.input==stamp.input;
}
inline bool same_physical(const service_program::Frame& a,const service_program::Frame& b) {
    return a.owner==b.owner&&a.round==b.round&&a.input==b.input&&a.pose==b.pose
        &&a.bag==b.bag&&a.gold==b.gold&&a.hp==b.hp&&a.alive==b.alive&&a.stunned==b.stunned
        &&a.completed_turn==b.completed_turn&&a.channel==b.channel&&a.permissions==b.permissions
        &&a.kit_revision==b.kit_revision&&a.inventory_revision==b.inventory_revision&&a.capability_revision==b.capability_revision
        &&a.occupancy_revision==b.occupancy_revision&&a.foe_revision==b.foe_revision&&a.lock_revision==b.lock_revision
        &&a.next_investment.target==b.next_investment.target&&a.next_investment.cost==b.next_investment.cost
        &&a.next_investment.revision==b.next_investment.revision&&a.next_investment.inventory_revision==b.next_investment.inventory_revision
        &&a.next_investment.observed==b.next_investment.observed&&a.next_investment.available==b.next_investment.available;
}
}

#ifndef WB_SERVICE_NATIVE_HOST_FREE
#include <cambc.h>
#include "capital_conversion_native.h"
#include <bit>
#include <optional>
#include <vector>

// Invocation-local operational authority for the complete Runtime economic
// interpreter. This owns no score inference, opponent forecast or safety proof.
// Every command consumes a lease before its host attempt; only an actual fresh
// packet can rebind remaining permissions. Known contact/lock changes hand the
// actual prefix back for replanning rather than borrowing an old quoted exit.
namespace wb::capital::service_native {
namespace p=service_program;
using Metadata=void(*)(void*,p::Frame&,bool preparation_only);
inline std::uint64_t mix(std::uint64_t a,std::uint64_t b){return (a^b)*1099511628211ull;}
inline std::uint64_t scalar(float value){return std::bit_cast<std::uint32_t>(value);}
struct Memory {
    std::optional<p::State> state;
    p::Frame actual{};p::Program program{};p::Grant grant{};
    p::Kind stage{p::Kind::Acquire};
    std::vector<p::Failure> failures;
    std::optional<p::ActualPurchase> purchase;
    std::uint64_t purchase_event{};
    std::uint64_t input{},sample{},action{},foe_revision{1},lock_revision{1},occupancy_revision{1},observation_revision{1};
    std::uint64_t effect_generation{},observed_generation{};
    int owner{-1},round{-1},team{-1};void* context{};Metadata metadata{};
    bool active{},pending{},replan{},boards_known{},executing{};
    cambc::bitboard foes,locks,occupied;
};
inline Memory& memory(){static Memory value;return value;}
inline bool current(){const auto& m=memory();return m.owner==static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()))&&m.round==static_cast<int>(cambc::round_number());}
inline bool owned(){const auto& m=memory();return m.active&&!m.replan&&current();}
inline bool refresh(const p::ActualPurchase* purchase=nullptr) {
    auto& m=memory();if(!current()||!m.state)return false;
    cambc::detail::champion_info raw{};cambc::detail::check(cambc::detail::sdk_champion(m.owner,&raw));if(!current())return false;
    m.team=raw.team;
    p::Frame f;f.owner=m.owner;f.round=m.round;f.input=m.input;f.sample=++m.sample;f.pose={raw.x,raw.y};f.hp=raw.health;
    const auto bits=cambc::champion_state::from_bits_retain(raw.state);
    f.alive=bits.contains(cambc::champion_state::alive);f.completed_turn=bits.contains(cambc::champion_state::completed_turn);
    f.channel=bits.contains(cambc::champion_state::submitting_gems)?p::Channel::Submit:
        bits.contains(cambc::champion_state::recalling)||bits.contains(cambc::champion_state::teleporting)?p::Channel::Other:p::Channel::None;
    f.bag=cambc::self().gem_count();f.gold=cambc::self().gold_count();
    const auto restrictions=f.alive?cambc::self().restrictions():cambc::buff_restrictions{};
    auto status=mix(raw.state,restrictions.bits());
    if(f.alive) {
        const auto buffs=cambc::self().buffs();f.stunned=buffs.has_stun();
        for(const auto& buff:buffs)std::visit([&](const auto& value){
            status=mix(status,static_cast<unsigned>(value.kind));status=mix(status,static_cast<std::uint32_t>(value.duration));
            if constexpr(requires{value.value;})status=mix(status,scalar(value.value));
        },buff);
    }
    if(bits.contains(cambc::champion_state::can_move)&&!restrictions.contains(cambc::buff_restrictions::move))f.permissions|=p::Move;
    if(bits.contains(cambc::champion_state::can_attack)&&!restrictions.contains(cambc::buff_restrictions::attack))f.permissions|=p::Attack;
    if(bits.contains(cambc::champion_state::can_cast)&&!restrictions.contains(cambc::buff_restrictions::cast))f.permissions|=p::Cast;
    if(f.alive&&!f.stunned&&!f.completed_turn&&f.channel==p::Channel::None) {
        if(!bits.contains(cambc::champion_state::submit_gems_started))f.permissions|=p::Submit;
        f.permissions|=p::Recall;
    }
    std::uint64_t kit=1469598103934665603ull;
    for(const auto value:{std::uint64_t(raw.type),scalar(raw.max_health),scalar(raw.strength),scalar(raw.focus),std::uint64_t(raw.move_speed),std::uint64_t(raw.attack_range)})kit=mix(kit,value);
    f.kit_revision=kit;f.capability_revision=mix(kit,status);
    if(f.alive) {
    auto foes=cambc::vision::champion_tiles(cambc::team::enemy);
    auto locks=cambc::vision::altar_lock_tiles(cambc::team::enemy);
    auto occupied=cambc::vision::champion_tiles()|cambc::vision::camp_tiles()|cambc::vision::teleport_tiles();
    if(cambc::map::is_in_bounds({raw.x,raw.y}))occupied.set({raw.x,raw.y},false);
    if(!current())return false;
    if(m.boards_known) {
        if(!(foes==m.foes)){++m.foe_revision;if(m.active)m.replan=true;}
        if(!(locks==m.locks)){++m.lock_revision;if(m.active)m.replan=true;}
        if(!(occupied==m.occupied))++m.occupancy_revision;
    }
    m.foes=std::move(foes);m.locks=std::move(locks);m.occupied=std::move(occupied);m.boards_known=true;
    }
    f.foe_revision=m.foe_revision;f.lock_revision=m.lock_revision;f.occupancy_revision=m.occupancy_revision;
    f.observation_revision=++m.observation_revision;f.fuel_revision=1;
    if(m.metadata)m.metadata(m.context,f,false);
    if(!current())return false;
    f.available_fuel=cambc::fuel_remaining();if(!current())return false;f.observed=f.fresh=true;
    if(!purchase&&m.purchase) {
        if(m.purchase->owner==f.owner&&m.purchase->round==f.round&&m.purchase->input==f.input) {
            m.purchase->sample=f.sample;purchase=&*m.purchase;
        } else m.purchase.reset(); // no attribution across a suspended readback
    }
    const auto observed=m.state->observe(f,purchase);if(!observed.accepted)return false;
    if(purchase)m.purchase.reset();
    m.actual=f;m.pending=false;m.observed_generation=m.effect_generation;if(m.replan)m.active=false;return true;
}
inline bool begin(int owner,int round,void* context,Metadata metadata) {
    auto& m=memory();m.owner=owner;m.round=round;++m.input;m.context=context;m.metadata=metadata;
    m.active=m.pending=m.replan=m.executing=false;
    if(!m.state)m.state.emplace(owner);
    return refresh();
}
inline bool rebind_preparation() {
    auto& m=memory();
    // Inside one continuing owner input, read-only preparation cannot change
    // physical facts. Do not repeat the full scene before the first useful
    // word. ANY attempted primitive revokes this memo, even a false command.
    const PreparationStamp stamp{m.owner,m.round,m.input,m.effect_generation,m.observed_generation,m.pending,m.purchase.has_value()};
    if(!current()||!m.state||m.state->awaiting_actual()||!preparation_rebindable(stamp,m.actual))return false;
    auto f=m.actual;f.sample=++m.sample;f.observation_revision=++m.observation_revision;
    if(m.metadata)m.metadata(m.context,f,true);
    if(!same_physical(f,m.actual))return false;
    f.available_fuel=cambc::fuel_remaining();if(!current())return false;
    const auto observed=m.state->observe(f);if(!observed.accepted)return false;
    m.actual=f;return true;
}
inline p::Grant publish(p::Kind kind,p::FirstAction first) {
    auto& m=memory();if(!m.state||!current())return {};
    m.program={};m.program.kind=kind;m.program.source=m.actual;m.program.action=first;
    m.program.risk={1,0,0,m.round+1,p::Knowledge::Unknown,p::Knowledge::Unknown};
    m.program.complete_interpreter=true;m.program.funded=true;
    m.grant=m.state->grant(m.actual,m.program);m.active=p::granted(m.grant);
    if(m.active)m.executing=true;return m.grant;
}
// 9K is the existing dispatch obligation; the independent 9K readback reserve
// is the native observation obligation. Native health still qualifies this sum.
inline bool before(std::uint32_t required,std::uint64_t action_id) {
    auto& m=memory();if(!owned()||(!rebind_preparation()&&!refresh())||!owned())return false;
    auto kind=m.stage;
    if(required==p::Buy)kind=m.state->investment_obligation(m.actual).affordable?p::Kind::Investment:p::Kind::Recovery;
    if(p::detail::camp_kind(kind)&&(m.actual.selected_group<0||!m.actual.source_life||!m.actual.source_revision))kind=p::Kind::Acquire;
    constexpr conversion_native::Options obligations{};
    const auto grant=publish(kind,{p::FirstKind::PhysicalWord,action_id? action_id:++m.action,required,0,9000u+obligations.observation_reserve});
    if(!p::granted(grant))return false;
    m.actual.available_fuel=cambc::fuel_remaining();
    if(!current()||!m.state->before_primitive(grant.lease,m.actual))return false;
    ++m.effect_generation;m.pending=true;return true;
}
inline void after(){auto& m=memory();if(!refresh())m.active=false;}
inline void release(){memory().active=memory().executing=false;}
template<bool Enabled> struct Scope {};
template<> struct Scope<true> {~Scope(){release();}};
} // namespace wb::capital::service_native
#endif
