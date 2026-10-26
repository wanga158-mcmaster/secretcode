#include "../src/native_service_cycle.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <type_traits>

namespace N=mosaic::native_service;
using U=N::CycleUpdate;
using P=N::CyclePhase;
static unsigned checks{},groups{};
static void check(bool value,const char* expression,int line){
    ++checks;if(!value){std::fprintf(stderr,"cycle check failed at %d: %s\n",line,expression);std::abort();}
}
#define CHECK(...) check(bool(__VA_ARGS__),#__VA_ARGS__,__LINE__)
[[maybe_unused]] static constinit N::CycleState constant_cycle{};
static_assert(std::is_trivially_copyable_v<N::CycleState>);
static_assert(std::is_standard_layout_v<N::CycleState>);
static_assert(noexcept(constant_cycle.observe_work({})));

static constexpr N::CycleJob job(int n=0){return {7u+unsigned(n),20u+unsigned(n),2+n,4+n};}
static N::CycleWork work(std::uint64_t serial=1,N::EgressEffect effect=N::EgressEffect::yes,
                        N::CycleJob owner=job(),N::Epoch epoch={10,4,0},bool hazardous=true){
    return {{N::PrimitiveKind::basic,effect,serial,effect==N::EgressEffect::yes?serial:serial-1,
        epoch,{10,10},{owner.group_ordinal,owner.member_ordinal,owner.member_id}},owner,hazardous};
}
static N::SelfFact self(N::Epoch epoch={10,4,0},float hp=100,N::Point pose={10,10}){
    N::SelfFact value{};value.epoch=epoch;value.id=4;value.pose=pose;value.hp=hp;value.max_hp=100;
    value.alive=value.raw_m=value.raw_a=value.raw_c=true;
    value.body_actual=value.restrictions_actual=value.buffs_actual=value.channel_actual=true;
    value.gems_actual=value.custody_actual=true;return value;
}
static N::CyclePlace place(const N::SelfFact& body,bool value=true){return {body.epoch,body.pose,true,value};}
static N::GroupFact group(N::GroupLife life,N::Epoch epoch,N::CycleJob owner=job()){
    N::GroupFact value{};value.epoch=epoch;value.id=owner.group_id;value.ordinal=owner.group_ordinal;
    value.life=life;value.actual=value.visibility_actual=value.visible=true;return value;
}
static N::CaptureFact capture(const N::GroupFact& source,bool holding){
    return {source.epoch,source.id,source.life,true,holding,true};
}
static N::CampFact member(N::Life life,N::Epoch epoch,N::CycleJob owner=job()){
    N::CampFact value{};value.epoch=epoch;value.id=owner.member_id;value.group_id=owner.group_id;
    value.member_ordinal=owner.member_ordinal;value.group_ordinal=owner.group_ordinal;value.life=life;
    value.life_actual=true;return value;
}
static N::CycleReplacement replacement(std::uint64_t serial,N::Epoch epoch,N::CycleJob from,N::CycleJob to){
    return {serial,epoch,from,to,true,true};
}
static N::CampFact living_member(float hp,N::Epoch epoch,N::CycleJob owner=job(),float shield=0){
    auto value=member(N::Life::awake,epoch,owner);value.body_actual=value.buffs_actual=true;
    value.hp=hp;value.max_hp=2048;value.shield=shield;return value;
}
static N::CycleMemberBinding binding(const N::CycleState& state,std::uint64_t serial,N::Epoch epoch){
    return {serial,state.group_generation(),epoch,true,true};
}
static N::CycleGroupShape shape(N::CycleJob owner,std::uint64_t generation,int first,std::size_t count){
    return {owner.group_id,owner.group_ordinal,generation,first,count};
}

static void exact_receipts_and_quiet_ownership(){
    N::CycleState state;CHECK(state.phase()==P::idle&&!state.retained_job().valid()&&!state.restore_required());
    auto bad=work();bad.receipt.serial=0;CHECK(state.observe_work(bad)==U::invalid);
    bad=work();bad.receipt.accepted_prefix=2;CHECK(state.observe_work(bad)==U::invalid);
    bad=work();bad.job.member_ordinal=-1;CHECK(state.observe_work(bad)==U::invalid);
    bad=work();++bad.receipt.job.target;CHECK(state.observe_work(bad)==U::invalid);
    for(auto kind:{N::PrimitiveKind::move,N::PrimitiveKind::bank,N::PrimitiveKind::recall,N::PrimitiveKind::reset}){
        auto other=work();other.receipt.kind=kind;CHECK(state.observe_work(other)==U::ignored);
    }
    CHECK(state.observe_work(work(1,N::EgressEffect::no))==U::ignored&&!state.retained_job().valid());
    auto quiet_work=work(1,N::EgressEffect::yes,job(),{10,4,0},false);
    CHECK(state.observe_work(quiet_work)==U::retained&&state.retained_job()==job());
    CHECK(!state.restore_required()&&!state.has_egressed()&&state.latest_work().receipt.serial==1);
    CHECK(state.observe_work(quiet_work)==U::unchanged);
    bad=quiet_work;bad.receipt.pose.x++;CHECK(state.observe_work(bad)==U::conflict);
    auto current=self({10,5,1});CHECK(state.observe_actual(current,place(current))==U::observed);
    CHECK(state.resume_ready()&&state.phase()==P::redeploy&&!state.batch_active());
    const auto reward=group(N::GroupLife::reward,current.epoch);
    CHECK(state.observe_lifecycle(reward,capture(reward,true),current.epoch)==U::observed);
    CHECK(state.phase()==P::capture&&state.capture_holding()&&state.retained_job()==job());
    CHECK(!state.restore_required()); // Safely ranged service still owns its unfinished group.
    const auto respawning=group(N::GroupLife::respawning,{11,6,0});
    CHECK(state.observe_lifecycle(respawning,{},respawning.epoch)==U::released);
    CHECK(!state.retained_job().valid()&&state.phase()==P::idle);
    ++groups;
}

