#pragma once
#include "native_service_words.h"
#include <cstdint>

namespace mosaic::native_service {

enum class EgressEffect : std::uint8_t { unknown,no,yes };
enum class EgressProducer : std::uint8_t { none,complete_potential_zero,home };
enum class EgressUpdate : std::uint8_t { ignored,unchanged,created,extended,resolved,stale,conflict,invalid,closed,transferred,reclaimed };
struct EgressJob {
    int group{-1},member{-1}; NativeId target{};
    friend constexpr bool operator==(EgressJob,EgressJob)=default;
};
struct EgressWork {
    PrimitiveKind kind{PrimitiveKind::basic}; EgressEffect effect{EgressEffect::unknown};
    std::uint64_t serial{},accepted_prefix{};
    // The actual validated PRE-attempt epoch/pose. Publish known return truth
    // immediately; acquiring feedback is not a prerequisite for this debt.
    Epoch epoch{}; Point pose{}; EgressJob job{};
};
struct EgressActual { Epoch epoch{}; Point pose{}; bool actual{}; };
struct EgressClosure {
    Epoch epoch{}; Point pose{}; std::uint64_t through_serial{},accepted_prefix{};
    EgressProducer producer{EgressProducer::none}; bool qualified{};
};
struct EgressTransfer {
    Epoch epoch{}; Point pose{}; std::uint64_t through_serial{},accepted_prefix{},replacement_owner{};
    bool complete{},funded{};
};
struct EgressReclaim {
    Epoch epoch{};Point pose{};std::uint64_t through_serial{},accepted_prefix{},expected_owner{};
};

// One work-induced obligation, independent of generic next-input hints, job
// nominations, custody and the executor's pending ACK. There is deliberately no
// begin-input/reset/ordinary-move clearing API. A route nomination or one lower
// potential step is not a complete egress observation.
class EgressState {
public:
    constexpr EgressUpdate observe_work(const EgressWork& event) noexcept {
        if(!work_kind(event.kind))return EgressUpdate::ignored;
        if(!valid(event.epoch)||!event.serial||event.accepted_prefix>event.serial ||
           (event.effect==EgressEffect::yes&&!event.accepted_prefix))return EgressUpdate::invalid;
        if(event.effect!=EgressEffect::unknown&&event.effect!=EgressEffect::no&&event.effect!=EgressEffect::yes)
            return EgressUpdate::invalid;
        if(event.serial<seen_serial_)return EgressUpdate::stale;
        if(event.serial==seen_serial_){
            if(!outstanding())return EgressUpdate::stale;
            if(event.serial!=latest_.serial||!same_attempt(event,latest_))return EgressUpdate::conflict;
            if(latest_.effect!=EgressEffect::unknown){
                return event.effect==latest_.effect&&event.accepted_prefix==latest_.accepted_prefix?
                    EgressUpdate::unchanged:EgressUpdate::conflict;
            }
            if(event.effect==EgressEffect::unknown)return event.accepted_prefix==latest_.accepted_prefix?
                EgressUpdate::unchanged:EgressUpdate::conflict;
            if(event.accepted_prefix<latest_.accepted_prefix)return EgressUpdate::invalid;
            replacement_owner_=0;
            if(event.effect==EgressEffect::yes){
                accepted_=true;accepted_tail_=latest_=event;
                if(origin_.serial==event.serial)origin_=event;
                if(!ambiguous_)conditional_=false;
                return EgressUpdate::resolved;
            }
            if(ambiguous_)return EgressUpdate::conflict;
            conditional_=false;latest_=accepted_?accepted_tail_:event;
            return EgressUpdate::resolved;
        }
        // A known FALSE work attempt creates no work debt, nor does it revoke
        // an accepted/conditional prefix from an earlier attempt.
        if(event.effect==EgressEffect::no)return EgressUpdate::ignored;
        if(outstanding()&&(before(event.epoch,latest_.epoch)||event.accepted_prefix<latest_.accepted_prefix))
            return EgressUpdate::invalid;
        const bool was_outstanding=outstanding();
        if(!was_outstanding){origin_=event;accepted_=conditional_=ambiguous_=false;}
        seen_serial_=event.serial;latest_=event;replacement_owner_=0;
        if(event.effect==EgressEffect::yes){accepted_=true;accepted_tail_=event;}
        else{if(conditional_)ambiguous_=true;conditional_=true;}
        return ambiguous_?EgressUpdate::conflict:was_outstanding?EgressUpdate::extended:EgressUpdate::created;
    }

