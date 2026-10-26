#include "../src/capital_service_program.h"
#include <cstdio>
#include <limits>
using namespace wb::capital::service_program;
namespace {
unsigned checks{},failures{};
#define CHECK(x) do{++checks;if(!(x)){++failures;if(failures<=24)std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
Frame frame() {
    Frame f;f.owner=4;f.round=10;f.input=1;f.sample=1;f.pose={2,3};f.hp=100;f.alive=true;
    f.channel=Channel::None;f.permissions=all_permissions;f.kit_revision=f.inventory_revision=1;
    f.source_family=2;f.source_life=f.source_revision=f.capability_revision=1;
    f.route_revision=f.occupancy_revision=f.lock_revision=f.foe_revision=f.observation_revision=f.fuel_revision=1;
    f.available_fuel=100000;f.observed=f.fresh=true;return f;
}
Frame next(Frame f) {++f.round;++f.input;f.sample=1;return f;}
Frame sample(Frame f) {++f.sample;return f;}
Program program(const Frame& f,Kind kind=Kind::Acquire,std::uint32_t permissions=Move) {
    Program p;p.kind=kind;p.source=f;p.action={FirstKind::PhysicalWord,17,permissions,0,9000};
    p.risk.horizon_round=f.round+1;p.complete_interpreter=p.funded=true;return p;
}
InvestmentQuote quote(const Frame& f,int target=7,std::uint64_t cost=90) {return {target,cost,11,f.inventory_revision,true,true};}
void postbank_acquire_and_real_preparation() {
    State state(4);auto f=frame();f.selected_group=-1;f.bag=0;
    CHECK(state.observe(f).accepted);auto p=program(f);
    const auto acquire=state.grant(f,p);CHECK(acquire.status==GrantStatus::CompleteIncumbent&&state.matches(acquire.lease,f));
    CHECK(acquire.lease.program.risk.replies==Knowledge::Unknown&&acquire.lease.program.risk.services==Knowledge::Unknown);
    // Preparation is actual funded work in THIS invocation, not authority to
    // yield an empty input or defer the economic programme indefinitely.
    p.complete_interpreter=false;p.action={FirstKind::FundedPreparation,18,0,7000,9000};
    const auto prep=state.grant(f,p);CHECK(prep.status==GrantStatus::FundedPreparation);
    CHECK(!state.matches(acquire.lease,f)&&state.before_primitive(prep.lease,f));
    CHECK(state.awaiting_actual()&&!state.actual_current());
    CHECK(state.grant(f,p).status==GrantStatus::AwaitActual);
    f=sample(f);CHECK(state.observe(f).accepted);p.source=f;
    CHECK(state.grant(f,p).status==GrantStatus::FundedPreparation);
    p.action.preparation_fuel=0;CHECK(state.grant(f,p).status==GrantStatus::Unfunded);
    p.action.preparation_fuel=100001;CHECK(state.grant(f,p).status==GrantStatus::NeedFuel);
    p.action.preparation_fuel=7000;p.funded=false;CHECK(state.grant(f,p).status==GrantStatus::Unfunded);
    p=program(f,Kind::Work);CHECK(state.grant(f,p).status==GrantStatus::MissingSource);
    f.selected_group=2;f=sample(f);CHECK(state.observe(f).accepted);p.source=f;
    CHECK(granted(state.grant(f,p)));
}
void investment_survives_preemption_and_time() {
    State state(4);auto f=frame();f.gold=100;f.next_investment=quote(f);
    CHECK(state.observe(f).accepted);auto obligation=state.investment_obligation(f);
    CHECK(obligation.active&&obligation.current&&obligation.affordable&&obligation.id==1&&obligation.target==7);
    for(unsigned i=0;i<20;++i) {
        auto lease=state.grant(f,program(f));CHECK(granted(lease));
        CHECK(state.before_primitive(lease.lease,f));
        CHECK(!state.investment_obligation(f).current&&state.investment_obligation(f).active);
        f=next(f);f.round+=10;f.next_investment.observed=false;
        CHECK(state.observe(f).accepted);obligation=state.investment_obligation(f);
        CHECK(obligation.active&&obligation.id==1&&obligation.target==7&&!obligation.affordability_known);
    }
    f=sample(f);f.next_investment=quote(f,9,10);CHECK(state.observe(f).accepted);
    obligation=state.investment_obligation(f);CHECK(obligation.active&&obligation.id==1&&obligation.target==7&&!obligation.affordability_known);
    auto buy=program(f,Kind::Investment,Buy);CHECK(state.grant(f,buy).status==GrantStatus::MissingInvestment);
    f=sample(f);f.gold=200;f.next_investment=quote(f,7,110);f.next_investment.revision=12;
    CHECK(state.observe(f).accepted);obligation=state.investment_obligation(f);
    CHECK(obligation.active&&obligation.id==1&&obligation.cost==110&&obligation.quote_revision==12&&obligation.affordable);
    buy=program(f,Kind::Investment,Buy);const auto grant=state.grant(f,buy);CHECK(granted(grant));
    CHECK(state.before_primitive(grant.lease,f));
    const auto old_inventory=f.inventory_revision;
    f=sample(f);f.gold=90;++f.inventory_revision;++f.kit_revision;f.next_investment.observed=false;
    ActualPurchase receipt{true,1,f.owner,f.round,7,f.input,f.sample,110,12,old_inventory,f.inventory_revision};
    const auto observed=state.observe(f,&receipt);CHECK(observed.accepted&&observed.purchase_accepted&&!observed.purchase_rejected);
    CHECK(observed.investment_end==InvestmentEnd::Purchased&&!state.investment_obligation(f).active);
    const auto resolution=state.last_investment_resolution();CHECK(resolution.reason==InvestmentEnd::Purchased&&resolution.obligation==1&&resolution.target==7&&resolution.purchase_event==1);
    f=sample(f);CHECK(state.observe(f,&receipt).purchase_rejected);
    CHECK(!state.investment_obligation(f).active&&state.last_investment_resolution().purchase_event==1);
}
void investment_refutations_and_false_receipts() {
    for(unsigned mode=0;mode<3;++mode) {
        State state(4);auto f=frame();f.gold=100;f.next_investment=quote(f);CHECK(state.observe(f).accepted);
        f=next(f);
        if(mode==0)f.gold=89;
        if(mode==1){f.next_investment.available=false;f.next_investment.cost=0;}
        if(mode==2){f.alive=false;f.hp=0;f.pose={-1,-1};f.gold=0;f.bag=0;f.next_investment.observed=false;}
        const auto observed=state.observe(f);CHECK(observed.accepted&&!state.investment_obligation(f).active);
        CHECK(observed.investment_end==(mode==0?InvestmentEnd::AffordabilityRefuted:mode==1?InvestmentEnd::TargetRefuted:InvestmentEnd::Death));
    }
    for(unsigned mode=0;mode<8;++mode) {
        State state(4);auto f=frame();f.gold=100;f.next_investment=quote(f);CHECK(state.observe(f).accepted);
        f=sample(f);f.next_investment.observed=false;f.inventory_revision=2;
        ActualPurchase receipt{true,1,f.owner,f.round,7,f.input,f.sample,90,11,1,2};
        if(mode==0)++receipt.owner;if(mode==1)++receipt.round;if(mode==2)++receipt.input;if(mode==3)++receipt.sample;
        if(mode==4)++receipt.target;if(mode==5)++receipt.paid;if(mode==6)++receipt.quote_revision;if(mode==7)++receipt.inventory_before;
        const auto observed=state.observe(f,&receipt);
        CHECK(observed.accepted&&observed.purchase_rejected&&!observed.purchase_accepted);
        CHECK(state.investment_obligation(f).active&&state.investment_obligation(f).id==1);
        CHECK(state.last_investment_resolution().reason==InvestmentEnd::None);
    }
    // Inventory change alone, or selecting a new candidate/issuing a new lease,
    // supplies no purchase authority and cannot throw away the old obligation.
    State state(4);auto f=frame();f.gold=100;f.next_investment=quote(f);state.observe(f);
    f=next(f);++f.inventory_revision;++f.kit_revision;f.next_investment.observed=false;state.observe(f);
    CHECK(state.investment_obligation(f).active&&!state.investment_obligation(f).affordability_known);
    CHECK(granted(state.grant(f,program(f)))&&state.investment_obligation(f).id==1);
}
void dead_shop_is_real_economic_work() {
    State state(4);auto f=frame();f.gold=100;f.next_investment=quote(f);state.observe(f);
    f=next(f);f.alive=false;f.hp=0;f.pose={-1,-1};f.permissions=Buy;state.observe(f);
    CHECK(state.investment_obligation(f).active&&state.investment_obligation(f).id==1&&state.investment_obligation(f).affordable);
    auto closed=program(f,Kind::Recovery,0);closed.action.kind=FirstKind::ActualClosedInput;
    CHECK(state.grant(f,closed).status==GrantStatus::Invalid);
    const auto buy=state.grant(f,program(f,Kind::Investment,Buy));CHECK(granted(buy));
    CHECK(state.before_primitive(buy.lease,f));
    f=sample(f);f.gold=10;++f.inventory_revision;f.next_investment.observed=false;
    ActualPurchase receipt{true,1,4,f.round,7,f.input,f.sample,90,11,1,2};
    CHECK(state.observe(f,&receipt).purchase_accepted&&!state.investment_obligation(f).active);
    // The same actual affordable dead quote stays ONE obligation; it does not
    // end/recreate a pulse on each owner input while waiting for actual Buy.
    State waiting(4);f=frame();f.alive=false;f.hp=0;f.pose={-1,-1};f.gold=100;f.permissions=Buy;f.next_investment=quote(f);
    CHECK(waiting.observe(f).accepted&&waiting.investment_obligation(f).id==1);
    for(unsigned i=0;i<5;++i){f=next(f);CHECK(waiting.observe(f).accepted&&waiting.investment_obligation(f).active&&waiting.investment_obligation(f).id==1);}
}
void revocation_and_actual_mac() {
    State state(4);auto f=frame();f.selected_group=2;f.bag=14;f.gold=100;f.next_investment=quote(f);state.observe(f);
    auto offered=state.grant(f,program(f,Kind::Work,Move|Attack));CHECK(granted(offered));
    // An optional Basic preempts at the SAME pose. Invalidate BEFORE dispatch;
    // the actual post-command A value, not modeled permissions, controls rebind.
    CHECK(state.before_primitive(offered.lease,f));CHECK(!state.matches(offered.lease,f));
    CHECK(state.actual().permissions==all_permissions&&!state.actual().fresh&&state.actual().bag==14);
    CHECK(state.observe(f).status==ObserveStatus::Stale);
    f=sample(f);f.permissions&=~Attack;CHECK(state.observe(f).accepted);
    CHECK(state.actual().permissions==(all_permissions&~Attack)&&state.investment_obligation(f).active);
    CHECK(state.grant(f,program(f,Kind::Work,Attack)).status==GrantStatus::PermissionChanged);
    offered=state.grant(f,program(f,Kind::Exit,Move));CHECK(granted(offered));
    CHECK(state.before_primitive(offered.lease,f));
    // A refused Move preserves M. The old lease is still spent; fresh actual
    // readback may explicitly issue another bounded interpreter word.
    f=sample(f);CHECK(state.observe(f).accepted&&state.actual().permissions==(all_permissions&~Attack));
    offered=state.grant(f,program(f,Kind::Exit,Move));CHECK(granted(offered)&&state.matches(offered.lease,f));
    CHECK(state.before_primitive(offered.lease,f));
    // No readback follows a suspended/throwing host call. A new input observes
    // actual capital/MAC but never receives invented work/score/purchase credit.
    CHECK(state.awaiting_actual()&&state.investment_obligation(f).active);
    f=next(f);f.permissions=Cast|Submit;f.pose={3,3};f.next_investment.observed=false;
    CHECK(state.observe(f).accepted&&state.actual().permissions==(Cast|Submit));
    CHECK(state.investment_obligation(f).active&&state.last_investment_resolution().reason==InvestmentEnd::None);
}
void reveals_locks_and_immutable_leases() {
    State state(4);auto f=frame();state.observe(f);auto old_program=program(f);
    const auto old=state.grant(f,old_program);CHECK(granted(old));
    f=sample(f);++f.foe_revision;CHECK(state.observe(f).accepted);
    CHECK(!state.matches(old.lease,f)&&state.grant(f,old_program).status==GrantStatus::ChangedDependencies);
    auto fresh=state.grant(f,program(f));CHECK(granted(fresh));
    f=sample(f);++f.lock_revision;state.observe(f);CHECK(!state.matches(fresh.lease,f));
    fresh=state.grant(f,program(f));CHECK(granted(fresh));
    auto altered=fresh.lease;++altered.serial;CHECK(!state.matches(altered,f));
    altered=fresh.lease;++altered.primitive_generation;CHECK(!state.matches(altered,f));
    altered=fresh.lease;altered.minimum_fuel=0;CHECK(!state.matches(altered,f));
    altered=fresh.lease;++altered.program.action.action_id;CHECK(!state.matches(altered,f));
    altered=fresh.lease;altered.program.source.available_fuel=1;CHECK(!state.matches(altered,f));
    altered=fresh.lease;++altered.program.risk.id;CHECK(!state.matches(altered,f));
    auto fuel=f;fuel.available_fuel=9000;CHECK(state.matches(fresh.lease,fuel));
    fuel.available_fuel=8999;CHECK(!state.matches(fresh.lease,fuel));
    fuel.available_fuel=f.available_fuel+1;CHECK(!state.matches(fresh.lease,fuel));
    fuel=f;fuel.available_fuel=50000;CHECK(state.observe(fuel).status==ObserveStatus::Duplicate&&state.matches(fresh.lease,fuel));
    CHECK(!state.matches(fresh.lease,f)&&state.grant(f,program(f)).status==GrantStatus::NeedObservation);
    auto inflated=sample(f);CHECK(state.observe(inflated).status==ObserveStatus::Stale);
    f=sample(fuel);CHECK(state.observe(f).accepted&&!state.matches(fresh.lease,f));
    fresh=state.grant(f,program(f));CHECK(granted(fresh));
    fuel=f;fuel.available_fuel=8999;CHECK(state.observe(fuel).status==ObserveStatus::Duplicate);
    CHECK(!state.matches(fresh.lease,f)&&!state.matches(fresh.lease,fuel));
    CHECK(state.grant(fuel,program(fuel)).status==GrantStatus::NeedFuel);
}
void sole_exit_blocked_then_vacated() {
    State state(4);auto f=frame();f.selected_group=3;state.observe(f);
    const FailureTarget exit{Kind::Exit,71,{3,3},0};
    auto occupied=failure(FailureReason::TransientOccupied,f,exit);CHECK(state.record_failure(occupied));
    CHECK(state.blocked(occupied.key));
    f=next(f);++f.foe_revision;state.observe(f);auto still=failure(FailureReason::TransientOccupied,f,exit);
    CHECK(state.blocked(still.key)&&state.invalidate_changed(still.key)==0);
    f=next(f);++f.kit_revision;++f.inventory_revision;++f.source_revision;state.observe(f);
    still=failure(FailureReason::TransientOccupied,f,exit);CHECK(state.blocked(still.key)&&state.invalidate_changed(still.key)==0);
    // The only exit becomes vacant. All kit/combat/source/route stats remain
    // identical: target-scoped occupancy change alone refutes the old failure.
    f=next(f);++f.occupancy_revision;state.observe(f);auto open=failure(FailureReason::TransientOccupied,f,exit);
    CHECK(!state.blocked(open.key)&&invalidated(occupied.key,open.key));
    CHECK(state.invalidate_changed(open.key)==1&&!state.blocked(open.key));
    CHECK(granted(state.grant(f,program(f,Kind::Exit,Move))));
    CHECK(!state.record_failure(occupied));
    // A new life of the same group cannot inherit an old cycle's refusal.
    CHECK(state.record_failure(open));f=next(f);++f.source_life;state.observe(f);
    const auto new_life=failure(FailureReason::TransientOccupied,f,exit);CHECK(!state.blocked(new_life.key));
    CHECK(state.invalidate_changed(new_life.key)==1);
}
void typed_failure_dependencies() {
    State state(4);auto f=frame();f.selected_group=2;f.available_fuel=900;state.observe(f);
    const FailureTarget move{Kind::Acquire,17,{3,3},1000};
    auto low=failure(FailureReason::NeedFuel,f,move);CHECK(state.record_failure(low));
    f=next(f);f.available_fuel=800;state.observe(f);auto current=failure(FailureReason::NeedFuel,f,move);
    CHECK(state.blocked(current.key));
    f=next(f);++f.kit_revision;++f.inventory_revision;++f.source_revision;state.observe(f);
    current=failure(FailureReason::NeedFuel,f,move);CHECK(state.blocked(current.key));
    f=next(f);f.available_fuel=1000;state.observe(f);current=failure(FailureReason::NeedFuel,f,move);
    CHECK(!state.blocked(current.key)&&state.invalidate_changed(current.key)==1&&!state.record_failure(current));
    auto unknown=failure(FailureReason::NeedObservation,f,move);CHECK(state.record_failure(unknown));
    f=sample(f);++f.observation_revision;state.observe(f);auto observed=failure(FailureReason::NeedObservation,f,move);
    CHECK(!state.blocked(observed.key)&&state.invalidate_changed(observed.key)==1);
    auto unsupported=failure(FailureReason::UnsupportedFamily,f,move);CHECK(state.record_failure(unsupported));
    f=next(f);f.pose={4,4};++f.route_revision;++f.occupancy_revision;++f.foe_revision;++f.kit_revision;state.observe(f);
    auto same_family=failure(FailureReason::UnsupportedFamily,f,{Kind::Work,99,{9,9},0});
    CHECK(state.blocked(same_family.key));
    f=next(f);++f.capability_revision;state.observe(f);auto supported=failure(FailureReason::UnsupportedFamily,f,move);
    CHECK(!state.blocked(supported.key)&&state.invalidate_changed(supported.key)==1);
    f=sample(f);f.capability_revision=0;state.observe(f);
    CHECK(!failure(FailureReason::UnsupportedFamily,f,move).key.valid);
    // Bounded storage is a cache, never a growing blacklist or a game budget.
    for(unsigned i=0;i<failure_capacity+3;++i)CHECK(state.record_failure(failure(FailureReason::PoseChanged,f,{Kind::Exit,static_cast<int>(i),{5,5},0})));
}
void admission_rejects_fake_closure_and_context() {
    State state(4);auto f=frame();auto p=program(f);
    CHECK(state.grant(f,p).status==GrantStatus::NeedObservation);CHECK(state.observe(f).accepted);
    auto wrong=f;++wrong.owner;CHECK(state.grant(wrong,program(wrong)).status==GrantStatus::WrongOwner);
    p.action.kind=FirstKind::ActualClosedInput;p.action.required_permissions=0;
    CHECK(state.grant(f,p).status==GrantStatus::Invalid);
    f=sample(f);f.stunned=true;f.permissions=0;state.observe(f);p.source=f;
    CHECK(state.grant(f,p).status==GrantStatus::ActualClosedInput);
    CHECK(state.grant(f,program(f)).status==GrantStatus::PermissionChanged);
    p.complete_interpreter=false;CHECK(state.grant(f,p).status==GrantStatus::MissingInterpreter);
    f=sample(f);f.stunned=false;f.permissions=all_permissions;state.observe(f);p=program(f);p.risk.services=Knowledge::Modeled;
    CHECK(state.grant(f,p).status==GrantStatus::Invalid);
    p.risk.service_model_revision=1;CHECK(granted(state.grant(f,p)));
    p.risk.horizon_round=f.round-1;CHECK(state.grant(f,p).status==GrantStatus::Invalid);
    p=program(f);p.action.action_id=0;CHECK(state.grant(f,p).status==GrantStatus::Invalid);
    p=program(f);p.action.mandatory_fuel=0;CHECK(state.grant(f,p).status==GrantStatus::Unfunded);
    p=program(f);p.action.required_permissions=128;CHECK(state.grant(f,p).status==GrantStatus::Invalid);
    auto conflict=f;++conflict.bag;CHECK(state.observe(conflict).status==ObserveStatus::ConflictingObservation);
    auto stale=f;--stale.input;CHECK(state.observe(stale).status==ObserveStatus::NeedObservation);
    auto invalid=f;invalid.hp=std::numeric_limits<double>::quiet_NaN();CHECK(state.observe(invalid).status==ObserveStatus::NeedObservation);
}
}
int main() {
    postbank_acquire_and_real_preparation();investment_survives_preemption_and_time();investment_refutations_and_false_receipts();dead_shop_is_real_economic_work();
    revocation_and_actual_mac();reveals_locks_and_immutable_leases();sole_exit_blocked_then_vacated();typed_failure_dependencies();admission_rejects_fake_closure_and_context();
    std::printf("capital_service_program: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