static void finite_batch_liability_and_actual_restoration(){
    N::CycleState state;auto current=self();state.observe_actual(current,place(current,false));
    CHECK(state.observe_work(work())==U::retained&&state.restore_required());
    CHECK(state.batch_active()&&state.phase()==P::work&&!state.has_egressed()&&!state.resume_ready());
    // The pre-command full-HP/quiet frame cannot close a later command's debt.
    CHECK(state.observe_actual(current,place(current))==U::observed&&state.restore_required()&&!state.has_egressed());
    current=self({10,5,1},80);state.observe_actual(current,place(current,false));
    CHECK(state.batch_active()&&!state.restoration_barrier());
    CHECK(state.observe_work(work(2,N::EgressEffect::yes,job(),current.epoch))==U::extended);
    CHECK(state.restore_required()&&state.batch_active()&&state.latest_work().receipt.serial==2);
    current=self({11,6,0},70,{9,10});state.observe_actual(current,place(current));
    CHECK(state.has_egressed()&&state.restoration_barrier()&&!state.batch_active()&&state.phase()==P::recall);
    current=self({12,7,0},70,{10,10});state.observe_actual(current,place(current,false));
    CHECK(state.phase()==P::recall&&state.restoration_barrier()); // Walking back cannot rearm the batch.
    CHECK(state.observe_work(work(3,N::EgressEffect::yes,job(),current.epoch))==U::conflict);
    CHECK(state.rearm_violation()&&state.latest_work().receipt.serial==3&&state.retained_job()==job());
    current=self({13,8,0},100,{9,10});
    CHECK(state.observe_actual(current,place(current))==U::restored);
    CHECK(!state.restore_required()&&!state.rearm_violation()&&state.resume_ready()&&state.retained_job()==job());
    CHECK(state.restored_through()==3&&state.phase()==P::redeploy);
    // An actual already-full quiet body needs no invented damage or arbitrary Recall.
    CHECK(state.observe_work(work(4,N::EgressEffect::yes,job(),current.epoch))==U::extended);
    CHECK(!state.has_egressed()&&state.restore_required());
    current=self({13,9,1});CHECK(state.observe_actual(current,place(current))==U::restored);
    CHECK(state.resume_ready()&&state.retained_job()==job());
    ++groups;
}

static void recall_death_home_heal_and_redeploy_keep_the_job(){
    N::CycleState state;state.observe_work(work());auto current=self({11,5,0},45,{9,10});
    state.observe_actual(current,place(current));CHECK(state.phase()==P::recall);
    auto start=work(2);start.receipt.kind=N::PrimitiveKind::recall;
    CHECK(state.observe_work(start)==U::ignored&&state.restore_required()&&state.retained_job()==job());
    current=self({12,6,0},45,{9,10});current.recalling=true;
    state.observe_actual(current,place(current));CHECK(state.phase()==P::recall&&!state.resume_ready());
    current=self({13,7,0},45,{1,1});state.observe_actual(current,{},place(current));
    CHECK(state.phase()==P::home_heal&&state.restore_required()&&state.retained_job()==job());
    current=self({14,8,0},0,{1,1});current.alive=false;current.buffs_actual=current.restrictions_actual=false;
    state.observe_actual(current,{},place(current));CHECK(state.phase()==P::dead&&state.restore_required());
    current=self({15,9,0},100,{1,1});current.teleporting=true;
    CHECK(state.observe_actual(current,{},place(current))==U::observed&&state.restore_required());
    current=self({16,10,0},90,{1,1});state.observe_actual(current,{},place(current));
    CHECK(state.phase()==P::home_heal&&!state.resume_ready());
    current=self({17,11,0},100,{1,1});CHECK(state.observe_actual(current,{},place(current))==U::restored);
    CHECK(state.retained_job()==job()&&state.resume_ready()&&state.phase()==P::redeploy);
    ++groups;
}

static void conditional_truth_does_not_erase_accepted_or_body_debt(){
    {
        N::CycleState state;auto pending=work(1,N::EgressEffect::unknown);
        CHECK(state.observe_work(pending)==U::retained&&state.conditional()&&state.restore_required());
        auto current=self({11,5,0});CHECK(state.observe_actual(current,place(current))==U::restored);
        CHECK(!state.restore_required()&&!state.resume_ready()&&state.phase()==P::observe);
        auto known=pending;known.receipt.effect=N::EgressEffect::yes;known.receipt.accepted_prefix=1;
        CHECK(state.observe_work(known)==U::resolved&&!state.conditional()&&!state.restore_required());
        CHECK(state.resume_ready()&&state.observe_work(known)==U::unchanged);
    }
    {
        N::CycleState state;state.observe_work(work());
        auto pending=work(2,N::EgressEffect::unknown,job(),{11,5,0});state.observe_work(pending);
        auto known=pending;known.receipt.effect=N::EgressEffect::no;
        CHECK(state.observe_work(known)==U::resolved&&state.restore_required()&&state.retained_job()==job());
        CHECK(state.latest_work().receipt.effect==N::EgressEffect::no);
        CHECK(state.origin_work().receipt.serial==1&&state.origin_work().receipt.effect==N::EgressEffect::yes);
    }
    {
        N::CycleState state;auto pending=work(1,N::EgressEffect::unknown);state.observe_work(pending);
        auto dead=self({11,5,0},0);dead.alive=false;state.observe_actual(dead);
        auto known=pending;known.receipt.effect=N::EgressEffect::no;
        CHECK(state.observe_work(known)==U::resolved&&state.restore_required()&&state.phase()==P::dead);
        auto current=self({12,6,0});CHECK(state.observe_actual(current,{},place(current))==U::restored);
        CHECK(state.resume_ready());
    }
    {
        N::CycleState state;auto pending=work(1,N::EgressEffect::unknown,job(),{10,4,0},false);
        state.observe_work(pending);CHECK(state.conditional()&&!state.restore_required()&&state.retained_job()==job());
        auto wrong=pending;++wrong.receipt.pose.x;
        CHECK(state.observe_work(wrong)==U::conflict&&state.latest_work().receipt.pose==pending.receipt.pose);
        auto later=work(2,N::EgressEffect::yes,job(),{11,5,0});
        CHECK(state.observe_work(later)==U::conflict&&state.latest_work().receipt.serial==1&&state.conditional());
    }
    ++groups;
}

