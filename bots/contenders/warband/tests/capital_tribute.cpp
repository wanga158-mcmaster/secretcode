#include "capital_tribute.h"
#include <cstdio>
#include <type_traits>
using namespace wb::capital::tribute;
static unsigned checks=0,failed=0;
static void check(bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAIL %s\n",why);}}
static Observation body(int round=40) {
    Observation o;o.owner=2;o.round=round;o.input=static_cast<std::uint64_t>(round+100);o.sample=1;
    o.position={7,8};o.bag=14;o.team_score=100;o.channel=Channel::None;
    o.observed=o.fresh=o.alive=o.submit_permitted=o.can_move=o.can_attack=o.can_cast=true;return o;
}
static Context world(const Observation& o,unsigned ticks=2) {
    Context c;c.owner=o.owner;c.round=o.round;c.input=o.input;c.sample=o.sample;c.altar={8,8};
    c.queried_ticks=ticks;c.planned_horizon=o.round+static_cast<int>(ticks);
    c.fresh=c.altar_known=c.adjacent=c.unlocked=c.minimum_gap_queried=c.bag_loss_model_closed=true;
    c.minimum_alive_cycle_gap=2;return c;
}
static Guard guarded(const Observation& o,const Context& c,Action action,Point destination,std::uint64_t token=7) {
    return {o.sample+20,token,o.owner,o.round,o.input,o.sample,action,destination,c.queried_ticks,c.planned_horizon,true};
}
static void new_input(Observation& o) {
    ++o.round;++o.input;o.sample=1;o.submit_started=o.submit_cancelled=false;
    o.can_move=o.can_attack=o.can_cast=true;
}
static void start(Monitor& m,Observation& o,unsigned ticks=2) {
    auto c=world(o,ticks);check(m.begin(7,o,c).outcome==Outcome::NewReceipt,"bind actual original bag once");
    const auto p=m.prepare(Action::Start,c.altar,o,c,guarded(o,c,Action::Start,c.altar));check(p.prepared,"queried duration bookkeeping start");
    ++o.sample;o.channel=Channel::Submit;o.submit_started=true;o.submit_permitted=false;o.remaining_known=true;o.remaining_ticks=ticks;
    c=world(o,ticks);const auto r=m.confirm_start(p,true,o,c);
    check(r.outcome==Outcome::PendingChannel&&r.credited_original==0&&r.evidence==Evidence::ModeledExpected,"actual start is zero bank credit");
    check(m.expected_round()==o.round+static_cast<int>(ticks)&&m.queried_ticks_at_start()==ticks,"queried N fixes due round");
}
static void pending(Monitor& m,Observation& o,unsigned remaining,unsigned query_ticks=2) {
    new_input(o);o.channel=Channel::Submit;o.remaining_known=true;o.remaining_ticks=remaining;
    check(m.observe(o,world(o,query_ticks)).outcome==Outcome::PendingChannel,"actual dated remaining countdown");
}
static void duration_and_growth() {
    for(unsigned ticks=1;ticks<=8;++ticks)for(unsigned growth=0;growth<4;++growth) {
        Monitor m;auto o=body();start(m,o,ticks);const auto due=m.expected_round();
        for(unsigned k=1;k<ticks;++k) {
            if(k==1)o.bag+=growth*7;
            pending(m,o,ticks-k,static_cast<unsigned>(20+growth));
            check(m.expected_round()==due,"capture growth never extends actual timer");
            check(m.observe(o,world(o,20+growth)).credited_original==0,"repeated pending read is not a bank");
        }
        new_input(o);o.channel=Channel::None;o.remaining_ticks=0;o.bag=0;
        o.team_score=114+(ticks>1?growth*7:0)+11;
        const auto r=m.observe(o,world(o));
        check(r.outcome==Outcome::ConservationPayout&&r.evidence==Evidence::ConservationSupported&&r.credited_original==14,"N tick conservation credits original once");
        check(!r.observed_total_known&&r.unattributed_score_delta==o.team_score-114,"team surplus is not an observed own payout");
        check(m.observe(o,world(o)).credited_original==0,"completion is idempotent");
        auto dead=o;++dead.sample;dead.alive=false;dead.position={-1,-1};
        check(m.observe(dead,world(dead)).original_presence==Presence::Credited,"later death cannot undo permanent credited tribute");
    }
    // R40 fourteen gems, queried two ticks; R42 capture seven before payout.
    Monitor capture;auto o=body();start(capture,o,2);pending(capture,o,1);
    new_input(o);o.channel=Channel::None;o.remaining_ticks=0;o.bag=0;o.team_score=121;
    const auto r=capture.observe(o,world(o));
    check(r.credited_original==14&&r.own_payout_lower==14&&r.unattributed_score_delta==7&&!r.observed_total_known,"unseen payout-round capture is kept unattributed");
    Monitor triple;o=body();start(triple,o,3);pending(triple,o,2);o.bag=21;pending(triple,o,1,4);
    new_input(o);o.channel=Channel::None;o.remaining_ticks=0;o.bag=0;o.team_score=121;
    const auto q=triple.observe(o,world(o));check(q.credited_original==14&&q.own_payout_lower==21,"observed held bag supplies a lower bound, not timer reset");
}
static void move_then_submit_and_refusals() {
    Monitor m;auto o=body();o.position={6,8};auto c=world(o);c.adjacent=false;
    m.begin(7,o,c);const Point destination{7,8};auto p=m.prepare(Action::Approach,destination,o,c,guarded(o,c,Action::Approach,destination));
    check(p.prepared,"approach has its own exact guard");auto forged=p;++forged.guard.id;
    auto after=o;++after.sample;after.position=destination;after.can_move=false;c=world(after);
    check(m.confirm_move(forged,true,after,c).outcome==Outcome::Invalid,"edited approach guard cannot confirm");
    const auto moved=m.confirm_move(p,true,after,c);
    check(moved.outcome==Outcome::Approached&&moved.actual_movement&&moved.original_amount==14,"actual move retains original receipt");
    check(!m.prepare(Action::Start,c.altar,o,world(o),guarded(o,world(o),Action::Start,c.altar)).prepared,"pre-move snapshot cannot start after move");
    p=m.prepare(Action::Start,c.altar,after,c,guarded(after,c,Action::Start,c.altar));check(p.prepared,"fresh same-input start after actual movement");
    o=after;++o.sample;o.channel=Channel::Submit;o.submit_started=true;o.remaining_known=true;o.remaining_ticks=2;
    check(m.confirm_start(p,true,o,world(o)).credited_original==0,"Move then Submit still zero bank at start");
    check(!m.latest().can_move,"monitor retains spent movement allowance");
    for(bool host_accepted:{false,true}) {
        Monitor rejected;o=body();c=world(o);rejected.begin(7,o,c);
        p=rejected.prepare(Action::Start,c.altar,o,c,guarded(o,c,Action::Start,c.altar));
        ++o.sample;o.can_attack=false; // locked-altar action may have attacked instead
        const auto failure=rejected.confirm_start(p,host_accepted,o,world(o));
        check(failure.outcome==Outcome::StartRejected&&failure.credited_original==0&&failure.original_presence==Presence::Held,"nonsubmit readback rejects even accepted lock attack");
        check(!rejected.latest().can_attack&&rejected.attempts()==1,"failed start preserves actual spent attack and attempt");
        new_input(o);o.submit_permitted=true;rejected.observe(o,world(o));
        check(!rejected.prepare(Action::Start,c.altar,o,world(o),guarded(o,world(o),Action::Start,c.altar)).prepared,"elapsed input alone cannot rearm failed start");
        check(!rejected.rearm(Change::None,o,world(o))&&!rejected.rearm(Change::ActualStep,o,world(o)),"empty change is not a retry");
        ++o.sample;++o.position.x;rejected.observe(o,world(o));
        check(rejected.rearm(Change::ActualStep,o,world(o)),"actual changed pose can rearm retained receipt");
        check(rejected.prepare(Action::Start,c.altar,o,world(o),guarded(o,world(o),Action::Start,c.altar)).prepared,"retry still needs fresh exact guard");
    }
    Monitor partial;o=body();c=world(o);partial.begin(7,o,c);
    p=partial.prepare(Action::Approach,{9,8},o,c,guarded(o,c,Action::Approach,{9,8}));
    ++o.sample;o.position={8,8};
    const auto stopped=partial.confirm_move(p,true,o,world(o));
    check(stopped.outcome==Outcome::EndpointChanged&&stopped.actual_movement&&stopped.original_amount==14,"partial actual move retains receipt then stops plan");
}
static void interruption_and_unknown() {
    for(unsigned failure=0;failure<9;++failure) {
        Monitor m;auto o=body();start(m,o,3);new_input(o);o.remaining_ticks=2;auto c=world(o);
        switch(failure) {
        case 0:o.alive=false;o.position={-1,-1};o.bag=0;break;
        case 1:o.stunned=true;break;case 2:c.unlocked=false;break;case 3:o.channel=Channel::None;break;
        case 4:o.remaining_ticks=3;break;case 5:o.submit_started=true;break;case 6:o.submit_cancelled=true;break;
        case 7:o.bag=13;break;case 8:o.channel=Channel::Other;break;
        }
        const auto r=m.observe(o,c);check(m.state()==State::Blocked&&r.credited_original==0,"death/stun/lock/cancel/timer/bag mismatches never bank");
        check(!m.prepare(Action::None,o.position,o,c,{}).prepared,"blocked state never grants an empty wait");
        check(m.begin(99,o,c).outcome==Outcome::RetryBlocked,"new serial cannot clone/rearm unresolved receipt");
    }
    for(unsigned premise=0;premise<4;++premise) {
        Monitor m;auto o=body();start(m,o,2);new_input(o);o.remaining_ticks=1;auto c=world(o);
        if(premise==0)c.minimum_gap_queried=false;
        if(premise==1)c.minimum_alive_cycle_gap=1;
        if(premise==2)c.bag_loss_model_closed=false;
        if(premise==3){++o.round;++o.input;c=world(o);o.remaining_ticks=0;o.channel=Channel::None;o.bag=0;o.team_score=114;}
        const auto r=m.observe(o,c);check(r.credited_original==0&&r.original_presence==Presence::Unknown&&m.state()==State::Blocked,"missing continuity or skipped input forfeits attribution");
        new_input(o);o.channel=Channel::None;o.bag=0;o.team_score+=100;
        check(m.observe(o,world(o)).credited_original==0,"later ally score cannot repair lost observation history");
    }
    Monitor ally;auto o=body();start(ally,o,2);pending(ally,o,1);new_input(o);o.channel=Channel::None;o.remaining_ticks=0;o.team_score=500;
    check(ally.observe(o,world(o)).credited_original==0,"ally score with our bag intact is not our bank");
    Monitor empty;auto b=body();start(empty,b,2);pending(empty,b,1);new_input(b);b.channel=Channel::None;b.remaining_ticks=0;b.bag=0;b.team_score=100;
    check(empty.observe(b,world(b)).credited_original==0&&empty.original_presence()==Presence::Unknown,"zero bag without matching score is unknown, not payout");
}
static void native_events() {
    Monitor m;auto o=body();const auto initial=o;start(m,o,2);pending(m,o,1);new_input(o);
    o.channel=Channel::None;o.remaining_ticks=0;o.bag=0;o.team_score=124;
    PayoutEvent e{true,91,7,o.owner,o.round,initial.round,o.input,initial.input,21};
    const auto paid=m.observe(o,world(o),&e);
    check(paid.evidence==Evidence::ObservedPayout&&paid.observed_total_known&&paid.observed_total==21&&paid.credited_original==14,"actual attributed payout distinguishes total from original");
    check(paid.unattributed_score_delta==3,"only score outside observed own total stays unattributed");
    check(m.observe(o,world(o),&e).credited_original==0,"replayed event cannot credit original twice");
    auto other=e;++other.id;check(m.observe(o,world(o),&other).outcome==Outcome::Conflict,"second event cannot clone settled episode");
    Monitor upgrade;o=body();start(upgrade,o,2);pending(upgrade,o,1);new_input(o);o.channel=Channel::None;o.remaining_ticks=0;o.bag=0;o.team_score=121;
    check(upgrade.observe(o,world(o)).credited_original==14,"first conservation settlement");
    e={true,92,7,o.owner,o.round,40,o.input,140,21};
    const auto stronger=upgrade.observe(o,world(o),&e);
    check(stronger.evidence==Evidence::ObservedPayout&&stronger.credited_original==0&&stronger.unattributed_score_delta==0,"actual evidence upgrade never double credits original");
    Monitor death;o=body();start(death,o,2);new_input(o);o.alive=false;o.position={-1,-1};o.bag=0;death.observe(o,world(o));
    new_input(o);o.alive=true;o.position={7,8};o.channel=Channel::None;o.bag=0;o.team_score=114;
    e={true,93,7,o.owner,o.round,40,o.input,140,14};
    const auto own_new=death.observe(o,world(o),&e);
    check(own_new.outcome==Outcome::Conflict&&!own_new.observed_total_known&&own_new.credited_original==0
        &&own_new.original_presence==Presence::Lost,"old channel event contradicts observed death and cannot restore token");
    Monitor gap;o=body();start(gap,o,3);new_input(o);new_input(o);o.remaining_ticks=1;
    check(gap.observe(o,world(o)).original_presence==Presence::Unknown,"gap loses original provenance");
    new_input(o);o.channel=Channel::None;o.remaining_ticks=0;o.bag=0;o.team_score=114;
    e={true,95,7,o.owner,o.round,40,o.input,140,14};
    const auto actual_gap=gap.observe(o,world(o),&e);
    check(actual_gap.evidence==Evidence::ObservedPayout&&actual_gap.observed_total==14&&actual_gap.credited_original==0
        &&actual_gap.original_presence==Presence::Unknown,"actual old episode can establish total without repairing gap provenance");
    Monitor inconsistent;o=body();start(inconsistent,o,1);new_input(o);o.channel=Channel::None;o.remaining_ticks=0;o.bag=0;o.team_score=114;
    check(inconsistent.observe(o,world(o)).credited_original==14,"conservation original settled before event upgrade");
    e={true,96,7,o.owner,o.round,40,o.input,140,1};
    const auto bad_upgrade=inconsistent.observe(o,world(o),&e);
    check(bad_upgrade.outcome==Outcome::Conflict&&!bad_upgrade.observed_total_known
        &&bad_upgrade.original_presence==Presence::Credited&&bad_upgrade.own_payout_lower==14,"contradictory event cannot downgrade credited payout");
    Monitor contradiction;o=body();start(contradiction,o,1);new_input(o);o.channel=Channel::None;o.remaining_ticks=0;o.bag=0;o.team_score=114;
    e={true,97,7,o.owner,o.round,40,o.input,140,1};
    check(contradiction.observe(o,world(o),&e).outcome==Outcome::Conflict
        &&contradiction.original_presence()==Presence::Unknown,"contradictory actual amount poisons uncredited original provenance");
    check(contradiction.observe(o,world(o)).credited_original==0,"later SDK conservation cannot ignore contradictory payout witness");
    Monitor wrong;o=body();start(wrong,o,1);new_input(o);o.channel=Channel::None;o.remaining_ticks=0;o.bag=0;o.team_score=114;
    e={true,94,7,o.owner+1,o.round,40,o.input,140,14};
    check(wrong.observe(o,world(o),&e).outcome==Outcome::Invalid,"foreign payout event is not ours");
}
static void provenance_counterexamples() {
    // A fungible original cannot remain fully attributed after ANY unexplained
    // decrease, including when the remaining bag equals the original amount.
    for(bool active:{false,true}) {
        Monitor m;auto o=body();
        if(active)start(m,o,3);else m.begin(7,o,world(o));
        ++o.sample;o.bag=21;m.observe(o,world(o));
        ++o.sample;o.bag=14;const auto r=m.observe(o,world(o));
        check(r.original_presence==Presence::Unknown&&r.credited_original==0,"above-original bag loss forfeits full original provenance");
        ++o.sample;++o.position.x;m.observe(o,world(o));
        check(!m.rearm(Change::ActualStep,o,world(o)),"movement cannot recover dropped original provenance");
    }
    Monitor interrupted;auto o=body();start(interrupted,o,2);
    new_input(o);o.channel=Channel::None;o.remaining_ticks=0;interrupted.observe(o,world(o));
    ++o.sample;++o.position.x;interrupted.observe(o,world(o));
    check(interrupted.rearm(Change::ActualStep,o,world(o)),"real step rearms retained interrupted bag");
    new_input(o);o.channel=Channel::None;o.bag=0;o.team_score=114;
    check(interrupted.observe(o,world(o)).original_presence==Presence::Unknown,"old due round in Tracking cannot hide empty bag");
    new_input(o);o.bag=14;o.submit_permitted=true;interrupted.observe(o,world(o));auto c=world(o);
    check(!interrupted.prepare(Action::Start,c.altar,o,c,guarded(o,c,Action::Start,c.altar)).prepared,"later new gems cannot remint old empty original token");
    Monitor zero;o=body();start(zero,o,2);++o.sample;o.remaining_ticks=0;
    check(zero.observe(o,world(o)).outcome==Outcome::TimerMismatch,"same-input timer zero is not an early payout");
    Monitor readback;o=body();c=world(o);readback.begin(7,o,c);
    const auto p=readback.prepare(Action::Start,c.altar,o,c,guarded(o,c,Action::Start,c.altar));
    ++o.sample;o.channel=Channel::Submit;o.submit_started=o.remaining_known=true;o.remaining_ticks=1;
    check(readback.confirm_start(p,true,o,world(o)).outcome==Outcome::StartRejected,"start readback must show actual queried full duration");
}
static void identity_and_bounds() {
    Monitor m;auto o=body();auto c=world(o);m.begin(7,o,c);
    auto p=m.prepare(Action::Start,c.altar,o,c,guarded(o,c,Action::Start,c.altar));check(p.prepared,"identity start prepared");
    check(!m.prepare(Action::Start,c.altar,o,c,guarded(o,c,Action::Start,c.altar)).prepared,"one pending local word");
    auto wrong=o;++wrong.owner;auto wc=world(wrong);
    check(m.observe(wrong,wc).outcome==Outcome::WrongOwner,"wrong body cannot mutate receipt");
    auto stale=o;--stale.round;check(m.observe(stale,world(stale)).outcome==Outcome::Stale,"stale round rejected");
    wrong=o;++wrong.input;check(m.observe(wrong,world(wrong)).outcome==Outcome::WrongInput,"same round wrong input rejected");
    wrong=o;++wrong.bag;check(m.observe(wrong,world(wrong)).outcome==Outcome::Conflict,"same sample conflicting body rejected");
    auto after=o;++after.sample;after.channel=Channel::Submit;after.submit_started=after.remaining_known=true;after.remaining_ticks=2;
    check(m.confirm_start(p,true,after,world(after)).outcome==Outcome::PendingChannel,"proper same-input actual start");
    check(m.confirm_start(p,true,after,world(after)).outcome==Outcome::Invalid,"private start confirmation not replayable");
    for(unsigned ticks:{0u,max_ticks+1}) {
        Monitor invalid;auto x=body();auto ctx=world(x,ticks);invalid.begin(7,x,ctx);
        check(!invalid.prepare(Action::Start,ctx.altar,x,ctx,guarded(x,ctx,Action::Start,ctx.altar)).prepared,"duration input bounds explicit");
    }
    Monitor horizon;auto x=body();auto ctx=world(x);horizon.begin(7,x,ctx);ctx.planned_horizon=x.round+1;
    horizon.observe(x,ctx);check(!horizon.prepare(Action::Start,ctx.altar,x,ctx,guarded(x,ctx,Action::Start,ctx.altar)).prepared,"short planned horizon cannot silently force N1");
    Monitor edge;x=body(max_round-1);ctx=world(x,2);edge.begin(7,x,ctx);
    check(!edge.prepare(Action::Start,ctx.altar,x,ctx,guarded(x,ctx,Action::Start,ctx.altar)).prepared,"duration addition cannot exceed bounded round domain");
    Monitor maximum;x=body();start(maximum,x,max_ticks);
    check(maximum.expected_round()==x.round+static_cast<int>(max_ticks),"maximum supported queried duration retained");
    Monitor zero;auto empty=body();empty.bag=0;check(zero.begin(7,empty,world(empty)).outcome==Outcome::Invalid,"empty bag cannot fabricate receipt");
}
int main(){duration_and_growth();move_then_submit_and_refusals();interruption_and_unknown();native_events();identity_and_bounds();provenance_counterexamples();
    std::printf("capital_tribute checks=%u failed=%u\n",checks,failed);return failed?1:0;}