    constexpr EgressUpdate close(const EgressClosure& proof,const EgressActual& current) noexcept {
        if(!outstanding())return EgressUpdate::unchanged;
        if(!proof.qualified||(proof.producer!=EgressProducer::complete_potential_zero&&proof.producer!=EgressProducer::home))
            return EgressUpdate::invalid;
        const auto result=boundary(proof.epoch,proof.pose,proof.through_serial,proof.accepted_prefix,current);
        if(result!=EgressUpdate::unchanged)return result;
        accepted_=conditional_=ambiguous_=false;replacement_owner_=0;
        return EgressUpdate::closed;
    }

    // This transfers ownership, not observed safety. The complete, funded
    // replacement must itself retain pending feedback, refusal and egress.
    // Debt and origin/latest serial/job/prefix stay inspectable after transfer.
    constexpr EgressUpdate transfer(const EgressTransfer& proof,const EgressActual& current) noexcept {
        if(!outstanding())return EgressUpdate::invalid;
        if(!proof.replacement_owner||!proof.complete||!proof.funded)return EgressUpdate::invalid;
        const auto result=boundary(proof.epoch,proof.pose,proof.through_serial,proof.accepted_prefix,current);
        if(result!=EgressUpdate::unchanged)return result;
        replacement_owner_=proof.replacement_owner;
        return EgressUpdate::transferred;
    }

    // A failed/expired replacement returns the SAME obligation to direct
    // egress. It never discharges the physical frontier or rewrites its job.
    constexpr EgressUpdate reclaim(const EgressReclaim& proof,const EgressActual& current) noexcept {
        if(!outstanding()||!replacement_owner_)return EgressUpdate::unchanged;
        if(!proof.expected_owner||proof.expected_owner!=replacement_owner_)return EgressUpdate::invalid;
        const auto result=boundary(proof.epoch,proof.pose,proof.through_serial,proof.accepted_prefix,current);
        if(result!=EgressUpdate::unchanged)return result;
        replacement_owner_=0;return EgressUpdate::reclaimed;
    }

    constexpr bool outstanding()const noexcept {return accepted_||conditional_;}
    constexpr bool direct_required()const noexcept {return outstanding()&&!replacement_owner_;}
    constexpr bool accepted()const noexcept {return accepted_;}
    constexpr bool conditional()const noexcept {return conditional_;}
    constexpr bool transferred()const noexcept {return outstanding()&&replacement_owner_;}
    constexpr std::uint64_t replacement_owner()const noexcept {return replacement_owner_;}
    constexpr const EgressWork& origin()const noexcept {return origin_;}
    constexpr const EgressWork& latest()const noexcept {return latest_;}

private:
    EgressWork origin_{},latest_{},accepted_tail_{};
    std::uint64_t seen_serial_{},replacement_owner_{};
    bool accepted_{},conditional_{},ambiguous_{};
    static constexpr bool valid(Epoch epoch) noexcept {return epoch.round>=0;}
    static constexpr bool before(Epoch a,Epoch b) noexcept {
        return a.round!=b.round?a.round<b.round:
            a.revision!=b.revision?a.revision<b.revision:a.attempt<b.attempt;
    }
    static constexpr bool work_kind(PrimitiveKind kind) noexcept {
        return kind==PrimitiveKind::basic||kind==PrimitiveKind::cast_unit||kind==PrimitiveKind::cast_ground;
    }
    static constexpr bool same_attempt(const EgressWork& a,const EgressWork& b) noexcept {
        return a.kind==b.kind&&a.epoch==b.epoch&&a.pose==b.pose&&a.job==b.job;
    }
    constexpr EgressUpdate boundary(Epoch epoch,Point pose,std::uint64_t serial,std::uint64_t prefix,
                                    const EgressActual& current)const noexcept {
        if(!current.actual||!valid(epoch)||!valid(current.epoch)||epoch!=current.epoch||pose!=current.pose)
            return EgressUpdate::invalid;
        if(serial!=latest_.serial||prefix!=latest_.accepted_prefix||!before(latest_.epoch,epoch))
            return EgressUpdate::stale;
        return EgressUpdate::unchanged;
    }
};

} // namespace mosaic::native_service
