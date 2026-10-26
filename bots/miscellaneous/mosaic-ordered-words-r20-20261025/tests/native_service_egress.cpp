#include "native_service_egress.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

namespace N=mosaic::native_service;
using U=N::EgressUpdate;
using Effect=N::EgressEffect;
static unsigned checks=0,groups=0;
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"egress check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)
static constinit N::EgressState constant_state{};
static_assert(std::is_trivially_copyable_v<N::EgressState>);
static_assert(noexcept(constant_state.observe_work({})));
static constexpr N::EgressWork work(std::uint64_t serial=1,Effect effect=Effect::yes,
                                    N::PrimitiveKind kind=N::PrimitiveKind::basic){
    return {kind,effect,serial,effect==Effect::yes?serial:serial-1,{10,3,serial-1},{5,5},{2,7,99}};
}
static constexpr N::EgressClosure closure(const N::EgressState& state,N::Epoch epoch={10,4,1},N::Point pose={4,5}){
    return {epoch,pose,state.latest().serial,state.latest().accepted_prefix,N::EgressProducer::complete_potential_zero,true};
}
static constexpr bool constexpr_debt(){
    N::EgressState state;
    if(state.observe_work(work())!=U::created||!state.direct_required())return false;
    const auto proof=closure(state);
    return state.close(proof,{proof.epoch,proof.pose,true})==U::closed&&!state.outstanding();
}
static_assert(constexpr_debt());

static void accepted_prefix_precedes_feedback(){
    ++groups;N::EgressState state;
    CHECK(!constant_state.outstanding()&&!constant_state.direct_required());
    CHECK(state.observe_work(work())==U::created);
    // No feedback/ACK success is supplied: the physical return already owns it.
    CHECK(state.outstanding()&&state.accepted()&&!state.conditional()&&state.direct_required());
    CHECK(state.origin().serial==1&&state.origin().accepted_prefix==1&&state.origin().job==N::EgressJob{2,7,99});
    CHECK(state.latest().serial==1&&state.latest().epoch==N::Epoch{10,3,0});
    // New input, unrelated nomination, refusal, custody and generic NextInput
    // have no clearing operation in this independent state.
    auto move=work(2,Effect::yes,N::PrimitiveKind::move);
    CHECK(state.observe_work(move)==U::ignored&&state.outstanding());
    move.kind=N::PrimitiveKind::recall;CHECK(state.observe_work(move)==U::ignored&&state.outstanding());
    move.kind=N::PrimitiveKind::bank;CHECK(state.observe_work(move)==U::ignored&&state.outstanding());
    move.kind=N::PrimitiveKind::reset;CHECK(state.observe_work(move)==U::ignored&&state.outstanding());
    move.kind=N::PrimitiveKind::shield;CHECK(state.observe_work(move)==U::ignored&&state.outstanding());
    CHECK(state.observe_work(work(2,Effect::no))==U::ignored&&state.latest().serial==1);
    auto proof=closure(state);proof.qualified=false;
    CHECK(state.close(proof,{proof.epoch,proof.pose,true})==U::invalid&&state.outstanding());
    proof.qualified=true;proof.producer=N::EgressProducer::none;
    CHECK(state.close(proof,{proof.epoch,proof.pose,true})==U::invalid&&state.outstanding());
}

static void unknown_work_is_conditional_not_an_accepted_hit(){
    ++groups;N::EgressState state;
    for(const auto kind:std::array{N::PrimitiveKind::basic,N::PrimitiveKind::cast_unit,N::PrimitiveKind::cast_ground}){
        N::EgressState refused;
        CHECK(refused.observe_work(work(1,Effect::no,kind))==U::ignored&&!refused.outstanding());
        CHECK(refused.observe_work(work(2,Effect::yes,kind))==U::created&&refused.accepted());
    }
    auto event=work(1,Effect::unknown);
    CHECK(state.observe_work(event)==U::created);
    CHECK(state.outstanding()&&state.conditional()&&!state.accepted());
    CHECK(state.latest().accepted_prefix==0);
    CHECK(state.observe_work(event)==U::unchanged&&state.conditional());
    event.effect=Effect::no;
    CHECK(state.observe_work(event)==U::resolved&&!state.outstanding());
    CHECK(state.observe_work(work(1,Effect::unknown))==U::stale&&!state.outstanding());
    CHECK(state.observe_work(work(2,Effect::unknown))==U::created);
    event=work(2);CHECK(state.observe_work(event)==U::resolved);
    CHECK(state.accepted()&&!state.conditional()&&state.direct_required());
    CHECK(state.origin().serial==2&&state.origin().effect==Effect::yes&&state.origin().accepted_prefix==2);
    CHECK(state.observe_work(work(2,Effect::no))==U::conflict&&state.accepted());
    // A FALSE resolution of a later conditional attempt preserves older work.
    CHECK(state.observe_work(work(3,Effect::unknown))==U::extended);
    CHECK(state.accepted()&&state.conditional());
    CHECK(state.observe_work(work(3,Effect::no))==U::resolved);
    CHECK(state.accepted()&&!state.conditional()&&state.latest().serial==2);
    CHECK(state.observe_work(work(3,Effect::unknown))==U::conflict&&state.outstanding());
}