static void lifecycle_capture_and_release_are_actual_and_distinct(){
    N::CycleState state;state.observe_work(work());auto current=self({11,5,0},60);
    state.observe_actual(current,place(current));
    auto alive=group(N::GroupLife::alive,current.epoch);
    CHECK(state.observe_lifecycle(alive,{},current.epoch)==U::observed);
    CHECK(state.retained_job()==job()&&state.restore_required()&&state.phase()==P::recall);
    auto reward=group(N::GroupLife::reward,current.epoch);
    CHECK(state.observe_lifecycle(reward,capture(reward,true),current.epoch)==U::observed);
    CHECK(state.capture_holding()&&state.retained_job()==job()&&state.restore_required());
    auto pre=group(N::GroupLife::respawning,{10,4,0});
    CHECK(state.observe_lifecycle(pre,{},pre.epoch)==U::stale&&state.retained_job()==job());
    auto wrong=reward;wrong.id++;
    CHECK(state.observe_lifecycle(wrong,{},current.epoch)==U::stale&&state.retained_job()==job());
    auto unknown=reward;unknown.actual=false;
    CHECK(state.observe_lifecycle(unknown,{},current.epoch)==U::invalid&&state.retained_job()==job());
    auto respawning=group(N::GroupLife::respawning,{12,6,0});
    CHECK(state.observe_lifecycle(respawning,{},respawning.epoch)==U::released);
    CHECK(!state.retained_job().valid()&&state.restore_required()); // A team clear does not heal this actor.
    current=self({13,7,0});CHECK(state.observe_actual(current,{},place(current))==U::restored);
    CHECK(state.phase()==P::idle&&!state.resume_ready());
    ++groups;
}

static void positive_custody_remains_bank_owned_without_payout_inference(){
    N::CycleState state;state.observe_work(work(1,N::EgressEffect::yes,job(),{10,4,0},false));
    auto current=self({11,5,0});current.gems=30;state.observe_actual(current,place(current));
    CHECK(state.bank_required()&&state.custody()==30&&state.phase()==P::bank&&!state.resume_ready());
    auto start=work(2);start.receipt.kind=N::PrimitiveKind::bank;
    CHECK(state.observe_work(start)==U::ignored&&state.retained_job()==job()&&state.bank_required());
    current=self({12,6,0});current.gems_actual=false;state.observe_actual(current,place(current));
    CHECK(state.bank_required()&&state.custody()==30&&state.phase()==P::bank);
    auto stale=self({10,4,0});CHECK(state.observe_custody(stale)==U::stale&&state.bank_required());
    current=self({13,7,0});current.gems=30;current.submitting=true;state.observe_actual(current,place(current));
    CHECK(state.bank_required()&&!state.resume_ready()&&state.retained_job()==job());
    current=self({14,8,0});CHECK(state.observe_actual(current,place(current))==U::observed);
    CHECK(!state.bank_required()&&state.resume_ready()&&state.retained_job()==job());
    // There is deliberately no submitted-score, START completion or numeric
    // payout field/API. Actual zero is custody release only.
    ++groups;
}

static void paid_replacement_preserves_both_unfinished_jobs_and_liabilities(){
    N::CycleState state;state.observe_work(work());auto current=self({11,5,0},70);
    current.gems=9;state.observe_actual(current,place(current));
    auto paid=replacement(1,current.epoch,job(),job(1));paid.funded=false;
    CHECK(state.replace(paid)==U::invalid&&state.retained_job()==job()&&!state.suspended_job().valid());
    paid.funded=true;paid.complete=false;CHECK(state.replace(paid)==U::invalid);
    paid.complete=true;CHECK(state.replace(paid)==U::replaced);
    CHECK(state.retained_job()==job(1)&&state.suspended_job()==job()&&state.restore_required()&&state.bank_required());
    CHECK(state.restoration_barrier()&&!state.resume_ready());
    CHECK(state.replace(replacement(2,current.epoch,job(1),job(2)))==U::conflict);
    CHECK(state.retained_job()==job(1)&&state.suspended_job()==job());
    auto respawning=group(N::GroupLife::respawning,{12,6,0},job(1));
    CHECK(state.observe_lifecycle(respawning,{},respawning.epoch)==U::released);
    CHECK(!state.retained_job().valid()&&state.suspended_job()==job()&&state.restore_required());
    current=self({13,7,0});CHECK(state.observe_actual(current,{},place(current))==U::restored);
    CHECK(state.replace(replacement(2,current.epoch,{},job()))==U::replaced);
    CHECK(state.retained_job()==job()&&!state.suspended_job().valid()&&state.resume_ready());
    CHECK(state.latest_work().job==job()&&state.latest_work().receipt.serial==1);
    N::CycleState pending;pending.observe_work(work(1,N::EgressEffect::unknown));pending.observe_actual(current);
    CHECK(pending.replace(replacement(1,current.epoch,job(),job(1)))==U::invalid&&pending.retained_job()==job());
    ++groups;
}

static void qualified_place_channel_and_identity_guards(){
    N::CycleState state;state.observe_work(work());auto current=self({11,5,0});
    auto stale=place(current);stale.epoch.round--;
    CHECK(state.observe_actual(current,stale)==U::observed&&state.restore_required()&&!state.has_egressed());
    auto wrong=place(current);wrong.pose.x++;
    state.observe_actual(current,wrong);CHECK(state.restore_required()&&!state.has_egressed());
    auto absent=place(current);absent.actual=false;state.observe_actual(current,absent);
    CHECK(state.restore_required()&&!state.has_egressed());
    const auto& receipt=state.latest_work().receipt;
    N::CycleEgressClosed closed{current.epoch,current.pose,receipt.serial,receipt.accepted_prefix,true};
    CHECK(state.observe_actual(current,{}, {},closed)==U::observed);
    CHECK(state.has_egressed()&&state.restore_required()&&state.phase()==P::recall); // Closed egress is not HP restoration.
    current.channel_actual=false;CHECK(state.observe_actual(current,place(current))==U::observed&&state.restore_required());
    current.channel_actual=true;current.submitting=true;
    CHECK(state.observe_actual(current,place(current))==U::observed&&state.restore_required());
    current.submitting=false;current.max_hp=current.hp=std::numeric_limits<float>::infinity();
    CHECK(state.observe_actual(current,place(current))==U::observed&&state.restore_required());
    current.max_hp=100;current.hp=std::numeric_limits<float>::quiet_NaN();
    CHECK(state.observe_actual(current,place(current))==U::observed&&state.restore_required());
    current.hp=100;CHECK(state.observe_actual(current,place(current))==U::restored);
    auto old=self({10,4,0},10);CHECK(state.observe_actual(old,place(old))==U::stale&&state.resume_ready());
    auto different=current;different.id=5;CHECK(state.observe_actual(different,place(different))==U::conflict);
    CHECK(state.retained_job()==job()&&state.resume_ready());
    ++groups;
}

