#include "service_ready_field.h"
#include <cstdio>

namespace s=wb::service;
namespace r=wb::ready_field;
namespace {
int checks{},failures{};
void check(bool ok,const char* why) {++checks;if(!ok){++failures;std::printf("FAIL %s\n",why);}}
s::Authority auth(int round) {return {1,round,static_cast<unsigned>(round),1};}
s::Contract contract() {
    s::Contract c;c.owner=1;c.group=2;c.start_from_receipt=true;
    c.carried_receipt={true,true,2,28,5};return c;
}
s::Frame frame(int round) {
    s::Frame f;f.authority=auth(round);f.fresh=f.alive=true;f.hp=f.max_hp=1200;
    f.domain_harmless=f.static_hazard_excluded=true;f.position={8,7};
    f.can_move=f.can_attack=true;f.bag=28;f.gold=100;f.receipt=contract().carried_receipt;
    f.route.role=s::RouteRole::Bank;f.route.goal_reached=f.submit_ready=true;
    return f;
}
r::Facts facts(const s::Frame& f) {
    r::Facts o;o.authority=f.authority;
    o.self_known=o.life_known=o.health_known=o.position_known=true;
    o.bag_known=o.gold_known=o.channel_known=o.stun_known=true;
    o.permissions_known={true,true,false};o.restrictions_known_mask=3;
    o.input_unused_known=o.input_unused=true;
    o.episode_continuity_known=o.same_episode=true;
    o.inventory={};o.inventory.authority=f.authority;o.inventory.known=o.inventory.complete=true;
    o.field={f.authority,true,f.static_hazard_excluded,f.position};return o;
}
r::NominationEntry entry(const s::Frame& f,bool candidate=true) {
    r::NominationEntry n;n.authority=f.authority;n.position=f.position;
    n.world_known=n.world_ready=n.position_known=n.static_quiet_known=true;
    n.static_npc_quiet=f.static_hazard_excluded;n.candidate_known=true;n.candidate_present=candidate;
    n.bag_known=n.input_unused_known=n.input_unused=true;n.bag=f.bag;return n;
}
void composition(bool candidate) {
    s::Interpreter old;check(old.begin(contract()),"old explicit HoldCash service starts");
    auto f=frame(120);const auto submit=old.step(f);
    check(submit.op==s::Op::Submit,"actual core submits loaded capture");
    check(old.complete(f.authority,{s::Acceptance::Accepted})==s::Status::NeedObservation,"accepted is not bank");
    f=frame(121);f.bag=0;f.own_bank_receipt={true,true,28,7};
    f.route.role=s::RouteRole::Spawn;f.route.goal_reached=false;
    f.route.next_step_available=f.route.next_step_legal=f.route.quiet=true;f.route.next={7,7};
    r::Endpoint endpoint;r::Episode e;e.id=11;e.sealed=contract();e.capture=e.sealed.carried_receipt;
    e.bank_serial_baseline=6;e.opened_at=f.authority;e.terminal=r::Terminal::ReadyFieldHoldCash;
    e.deadline_round=153;e.max_observation_calls=32;
    check(endpoint.seal(f.authority,e),"one bank episode seals");
    r::OwnBankEvidence b{f.authority,1,2,11,5,r::ProofSource::ActorJoinedOwnCompletion,f.own_bank_receipt};
    auto observed=endpoint.observe_readiness(f.authority,f,facts(f),r::bare_move_basic(),&b);
    check(observed.status==r::Status::BankVerified,"bank input cannot release");
    const auto walk=old.step(f);
    check(walk.op==s::Op::Move && old.phase()==s::Phase::ReturnTravel,"bank input retains legacy return primitive");
    check(old.complete(f.authority,{s::Acceptance::Accepted})==s::Status::NeedObservation,"return acceptance is retained");
    f.authority=auth(122);f.position=walk.position;f.route.next={6,7};
    observed=endpoint.observe_readiness(f.authority,f,facts(f));
    check(observed.status==r::Status::ObservedReadyForBasic,"later ready body recognized without C or CDs");
    const auto before=old.phase();
    if(r::nomination_entry_allowed(observed,f.authority,entry(f,candidate))) {
        check(candidate,"release requires an actual current nomination candidate");
        check(endpoint.release(f.authority,observed.ticket).status==r::Status::Released,"separate observed terminal releases");
        check(old.phase()==before && old.phase()!=s::Phase::Complete,"release does not forge legacy Complete");
        old=s::Interpreter{};auto next=contract();next.group=3;next.carried_receipt.group=3;
        check(old.begin(next),"caller may retire old word only after distinct endpoint");
    } else {
        check(!candidate,"unavailable nomination keeps original return");
        const auto declined=endpoint.continue_existing(f.authority,observed.ticket);
        check(declined.reason==r::Reason::CallerContinues && old.phase()==before,"decline leaves actual core phase unchanged");
        check(declined.deadline_round==observed.deadline_round &&
            declined.observation_calls_left==observed.observation_calls_left,"decline cannot reset monitor budget");
        const auto continued=old.step(f);
        check(continued.op==s::Op::Move && continued.position==f.route.next,"existing pending move effect resolves then original route advances");
        check(endpoint.observe_readiness(f.authority,f,facts(f)).reason==r::Reason::InputTicketConsumed,
            "same input cannot reset endpoint through fresh polling");
    }
}
}
int main() {
    composition(true);composition(false);
    std::printf("service_ready_transition: %d checks, %d failures; pure core/endpoint composition only\n",checks,failures);
    return failures?1:0;
}
