#include "../src/service.h"
#include <cstdio>

using namespace wb::service;
namespace {
int checks=0, failures=0;
#define CHECK(x) do {++checks; if(!(x)){++failures;std::printf("FAIL line %d: %s\n",__LINE__,#x);}} while(false)
Contract contract(std::size_t n=2) {
    Contract c;c.owner=2;c.group=5;c.member_count=n;
    for(std::size_t i=0;i<n;++i)c.member_ids[i]=10+int(i);
    return c;
}
Frame frame(std::size_t n=2) {
    Frame f;f.authority={2,1,1,1};f.fresh=f.alive=true;
    f.domain_harmless=f.static_hazard_excluded=true;f.hp=f.max_hp=1000;
    f.attack_range=1;f.can_move=f.can_attack=f.can_cast=true;
    f.group_fresh=true;f.group_state=GroupState::Alive;
    f.route.quiet=true;f.route.next_step_available=f.route.next_step_legal=true;
    f.route.next={1,0};f.renewal_ready=true;
    for(std::size_t i=0;i<n;++i)f.members[i]={10+int(i),true,true,true,true,{1+int(i)*2,0},120};
    return f;
}
void next(Frame& f) {++f.authority.round;++f.authority.input_generation;f.authority.view_revision=1;}
void post(Frame& f) {++f.authority.view_revision;}
void accepted(Interpreter& i, const Step& s) {
    CHECK(s.status==Status::Issued);
    CHECK(i.complete(s.authority,{Acceptance::Accepted})==Status::NeedObservation);
}
void move_effect(Interpreter& i, Frame& f, const Step& s) {
    CHECK(s.op==Op::Move);accepted(i,s);f.position=s.position;post(f);
    CHECK(i.step(f).reason==Reason::ConsumedInput);
}
void harmless_domain_and_authority() {
    Interpreter i;auto c=contract();c.member_ids[1]=c.member_ids[0];CHECK(!i.begin(c));
    c=contract();CHECK(i.begin(c));auto f=frame();f.domain_harmless=false;
    CHECK(i.step(f).status==Status::Unsupported);
    CHECK(i.phase()==Phase::Aborted);
    CHECK(i.begin(c));f=frame();f.authority.owner=3;CHECK(i.step(f).reason==Reason::WrongOwner);
    CHECK(i.begin(c));f=frame();f.alive=false;CHECK(i.step(f).reason==Reason::ActorDead);
    CHECK(i.begin(c));f=frame();f.members[0].harmless=false;CHECK(i.step(f).status==Status::Unsupported);
    CHECK(i.begin(c));f=frame();f.members[0].id=999;CHECK(i.step(f).reason==Reason::WrongMember);
}
void hidden_ingress_and_refusal() {
    Interpreter i;CHECK(i.begin(contract()));auto f=frame();
    f.group_fresh=false;f.group_state=GroupState::Unknown;
    for(auto& m:f.members){m.known=m.visible=false;}
    auto s=i.step(f);CHECK(s.op==Op::Move);CHECK(s.member_index==0);
    CHECK(i.step(f).reason==Reason::AwaitResult);
    CHECK(i.complete(s.authority,{Acceptance::Refused})==Status::Refused);
    post(f);CHECK(i.step(f).reason==Reason::ConsumedInput);
    next(f);s=i.step(f);CHECK(s.op==Op::Move);
    CHECK(i.complete(s.authority,{Acceptance::Refused})==Status::Aborted);
    CHECK(i.step(f).reason==Reason::RefusalLimit);
    CHECK(i.begin(contract()));f=frame();f.members[1].known=f.members[1].visible=false;
    CHECK(i.step(f).op==Op::Basic); // Hidden siblings do not forbid current harmless work.
}
void repeat_ready_work_and_one_input() {
    Interpreter i;CHECK(i.begin(contract(1)));auto f=frame(1);f.members[0].hp=1000;
    for(int k=0;k<24;++k) {
        const auto s=i.step(f);CHECK(s.op==Op::Basic);accepted(i,s);
        f.members[0].hp-=10;post(f);CHECK(i.step(f).reason==Reason::ConsumedInput);
        post(f);CHECK(i.step(f).reason==Reason::ConsumedInput);next(f);
    }
    CHECK(f.members[0].hp==760);
    CHECK(i.phase()==Phase::Work);
}
void death_to_next_member_approach() {
    Interpreter i;CHECK(i.begin(contract()));auto f=frame();
    auto s=i.step(f);accepted(i,s);f.members[0].alive=false;post(f);
    CHECK(i.step(f).reason==Reason::ConsumedInput);next(f);
    f.route.member_index=1;f.route.next={1,0};s=i.step(f);
    CHECK(s.op==Op::Move);CHECK(s.member_index==1);move_effect(i,f,s);next(f);
    f.route.next={2,0};s=i.step(f);move_effect(i,f,s);next(f);
    s=i.step(f);CHECK(s.op==Op::Basic);CHECK(s.member_index==1);
    CHECK(i.phase()==Phase::Work);CHECK(s.position==f.members[1].position);
}
void accepted_is_not_effect() {
    Interpreter i;CHECK(i.begin(contract(1)));auto f=frame(1);auto s=i.step(f);accepted(i,s);
    CHECK(i.step(f).reason==Reason::AwaitEffect);
    for(int k=0;k<2;++k){post(f);CHECK(i.step(f).status==Status::NeedObservation);}
    post(f);CHECK(i.step(f).status==Status::Aborted);CHECK(i.phase()==Phase::Aborted);
    i=Interpreter{};
    CHECK(i.begin(contract(1)));f=frame(1);s=i.step(f);
    auto wrong=s.authority;++wrong.view_revision;
    CHECK(i.complete(wrong,{Acceptance::Accepted})==Status::NoCommand);
    next(f);CHECK(i.step(f).reason==Reason::MissingResult);
}
void death_and_epoch_invalidation() {
    Interpreter i;CHECK(i.begin(contract(1)));auto f=frame(1);auto s=i.step(f);
    f.alive=false;post(f);CHECK(i.step(f).reason==Reason::ActorDead);
    CHECK(i.complete(s.authority,{Acceptance::Accepted})==Status::NoCommand);
    i=Interpreter{};
    CHECK(i.begin(contract(1)));f=frame(1);s=i.step(f);
    CHECK(i.complete(s.authority,{Acceptance::Refused})==Status::Refused);
    post(f);CHECK(i.step(f).reason==Reason::ConsumedInput);
    ++f.authority.input_generation;CHECK(i.step(f).reason==Reason::StaleFrame);
}
void clear_fact_and_capture_waits() {
    auto c=contract(2);c.capture_wait_bound=2;Interpreter i;CHECK(i.begin(c));auto f=frame();
    for(auto& m:f.members)m.known=false;
    f.group_state=GroupState::RewardAvailable;f.capture_occupied=true;
    CHECK(i.step(f).op==Op::Hold);CHECK(i.phase()==Phase::CaptureHold);
    next(f);CHECK(i.step(f).op==Op::Hold);next(f);
    CHECK(i.step(f).reason==Reason::CaptureTimeout);
    CHECK(i.begin(c));f=frame();for(auto& m:f.members)m.alive=false;
    f.group_state=GroupState::RewardAvailable;f.capture_occupied=true;
    CHECK(i.step(f).op==Op::Hold);next(f);f.capture_contested=true;
    CHECK(i.step(f).reason==Reason::CaptureContested);
    CHECK(i.begin(c));f=frame();for(auto& m:f.members)m.alive=false;
    f.group_state=GroupState::RewardAvailable;f.capture_occupied=true;
    CHECK(i.step(f).op==Op::Hold);next(f);f.capture_occupied=false;
    CHECK(i.step(f).status==Status::Aborted);
}
void receipt_is_not_bank() {
    Interpreter i;CHECK(i.begin(contract(1)));auto f=frame(1);f.members[0].alive=false;
    f.group_state=GroupState::RewardAvailable;f.capture_occupied=true;
    CHECK(i.step(f).op==Op::Hold);next(f);
    f.receipt={true,true,5,7,1};f.bag=7;f.group_state=GroupState::Captured;
    f.route.role=RouteRole::Bank;f.route.goal_reached=true;f.submit_ready=true;
    auto s=i.step(f);CHECK(s.op==Op::Submit);accepted(i,s);
    f.channel=Channel::Tribute;f.channel_remaining=1;post(f);
    CHECK(i.step(f).reason==Reason::ConsumedInput);next(f);
    f.channel=Channel::None;f.bag=0;f.submitted_counter=1000;
    CHECK(i.step(f).reason==Reason::BankProofMissing); // Unverified team counter is not own conversion.
    i=Interpreter{};
    CHECK(i.begin(contract(1)));f=frame(1);f.members[0].alive=false;
    f.group_state=GroupState::RewardAvailable;f.capture_occupied=true;
    CHECK(i.step(f).op==Op::Hold);next(f);f.receipt={true,false,5,7,1};f.bag=7;
    CHECK(i.step(f).reason==Reason::ReceiptInvalid);
    i=Interpreter{};
    CHECK(i.begin(contract(1)));f=frame(1);f.members[0].alive=false;
    f.group_state=GroupState::RewardAvailable;f.capture_occupied=true;
    CHECK(i.step(f).op==Op::Hold);next(f);f.receipt={true,true,5,0,1};
    CHECK(i.step(f).reason==Reason::ReceiptInvalid); // Positive-gem service cannot count a zero share.
}
void full_service_and_renewed_work() {
    auto c=contract(1);c.require_shop=true;Interpreter i;CHECK(i.begin(c));auto f=frame(1);
    auto s=i.step(f);CHECK(s.op==Op::Basic);accepted(i,s);
    f.members[0].alive=false;f.group_state=GroupState::RewardAvailable;post(f);
    CHECK(i.step(f).reason==Reason::ConsumedInput);next(f);
    f.route.role=RouteRole::Capture;f.route.next={1,0};s=i.step(f);move_effect(i,f,s);
    f.capture_occupied=true;next(f);CHECK(i.step(f).op==Op::Hold);next(f);
    f.receipt={true,true,5,7,1};f.bag=7;f.gold=30;f.group_state=GroupState::Captured;
    f.route.role=RouteRole::Bank;f.route.next={1,1};s=i.step(f);move_effect(i,f,s);next(f);
    f.route.goal_reached=true;f.submit_ready=true;s=i.step(f);CHECK(s.op==Op::Submit);accepted(i,s);
    f.channel=Channel::Tribute;f.channel_remaining=1;post(f);
    CHECK(i.step(f).reason==Reason::ConsumedInput);next(f);
    f.channel_remaining=0;CHECK(i.step(f).reason==Reason::BankWait);next(f);
    f.channel=Channel::None;f.bag=0;f.own_bank_receipt={true,true,7,1};
    f.route.role=RouteRole::Spawn;f.route.goal_reached=false;f.route.next={0,1};
    s=i.step(f);move_effect(i,f,s);f.at_spawn=true;f.hp=500;next(f);
    f.shop_ready=true;s=i.step(f);CHECK(s.op==Op::Shop);accepted(i,s);
    f.shop_complete=true;f.gold=0;post(f);CHECK(i.step(f).reason==Reason::ConsumedInput);next(f);
    CHECK(i.step(f).op==Op::Heal);CHECK(i.phase()==Phase::Heal);next(f);f.hp=1000;
    s=i.step(f);CHECK(s.status==Status::Complete);CHECK(s.op==Op::Renew);CHECK(i.phase()==Phase::Complete);
    // A renewed endpoint is followed by a genuinely new service, not a repeated label.
    CHECK(i.begin(c));next(f);f.members[0].alive=true;f.members[0].hp=120;
    f.members[0].position={1,1};f.group_state=GroupState::Alive;
    s=i.step(f);CHECK(s.op==Op::Basic);CHECK(s.status==Status::Issued);
}
void stale_receipts_and_deferred_routes() {
    Interpreter i;CHECK(i.begin(contract(1)));auto f=frame(1);f.members[0].alive=false;
    f.receipt={true,true,5,7,44};f.own_bank_receipt={true,true,7,55};
    f.group_state=GroupState::RewardAvailable;f.capture_occupied=true;
    CHECK(i.step(f).op==Op::Hold);next(f);CHECK(i.step(f).op==Op::Hold);
    CHECK(i.phase()==Phase::CaptureHold); // Historical receipt does not count again.
    CHECK(!i.begin(contract(1))); // A replacement cannot reset a live wait budget.
    i=Interpreter{}; // The following is a separate fixture, not a continuation.
    CHECK(i.begin(contract(1)));f=frame(1);f.members[0].known=f.members[0].visible=false;
    f.route.deferred=true;CHECK(i.step(f).status==Status::ComputeDeferred);
    next(f);CHECK(i.step(f).status==Status::ComputeDeferred);next(f);
    CHECK(i.step(f).status==Status::Aborted);
}
void long_channel_and_contradictory_facts() {
    Interpreter i;CHECK(i.begin(contract(30)));auto f=frame(30);
    for(auto& m:f.members)m.alive=false;
    f.group_state=GroupState::RewardAvailable;f.capture_occupied=true;
    CHECK(i.step(f).op==Op::Hold);next(f);f.receipt={true,true,5,210,1};f.bag=210;
    f.group_state=GroupState::Captured;f.route.role=RouteRole::Bank;
    f.route.goal_reached=f.submit_ready=true;auto s=i.step(f);CHECK(s.op==Op::Submit);accepted(i,s);
    f.channel=Channel::Tribute;f.channel_remaining=20;post(f);
    CHECK(i.step(f).reason==Reason::ConsumedInput);
    for(int timer=20;timer>=0;--timer) {
        next(f);f.channel_remaining=timer;CHECK(i.step(f).reason==Reason::BankWait);
        CHECK(i.phase()==Phase::BankChannel);
    }
    next(f);f.channel=Channel::None;f.bag=0;f.own_bank_receipt={true,true,210,1};f.at_spawn=true;
    CHECK(i.step(f).reason==Reason::ReadinessWait);next(f);
    CHECK(i.step(f).status==Status::Complete);
    CHECK(i.begin(contract(1)));f=frame(1);f.group_state=GroupState::RewardAvailable;
    CHECK(i.step(f).reason==Reason::GroupUnknown); // Native clear and fresh live HP disagree.
    i=Interpreter{};
    CHECK(i.begin(contract(1)));f=frame(1);f.hp=2000;
    CHECK(i.step(f).reason==Reason::StaleFrame);
    i=Interpreter{};
    CHECK(i.begin(contract(1)));f=frame(1);f.members[0].known=false;
    f.route.next={-1,0};CHECK(i.step(f).reason==Reason::RouteInvalidated);
    CHECK(i.begin(contract(1)));f=frame(1);f.members[0].alive=false;
    f.group_state=GroupState::RewardAvailable;f.route.role=RouteRole::Capture;f.route.goal_reached=true;
    CHECK(i.step(f).reason==Reason::RouteInvalidated);
}
void recall_and_actual_renewal() {
    auto c=contract(1);c.return_by_recall=true;Interpreter i;CHECK(i.begin(c));auto f=frame(1);
    f.members[0].alive=false;f.group_state=GroupState::RewardAvailable;f.capture_occupied=true;
    CHECK(i.step(f).op==Op::Hold);next(f);f.receipt={true,true,5,7,1};f.bag=7;
    f.route.role=RouteRole::Bank;f.route.goal_reached=f.submit_ready=true;
    auto s=i.step(f);accepted(i,s);f.channel=Channel::Tribute;post(f);
    CHECK(i.step(f).reason==Reason::ConsumedInput);next(f);
    f.bag=0;f.channel=Channel::None;f.own_bank_receipt={true,true,7,1};
    s=i.step(f);CHECK(s.op==Op::Recall);accepted(i,s);
    f.channel=Channel::Recall;f.channel_remaining=4;post(f);
    CHECK(i.step(f).reason==Reason::ConsumedInput);
    for(int timer=3;timer>=0;--timer){next(f);f.channel_remaining=timer;CHECK(i.step(f).op==Op::Hold);}
    next(f);f.channel=Channel::None;f.at_spawn=true;f.can_attack=false;
    CHECK(i.step(f).reason==Reason::ReadinessWait);next(f);
    CHECK(i.step(f).reason==Reason::ReadinessWait);f.can_attack=true;post(f);
    CHECK(i.step(f).status==Status::Complete);
    CHECK(i.begin(c));f=frame(1);f.channel=Channel::Other;
    CHECK(i.step(f).reason==Reason::ChannelInterrupted);
}
void replacement_retains_consumed_authority() {
    auto c=contract(1);c.max_refusals=1;Interpreter i;CHECK(i.begin(c));auto f=frame(1);
    auto s=i.step(f);CHECK(!i.begin(c));
    CHECK(i.complete(s.authority,{Acceptance::Refused})==Status::Aborted);
    CHECK(i.begin(c));post(f);CHECK(i.step(f).reason==Reason::ConsumedInput);
    next(f);CHECK(i.step(f).op==Op::Basic);
}
void loaded_conversion_suffix() {
    auto c=contract(0);c.start_from_receipt=true;c.carried_receipt={true,true,5,7,22};
    Interpreter i;auto bad=c;bad.carried_receipt.verified=false;CHECK(!i.begin(bad));
    bad=c;bad.carried_receipt.group=6;CHECK(!i.begin(bad));
    bad=c;bad.carried_receipt.serial=0;CHECK(!i.begin(bad));
    bad=c;bad.carried_receipt.gems=0;CHECK(!i.begin(bad));
    CHECK(i.begin(c));auto f=frame(0);f.bag=7;f.receipt=c.carried_receipt;
    f.route.role=RouteRole::Bank;f.route.goal_reached=f.submit_ready=true;
    auto s=i.step(f);CHECK(s.op==Op::Submit);CHECK(i.phase()==Phase::BankTravel);accepted(i,s);
    f.channel=Channel::Tribute;post(f);CHECK(i.step(f).reason==Reason::ConsumedInput);next(f);
    f.channel=Channel::None;f.bag=0;f.own_bank_receipt={true,true,7,6};
    f.route.role=RouteRole::Spawn;f.route.goal_reached=false;f.route.next={1,0};
    s=i.step(f);move_effect(i,f,s);f.at_spawn=true;next(f);
    CHECK(i.step(f).reason==Reason::ReadinessWait);next(f);
    CHECK(i.step(f).status==Status::Complete);
    CHECK(!i.begin(c)); // The already converted receipt cannot be loaded again.
    Interpreter stale;CHECK(stale.begin(c));f=frame(0);f.bag=7;f.receipt=c.carried_receipt;
    ++f.receipt.serial;CHECK(stale.step(f).reason==Reason::ReceiptInvalid);
    Interpreter no_bag;CHECK(no_bag.begin(c));f=frame(0);f.receipt=c.carried_receipt;f.bag=6;
    CHECK(no_bag.step(f).reason==Reason::ReceiptInvalid);
    // Route failure with retained bag can resume exactly the proved conversion suffix.
    Interpreter broken;CHECK(broken.begin(c));f=frame(0);f.bag=7;f.receipt=c.carried_receipt;
    f.route.role=RouteRole::Bank;f.route.invalidated=true;
    CHECK(broken.step(f).reason==Reason::RouteInvalidated);CHECK(f.bag==7);
    CHECK(broken.begin(c));next(f);f.route.invalidated=false;f.route.goal_reached=f.submit_ready=true;
    CHECK(broken.step(f).op==Op::Submit);
}
}
int main() {
    harmless_domain_and_authority();hidden_ingress_and_refusal();repeat_ready_work_and_one_input();
    death_to_next_member_approach();accepted_is_not_effect();death_and_epoch_invalidation();
    clear_fact_and_capture_waits();receipt_is_not_bank();full_service_and_renewed_work();
    stale_receipts_and_deferred_routes();
    long_channel_and_contradictory_facts();
    recall_and_actual_renewal();
    replacement_retains_consumed_authority();
    loaded_conversion_suffix();
    std::printf("service fixtures: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