static void zero_sdk_handles_are_bound_by_actual_facts_and_ordinals(){
    constexpr N::CycleJob zero{0,0,0,0};CHECK(zero.valid());
    N::CycleState state;auto current=self();current.id=0;state.observe_actual(current,place(current,false));
    CHECK(state.observe_work(work(1,N::EgressEffect::yes,zero,current.epoch))==U::retained);
    CHECK(state.retained_job()==zero&&state.restore_required()&&state.batch_active());
    current=self({11,5,0},80);current.id=0;current.gems=5;state.observe_actual(current,place(current));
    CHECK(state.bank_required()&&state.retained_job()==zero&&state.restoration_barrier());
    CHECK(state.observe_member(member(N::Life::dead,current.epoch,zero),current.epoch)==U::observed&&state.selected_member_dead());
    auto reward=group(N::GroupLife::reward,current.epoch,zero);
    CHECK(state.observe_lifecycle(reward,capture(reward,true),current.epoch)==U::observed&&state.capture_holding());
    current=self({12,6,0});current.id=0;CHECK(state.observe_actual(current,{},place(current))==U::restored);
    CHECK(state.resume_ready()&&!state.bank_required());
    CHECK(state.replace(replacement(1,current.epoch,zero,job()))==U::replaced&&state.suspended_job()==zero);
    auto respawning=group(N::GroupLife::respawning,{13,7,0},zero);
    CHECK(state.observe_lifecycle(respawning,{},respawning.epoch)==U::released&&!state.suspended_job().valid());
    auto different=current;different.id=4;CHECK(state.observe_custody(different)==U::conflict);
    CHECK(state.retained_job()==job());
    ++groups;
}

static void dead_member_advances_within_the_same_unfinished_group(){
    auto next=job();++next.member_id;++next.member_ordinal;
    {
        N::CycleState state;state.observe_work(work());auto current=self({11,5,0},80);state.observe_actual(current);
        const auto alive=group(N::GroupLife::alive,current.epoch);
        CHECK(state.observe_lifecycle(alive,{},current.epoch)==U::observed);
        CHECK(state.observe_member(member(N::Life::dead,current.epoch),current.epoch)==U::observed);
        CHECK(state.selected_member_dead()&&state.member_observation_epoch()==current.epoch);
        CHECK(state.advance_member(job(1),member(N::Life::dead,current.epoch),current.epoch)==U::invalid);
        CHECK(state.advance_member(next,member(N::Life::dead,current.epoch),current.epoch)==U::advanced);
        CHECK(state.retained_job()==next&&!state.suspended_job().valid()&&state.latest_work().receipt.serial==1);
        CHECK(state.last_member_advance().from==job()&&state.last_member_advance().to==next&&state.last_member_advance().actual_dead);
        CHECK(state.last_member_advance().epoch==current.epoch);
        current=self({12,6,0},75,{11,10});state.observe_actual(current); // A later approach input: old member may now be unseen.
        CHECK(state.observe_work(work(2,N::EgressEffect::yes,next,current.epoch))==U::extended);
        CHECK(state.retained_job()==next&&!state.suspended_job().valid()&&!state.ownership_conflict());
        CHECK(!state.selected_member_dead()&&state.restore_required()&&state.batch_active());
        CHECK(state.origin_work().job==job()&&state.latest_work().job==next);
        current=self({13,7,0},70);state.observe_actual(current);
        CHECK(state.observe_member(member(N::Life::dead,current.epoch,next),current.epoch)==U::observed);
        const auto reward=group(N::GroupLife::reward,current.epoch,next);
        CHECK(state.observe_lifecycle(reward,capture(reward,true),current.epoch)==U::observed);
        CHECK(state.retained_job()==next&&state.capture_holding()&&!state.batch_active()&&state.phase()==P::egress);
        const auto respawning=group(N::GroupLife::respawning,{14,8,0},next);
        CHECK(state.observe_lifecycle(respawning,{},respawning.epoch)==U::released);
        CHECK(!state.retained_job().valid()&&state.restore_required());
    }
    for(int invalid_case=0;invalid_case<4;++invalid_case){
        N::CycleState state;state.observe_work(work());auto current=self({11,5,0},80);state.observe_actual(current);
        auto observed=member(N::Life::dead,current.epoch);
        if(invalid_case==0)observed.life=N::Life::asleep;
        if(invalid_case==1)observed.epoch={10,4,0};
        if(invalid_case==2)++observed.group_id;
        if(invalid_case==3)observed.life_actual=false;
        state.observe_member(observed,current.epoch);
        CHECK(!state.selected_member_dead());
        CHECK(state.advance_member(next,observed,current.epoch)==U::invalid);
        CHECK(state.observe_work(work(2,N::EgressEffect::yes,next,current.epoch))==U::extended);
        CHECK(state.retained_job()==next&&!state.suspended_job().valid()&&state.restore_required()&&!state.ownership_conflict());
        CHECK(state.origin_work().job==job()); // Receipt truth changes member, never completes the living/unknown old one.
    }
    {
        N::CycleState state;state.observe_work(work());auto current=self({11,5,0},80);state.observe_actual(current);
        state.observe_member(member(N::Life::dead,current.epoch),current.epoch);
        const auto old_death=member(N::Life::dead,current.epoch);
        current=self({12,6,0},80);state.observe_actual(current);
        CHECK(state.advance_member(next,old_death,current.epoch)==U::invalid);
        CHECK(state.observe_work(work(2,N::EgressEffect::yes,next,current.epoch))==U::extended); // Actual same-group receipt needs no old death witness.
        CHECK(state.retained_job()==next&&state.restore_required()&&!state.ownership_conflict());
    }
    {
        N::CycleState state;state.observe_work(work(1,N::EgressEffect::unknown));auto current=self({11,5,0},80);state.observe_actual(current);
        CHECK(state.advance_member(next,member(N::Life::dead,current.epoch),current.epoch)==U::invalid);
        CHECK(state.conditional()&&state.retained_job()==job()&&state.latest_work().receipt.serial==1);
    }
    ++groups;
}

