#include "../src/capital_bank.h"
#include <cstdio>
#include <limits>
using namespace wb::capital::bank;
namespace {
unsigned checks=0,failures=0;
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL %u: %s\n",__LINE__,#x);}}while(false)
Observation body(int round=40) {
    Observation o;o.owner=1;o.round=round;o.position={7,8};o.hp=500;o.bag=7;o.team_score=100;
    o.channel=Channel::None;o.observed=o.fresh=o.alive=o.submit_allowed=true;return o;
}
Context world(const Observation& o) {
    Context c;c.owner=o.owner;c.round=o.round;c.altar={8,8};c.queried_ticks=1;
    c.fresh=c.altar_known=c.adjacent=c.altar_unlocked=true;
    c.foes_covered=c.npc_covered=c.active_effects_covered=true;
    c.survives_horizon=c.no_stun_horizon=true;return c;
}
void start(Monitor& m,Observation& o,Context& c) {
    CHECK(m.observe(o,c).outcome==Outcome::NoChange);
    auto p=m.plan(o,c);CHECK(p.status==Status::Admitted);
    o.channel=Channel::Submit;o.submit_allowed=false;
    auto r=m.accepted_start(p,true,o,c);
    CHECK(r.outcome==Outcome::AwaitOwnerStart&&r.credited_prior_bag==0);
    CHECK(m.state()==State::Active&&m.attempts()==1);
}
void admission() {
    auto o=body();auto c=world(o);CHECK(admit(o,c).status==Status::Admitted);
    c.foes_covered=false;CHECK(admit(o,c).status==Status::NeedModel);c.foes_covered=true;
    c.foe_opportunities_upper=1;CHECK(admit(o,c).status==Status::NeedModel);
    c.full_reply_horizon=true;CHECK(admit(o,c).status==Status::Admitted);
    c.no_stun_horizon=false;CHECK(admit(o,c).status==Status::Unsafe);c.no_stun_horizon=true;
    c.survives_horizon=false;CHECK(admit(o,c).status==Status::Unsafe);c.survives_horizon=true;
    c.npc_covered=false;CHECK(admit(o,c).status==Status::NeedModel);c.npc_covered=true;
    c.active_effects_covered=false;CHECK(admit(o,c).status==Status::NeedModel);c.active_effects_covered=true;
    c.queried_ticks=2;CHECK(admit(o,c).status==Status::Ineligible);c.queried_ticks=1;
    o.bag=30;CHECK(admit(o,c).status==Status::Admitted); // queried duration, no hardcoded bag threshold
    c.round++;CHECK(admit(o,c).status==Status::NeedModel);c.round--;
    o.stunned=true;CHECK(admit(o,c).status==Status::Ineligible);o.stunned=false;
    o.restrictions=1;CHECK(admit(o,c).status==Status::Ineligible);o.restrictions=0;
    c.altar_unlocked=false;CHECK(admit(o,c).status==Status::Ineligible);c.altar_unlocked=true;
    o.hp=std::numeric_limits<float>::quiet_NaN();CHECK(admit(o,c).status==Status::NeedModel);
}
void completion_and_capture() {
    Monitor m;auto o=body();auto c=world(o);start(m,o,c);
    CHECK(m.observe(o,c).outcome==Outcome::AwaitOwnerStart); // owner-end has not resolved it
    o.hp=400;o.bag=12; // damage does not cancel; capture additions do not renew
    CHECK(m.observe(o,c).outcome==Outcome::AwaitOwnerStart);
    ++o.round;++c.round;o.channel=Channel::None;o.bag=0;o.team_score=117;
    auto r=m.observe(o,c);CHECK(r.outcome==Outcome::Banked);
    CHECK(r.credited_prior_bag==7&&r.unattributed_score_delta==10);
    CHECK(m.observe(o,c).credited_prior_bag==0); // exactly-once local attribution
    CHECK(m.plan(o,c).status==Status::RetryBlocked);
    ++o.round;++c.round;o.bag=3;o.submit_allowed=true;
    CHECK(m.observe(o,c).outcome==Outcome::NewReceipt);
    CHECK(m.attempts()==0&&m.plan(o,c).status==Status::Admitted);
    // The actual phase order is capture, then owner-start channel resolution.
    // The bot's next input sees only the final zero bag and combined score.
    Monitor captured;o=body();c=world(o);start(captured,o,c);
    ++o.round;++c.round;o.channel=Channel::None;o.bag=0;o.team_score=112;
    r=captured.observe(o,c);CHECK(r.outcome==Outcome::Banked);
    CHECK(r.credited_prior_bag==7&&r.unattributed_score_delta==5);
}
void failed_start_and_bounded_retry() {
    Monitor m;auto o=body();auto c=world(o);m.observe(o,c);auto p=m.plan(o,c);
    auto r=m.accepted_start(p,false,o,c);CHECK(r.outcome==Outcome::StartRejected&&r.credited_prior_bag==0);
    CHECK(r.bag_preserved&&m.state()==State::Blocked);
    CHECK(m.plan(o,c).status==Status::RetryBlocked);
    ++o.round;++c.round;m.observe(o,c);CHECK(m.plan(o,c).status==Status::RetryBlocked);
    CHECK(!m.changed_context(o,c,Change::ActualStep));
    ++o.position.x;m.observe(o,c);CHECK(m.changed_context(o,c,Change::ActualStep));
    p=m.plan(o,c);CHECK(p.status==Status::Admitted);
    CHECK(m.accepted_start(p,false,o,c).outcome==Outcome::StartRejected);
    ++o.position.y;m.observe(o,c);CHECK(!m.changed_context(o,c,Change::ActualStep));
    CHECK(m.attempts()==2&&m.plan(o,c).status==Status::RetryBlocked);
    CHECK(m.accepted_start(p,true,o,c).outcome==Outcome::RetryBlocked);
    // A zero bag from BEFORE this receipt must never reset a failed attempt.
    Monitor history;o=body(20);o.bag=0;c=world(o);history.observe(o,c);
    ++o.round;++c.round;o.bag=7;history.observe(o,c);p=history.plan(o,c);
    CHECK(p.status==Status::Admitted);
    CHECK(history.accepted_start(p,false,o,c).outcome==Outcome::StartRejected);
    ++o.round;++c.round;CHECK(history.observe(o,c).outcome==Outcome::NoChange);
    CHECK(history.attempts()==1&&history.plan(o,c).status==Status::RetryBlocked);
}
void interruptions() {
    for(int kind=0;kind<6;++kind) {
        Monitor m;auto o=body();auto c=world(o);start(m,o,c);
        ++o.round;++c.round;o.channel=Channel::None;
        Outcome expected=Outcome::Interrupted;
        if(kind<=2){o.bag=0;o.team_score=107;} // score alone cannot overrule interruption
        if(kind==0){o.alive=false;o.hp=0;o.position={-1,-1};expected=Outcome::Dead;}
        if(kind==1){o.stunned=true;expected=Outcome::Stunned;}
        if(kind==2){c.altar_unlocked=false;expected=Outcome::Locked;}
        if(kind==3){o.bag=0;o.team_score=106;expected=Outcome::Unresolved;}
        if(kind==4){++o.position.x;}
        if(kind==5){o.channel=Channel::Submit;expected=Outcome::Unresolved;}
        const auto r=m.observe(o,c);CHECK(r.outcome==expected&&r.credited_prior_bag==0);
        CHECK(m.state()==State::Blocked);
    }
}
void expiry_and_authority() {
    Monitor m;auto o=body();auto c=world(o);m.observe(o,c);const auto p=m.plan(o,c);
    ++o.round;++c.round;m.observe(o,c);o.channel=Channel::Submit;
    CHECK(m.accepted_start(p,true,o,c).outcome==Outcome::RetryBlocked);
    Monitor n;o=body();c=world(o);start(n,o,c);
    auto foreign=o;foreign.owner++;CHECK(n.observe(foreign,c).outcome==Outcome::NeedObservation);
    auto fc=c;fc.owner=foreign.owner;CHECK(n.observe(foreign,fc).outcome==Outcome::WrongOwner);
    o.round+=2;c.round+=2;o.channel=Channel::None;o.bag=0;o.team_score=107;
    CHECK(n.observe(o,c).outcome==Outcome::Expired);CHECK(n.state()==State::Blocked);
    CHECK(n.observe(o,c).credited_prior_bag==0);
    Monitor unknown;o=body();c=world(o);start(unknown,o,c);
    ++o.round;++c.round;c.altar_known=false;
    CHECK(unknown.observe(o,c).outcome==Outcome::NeedObservation);
    CHECK(unknown.state()==State::Active);
    Monitor no_effect;o=body();c=world(o);no_effect.observe(o,c);const auto proposal=no_effect.plan(o,c);
    CHECK(no_effect.accepted_start(proposal,true,o,c).outcome==Outcome::StartRejected);
    ++o.round;++c.round;o.bag=0;o.team_score=107;
    CHECK(no_effect.observe(o,c).credited_prior_bag==0);
}
}
int main(){admission();completion_and_capture();failed_start_and_bounded_retry();interruptions();expiry_and_authority();
    std::printf("capital_bank: %u checks, %u failures\n",checks,failures);return failures?1:0;}