static void a_second_word_extends_the_same_egress_owner(){
    ++groups;N::EgressState state;
    CHECK(state.observe_work(work())==U::created);
    const auto old=closure(state,{10,4,1});
    auto cast=work(2,Effect::yes,N::PrimitiveKind::cast_ground);
    cast.epoch={10,4,1};cast.job={3,8,100};
    CHECK(state.observe_work(cast)==U::extended);
    CHECK(state.origin().serial==1&&state.origin().job==N::EgressJob{2,7,99});
    CHECK(state.latest().serial==2&&state.latest().job==N::EgressJob{3,8,100});
    CHECK(state.close(old,{old.epoch,old.pose,true})==U::stale&&state.outstanding());
    auto proof=closure(state,cast.epoch);
    CHECK(state.close(proof,{proof.epoch,proof.pose,true})==U::stale&&state.outstanding());
    proof=closure(state,{10,5,2});
    auto invalid=proof;invalid.accepted_prefix=1;
    CHECK(state.close(invalid,{invalid.epoch,invalid.pose,true})==U::stale);
    CHECK(state.close(proof,{proof.epoch,proof.pose,true})==U::closed);
    CHECK(!state.outstanding()&&state.origin().serial==1&&state.latest().serial==2);
    CHECK(state.observe_work(cast)==U::stale&&!state.outstanding());
    auto next=work(3);next.epoch={11,0,0};
    CHECK(state.observe_work(next)==U::created&&state.origin().serial==3);
}

static void closures_require_actual_qualified_complete_egress(){
    ++groups;N::EgressState state;
    CHECK(state.observe_work(work())==U::created);
    auto proof=closure(state);
    CHECK(state.close(proof,{proof.epoch,proof.pose,false})==U::invalid&&state.outstanding());
    CHECK(state.close(proof,{{11,0,0},proof.pose,true})==U::invalid&&state.outstanding());
    CHECK(state.close(proof,{proof.epoch,{99,99},true})==U::invalid&&state.outstanding());
    auto stale=proof;stale.epoch=work().epoch;
    CHECK(state.close(stale,{stale.epoch,stale.pose,true})==U::stale&&state.outstanding());
    stale=proof;stale.through_serial=0;
    CHECK(state.close(stale,{stale.epoch,stale.pose,true})==U::stale&&state.outstanding());
    stale=proof;stale.through_serial=2;
    CHECK(state.close(stale,{stale.epoch,stale.pose,true})==U::stale&&state.outstanding());
    proof.producer=N::EgressProducer::home;
    CHECK(state.close(proof,{proof.epoch,proof.pose,true})==U::closed&&!state.outstanding());
    CHECK(state.close(proof,{proof.epoch,proof.pose,true})==U::unchanged);
}