static void funded_living_member_binding_preserves_the_whole_group_and_other_owners(){
    N::CycleState state;state.observe_work(work());const auto generation=state.group_generation();
    auto current=self({11,5,0},70);current.gems=9;state.observe_actual(current,place(current));
    auto next=job();++next.member_id;++next.member_ordinal;
    auto actual=living_member(536,current.epoch,next);const auto original=state.latest_work();
    auto paid=binding(state,1,current.epoch);paid.funded=false;
    CHECK(state.bind_member(actual,current.epoch,paid)==U::invalid&&state.retained_job()==job());
    paid.funded=true;paid.complete=false;CHECK(state.bind_member(actual,current.epoch,paid)==U::invalid);
    paid.complete=true;++paid.group_generation;CHECK(state.bind_member(actual,current.epoch,paid)==U::invalid);--paid.group_generation;
    auto unknown=actual;unknown.body_actual=false;CHECK(state.bind_member(unknown,current.epoch,paid)==U::invalid);
    auto foreign=actual;++foreign.group_id;CHECK(state.bind_member(foreign,current.epoch,paid)==U::invalid);
    auto stale=actual;stale.epoch.round--;CHECK(state.bind_member(stale,current.epoch,paid)==U::invalid);
    CHECK(!state.selected_member_dead()); // No death of the old living member is required.
    CHECK(state.bind_member(actual,current.epoch,paid)==U::bound);
    CHECK(state.retained_job()==next&&state.group_generation()==generation&&!state.suspended_job().valid());
    CHECK(state.latest_work().receipt.serial==original.receipt.serial&&state.latest_work().job==original.job);
    CHECK(state.restoration_barrier()&&state.restore_required()&&state.bank_required()&&state.custody()==9&&!state.resume_ready());
    CHECK(state.last_member_binding().from==job()&&state.last_member_binding().to==next&&state.last_member_binding().group_generation==generation);
    CHECK(state.bind_member(actual,current.epoch,paid)==U::invalid); // A consumed funding serial cannot be reused.
    CHECK(state.bind_member(actual,current.epoch,binding(state,2,current.epoch))==U::unchanged);
    CHECK(state.group_generation()==generation&&state.origin_work_group_generation()==generation);
    ++groups;
}

static void same_group_receipt_truth_never_compares_different_members_hp(){
    N::CycleState state;state.observe_work(work(1,N::EgressEffect::yes,job(),{10,4,0},false));
    const auto generation=state.group_generation();auto current=self({11,5,0});state.observe_actual(current);
    auto next=job();++next.member_id;++next.member_ordinal;
    N::CycleGroupWork<4> facts;CHECK(facts.configure(shape(job(),generation,job().member_ordinal,2))==N::CycleGroupConfigure::configured);
    CHECK(facts.observe(living_member(1065,current.epoch),current.epoch)==N::CycleGroupUpdate::observed);
    CHECK(facts.observe(living_member(536,current.epoch,next),current.epoch)==N::CycleGroupUpdate::observed);
    CHECK(state.observe_work(work(2,N::EgressEffect::yes,next,current.epoch,false))==U::extended);
    CHECK(state.retained_job()==next&&state.group_generation()==generation&&!state.ownership_conflict()&&!state.suspended_job().valid());
    CHECK(state.origin_work().job==job()&&state.latest_work().job==next&&state.latest_work_group_generation()==generation);
    CHECK(!state.selected_member_dead()&&!state.last_member_advance().actual_dead);
    const auto remaining=facts.remaining(current.epoch);
    CHECK(remaining.observed_living==2&&remaining.observed_dead==0&&remaining.possible_members==2&&remaining.unknown_hp==0);
    CHECK(remaining.known_hp_shield==1601&&facts.damage_events()==0&&facts.death_events()==0); // 1065->536 across targets is no damage receipt.
    auto other=job(1);CHECK(state.observe_work(work(3,N::EgressEffect::yes,other,{12,6,0},false))==U::conflict);
    CHECK(state.retained_job()==next&&state.group_generation()==generation&&state.ownership_conflict());
    ++groups;
}

