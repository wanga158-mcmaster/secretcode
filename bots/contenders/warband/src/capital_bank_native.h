#pragma once
#include <cambc.h>
#include "capital_bank.h"
#include "capital_order_native.h"
#include "config.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>

namespace wb::capital::bank_native {
using cambc::vec2;
// Separate opt-in protocol state, never part of the existing bank Memory ABI.
inline bool& priority_protocol_reserved(){static bool value=false;return value;}
struct Geometry {
    int owner=-1,round=-1;
    vec2 position{},altar{};
    bool altar_known=false,npc_quiet=false;
};
struct State {
    Geometry geometry{};
    bank::Monitor monitor{};
    bank::Result latest{};
    std::optional<bank::Plan> plan;
    vec2 tracked_altar{};
    bool tracked=false,promoted=false;
    int promotion_round=-1;
    std::int32_t restore_priority=0;
    std::uint64_t offers=0,admitted=0,accepted=0,reconciled=0,credited=0;
    std::array<std::uint64_t,8> rejects{};
};
inline State& state(){static State value;return value;}
inline int owner(){return static_cast<int>(static_cast<cambc::handle_champion_t>(cambc::self()));}
inline bool same(vec2 a,vec2 b){return a.x==b.x&&a.y==b.y;}
inline void geometry(Geometry packet){state().geometry=packet;}
inline bank::Observation observation() {
    bank::Observation o;o.owner=owner();o.round=cambc::round_number();
    cambc::detail::champion_info raw{};cambc::detail::check(cambc::detail::sdk_champion(o.owner,&raw));
    const auto flags=cambc::champion_state::from_bits_retain(raw.state);
    o.observed=true;o.alive=flags.contains(cambc::champion_state::alive);o.position={raw.x,raw.y};o.hp=raw.health;
    o.channel=flags.contains(cambc::champion_state::submitting_gems)?bank::Channel::Submit:
        flags.contains(cambc::champion_state::recalling)||flags.contains(cambc::champion_state::teleporting)?bank::Channel::Other:bank::Channel::None;
    o.bag=cambc::self().gem_count();o.team_score=cambc::global_submitted_gems(cambc::team::ally);
    if(o.alive) {
        o.stunned=cambc::self().buffs().has_stun();
        std::uint8_t restrictions=0;cambc::detail::check(cambc::detail::sdk_champion_restrictions(o.owner,&restrictions));
        o.restrictions=restrictions;
    }
    o.submit_allowed=o.alive&&!o.stunned&&o.channel==bank::Channel::None
        &&!flags.contains(cambc::champion_state::submit_gems_started)
        &&!flags.contains(cambc::champion_state::completed_turn);
    o.fresh=cambc::round_number()==static_cast<unsigned>(o.round);return o;
}
inline unsigned ticks(std::uint64_t bag) {
    const double value=cambc::constants::submit_gem_base_duration+
        std::ceil(static_cast<double>(bag)*cambc::constants::submit_gem_duration_multiplier);
    if(!std::isfinite(value)||value<0||value>1000000)return 0;
    return static_cast<unsigned>(std::max<double>(cambc::constants::submit_gem_minimum_duration,value));
}
inline bank::Context context(const bank::Observation& o,vec2 altar) {
    bank::Context c;c.owner=o.owner;c.round=o.round;c.altar={altar.x,altar.y};
    c.queried_ticks=ticks(o.bag);
    c.adjacent=vec2{o.position.x,o.position.y}.chebyshev_distance(altar)<=cambc::constants::submit_gems_range;
    c.altar_known=cambc::vision::in_vision(altar)&&cambc::map::altars().is_set(altar);
    c.altar_unlocked=c.altar_known&&!cambc::vision::altar_lock_tiles(cambc::team::enemy).is_set(altar);
    c.fresh=o.fresh&&cambc::round_number()==static_cast<unsigned>(o.round);return c;
}
inline void observe_input() {
    auto& m=state();const auto o=observation();
    auto c=m.tracked?context(o,m.tracked_altar):bank::Context{};
    c.owner=o.owner;c.round=o.round;c.fresh=o.fresh;
    m.latest=m.monitor.observe(o,c);
    if(m.latest.outcome==bank::Outcome::Banked) {
        ++m.reconciled;m.credited+=m.latest.credited_prior_bag;
        std::cout<<"wb_bank_result "<<o.owner<<' '<<o.round<<' '<<m.latest.credited_prior_bag
            <<' '<<m.latest.unattributed_score_delta<<' '<<o.bag<<' '<<o.team_score<<'\n';
    }
    // Promotion is a real state mutation even when the subsequent submit was
    // refused or interrupted. Restore on the next actual input in every case.
    if(m.promoted&&o.round>m.promotion_round) {
        const auto epoch=cambc::round_number();
        cambc::self().set_priority(m.restore_priority);
        const auto restored=cambc::self().priority();
        if(cambc::round_number()==epoch&&restored==m.restore_priority)m.promoted=false;
    }
    if(o.round%128==0&&cambc::fuel_remaining()>85000)
        std::cout<<"wb_bank_counts "<<o.owner<<' '<<o.round<<' '<<m.offers<<' '<<m.admitted<<' '
            <<m.accepted<<' '<<m.reconciled<<' '<<m.credited<<' '
            <<m.rejects[0]<<' '<<m.rejects[1]<<' '<<m.rejects[2]<<' '<<m.rejects[3]<<' '
            <<m.rejects[4]<<' '<<m.rejects[5]<<' '<<m.rejects[6]<<' '<<m.rejects[7]<<'\n';
}
inline void actual_step() {
    auto& m=state();if(!m.tracked)return;
    const auto o=observation();const auto c=context(o,m.tracked_altar);
    (void)m.monitor.observe(o,c);(void)m.monitor.changed_context(o,c,bank::Change::ActualStep);
}
inline bool banked(){return state().latest.outcome==bank::Outcome::Banked;}
struct Offer {bool claimed=false;std::optional<bank::Plan> plan;};
// This constructive word is intentionally narrow: actual final-own current
// slot, enemy-first current round, no enemy tail, accepted strict-highest next
// priority, one-tick tribute, quiet NPC support, current unchannelled body.
// It has ZERO actor inputs before payout, including hidden/unsupported foes.
inline Offer prepare() {
    Offer out;if constexpr(config::named_party_controller)if(priority_protocol_reserved())return out;
    auto& m=state();m.plan.reset();const auto& g=m.geometry;
    const auto epoch=cambc::round_number();
    if(g.owner!=owner()||g.round!=static_cast<int>(epoch)||!g.altar_known)return out;
    const auto o=observation();
    if(!o.fresh||!same(g.position,{o.position.x,o.position.y}))return out;
    ++m.offers;
    if(!o.bag||ticks(o.bag)!=1){++m.rejects[0];return out;}
    if(!o.alive||o.stunned||o.restrictions||!o.submit_allowed){++m.rejects[1];return out;}
    auto c=context(o,g.altar);
    if(!c.fresh||!c.altar_known||!c.adjacent||!c.altar_unlocked){++m.rejects[2];return out;}
    if(!g.npc_quiet){++m.rejects[3];return out;}
    const auto tail=order_native::tail_before_bank();
    if(!tail.admitted){++m.rejects[4];return out;}
    if(cambc::round_number()!=epoch||cambc::fuel_remaining()<22000){++m.rejects[5];return out;}
    // Closed documented-action-model premise: supported own capital bodies
    // have no linked-death passive; current buffs are observed. No documented
    // persistent damage/stun zone exists. Existing locks were checked above;
    // pending foe teleport/lock/spell actions cannot resolve without an input.
    c.foe_opportunities_upper=0;c.foes_covered=true;c.npc_covered=true;
    c.active_effects_covered=true;c.survives_horizon=o.hp>0;c.no_stun_horizon=true;c.full_reply_horizon=true;
    m.monitor.observe(o,c);auto prospective=m.monitor.plan(o,c);
    if(prospective.status!=bank::Status::Admitted){++m.rejects[6];return out;}
    m.restore_priority=tail.prior_priority;m.promotion_round=o.round;m.promoted=true;
    cambc::self().set_priority(tail.request_priority);out.claimed=true;
    const auto requested=cambc::self().priority();
    if(cambc::round_number()!=epoch||requested!=tail.request_priority){++m.rejects[7];return out;}
    // The accepted mutation establishes rank zero NEXT round. Refresh all
    // actual start dependencies instead of treating the request as a receipt.
    const auto before=observation();auto fresh=context(before,g.altar);
    if(!before.fresh||!fresh.fresh||before.round!=o.round||cambc::round_number()!=epoch){++m.rejects[7];return out;}
    fresh.foes_covered=c.foes_covered;fresh.npc_covered=c.npc_covered;
    fresh.active_effects_covered=c.active_effects_covered;fresh.survives_horizon=c.survives_horizon;
    fresh.no_stun_horizon=c.no_stun_horizon;fresh.full_reply_horizon=c.full_reply_horizon;
    m.monitor.observe(before,fresh);auto plan=m.monitor.plan(before,fresh);
    if(plan.status!=bank::Status::Admitted){++m.rejects[7];return out;}
    m.plan=plan;m.tracked=true;m.tracked_altar=g.altar;++m.admitted;out.plan=plan;return out;
}
inline bool execute(vec2 altar) {
    auto& m=state();if(!m.plan||!same(altar,{m.plan->context.altar.x,m.plan->context.altar.y}))return false;
    const auto plan=*m.plan;const auto epoch=cambc::round_number();
    const auto before=observation();auto c=context(before,altar);
    if(!before.fresh||!c.fresh||cambc::round_number()!=epoch||before.owner!=plan.before.owner||before.round!=plan.before.round
        ||before.position!=plan.before.position||before.bag!=plan.before.bag||!c.altar_unlocked
        ||!before.submit_allowed||ticks(before.bag)!=1)return false;
    cambc::self().submit_gems(altar);
    const auto after=observation();auto post=context(after,altar);
    bool accepted=after.fresh&&cambc::round_number()==epoch&&after.channel==bank::Channel::Submit;
    if(accepted)accepted=cambc::self().remaining_channel_time()==1;
    accepted=accepted&&cambc::round_number()==epoch;
    m.latest=m.monitor.accepted_start(plan,accepted,after,post);
    if(accepted&&m.latest.outcome==bank::Outcome::AwaitOwnerStart) {
        ++m.accepted;
        std::cout<<"wb_bank_start "<<after.owner<<' '<<after.round<<' '<<plan.before.bag<<' '
            <<plan.before.team_score<<' '<<c.queried_ticks<<' '<<m.restore_priority<<' '<<cambc::self().priority()<<'\n';
    }
    m.plan.reset();return accepted;
}
}
