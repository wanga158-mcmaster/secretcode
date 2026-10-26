#pragma once
#include "native_service_words.h"
#include <array>
#include <cstdint>
#include <limits>

namespace mosaic::native_service {
// These are hypotheses, not qualified native prices. The clock starts at the
// WHOLE caller's entry, before cold state, selection, loops or instrumentation.
// Every import span is measured; all other elapsed fuel consumes one pure
// credit. The command import's 2000 also covers its fixed pending/result writes
// and pure invalidation below. This is not a sum of callback maximums.
struct Budget {
    using Reader=std::uint64_t(*)(void*);
    void* context{}; Reader reader{};
    std::uint64_t entry{},last{},sdk_spent{},span_entry{},span_bound{},held_tail{};
    std::uint64_t query_bound{2000},command_bound{2000},pure_bound{6000},yield_bound{2000};
    unsigned queries{},commands{}; bool poisoned{},in_span{},span_is_command{};
    Budget(void* object,Reader clock,std::uint64_t whole_entry):context(object),reader(clock),entry(whole_entry),last(whole_entry) {}
    static constexpr std::uint64_t add(std::uint64_t a,std::uint64_t b) {
        return b>std::numeric_limits<std::uint64_t>::max()-a?std::numeric_limits<std::uint64_t>::max():a+b;
    }
    static constexpr std::uint64_t mul(std::uint64_t a,std::uint64_t b) {
        return a&&b>std::numeric_limits<std::uint64_t>::max()/a?std::numeric_limits<std::uint64_t>::max():a*b;
    }
    std::uint64_t sample() {
        const auto now=reader(context);
        // A fuel increase indicates a suspended input, never free new credit.
        if(now>last||now>entry)poisoned=true;
        last=now;return now;
    }
    std::uint64_t pure_used()const {
        const auto elapsed=entry>=last?entry-last:0;
        return elapsed>=sdk_spent?elapsed-sdk_spent:0;
    }
    bool checkpoint() {
        sample();if(in_span||pure_used()>pure_bound||last<=yield_bound)poisoned=true;
        return !poisoned;
    }
    bool admit(std::uint64_t outstanding) {
        if(!checkpoint())return false;
        return last>add(add(outstanding,pure_bound-pure_used()),yield_bound);
    }
    void reserve(std::uint64_t tail) {held_tail=tail;}
    bool before(bool command,unsigned count,std::uint64_t extra_tail) {
        if(!count)return false;
        const auto bound=mul(count,command?command_bound:query_bound);
        // admit() samples after this fixed arithmetic. Sampling once here
        // includes the same whole-entry/round checks before every SDK span.
        if(!admit(add(bound,std::max(held_tail,extra_tail))))return false;
        span_entry=last;span_bound=bound;span_is_command=command;in_span=true;
        if(command)commands+=count;else queries+=count;
        return true;
    }
    bool before_query(unsigned count=1,std::uint64_t extra_tail=0) {return before(false,count,extra_tail);}
    bool before_command(std::uint64_t extra_tail=0) {return before(true,1,extra_tail);}
    bool after() {
        if(!in_span){poisoned=true;return false;}
        const auto now=sample();const auto spent=span_entry>=now?span_entry-now:0;
        sdk_spent=add(sdk_spent,spent);in_span=false;
        if(spent>span_bound||pure_used()>pure_bound||last<=yield_bound)poisoned=true;
        return !poisoned;
    }
    bool after_query() {return after();}
    bool after_command() {return after();}
    // Used only while propagating an unexpected SDK exception to the turn
    // boundary. No further command is authorized in this input.
    void interrupted() {if(in_span)after();poisoned=true;}
    std::uint64_t spent() {sample();return entry>=last?entry-last:0;}
};

enum class Truth : std::uint8_t {unknown,no,yes};
enum class NextInput : std::uint8_t {work,exit,refusal,feedback};
enum class ExecutionStatus : std::uint8_t {empty,complete,continuation,unfunded,changed,refused,overrun,pending,reconciled,invalid};
struct Validation {
    SelfFact self{}; CampFact camp{}; AbilityFact ability{};
    Epoch move_epoch{}; bool move_actual{},move_attempt_permitted{},occupancy_actual{},occupancy_open{};
    bool bank_altar_actual{},bank_lock_actual{},bank_unlocked{};int bank_range{};
};
struct Receipt {
    std::uint64_t serial{}; Primitive action{}; Epoch before_epoch{},after_epoch{};
    Point before_pose{},after_pose{}; Truth applied{Truth::unknown};
    bool attempted{},returned{},refused{},after_actual{},unknown_feedback{};
    bool raw_m_before{},raw_m_after{},move_restricted_before{},move_restricted_after{};
    bool recalling_after{},submitting_after{},channel_time_actual{};int channel_time_after{};
};
struct PhysicalEdge {Point from{},to{};std::uint64_t serial{};};
struct ExecutionState {
    // No job/knowledge reset clears an accepted prefix, edge or refusal debt.
    std::uint64_t serial{},accepted_prefix{},last_recorded{};
    int input_round{-1};unsigned input_attempts{},input_successes{},input_refusals{};
    bool input_blocked{},pending{},pending_recorded{},pending_ack{},bank_obligation{};
    bool reverse_truncated{},reverse_uncertain{},refusals_truncated{};
    Receipt pending_receipt{};NextInput next{NextInput::work};
    std::array<PhysicalEdge,64> reverse_history{};unsigned reverse_count{},reverse_cursor{};
    std::array<Point,8> refused_points{};unsigned refusal_debt{},refusal_cursor{};
    void begin(int round) {
        if(input_round==round)return;
        input_round=round;input_attempts=input_successes=input_refusals=0;input_blocked=false;
    }
    void record() {
        if(pending_recorded)return;
        const auto& r=pending_receipt;
        if(r.attempted&&r.applied==Truth::unknown&&r.action.kind==PrimitiveKind::move)reverse_uncertain=true;
        if(r.applied==Truth::yes){++accepted_prefix;
            if(r.action.kind==PrimitiveKind::move){
                // Successful ordinary movement authoritatively reaches its
                // commanded destination, even if subsequent feedback failed.
                if(reverse_count==reverse_history.size())reverse_truncated=true;
                reverse_history[reverse_cursor]={r.before_pose,r.action.point,r.serial};
                reverse_cursor=(reverse_cursor+1)%reverse_history.size();
                reverse_count=std::min<unsigned>(reverse_count+1,reverse_history.size());
            }
        }
        if(r.refused){if(refusal_debt>=refused_points.size())refusals_truncated=true;++refusal_debt;
            refused_points[refusal_cursor]=r.action.point;refusal_cursor=(refusal_cursor+1)%refused_points.size();}
        last_recorded=r.serial;pending_recorded=true;
    }
};
struct ExecutionOutcome {
    ExecutionStatus status{ExecutionStatus::empty}; NextInput next{NextInput::work};
    unsigned attempts{},successes{},refusals{};bool attempted{},applied{},feedback_closed{};
    std::uint64_t accepted_prefix{},spent{};
};
struct ExecutionOptions {unsigned refusal_attempts{2};bool allow_shorter_refusal_class{};};
template<class Host> void record_pending(Host& host,ExecutionState& state) {
    const bool first=!state.pending_recorded;state.record();
    // Physical prefix publication precedes feedback/ACK. Typed owners retain
    // work-induced obligations here even if the following readback fails.
    if(first){if constexpr(requires{host.recorded(state.pending_receipt,state.accepted_prefix);})
        host.recorded(state.pending_receipt,state.accepted_prefix);}
}
inline bool target_primitive(PrimitiveKind kind) {
    return kind==PrimitiveKind::basic||kind==PrimitiveKind::cast_unit||kind==PrimitiveKind::cast_ground;
}
inline EffectScope effect_scope(const Primitive& action,bool returned,bool applied) {
    if(returned&&!applied)return EffectScope::knowledge_only;
    if(action.kind==PrimitiveKind::reset||action.kind==PrimitiveKind::shield||
        action.kind==PrimitiveKind::recall||action.kind==PrimitiveKind::bank)return EffectScope::self_only;
    return EffectScope::position_or_damage;
}
inline bool authorize(const Primitive& action,const Validation& v,Epoch now) {
    if(!v.self.actual(now)||!v.self.alive||v.self.stunned)return false;
    const bool target=target_primitive(action.kind);
    if(target&&(!v.camp.living(now)||!v.camp.body_actual||!v.camp.buffs_actual||v.camp.id!=action.target_id))return false;
    if(action.kind==PrimitiveKind::basic)
        return v.self.can_attack(now)&&distance(v.self.pose,v.camp.pose)<=v.self.attack_range;
    if(action.kind==PrimitiveKind::move)
        return v.self.can_move(now)&&v.move_actual&&v.move_epoch==now&&
            (v.occupancy_actual?v.occupancy_open:v.move_attempt_permitted)&&
            action.point!=v.self.pose&&distance(v.self.pose,action.point)<=v.self.move_speed;
    if(action.kind==PrimitiveKind::recall||action.kind==PrimitiveKind::bank){
        if(v.self.recalling||v.self.submitting||v.self.teleporting||v.self.completed)return false;
        if(action.kind==PrimitiveKind::recall)return !v.self.recall_started;
        return !v.self.submit_started&&v.self.gems_actual&&v.self.gems>0&&v.bank_altar_actual&&
            v.bank_lock_actual&&v.bank_unlocked&&v.bank_range>=0&&distance(v.self.pose,action.point)<=v.bank_range;
    }
    if(action.slot>=2||!v.ability.ready(v.self,now))return false;
    const auto& d=v.ability.descriptor;
    if(d.exclusive_with_movement&&(!v.self.walked_actual||v.self.ordinary_walked))return false;
    if(action.kind==PrimitiveKind::reset)return d.kind==AbilityKind::reset&&!v.self.raw_a;
    if(action.kind==PrimitiveKind::shield)return d.kind==AbilityKind::shield&&(d.target_mask&target_self)!=0;
    if(d.kind!=AbilityKind::damage&&d.kind!=AbilityKind::aoe_damage&&d.kind!=AbilityKind::cc)return false;
    if(!finite_amount(d.range)||!finite_amount(d.radius))return false;
    if(action.kind==PrimitiveKind::cast_unit)return d.kind!=AbilityKind::aoe_damage&&d.camps()&&float(distance(v.self.pose,v.camp.pose))<=d.range;
    return action.kind==PrimitiveKind::cast_ground&&d.ground()&&
        float(distance(v.self.pose,action.point))<=d.range&&float(distance(action.point,v.camp.pose))<=d.radius;
}
// Source import counts, not measured callback maxima: first self<=4; selected
// camp state/body/buffs<=3; cold ability props+cooldown<=2; move has point
// visibility+camp/champion/teleport occupancy<=4. Bank adds bag+enemy-lock<=2.
// Combat feedback self3; recall/tribute/pending add active channel time (4).
// Refreshed self is reused before the next primitive.
inline unsigned validation_queries(const Primitive& action,bool first) {
    const bool ability=action.kind==PrimitiveKind::reset||action.kind==PrimitiveKind::shield||
        action.kind==PrimitiveKind::cast_unit||action.kind==PrimitiveKind::cast_ground;
    return (first?4u:0u)+(target_primitive(action.kind)?3u:0u)+
        (ability?2u:0u)+(action.kind==PrimitiveKind::bank?2u:0u)+(action.kind==PrimitiveKind::move?4u:0u);
}
inline unsigned feedback_queries(const Primitive& action) {
    // No combat primitive can start a channel. Authorization already excluded
    // pre-existing channels, so their feedback has no channel-time import.
    return action.kind==PrimitiveKind::recall||action.kind==PrimitiveKind::bank?4u:3u;
}
inline std::uint64_t word_tail(const Word& word,unsigned at,const Budget& budget,unsigned alternatives,bool first) {
    std::uint64_t queries=0,commands=0;
    for(unsigned i=at;i<word.count&&i<max_successful_primitives;++i){queries+=validation_queries(word.actions[i],first&&i==at)+feedback_queries(word.actions[i]);++commands;}
    // A refused move may choose at most two freshly checked alternatives. Both FALSE
    // branches include their refresh, and never reopen the primary suffix.
    queries+=alternatives*7;commands+=alternatives;
    return Budget::add(Budget::mul(queries,budget.query_bound),Budget::mul(commands,budget.command_bound));
}
template<class Host> ExecutionOutcome reconcile(Host& host,Budget& budget,ExecutionState& state) {
    ExecutionOutcome out{};out.next=NextInput::feedback;
    const auto caller_tail=budget.held_tail;
    auto finish=[&](ExecutionStatus status){budget.reserve(caller_tail);out.status=status;out.accepted_prefix=state.accepted_prefix;
        if(!budget.checkpoint()){state.input_blocked=true;out.status=ExecutionStatus::overrun;}
        out.spent=budget.entry>=budget.last?budget.entry-budget.last:0;return out;};
    if(!state.pending)return finish(ExecutionStatus::empty);
    state.begin(host.epoch().round);
    // Even a known ACK first reacquires minimal actual state. Later pose is
    // never retroactively attributed to the old attempt.
    if(!budget.admit(Budget::add(caller_tail,Budget::mul(4,budget.query_bound))))return finish(ExecutionStatus::unfunded);
    budget.reserve(caller_tail);SelfFact fresh{};
    if(!host.feedback(budget,fresh)||!fresh.actual(host.epoch())||!budget.checkpoint())return finish(budget.poisoned?ExecutionStatus::overrun:ExecutionStatus::pending);
    auto& r=state.pending_receipt;r.unknown_feedback=!r.after_actual;
    record_pending(host,state);
    // The Host must retain exact serial ACKs even when a hook returns FALSE or
    // throws after publishing its effect. Replaying such a hook is forbidden.
    state.pending_ack=state.pending_ack||host.commit_known(r.serial);
    if(!state.pending_ack)state.pending_ack=host.commit(r);
    if(!state.pending_ack)return finish(ExecutionStatus::pending);
    state.pending=false;state.next=r.refused?NextInput::refusal:NextInput::exit;out.next=state.next;out.feedback_closed=true;
    return finish(budget.checkpoint()?ExecutionStatus::reconciled:ExecutionStatus::overrun);
}
template<class Host> ExecutionOutcome execute(Host& host,Budget& budget,ExecutionState& state,const Word& word,ExecutionOptions options={}) {
    if(state.pending)return reconcile(host,budget,state);
    ExecutionOutcome out{};state.begin(host.epoch().round);
    const auto caller_tail=budget.held_tail;
    auto finish=[&](ExecutionStatus status){out.status=status;
        budget.reserve(caller_tail);
        if(out.applied&&status!=ExecutionStatus::complete&&state.next==NextInput::work)state.next=NextInput::exit;
        out.next=state.pending?NextInput::feedback:state.next;out.accepted_prefix=state.accepted_prefix;
        if(!budget.checkpoint()){state.input_blocked=true;out.status=ExecutionStatus::overrun;}
        out.spent=budget.entry>=budget.last?budget.entry-budget.last:0;return out;};
    if(state.input_blocked)return finish(ExecutionStatus::overrun);
    if(!word.count)return finish(ExecutionStatus::empty);
    if(word.count>max_successful_primitives||!word.refresh_after_each||word.epoch!=host.epoch())return finish(ExecutionStatus::invalid);
    unsigned alternatives=0;
    for(unsigned i=0;i<word.count;++i)if(word.actions[i].kind==PrimitiveKind::move)
        alternatives=std::min({2u,options.refusal_attempts,max_attempts-word.count});
    while(!budget.admit(Budget::add(caller_tail,word_tail(word,0,budget,alternatives,true)))){
        if(budget.poisoned||!options.allow_shorter_refusal_class||!alternatives)return finish(ExecutionStatus::unfunded);
        --alternatives;
    }
    unsigned at=0;bool alternative=false;Primitive action=word.actions[0];
    while(at<word.count){
        if(state.input_attempts>=max_attempts||state.input_successes>=max_successful_primitives){state.next=NextInput::exit;return finish(ExecutionStatus::continuation);}
        const auto future=Budget::add(caller_tail,alternative?word_tail(word,word.count,budget,alternatives,false):word_tail(word,at+1,budget,alternatives,false));
        const auto command_and_feedback=Budget::add(budget.command_bound,Budget::add(Budget::mul(feedback_queries(action),budget.query_bound),future));
        const auto validation=Budget::mul(validation_queries(action,at==0&&!alternative),budget.query_bound);
        if(!budget.admit(Budget::add(validation,command_and_feedback)))return finish(ExecutionStatus::unfunded);
        budget.reserve(command_and_feedback);Validation fresh{};
        if(!host.fresh(action,budget,fresh)||!budget.checkpoint())return finish(budget.poisoned?ExecutionStatus::overrun:ExecutionStatus::changed);
        if(!authorize(action,fresh,host.epoch())){state.next=out.applied?NextInput::exit:NextInput::work;return finish(ExecutionStatus::changed);}
        budget.reserve(Budget::add(Budget::mul(feedback_queries(action),budget.query_bound),future));
        if(!budget.before_command())return finish(budget.poisoned?ExecutionStatus::overrun:ExecutionStatus::unfunded);
        auto& receipt=state.pending_receipt;receipt={};receipt.serial=++state.serial;receipt.action=action;
        receipt.before_epoch=host.epoch();receipt.before_pose=fresh.self.pose;
        receipt.raw_m_before=fresh.self.raw_m;receipt.move_restricted_before=fresh.self.move_restricted;
        state.pending=true;state.pending_recorded=state.pending_ack=false;receipt.attempted=true;
        ++state.input_attempts;++out.attempts;out.attempted=true;
        bool applied=false;
        try {applied=host.attempt(action);receipt.returned=true;receipt.applied=applied?Truth::yes:Truth::no;receipt.refused=!applied;}
        catch(...){
            // A runtime round preflight may reject BEFORE its command import.
            // Preserve that distinction from an SDK attempt with unknown effect.
            bool started=true;if constexpr(requires{host.attempt_started();})started=host.attempt_started();
            if(started){host.invalidate(effect_scope(action,false,false));record_pending(host,state);}
            else{receipt.attempted=false;receipt.applied=Truth::no;state.pending=false;
                --state.input_attempts;--out.attempts;out.attempted=out.attempts!=0;}
            budget.interrupted();state.input_blocked=true;throw;
        }
        host.invalidate(effect_scope(action,true,applied));receipt.after_epoch=host.epoch();
        if(applied){++state.input_successes;++out.successes;out.applied=true;}else{++state.input_refusals;++out.refusals;}
        const bool command_ok=budget.after_command();
        // Record accepted/refused truth before a refresh or hook can fail.
        record_pending(host,state);
        if(!command_ok){state.input_blocked=true;return finish(ExecutionStatus::overrun);}
        budget.reserve(future);SelfFact after{};
        if(!host.feedback(budget,after)||!after.actual(host.epoch())||!budget.checkpoint())
            return finish(budget.poisoned?ExecutionStatus::overrun:ExecutionStatus::pending);
        receipt.after_actual=true;receipt.after_pose=after.pose;receipt.raw_m_after=after.raw_m;receipt.move_restricted_after=after.move_restricted;
        receipt.recalling_after=after.recalling;receipt.submitting_after=after.submitting;
        receipt.channel_time_actual=after.channel_time_actual;receipt.channel_time_after=after.channel_time;
        state.pending_ack=host.commit(receipt);
        if(!state.pending_ack)return finish(ExecutionStatus::pending);
        state.pending=false;out.feedback_closed=true;
        if(!budget.checkpoint()){state.input_blocked=true;return finish(ExecutionStatus::overrun);}
        if(!applied){state.next=NextInput::refusal;
            if(action.kind==PrimitiveKind::move&&alternatives){
                Primitive next{};const unsigned refused=state.input_refusals;
                bool offered=false;if constexpr(requires{host.alternative_move(action,refused,next);})offered=host.alternative_move(action,refused,next);
                if(offered&&next.kind==PrimitiveKind::move&&next.point!=action.point){--alternatives;action=next;alternative=true;continue;}
            }
            return finish(ExecutionStatus::refused);
        }
        if(alternative){state.next=NextInput::exit;return finish(ExecutionStatus::complete);}
        ++at;if(at<word.count)action=word.actions[at];
    }
    state.next=word.requires_paid_exit&&!word.move_after_work?NextInput::exit:NextInput::work;
    return finish(ExecutionStatus::complete);
}
}
