#pragma once
#include "capital_conversion_native.h"
#include "capital_conversion_refinement.h"
#include "capital_conversion_value.h"
#include <array>
#include <cstdint>
#include <iostream>

// Runtime policy ownership. Forecasts, assurance and receipt evidence stay
// separate: absent opponent/service models KEEP the playable Bank incumbent.
// No borrowed value span or observation permission survives an SDK attempt.
namespace wb::capital::conversion_runtime {
namespace cn=conversion_native;
namespace t=tribute;
namespace v=conversion_value;
namespace r=conversion_refinement;
struct Hooks {
    void* context{};
    r::Plan (*prepare)(void*,cn::Epoch,const cn::Packet&,const t::Context&,const cn::Geometry&,bool paid_ingress){};
    bool (*execute)(void*,r::Plan&,cn::Epoch,const cn::Packet&,const t::Context&,const cn::Geometry&){};
};
struct Counters {
    std::uint64_t offers{},approaches{},starts{},pending{},interruptions{},conservation_payouts{},credited_original{};
    std::uint64_t missing_value{},no_offer{},fallbacks{},unknown_receipts{};
    unsigned logs{};int pending_logged_round=-1;
};
struct Memory {
    cn::SdkHost host{};cn::Adapter<cn::SdkHost> native{host};t::Monitor monitor{};
    std::uint64_t input{},next_token{},next_guard{},original{},token{},last_credit_token{};
    std::array<int,cn::max_enemies> enemy_ids{};unsigned enemy_count{};bool roster_known{};
    bool tracking{},empty_boundary{},archived{};t::Point altar{};Counters counters{};
    bool fallback_moved{};
};
inline Memory& memory(){static Memory value;return value;}
inline bool current(cn::Epoch e){return memory().host.owner()==e.owner&&memory().host.round()==e.round;}
inline cn::Epoch begin(int owner,int round){auto& m=memory();m.fallback_moved=false;return {owner,round,++m.input};}
inline cn::Premises premises() {
    const auto gap=cambc::constants::respawn_time;
    // Closed documented bag-loss model: decreases are tribute or death. This
    // is a conservation premise, not an observed payout event or safety proof.
    return {static_cast<unsigned>(std::max(0,gap)),gap>=0,true};
}
inline cn::Geometry geometry(cn::Epoch e,std::span<const t::Point> altars,void* context,
                             bool(*walkable)(void*,t::Point)) {
    auto& m=memory();
    if(!m.roster_known&&current(e)) {
        const auto roster=cambc::champions(cambc::team::enemy);
        if(!current(e)||roster.empty()||roster.size()>m.enemy_ids.size())return {};
        m.enemy_count=0;
        for(const auto& foe:roster)m.enemy_ids[m.enemy_count++]=static_cast<int>(static_cast<cambc::handle_champion_t>(foe));
        m.roster_known=true;
    }
    cn::Geometry g;g.source=e;g.altars=altars;g.altars_queried=!altars.empty()&&altars.size()<=cn::max_altars;
    g.context=context;g.static_walkable=walkable;g.enemy_ids={m.enemy_ids.data(),m.enemy_count};g.enemy_ids_complete=m.roster_known;
    // Called only at the fresh Runtime input head, before any physical/cast
    // action. The conversion policy itself never casts or changes priority.
    g.root_history_known=true;g.exclusive_cast_used=false;return g;
}
inline void event(const char* tag,std::initializer_list<std::uint64_t> values) {
    char line[384];unsigned n=0;while(*tag&&n+1<sizeof(line))line[n++]=*tag++;
    for(auto value:values){char digits[20];unsigned k=0;do{digits[k++]=static_cast<char>('0'+value%10);value/=10;}while(value);
        if(n+k+2>=sizeof(line))return;line[n++]=' ';while(k)line[n++]=digits[--k];}
    line[n++]='\n';std::cout.write(line,n);
}
inline void observe_result(cn::Epoch e,const cn::Result& out) {
    auto& m=memory();auto& c=m.counters;const auto& r=out.receipt;
    if(r.new_evidence&&r.evidence==t::Evidence::ConservationSupported&&r.token!=m.last_credit_token) {
        m.last_credit_token=r.token;++c.conservation_payouts;c.credited_original+=r.credited_original;
        if(current(e)&&m.host.fuel()>10000)event("wb_conversion_payout",{static_cast<unsigned>(e.owner),static_cast<unsigned>(e.round),
            r.token,r.original_amount,r.credited_original,static_cast<unsigned>(r.evidence),out.actual.own.bag,out.actual.own.team_score});
    }
    if(out.fresh&&out.actual.own.bag==0&&out.actual.own.channel==t::Channel::None) {
        m.empty_boundary=true;
        if(m.tracking){if(r.original_presence==t::Presence::Unknown)++c.unknown_receipts;m.archived=true;m.tracking=false;}
    }
    if(r.state==t::State::Active&&out.actual.own.channel==t::Channel::Submit) {
        ++c.pending;
        if(c.logs<12&&c.pending_logged_round!=e.round&&current(e)&&m.host.fuel()>10000) {
            ++c.logs;c.pending_logged_round=e.round;
            event("wb_conversion_channel",{static_cast<unsigned>(e.owner),static_cast<unsigned>(e.round),r.token,
                out.actual.own.bag,out.actual.own.remaining_ticks,static_cast<unsigned>(r.expected_round),static_cast<unsigned>(r.evidence)});
        }
    }
}
inline cn::Result advance(cn::Epoch e,const cn::Geometry& g) {
    auto& m=memory();
    if(!m.tracking) {
        const auto bag=cambc::self().gem_count();
        const double ticks=std::max<double>(cambc::constants::submit_gem_minimum_duration,
            cambc::constants::submit_gem_base_duration+std::ceil(static_cast<double>(bag)*cambc::constants::submit_gem_duration_multiplier));
        if(!bag||!std::isfinite(ticks)||ticks<2)return {};
    }
    auto out=m.native.observe_input(e,g,premises(),m.tracking?&m.monitor:nullptr,m.altar);
    if(out.fresh&&out.actual.own.alive&&out.actual.own.bag&&out.actual.own.channel==t::Channel::None&&!m.tracking) {
        if(m.archived&&m.empty_boundary){m.monitor=t::Monitor{};m.token=m.original=0;m.archived=m.empty_boundary=false;}
        if(m.monitor.state()==t::State::Empty) {
            const auto token=++m.next_token;
            const auto begun=m.monitor.begin(token,out.actual.own,out.context);
            if(begun.outcome==t::Outcome::NewReceipt){m.token=token;m.original=out.actual.own.bag;m.tracking=true;out.receipt=begun;}
        }
    }
    observe_result(e,out);return out;
}
struct Call {
    cn::Epoch epoch{};const cn::Geometry* geometry{};cn::BankIntent root{};Hooks hooks{};
    r::Plan refinement{};
};
inline bool authorize(void* opaque,t::Action action,const cn::BankIntent& intent,const cn::Packet& packet,
                      const t::Context& context,t::Guard& guard) {
    auto& call=*static_cast<Call*>(opaque);auto& m=memory();
    if(!current(call.epoch)||!cn::same(call.epoch,intent.source)||!packet.own.alive||packet.own.stunned
        ||packet.own.channel!=t::Channel::None||intent.min_horizon>static_cast<int>(cambc::constants::max_rounds))return false;
    if(action==t::Action::Approach&&!packet.own.can_move)return false;
    if(action==t::Action::Start&&(!context.altar_known||!context.adjacent||!context.unlocked||!packet.own.submit_permitted))return false;
    v::Context domain;domain.owner=call.epoch.owner;domain.start={call.epoch.round,1};domain.horizon={intent.min_horizon,0};
    domain.input=call.epoch.input;domain.observation=packet.own.sample;domain.effect=packet.own.sample;
    domain.initial_own_score=packet.own.team_score;domain.initial_carried=packet.own.bag;
    domain.race_target={cambc::constants::winning_gems,true};
    // No legal/coherent opponent-service producer has been qualified here.
    // Keep that domain UNKNOWN, rather than pass independent maxima as a
    // forecast or invent zero displaced farming. The complete Bank policy is
    // the incumbent; incomplete/incomparable optional policies cannot freeze it.
    v::Policy bank;bank.id=1;bank.context=domain;bank.complete=call.hooks.prepare&&call.hooks.execute;bank.complete_economic_incumbent=true;
    for(std::uint64_t id:{2u,3u,4u}) {
        // No complete alternate-policy rollout is supplied yet. Its missing
        // implementation/forecast must not masquerade as a complete zero-cost
        // Hold/flee/counteraction/other-altar outcome.
        v::Policy alternative;alternative.id=id;alternative.context=domain;alternative.complete=false;
        const auto comparison=v::compare(alternative,bank);
        if(comparison.preference==v::Preference::Incomparable)++m.counters.missing_value;
        if(&v::select(alternative,bank)!=&bank)return false;
    }
    if(!current(call.epoch))return false;
    guard={++m.next_guard,intent.token,call.epoch.owner,call.epoch.round,call.epoch.input,packet.own.sample,
        action,action==t::Action::Approach?intent.landing:intent.altar,packet.queried_ticks,intent.min_horizon,true};
    return true;
}
inline bool fallback(void* opaque,const cn::Packet& packet,const t::Context& context) {
    auto& call=*static_cast<Call*>(opaque);++memory().counters.fallbacks;
    if(!current(call.epoch)||!call.hooks.prepare||!call.hooks.execute)return false;
    const bool paid=call.root.move_required&&packet.own.position!=call.root.position&&!packet.own.can_move;
    if(!r::playable(call.refinement))
        call.refinement=call.hooks.prepare(call.hooks.context,call.epoch,packet,context,*call.geometry,paid);
    return r::playable(call.refinement)
        &&call.hooks.execute(call.hooks.context,call.refinement,call.epoch,packet,context,*call.geometry)&&current(call.epoch);
}
inline bool operational_handoff(const cn::Result& out) {
    // Audit continuity and present physical control are different authorities.
    // A missing observation must never recover the old token's provenance, but
    // it must not turn that token into an endless movement-only Bank loop.
    return out.fresh&&!out.claimed&&out.actual.own.alive&&!out.actual.own.stunned
        &&out.actual.own.bag&&out.actual.own.channel==t::Channel::None&&!out.actual.completed_turn
        &&out.receipt.state==t::State::Blocked&&out.receipt.original_presence!=t::Presence::Held;
}
inline cn::Result choose(cn::Epoch e,const cn::Geometry& g,Hooks hooks) {
    auto& m=memory();
    if(!current(e))return {};
    auto observed=m.native.observe_input(e,g,premises(),m.tracking?&m.monitor:nullptr,m.altar);
    observe_result(e,observed);
    if(!observed.fresh||!observed.actual.own.alive||observed.actual.own.stunned||!observed.actual.own.bag
        ||observed.actual.own.channel!=t::Channel::None||observed.actual.queried_ticks<2)return observed;
    // Only an actual empty boundary releases a previous token. Its terminal
    // evidence/counters were archived above; a failed held receipt is not reset.
    if(!m.tracking&&m.archived&&m.empty_boundary){m.monitor=t::Monitor{};m.token=m.original=0;m.archived=m.empty_boundary=false;}
    if(m.monitor.state()==t::State::Blocked) {
        // Return actual current-bag authority to Runtime's Bank interpreter.
        // Do not mint a new original, rearm Unknown, or claim a movement prefix
        // on each input merely because the irreversible audit state is Blocked.
        if(observed.receipt.original_presence!=t::Presence::Held)return observed;
        Call call{e,&g,{},hooks};cn::Policy policy{&call,&authorize,&fallback};
        if(!hooks.prepare||!hooks.execute)return observed;
        call.refinement=hooks.prepare(hooks.context,e,observed.actual,observed.context,g,false);
        // The native callback claims before invoking its function. A new input
        // may call it only after a concrete physical plan has been prepared.
        if(!r::playable(call.refinement))return observed;
        const auto before=observed.actual.own.position;
        auto refined=m.native.fund_fallback(m.monitor,observed,g,premises(),policy);
        if(refined.fresh&&refined.actual.own.position!=before)
            (void)m.monitor.rearm(t::Change::ActualStep,refined.actual.own,refined.context);
        if(!refined.fresh||m.monitor.state()!=t::State::Tracking)return refined;
        observed=refined;
    }
    const bool empty=m.monitor.state()==t::State::Empty;
    const auto token=empty?++m.next_token:m.token;
    const auto original=empty?observed.actual.own.bag:m.original;
    auto intent=m.native.nominate(token,original,g);
    if(!intent.valid){++m.counters.no_offer;return observed;}
    ++m.counters.offers;Call call{e,&g,intent,hooks};cn::Policy policy{&call,&authorize,&fallback};
    auto out=m.native.execute(m.monitor,intent,g,premises(),policy);
    if(m.monitor.state()!=t::State::Empty){m.tracking=true;m.token=token;m.original=original;m.altar=intent.altar;}
    if(out.moved)++m.counters.approaches;
    if(out.started){++m.counters.starts;
        if(m.counters.logs<12&&current(e)&&m.host.fuel()>10000){++m.counters.logs;
            event("wb_conversion_start",{static_cast<unsigned>(e.owner),static_cast<unsigned>(e.round),token,original,
                out.actual.own.bag,intent.queried_ticks,static_cast<unsigned>(out.receipt.expected_round),out.moved});}}
    if(out.fallback_required)++m.counters.interruptions;
    observe_result(e,out);return out;
}
} // namespace wb::capital::conversion_runtime