static void dead59_far30_nomination_cannot_exclude_fresh_same_group57(){
    const N::CycleJob selected{0,59,0,59},far{0,30,0,30},near{0,57,0,57};
    N::CycleState state;state.observe_work(work(1,N::EgressEffect::yes,selected,{10,4,0},false));
    const auto generation=state.group_generation();auto current=self({11,5,0});state.observe_actual(current);
    N::CycleGroupWork<64> facts;CHECK(facts.configure(shape(selected,generation,30,30))==N::CycleGroupConfigure::configured);
    const auto dead59=member(N::Life::dead,current.epoch,selected);
    CHECK(facts.observe(dead59,current.epoch,{generation,100,true})==N::CycleGroupUpdate::observed);
    CHECK(state.observe_member(dead59,current.epoch)==U::observed&&state.advance_member(far,dead59,current.epoch)==U::advanced);
    CHECK(state.retained_job()==far&&state.group_generation()==generation);
    current=self({12,6,0});state.observe_actual(current);
    const auto available57=living_member(536,current.epoch,near);
    CHECK(facts.observe(available57,current.epoch)==N::CycleGroupUpdate::observed);
    CHECK(state.bind_member(available57,current.epoch,binding(state,1,current.epoch))==U::bound);
    CHECK(state.retained_job()==near&&state.group_generation()==generation&&!state.suspended_job().valid());
    CHECK(state.latest_work().receipt.serial==1&&facts.damage_events()==0&&facts.death_events()==0);
    CHECK(!facts.member_needed(59,current.epoch)&&facts.member_needed(30,current.epoch)&&facts.member_needed(57,current.epoch));
    const auto attack=work(2,N::EgressEffect::yes,near,current.epoch,false);
    CHECK(state.observe_work(attack)==U::extended);
    const N::Epoch after{12,7,1};const auto damaged57=living_member(500,after,near);
    N::CycleMemberEffect pair{generation,attack,after,true,living_member(1065,current.epoch,far),damaged57,{}};
    CHECK(facts.observe_effect(pair).status==N::CycleEffectStatus::invalid&&facts.damage_events()==0);
    pair.before=available57;const auto actual=facts.observe_effect(pair);
    CHECK(actual.progress()&&actual.rank_decreased&&!actual.member_dead&&facts.damage_events()==1&&facts.death_events()==0);
    const auto remaining=facts.remaining(after);
    CHECK(remaining.total==30&&remaining.excluded_dead==1&&remaining.possible_members==29&&remaining.fresh_hp==1&&remaining.unknown_hp==28);
    CHECK(remaining.known_hp_shield==500); // No unseen HP and no cross-member old rank enters this sum.
    ++groups;
}

static void group_generations_survive_selection_and_paid_suspension_but_not_respawn(){
    N::CycleState state;state.observe_work(work(1,N::EgressEffect::yes,job(),{10,4,0},false));
    const auto original_generation=state.group_generation();auto current=self({11,5,0});state.observe_actual(current);
    CHECK(state.replace(replacement(1,current.epoch,job(),job(1)))==U::replaced);
    const auto other_generation=state.group_generation();CHECK(other_generation!=original_generation&&state.suspended_group_generation()==original_generation);
    auto next=job(1);++next.member_id;++next.member_ordinal;
    CHECK(state.bind_member(living_member(100,current.epoch,next),current.epoch,binding(state,1,current.epoch))==U::bound);
    CHECK(state.group_generation()==other_generation&&state.suspended_job()==job());
    const auto released=group(N::GroupLife::respawning,{12,6,0},next);
    CHECK(state.observe_lifecycle(released,{},released.epoch)==U::released&&state.group_generation()==0);
    current=self({12,6,0});state.observe_actual(current);
    CHECK(state.replace(replacement(2,current.epoch,{},job()))==U::replaced&&state.group_generation()==original_generation);
    CHECK(state.suspended_group_generation()==0);
    const auto old_release=group(N::GroupLife::respawning,{13,7,0});
    CHECK(state.observe_lifecycle(old_release,{},old_release.epoch)==U::released&&state.group_generation()==0);
    current=self({14,8,0});state.observe_actual(current);
    CHECK(state.observe_work(work(2,N::EgressEffect::yes,job(),current.epoch,false))==U::retained);
    CHECK(state.group_generation()!=original_generation&&state.group_generation()!=other_generation&&state.group_generation()>other_generation);
    CHECK(state.observe_lifecycle(old_release,{},old_release.epoch)==U::stale&&state.retained_job()==job());
    ++groups;
}

static void timed_absence_revision_is_stable_on_hp_dates_and_expires_in_constant_work(){
    using G=N::CycleGroupUpdate;N::CycleGroupWork<8> facts;const N::CycleJob first{0,0,0,0},second{0,1,0,1},third{0,2,0,2};
    CHECK(facts.configure(shape(first,9,0,3))==N::CycleGroupConfigure::configured);const auto initial=facts.route_revision();
    N::Epoch now{10,5,0};CHECK(facts.member_needed(0,now)&&facts.member_needed(1,now));
    CHECK(facts.observe(member(N::Life::dead,now,first),now)==G::observed);
    CHECK(facts.member_needed(0,now)&&facts.route_revision()==initial); // Actual Dead without a qualified lifetime remains possible.
    CHECK(facts.observe(living_member(100,now,second),now)==G::observed&&facts.route_revision()==initial);
    CHECK(facts.observe(member(N::Life::dead,now,first),now,{8,20,true})==G::invalid&&facts.member_needed(0,now));
    CHECK(facts.observe(member(N::Life::dead,now,first),now,{9,20,true})==G::observed);
    CHECK(!facts.member_needed(0,now)&&facts.route_revision()==initial+1&&facts.earliest_absence_expiry()==20);
    now={11,6,0};CHECK(facts.observe(member(N::Life::dead,now,first),now,{9,25,true})==G::observed);
    CHECK(facts.route_revision()==initial+1); // Fresh same Dead while excluded cannot rebuild the route each round.
    CHECK(facts.observe(member(N::Life::dead,now,third),now,{9,30,true})==G::observed);
    const auto two_dead_revision=facts.route_revision();CHECK(two_dead_revision==initial+2);
    CHECK(facts.observe(living_member(50,now,second),now)==G::observed&&facts.route_revision()==two_dead_revision);
    const auto current=facts.remaining(now);
    CHECK(current.observed_dead==2&&current.excluded_dead==2&&current.fresh_hp==1&&current.unknown_hp==0&&current.known_hp_shield==50);
    CHECK(facts.possible_members(now)==1&&!facts.all_members_excluded(now));
    CHECK(!facts.refresh_time({19,9,0})&&!facts.member_needed(0,{19,9,0})&&!facts.member_needed(2,{19,9,0}));
    CHECK(facts.member_needed(0,{20,10,0})&&facts.member_needed(2,{20,10,0})); // Even before refresh, expiry cannot keep a false exclusion.
    CHECK(facts.refresh_time({20,10,0})&&facts.route_revision()==two_dead_revision+1&&facts.earliest_absence_expiry()==-1);
    CHECK(!facts.refresh_time({21,11,0})&&facts.route_revision()==two_dead_revision+1);
    const auto expired=facts.remaining({20,10,0});
    CHECK(expired.observed_dead==2&&expired.excluded_dead==0&&expired.possible_members==3&&expired.unknown_hp==3&&expired.known_hp_shield==0);
    now={21,11,0};CHECK(facts.observe(member(N::Life::dead,now,third),now,{9,40,true})==G::observed);
    CHECK(!facts.member_needed(2,now)&&facts.member_needed(0,now)); // New proof cannot resurrect the expired era for another member.
    const auto renewed=facts.route_revision();now={22,12,0};facts.observe(member(N::Life::dead,now,third),now);
    CHECK(!facts.member_needed(2,now)&&facts.route_revision()==renewed);
    now={23,13,0};facts.observe(living_member(10,now,third),now);
    CHECK(facts.member_needed(2,now)&&facts.route_revision()==renewed+1&&facts.earliest_absence_expiry()==-1);
    CHECK(!facts.refresh_time({40,20,0}));
    CHECK(facts.damage_events()==0&&facts.death_events()==0); // Dated imports and expiry are never own Work effects.
    ++groups;
}

