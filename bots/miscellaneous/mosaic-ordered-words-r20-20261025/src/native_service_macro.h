#pragma once
// A receipt/fact reducer, not a planner or physical authority. No field here
// owns, resets, or reprices the executor's reverse edges or refusal debt.
#include "native_service_words.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace mosaic::native_service {
enum class ServicePhase : std::uint8_t {acquire,work,capture,bank,exit,recover};
enum class ServiceApplied : std::uint8_t {unknown,no,yes};
enum class CustodyDisposition : std::uint8_t {none,received,bank_complete,loss,unknown};
struct ServiceJob {
    NativeId group_id{},member_id{};int group_ordinal{-1},member_ordinal{-1};
    bool retained{},active{},member_dead{},respawn_due_known{};int respawn_due_round{};
};
struct ServiceState {
    ServicePhase phase{ServicePhase::acquire},resume_phase{ServicePhase::acquire},intent{ServicePhase::acquire};
    ServiceJob job{};bool pending_intent{};
    std::uint64_t last_ack_serial{},accepted_prefix{},pending_ack_serial{};
    PrimitiveKind pending_ack_kind{PrimitiveKind::basic};bool pending_ack{};
    std::uint64_t accepted_banks{},accepted_recalls{},false_receipts{},unknown_receipts{};
    bool bank_start_seen{},recall_start_seen{},bank_channel_seen{},was_dead{};
    Epoch last_observation{},bag_epoch{},last_bank_start{};
    int bag{};bool bag_actual{},bank_obligation{};
    CustodyDisposition disposition{CustodyDisposition::none};
    std::uint64_t bag_receipts{},received_gems{},bank_completions{},banked_gems{},lost_gems{},unknown_gems{};
    // Completion truth and its numeric amount are separate. A net bag decrease
    // alone cannot exclude theft or other unobserved custody changes.
    std::uint64_t bank_amounts_unknown{};
    unsigned team_submitted{};bool team_submitted_actual{};std::uint64_t observed_team_submitted_increase{};
};
struct ServiceSignals {
    Epoch epoch{};
    // Explicit actual completion authority supplied by the caller. A vanished
    // channel, a START receipt, or a TEAM score delta is not this authority.
    bool bank_completion_actual{},bank_completed{};
    bool bank_amount_actual{};unsigned banked_gems{};
};
struct ServiceUpdate {
    bool fresh{},phase_changed{},custody_received{},bank_completed{},custody_lost{},custody_unknown{},recovered{};
};
constexpr bool service_epoch_before(Epoch a,Epoch b) {
    if(a.round!=b.round)return a.round<b.round;
    if(a.revision!=b.revision)return a.revision<b.revision;
    return a.attempt<b.attempt;
}
inline void set_macro_intent(ServiceState& state,ServicePhase phase) {
    state.intent=phase;state.pending_intent=true;
}
// Nomination can change knowledge/job choice, never accepted physical history.
// Repeating a nomination for the same job preserves its actual life evidence.
inline void retain_job(ServiceState& state,ServiceJob nomination) {
    const bool same=state.job.retained&&state.job.group_id==nomination.group_id;
    if(same){
        if(state.job.member_id!=nomination.member_id)state.job.member_dead=false;
        state.job.member_id=nomination.member_id;state.job.member_ordinal=nomination.member_ordinal;
        if(nomination.group_ordinal>=0)state.job.group_ordinal=nomination.group_ordinal;
        return;
    }
    nomination.retained=true;nomination.active=false;nomination.member_dead=false;nomination.respawn_due_known=false;
    state.job=nomination;state.resume_phase=ServicePhase::acquire;
    if(state.phase!=ServicePhase::bank&&state.phase!=ServicePhase::exit&&state.phase!=ServicePhase::recover)
        state.phase=ServicePhase::acquire;
}
inline bool respawn_probe_due(const ServiceState& state,Epoch now) {
    return state.job.retained&&!state.job.active&&state.job.respawn_due_known&&now.round>=state.job.respawn_due_round;
}
// Monotone exact-once ACK. UNKNOWN can later be reconciled at the SAME serial;
// a different command cannot overwrite that unresolved receipt.
inline bool ack_service(ServiceState& state,std::uint64_t serial,PrimitiveKind kind,ServiceApplied applied) {
    if(!serial||serial<=state.last_ack_serial)return false;
    if(state.pending_ack&&(serial!=state.pending_ack_serial||kind!=state.pending_ack_kind))return false;
    if(applied==ServiceApplied::unknown){
        if(!state.pending_ack)++state.unknown_receipts;
        state.pending_ack=true;state.pending_ack_serial=serial;state.pending_ack_kind=kind;return false;
    }
    state.pending_ack=false;state.last_ack_serial=serial;
    if(applied==ServiceApplied::no){++state.false_receipts;return true;}
    ++state.accepted_prefix;
    if(kind==PrimitiveKind::bank){
        ++state.accepted_banks;
        // A delayed accepted START remains history after a later observed
        // death. It cannot resurrect that channel or supersede recovery.
        if(!state.was_dead){
            state.bank_start_seen=true;
            if(state.phase!=ServicePhase::recover)state.phase=ServicePhase::bank;
        }
        if(state.pending_intent&&state.intent==ServicePhase::bank)state.pending_intent=false;
    }else if(kind==PrimitiveKind::recall){
        ++state.accepted_recalls;
        if(!state.was_dead)state.recall_start_seen=true;
        if(!state.was_dead&&state.phase!=ServicePhase::recover)state.phase=ServicePhase::exit;
        if(state.pending_intent&&state.intent==ServicePhase::exit)state.pending_intent=false;
    }
    return true;
}
namespace service_detail {
inline void resume(ServiceState& state,ServicePhase phase) {
    state.resume_phase=phase;
    if(state.phase!=ServicePhase::bank&&state.phase!=ServicePhase::exit&&state.phase!=ServicePhase::recover)
        state.phase=phase;
}
inline bool healthy(const SelfFact& self) {
    return self.body_actual&&self.alive&&finite_amount(self.hp)&&finite_amount(self.max_hp)&&self.max_hp>0&&
        self.hp>=self.max_hp&&self.channel_actual&&!self.recalling&&!self.submitting&&!self.teleporting&&
        self.buffs_actual&&!self.stunned;
}
inline int due(int round,int timer) {
    return timer>std::numeric_limits<int>::max()-std::max(0,round)?std::numeric_limits<int>::max():round+timer;
}
}
inline ServiceUpdate observe_service(ServiceState& state,const SelfFact& self,const GroupFact& group={},
    const CampFact& member={},const BankFact& bank={},const CaptureFact& capture={},const ServiceSignals& signals={}) {
    ServiceUpdate update;const auto before=state.phase;const Epoch now=self.epoch;
    if(now.round<0||service_epoch_before(now,state.last_observation))return update;
    update.fresh=true;state.last_observation=now;
    const bool dead=self.body_actual&&!self.alive;
    const bool completing=signals.epoch==now&&signals.bank_completion_actual&&signals.bank_completed&&state.bank_start_seen;
    if(self.channel_actual&&self.submitting){
        if(!state.bank_start_seen)state.last_bank_start=now;
        state.bank_start_seen=state.bank_channel_seen=true;
    }
    if(self.channel_actual&&self.recalling)state.recall_start_seen=true;
    // submitted is TEAM total. Preserve the actual delta separately; another
    // ally's deposit cannot certify this champion's bag disappearance.
    if(bank.epoch==now&&bank.submitted_actual){
        if(state.team_submitted_actual&&bank.submitted>=state.team_submitted)
            state.observed_team_submitted_increase+=bank.submitted-state.team_submitted;
        state.team_submitted=bank.submitted;state.team_submitted_actual=true;
    }
    const bool self_bag=self.gems_actual&&self.gems>=0;
    const bool bank_bag=bank.epoch==now&&bank.custody_actual&&bank.gems>=0;
    const bool coherent=!(self_bag&&bank_bag&&self.gems!=bank.gems);
    const bool bag_fresh=(self_bag||bank_bag)&&coherent;
    if(bag_fresh){
        const int bag=self_bag?self.gems:bank.gems;
        if(state.bag_actual&&bag>state.bag){
            ++state.bag_receipts;state.received_gems+=unsigned(bag-state.bag);
            state.disposition=CustodyDisposition::received;update.custody_received=true;
        }else if(state.bag_actual&&bag<state.bag){
            const auto amount=unsigned(state.bag-bag);
            if(completing){
                // The completed channel is certain, its amount need not be.
                // Account only the net disappearance not covered by an exact
                // caller receipt; it is not automatically a scored amount.
                const unsigned known=signals.bank_amount_actual?signals.banked_gems:0;
                if(amount>known)state.unknown_gems+=amount-known;
            }else if(dead){
                state.lost_gems+=amount;state.disposition=CustodyDisposition::loss;update.custody_lost=true;
                state.bank_start_seen=state.bank_channel_seen=false;
            }else{
                state.unknown_gems+=amount;state.disposition=CustodyDisposition::unknown;update.custody_unknown=true;
                state.bank_start_seen=state.bank_channel_seen=false;
            }
        }
        state.bag=bag;state.bag_actual=true;state.bag_epoch=now;state.bank_obligation=bag>0;
    }
    if(!coherent){state.disposition=CustodyDisposition::unknown;update.custody_unknown=true;}
    // Consume the tracked completion once even with an unknown or unchanged
    // current bag. A fresh positive bag is a new outstanding obligation; an
    // absent current observation is not proof of either residual amount.
    if(completing){
        ++state.bank_completions;
        if(signals.bank_amount_actual)state.banked_gems+=signals.banked_gems;
        else ++state.bank_amounts_unknown;
        state.disposition=CustodyDisposition::bank_complete;update.bank_completed=true;
        state.bank_start_seen=state.bank_channel_seen=false;
        if(!bag_fresh){
            state.bag_actual=false;state.bank_obligation=false;
            update.custody_unknown=true;
        }
    }
    if(state.job.retained&&group.id==state.job.group_id&&group.current(now)){
        if(group.life==GroupLife::reward){
            state.job.active=true;state.job.respawn_due_known=false;service_detail::resume(state,ServicePhase::capture);
        }else if(group.life==GroupLife::respawning){
            state.job.active=false;
            state.job.respawn_due_known=group.respawn_actual&&group.respawn_time>=0;
            if(state.job.respawn_due_known)state.job.respawn_due_round=service_detail::due(now.round,group.respawn_time);
            service_detail::resume(state,ServicePhase::acquire);
        }else if(group.life==GroupLife::alive){
            state.job.active=true;state.job.respawn_due_known=false;
            if(member.group_id==state.job.group_id&&member.living(now)){
                state.job.member_id=member.id;state.job.member_ordinal=member.member_ordinal;state.job.member_dead=false;
                service_detail::resume(state,ServicePhase::work);
            }else service_detail::resume(state,ServicePhase::acquire);
        }
    }else if(state.job.retained&&member.group_id==state.job.group_id&&member.actual(now)){
        if(member.living(now)){
            state.job.active=true;state.job.member_id=member.id;state.job.member_ordinal=member.member_ordinal;
            state.job.member_dead=false;service_detail::resume(state,ServicePhase::work);
        }else if(member.id==state.job.member_id&&member.life==Life::dead){
            state.job.member_dead=true;service_detail::resume(state,ServicePhase::acquire);
        }
    }
    // Occupying Reward preserves capture intent only. No uncontested timer,
    // reward receipt, or score follows from this observation.
    if(state.job.retained&&capture.group_id==state.job.group_id&&capture.holding_reward(now))
        service_detail::resume(state,ServicePhase::capture);
    if(dead){
        if(!state.was_dead&&state.bag_actual&&state.bag>0){
            state.unknown_gems+=unsigned(state.bag);state.bag_actual=false;state.disposition=CustodyDisposition::unknown;
            update.custody_unknown=true;
        }
        state.was_dead=true;state.bank_obligation=false;state.bank_start_seen=state.bank_channel_seen=false;
        state.phase=ServicePhase::recover;
    }else if(self.body_actual&&self.alive){
        if(state.phase==ServicePhase::recover||state.was_dead||state.recall_start_seen){
            if(service_detail::healthy(self)){
                state.was_dead=false;state.recall_start_seen=false;state.phase=state.resume_phase;update.recovered=true;
            }else if(self.channel_actual&&!self.recalling)state.phase=ServicePhase::recover;
        }
        if(state.bank_obligation){
            if(state.phase!=ServicePhase::bank&&state.phase!=ServicePhase::exit&&state.phase!=ServicePhase::recover)
                state.resume_phase=state.phase;
            if(state.phase!=ServicePhase::recover&&!(self.channel_actual&&self.recalling))state.phase=ServicePhase::bank;
        }else if(update.bank_completed||update.custody_lost||update.custody_unknown){
            if(state.phase==ServicePhase::bank)state.phase=state.resume_phase;
        }
        if(self.channel_actual&&self.submitting)state.phase=ServicePhase::bank;
        else if(self.channel_actual&&self.recalling)state.phase=ServicePhase::exit;
    }
    update.phase_changed=state.phase!=before;return update;
}
}