static void complete_funded_replacement_retains_the_debt(){
    ++groups;N::EgressState state;CHECK(state.observe_work(work())==U::created);
    N::EgressTransfer transfer{{10,4,1},{4,5},1,1,77,true,true};
    auto invalid=transfer;invalid.funded=false;
    CHECK(state.transfer(invalid,{invalid.epoch,invalid.pose,true})==U::invalid&&state.direct_required());
    invalid=transfer;invalid.complete=false;
    CHECK(state.transfer(invalid,{invalid.epoch,invalid.pose,true})==U::invalid&&state.direct_required());
    invalid=transfer;invalid.replacement_owner=0;
    CHECK(state.transfer(invalid,{invalid.epoch,invalid.pose,true})==U::invalid&&state.direct_required());
    invalid=transfer;invalid.through_serial=2;
    CHECK(state.transfer(invalid,{invalid.epoch,invalid.pose,true})==U::stale&&state.direct_required());
    invalid=transfer;invalid.epoch=work().epoch;
    CHECK(state.transfer(invalid,{invalid.epoch,invalid.pose,true})==U::stale&&state.direct_required());
    CHECK(state.transfer(transfer,{transfer.epoch,transfer.pose,false})==U::invalid&&state.direct_required());
    CHECK(state.transfer(transfer,{transfer.epoch,transfer.pose,true})==U::transferred);
    CHECK(state.outstanding()&&state.accepted()&&state.transferred()&&!state.direct_required());
    CHECK(state.replacement_owner()==77&&state.origin().job==N::EgressJob{2,7,99}&&state.latest().accepted_prefix==1);
    CHECK(state.observe_work(work(2,Effect::no))==U::ignored&&state.transferred());
    CHECK(state.observe_work(work(2,Effect::yes,N::PrimitiveKind::move))==U::ignored&&state.transferred());
    auto second=work(2,Effect::yes,N::PrimitiveKind::cast_unit);second.epoch={11,0,0};
    CHECK(state.observe_work(second)==U::extended);
    CHECK(state.direct_required()&&!state.transferred()&&state.origin().serial==1&&state.latest().serial==2);
    CHECK(state.transfer(transfer,{transfer.epoch,transfer.pose,true})==U::stale&&state.direct_required());
}

static void malformed_or_multiple_uncertain_attempts_do_not_forge_closure(){
    ++groups;N::EgressState state;
    auto invalid=work();invalid.serial=0;CHECK(state.observe_work(invalid)==U::invalid&&!state.outstanding());
    invalid=work();invalid.epoch.round=-1;CHECK(state.observe_work(invalid)==U::invalid&&!state.outstanding());
    invalid=work();invalid.accepted_prefix=0;CHECK(state.observe_work(invalid)==U::invalid&&!state.outstanding());
    CHECK(state.observe_work(work(1,Effect::unknown))==U::created);
    CHECK(state.observe_work(work(2,Effect::unknown))==U::conflict&&state.conditional()&&!state.accepted());
    CHECK(state.observe_work(work(2,Effect::no))==U::conflict&&state.outstanding());
    auto proof=closure(state,{11,0,0});
    CHECK(state.close(proof,{proof.epoch,proof.pose,true})==U::closed&&!state.outstanding());
    CHECK(state.observe_work(work(1))==U::stale&&!state.outstanding());
}
static void expired_replacement_reclaims_without_forging_closure(){
    ++groups;N::EgressState state;CHECK(state.observe_work(work())==U::created);
    const N::EgressTransfer transfer{{11,0,0},{4,4},1,1,77,true,true};
    CHECK(state.transfer(transfer,{transfer.epoch,transfer.pose,true})==U::transferred);
    N::EgressReclaim proof{{12,0,0},{4,4},1,1,76};
    CHECK(state.reclaim(proof,{proof.epoch,proof.pose,true})==U::invalid&&state.transferred());
    proof.expected_owner=77;proof.through_serial=2;
    CHECK(state.reclaim(proof,{proof.epoch,proof.pose,true})==U::stale&&state.transferred());
    proof.through_serial=1;
    CHECK(state.reclaim(proof,{proof.epoch,proof.pose,false})==U::invalid&&state.transferred());
    CHECK(state.reclaim(proof,{proof.epoch,proof.pose,true})==U::reclaimed);
    CHECK(state.outstanding()&&state.direct_required()&&state.accepted()&&!state.transferred());
    CHECK(state.origin().serial==1&&state.latest().accepted_prefix==1&&state.origin().job==N::EgressJob{2,7,99});
    CHECK(state.reclaim(proof,{proof.epoch,proof.pose,true})==U::unchanged&&state.direct_required());
}

int main(){
    accepted_prefix_precedes_feedback();unknown_work_is_conditional_not_an_accepted_hit();
    a_second_word_extends_the_same_egress_owner();closures_require_actual_qualified_complete_egress();
    complete_funded_replacement_retains_the_debt();malformed_or_multiple_uncertain_attempts_do_not_forge_closure();
    expired_replacement_reclaims_without_forging_closure();
    std::printf("PASS native_service_egress: %u checks, %u groups\n",checks,groups);
}