static void exact_same_member_effects_require_ack_dates_and_fresh_numeric_support(){
    const N::CycleJob first{0,0,0,0},second{0,1,0,1};N::CycleGroupWork<4> facts;
    CHECK(facts.configure(shape(first,1,0,2))==N::CycleGroupConfigure::configured);
    const auto attack=work(1,N::EgressEffect::yes,first,{10,4,0},false);const N::Epoch after{10,5,1};
    N::CycleMemberEffect pair{1,attack,after,true,living_member(100,{10,4,0},first,20),living_member(80,after,first,10),{}};
    const auto damaged=facts.observe_effect(pair);
    CHECK(damaged.progress()&&damaged.rank_decreased&&!damaged.member_dead&&facts.damage_events()==1&&facts.death_events()==0);
    CHECK(facts.last_effect().numeric_actual&&facts.last_effect().before_rank==120&&facts.last_effect().after_rank==90);
    CHECK(facts.observe_effect(pair).status==N::CycleEffectStatus::duplicate&&facts.damage_events()==1);
    auto next=work(2,N::EgressEffect::yes,second,{11,6,0},false);const N::Epoch next_after{11,7,1};
    N::CycleMemberEffect mixed{1,next,next_after,true,living_member(1065,{11,6,0},first),living_member(536,next_after,second),{}};
    CHECK(facts.observe_effect(mixed).status==N::CycleEffectStatus::invalid&&facts.damage_events()==1);
    mixed.before=living_member(600,{11,6,0},second);mixed.exact_ack=false;
    CHECK(facts.observe_effect(mixed).status==N::CycleEffectStatus::unknown);
    mixed.exact_ack=true;mixed.after_fact.body_actual=false;
    CHECK(facts.observe_effect(mixed).status==N::CycleEffectStatus::unknown&&facts.damage_events()==1);
    mixed.after_fact=living_member(536,{12,7,1},second);mixed.after=mixed.after_fact.epoch;
    CHECK(facts.observe_effect(mixed).status==N::CycleEffectStatus::unknown); // Cross-round damage may contain other actors.
    mixed.after=next_after;mixed.after_fact=living_member(600,next_after,second);
    CHECK(!facts.observe_effect(mixed).progress()&&facts.damage_events()==1); // Equal-rank accepted Work is no positive progress.
    auto lethal=work(3,N::EgressEffect::yes,second,{12,8,0},false);const N::Epoch death_after{12,9,1};
    N::CycleMemberEffect death{1,lethal,death_after,true,living_member(536,{12,8,0},second),member(N::Life::dead,death_after,second),{1,30,true}};
    const auto ended=facts.observe_effect(death);
    CHECK(ended.progress()&&ended.member_dead&&!ended.rank_decreased&&facts.death_events()==1&&facts.damage_events()==1);
    CHECK(!facts.last_effect().numeric_actual&&!facts.member_needed(1,death_after)); // Actual death need not invent a numeric zero HP read.
    CHECK(facts.observe_effect(death).status==N::CycleEffectStatus::duplicate&&facts.death_events()==1);
    const auto last=facts.last_effect();CHECK(facts.configure(shape(first,2,0,2))==N::CycleGroupConfigure::configured);
    CHECK(facts.member_needed(1,death_after)&&facts.remaining(death_after).unseen==2&&facts.last_effect().serial==last.serial);
    CHECK(facts.observe_effect(death).status==N::CycleEffectStatus::invalid); // Old-generation receipt cannot complete a new cycle.
    const auto fresh_work=work(4,N::EgressEffect::yes,first,{13,10,0},false);const N::Epoch fresh_after{13,11,1};
    N::CycleMemberEffect new_cycle{2,fresh_work,fresh_after,true,living_member(100,{13,10,0},first),living_member(90,fresh_after,first),{}};
    CHECK(facts.observe_effect(new_cycle).progress()&&facts.damage_events()==1&&facts.death_events()==0);
    CHECK(facts.last_effect().serial==4&&facts.last_effect().group_generation==2&&facts.member_needed(1,fresh_after));
    ++groups;
}

static void large_group_fact_storage_is_constinit_noncopyable_and_new_generation_is_unknown(){
    static constinit N::CycleGroupWork<16384> large{};
    static_assert(!std::is_copy_constructible_v<N::CycleGroupWork<16384>>);
    static_assert(!std::is_move_constructible_v<N::CycleGroupWork<16384>>);
    static_assert(noexcept(large.member_needed(0,{}))&&noexcept(large.refresh_time({})));
    const N::CycleJob last{0,0,0,16383};const N::Epoch now{10,1,0};
    CHECK(large.configure(shape(last,1,0,16384))==N::CycleGroupConfigure::configured);
    CHECK(large.member_needed(0,now)&&large.member_needed(16383,now));
    CHECK(large.observe(member(N::Life::dead,now,last),now,{1,20,true})==N::CycleGroupUpdate::observed);
    CHECK(!large.member_needed(16383,now)&&large.member_needed(0,now));
    const auto revision=large.route_revision();CHECK(large.configure(shape(last,1,0,16384))==N::CycleGroupConfigure::retained);
    CHECK(large.route_revision()==revision&&!large.member_needed(16383,now));
    CHECK(large.configure(shape(last,1,1,16383))==N::CycleGroupConfigure::changed&&!large.member_needed(16383,now));
    CHECK(large.configure(shape(last,2,0,16384))==N::CycleGroupConfigure::configured);
    CHECK(large.route_revision()==revision+1&&large.member_needed(16383,now));
    CHECK(large.member_needed(-1,now)&&large.member_needed(16384,now));
    ++groups;
}

static void group_snapshot_hp_is_dated_and_unknown_or_sleep_is_not_a_reset(){
    N::CycleGroupWork<4> facts;CHECK(facts.configure(shape(job(),1,job().member_ordinal,2))==N::CycleGroupConfigure::configured);
    const auto revision=facts.route_revision();N::Epoch now{10,4,0};
    CHECK(facts.observe(living_member(1065,now),now)==N::CycleGroupUpdate::observed);
    now={11,5,0};auto asleep=living_member(536,now);asleep.life=N::Life::asleep;
    CHECK(facts.observe(asleep,now)==N::CycleGroupUpdate::observed);
    const auto sleeping=facts.remaining(now);
    CHECK(sleeping.known_hp_shield==536&&sleeping.fresh_hp==1&&sleeping.unknown_hp==1&&sleeping.unseen==1);
    CHECK(facts.damage_events()==0&&facts.death_events()==0&&facts.route_revision()==revision); // Ordinary imports credit no own damage.
    now={12,6,0};auto hidden=living_member(0,now);hidden.body_actual=false;
    CHECK(facts.observe(hidden,now)==N::CycleGroupUpdate::observed);
    auto unknown=member(N::Life::unknown,now);++unknown.id;++unknown.member_ordinal;unknown.life_actual=false;
    CHECK(facts.observe(unknown,now)==N::CycleGroupUpdate::invalid);
    const auto missing=facts.remaining(now);
    CHECK(missing.observed_living==1&&missing.unseen==1&&missing.fresh_hp==0&&missing.unknown_hp==2&&missing.known_hp_shield==0);
    CHECK(!missing.all_remaining_hp_current&&facts.member_needed(job().member_ordinal,now)&&facts.route_revision()==revision);
    ++groups;
}

static void all_excluded_requests_lifecycle_and_expiry_never_completes_the_group(){
    const N::CycleJob first{0,0,0,0},second{0,1,0,1};N::CycleState state;
    state.observe_work(work(1,N::EgressEffect::yes,first,{10,4,0},false));const auto generation=state.group_generation();
    auto current=self({11,5,0});state.observe_actual(current);
    N::CycleGroupWork<4> facts;CHECK(!facts.all_members_excluded(current.epoch));
    CHECK(facts.configure(shape(first,generation,0,2))==N::CycleGroupConfigure::configured);
    CHECK(facts.possible_members(current.epoch)==2&&!facts.all_members_excluded(current.epoch));
    facts.observe(member(N::Life::dead,current.epoch,first),current.epoch,{generation,20,true});
    CHECK(facts.possible_members(current.epoch)==1&&!facts.all_members_excluded(current.epoch));
    facts.observe(member(N::Life::dead,current.epoch,second),current.epoch,{generation,25,true});
    CHECK(facts.possible_members(current.epoch)==0&&facts.all_members_excluded(current.epoch));
    CHECK(state.retained_job()==first&&state.group_generation()==generation); // Empty frontier is not service completion or reward.
    const auto still_alive=group(N::GroupLife::alive,current.epoch,first);
    CHECK(state.observe_lifecycle(still_alive,{},current.epoch)==U::observed&&state.retained_job()==first);
    CHECK(facts.damage_events()==0&&facts.death_events()==0);
    CHECK(facts.possible_members({20,6,0})==2&&!facts.all_members_excluded({20,6,0}));
    CHECK(facts.refresh_time({20,6,0})&&facts.possible_members({20,6,0})==2);
    CHECK(facts.remaining({20,6,0}).observed_dead==2&&facts.remaining({20,6,0}).excluded_dead==0);
    current=self({20,6,0});state.observe_actual(current);
    const auto respawn=group(N::GroupLife::respawning,current.epoch,first);
    CHECK(state.observe_lifecycle(respawn,{},current.epoch)==U::released&&state.group_generation()==0);
    current=self({21,7,0});state.observe_actual(current);
    CHECK(state.observe_work(work(2,N::EgressEffect::yes,first,current.epoch,false))==U::retained);
    CHECK(state.group_generation()!=generation);
    CHECK(facts.configure(shape(first,state.group_generation(),0,2))==N::CycleGroupConfigure::configured);
    CHECK(facts.possible_members(current.epoch)==2&&!facts.all_members_excluded(current.epoch));
    ++groups;
}

int main(){
    exact_receipts_and_quiet_ownership();finite_batch_liability_and_actual_restoration();
    recall_death_home_heal_and_redeploy_keep_the_job();conditional_truth_does_not_erase_accepted_or_body_debt();
    lifecycle_capture_and_release_are_actual_and_distinct();positive_custody_remains_bank_owned_without_payout_inference();
    paid_replacement_preserves_both_unfinished_jobs_and_liabilities();qualified_place_channel_and_identity_guards();
    zero_sdk_handles_are_bound_by_actual_facts_and_ordinals();dead_member_advances_within_the_same_unfinished_group();
    funded_living_member_binding_preserves_the_whole_group_and_other_owners();same_group_receipt_truth_never_compares_different_members_hp();
    dead59_far30_nomination_cannot_exclude_fresh_same_group57();group_generations_survive_selection_and_paid_suspension_but_not_respawn();
    timed_absence_revision_is_stable_on_hp_dates_and_expires_in_constant_work();exact_same_member_effects_require_ack_dates_and_fresh_numeric_support();
    large_group_fact_storage_is_constinit_noncopyable_and_new_generation_is_unknown();
    group_snapshot_hp_is_dated_and_unknown_or_sleep_is_not_a_reset();
    all_excluded_requests_lifecycle_and_expiry_never_completes_the_group();
    std::printf("PASS native_service_cycle: %u checks, %u groups\n",checks,groups);
}
