#include "../common/src/receipts.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
using namespace p9::live;
int cases=0,checks=0,failures=0;
void check(bool condition,std::string_view name) {
    ++checks;
    if(!condition) {++failures;std::cerr<<"FAIL live receipts: "<<name<<'\n';}
}
BankSample before(int round=10,Gems bag=10,Gems score=100,int respawn=2) {
    return {round,-1,respawn,true,false,bag,score};
}
BankSample channel(int round=10,int remaining=1,Gems bag=10,Gems score=100,int respawn=2) {
    return {round,remaining,respawn,true,true,bag,score};
}
BankSample completed(int round=11,Gems score=110,int respawn=2) {
    return {round,-1,respawn,true,false,0,score};
}
void none(BankReceipt receipt,std::string_view name) {
    check(!receipt.completed&&receipt.lower==0&&receipt.upper==-1,name);
}
void begin(PersonalBank& bank,int ticks=1) {
    check(bank.begin(before(),channel(10,ticks),ticks),"valid own start is admitted");
}
void successful_intervals() {
    ++cases;PersonalBank immediate;begin(immediate);
    const auto exact=immediate.observe(completed());
    check(exact.completed&&exact.lower==10&&exact.upper==10,"one-tick owner-local completion proves exact observed whole bag");
    none(immediate.observe(completed(12,120)),"one completion cannot be credited twice");

    ++cases;PersonalBank duration;begin(duration,3);
    none(duration.observe(channel(10,3)),"same-round refresh does not decrement the channel timer");
    none(duration.observe(channel(11,2)),"first owner tick preserves channel continuity");
    none(duration.observe(channel(12,1)),"second owner tick preserves channel continuity");
    const auto third=duration.observe(completed(13));
    check(third.completed&&third.lower==10&&third.upper==10,"three-tick completion occurs at the exact owner boundary");

    ++cases;PersonalBank capture_at_completion;begin(capture_at_completion);
    const auto upper=capture_at_completion.observe(completed(11,117));
    check(upper.completed&&upper.lower==10&&upper.upper==17,
        "capture-before-bank or teammate conversion widens upper without inventing exact own payment");

    ++cases;PersonalBank growing;begin(growing,3);
    none(growing.observe(channel(11,2,15,105)),"observed incoming own bag raises lower bound while channel remains active");
    none(growing.observe(channel(12,1,15,105)),"intermediate public-score delta is consumed at its own boundary");
    const auto grown=growing.observe(completed(13,125));
    check(grown.completed&&grown.lower==15&&grown.upper==20,
        "largest actually observed bag is lower and final contiguous score delta is upper");

    ++cases;PersonalBank enlarged;
    check(enlarged.begin(before(),channel(10,1,12,110),1),"same-round own bag addition before verified channel start is retained");
    const auto after_start_gain=enlarged.observe(completed(11,122));
    check(after_start_gain.completed&&after_start_gain.lower==12&&after_start_gain.upper==12,
        "score increase at start is excluded from later attribution");
}
void start_rejections() {
    struct Case {BankSample before,after;int ticks;const char* name;};
    auto dead_before=before();dead_before.alive=false;
    auto dead_after=channel();dead_after.alive=false;
    auto already_running=before();already_running.channel=true;
    auto no_channel=channel();no_channel.channel=false;
    const std::array starts{
        Case{before(),channel(10,2),1,"start timer must equal the queried duration"},
        Case{before(),channel(10,0),0,"zero duration does not license a receipt"},
        Case{before(),channel(10,-1),-1,"unknown duration does not license a receipt"},
        Case{before(),channel(11),1,"before and after start must share one owner round"},
        Case{before(10,0),channel(),1,"empty initial bag is not a conversion premise"},
        Case{before(),channel(10,1,9),1,"start cannot silently lose own bag"},
        Case{before(),channel(10,1,10,99),1,"start cannot reverse permanent team score"},
        Case{dead_before,channel(),1,"dead before start is rejected"},
        Case{before(),dead_after,1,"dead after start is rejected"},
        Case{already_running,channel(),1,"another already active channel is rejected"},
        Case{before(),no_channel,1,"void start without observed active channel is rejected"},
        Case{before(10,10,100,1),channel(10,1,10,100,1),1,"one-turn respawn cannot exclude death and return"},
        Case{before(10,10,100,0),channel(10,1,10,100,0),1,"unknown zero respawn is rejected"},
        Case{before(10,10,100,-1),channel(10,1,10,100,-1),1,"negative respawn is rejected"},
        Case{before(),channel(10,1,10,100,3),1,"changed respawn settings invalidate the start"},
        Case{before(-1),channel(-1),1,"unknown negative round cannot start a personal receipt history"},
        Case{before(10,10,-1),channel(10,1,10,-1),1,"unknown negative team score cannot start a personal receipt history"}
    };
    for(const auto& test:starts) {
        ++cases;PersonalBank bank;
        check(!bank.begin(test.before,test.after,test.ticks),test.name);
    }
}
void continuity_rejections() {
    struct Case {BankSample sample;int ticks;const char* name;};
    auto dead=channel(11,2);dead.alive=false;
    auto nonempty=completed();nonempty.bag=1;
    const std::array cases_to_reject{
        Case{channel(12,1),3,"missing owner round rejects continuity"},
        Case{channel(9,4),3,"backward owner round rejects continuity"},
        Case{channel(10,2),3,"same-round channel timer change rejects continuity"},
        Case{channel(11,1),3,"timer skipping an owner tick rejects continuity"},
        Case{channel(11,4),3,"timer increase rejects continuity"},
        Case{channel(11,2,9),3,"bag loss during running tribute rejects continuity"},
        Case{channel(11,2,10,99),3,"team-score decrease rejects continuity"},
        Case{dead,3,"death rejects completion even if later respawn is observed"},
        Case{channel(11,2,10,100,3),3,"respawn setting changes reject continuity"},
        Case{before(11),3,"early channel cancellation cannot prove completion"},
        Case{completed(11),3,"early cancellation plus a public score increase cannot prove completion"},
        Case{nonempty,1,"remaining own bag contradicts whole-bag completion"},
        Case{completed(11,109),1,"insufficient own-team score increase cannot fund the observed whole bag"},
        Case{completed(10),1,"same-round bag disappearance is not next-owner-start completion"},
        Case{channel(11,0),1,"a still-active zero timer does not fabricate already resolved conversion"}
    };
    for(const auto& test:cases_to_reject) {
        ++cases;PersonalBank bank;begin(bank,test.ticks);
        none(bank.observe(test.sample),test.name);
        none(bank.observe(completed(10+test.ticks,110)),"rejected continuity cannot resume from a later plausible completion");
    }
}
void cancellation_and_no_reset() {
    ++cases;PersonalBank cancelled;begin(cancelled);
    cancelled.invalidate();none(cancelled.observe(completed()),"explicit cancellation/loss action invalidation rejects an apparent completion");

    ++cases;PersonalBank running;begin(running,3);
    check(!running.begin(before(11,50,100),channel(11,1,50,100),1),"a second start never resets a running bank history");
    none(running.observe(channel(11,2)),"failed second start retains original timer and bag");
    none(running.observe(channel(12,1)),"original bank remains continuous after refused reset");
    const auto original=running.observe(completed(13));
    check(original.completed&&original.lower==10&&original.upper==10,"completion retains original start and costs");

    ++cases;PersonalBank unchanged;begin(unchanged,2);
    auto invalid=channel();invalid.alive=false;
    check(!unchanged.begin(before(),invalid,1),"invalid repeated start cannot overwrite valid channel evidence");
    none(unchanged.observe(channel(11,1)),"valid old channel remains after an invalid repeated start");
    check(unchanged.observe(completed(12)).completed,"old bank can complete once after failed replacement");
}
World world(int round,bool safe=true) {World w;w.round=round;w.safe_now=safe;return w;}
void campaign_admission() {
    ++cases;Campaign campaign;
    check(!campaign.may_start(world(0,false))&&!campaign.start(world(0,false))&&campaign.used==0,
        "unsafe admission neither starts nor consumes a campaign");
    check(campaign.start(world(0))&&campaign.used==1&&campaign.active&&campaign.turns==1,
        "admission charges the first actual strategic owner turn");
    check(!campaign.start(world(0))&&campaign.used==1&&campaign.admitted==0,
        "running campaign cannot reset its start or charge");
    campaign.finish();check(!campaign.active&&campaign.used==1&&campaign.epoch[0]==0,
        "retirement preserves spent campaign and epoch");
    check(campaign.start(world(63))&&campaign.used==2&&campaign.epoch[1]==0,
        "same epoch admits its second bounded campaign");
    check(!campaign.start(world(64))&&campaign.used==2,
        "an active campaign cannot reset its admission at an epoch boundary");
    campaign.finish();check(campaign.start(world(64))&&campaign.used==1&&campaign.epoch[0]==1,
        "new epoch renews the two-attempt allowance after retirement");
    campaign.finish();check(campaign.start(world(65))&&campaign.used==2,
        "new epoch admits its second bounded attempt");
    campaign.finish();check(!campaign.start(world(66))&&campaign.used==2,
        "third attempt remains blocked inside its epoch");
    check(campaign.start(world(128))&&campaign.used==1,
        "later epochs do not inherit a lifetime lockout");

    ++cases;Campaign boundary;
    check(boundary.start(world(63)),"last turn of epoch zero admits once");boundary.finish();
    check(boundary.start(world(64)),"epoch latch allows a distinct epoch without inventing a sixty-four-turn cooldown");
}
void campaign_turns_and_expiry() {
    ++cases;Campaign segment;check(segment.start(world(0)),"segment starts");
    segment.tick(world(0),true);check(segment.turns==1,"same-round refresh does not advance strategic turn count");
    segment.tick(world(1),true);segment.tick(world(1),true);
    check(segment.turns==2,"duplicate view of the same owner round does not double-charge");
    segment.tick(world(14),true);check(segment.turns==15&&!segment.expired(world(14)),"fifteenth charged turn remains below cap");
    segment.tick(world(15),true);
    check(segment.turns==16&&segment.expired(world(15))&&!segment.disabled,
        "sixteenth charged turn reaches normal segment stop without an overrun");
    segment.tick(world(16),true);
    check(segment.turns==17&&segment.disabled&&segment.expired(world(16)),"safe completion overrun disables its epoch");
    segment.finish();check(!segment.may_start(world(63)),"overrun remains disabling for the rest of its epoch");
    check(segment.start(world(64))&&!segment.disabled&&segment.used==1,
        "a later epoch renews admission after an overrun without resetting an active segment");

    ++cases;Campaign stale;check(stale.start(world(10)),"freshness case starts");
    stale.tick(world(17),false);check(stale.turns==8&&!stale.expired(world(17)),"seven rounds without fresh qualification retain the certificate");
    stale.tick(world(18),false);check(stale.expired(world(18)),"eight rounds without fresh qualification expire the certificate");

    ++cases;Campaign renewed;check(renewed.start(world(10)),"renewal case starts");
    renewed.tick(world(12),true);renewed.tick(world(19),false);
    check(!renewed.expired(world(19))&&renewed.turns==10,"fresh qualifying observation renews freshness without resetting spent turns");
    renewed.tick(world(20),false);check(renewed.expired(world(20))&&renewed.turns==11,"renewed certificate still expires after eight unobserved owner rounds");
    renewed.finish();const auto turns=renewed.turns;const auto fresh=renewed.last_fresh;
    renewed.tick(world(25),true);check(renewed.turns==turns&&renewed.last_fresh==fresh,"inactive tick cannot reset historical costs or observations");
}
ResponseObservation rival(int round,int primary=5,int id=20,Point point={2,3}) {
    return {id,round,primary,point};
}
void response_history() {
    // The World contract's documented labels are contest/substitute/wait/unknown.
    constexpr int contest=0,substitute=1,wait=2,unknown=3;
    ++cases;ResponseHistory stationary;
    check(stationary.classify(rival(10),false,false)==unknown,"one personally observed stationary sample is unknown");
    check(stationary.classify(rival(11),false,false)==unknown,"two stationary owner rounds do not establish waiting");
    check(stationary.classify(rival(12),false,false)==wait,"three fresh consecutive same-rival same-tile observations establish waiting");
    check(stationary.classify(rival(13),true,false)==contest,"stationary rival retains all current legal contest constraints");

    ++cases;ResponseHistory approach;
    (void)approach.classify(rival(10),false,false);
    check(approach.classify(rival(11,4,20,{3,3}),false,true)==contest,
        "decreased known static primary distance precedes an other-site flag");
    ++cases;ResponseHistory relocation;
    (void)relocation.classify(rival(10),false,false);
    check(relocation.classify(rival(11,6,20,{1,3}),false,true)==substitute,
        "observed other named site plus increased primary distance establishes substitution");
    ++cases;ResponseHistory departure;
    (void)departure.classify(rival(10),false,false);
    check(departure.classify(rival(11,6,20,{1,3}),false,false)==unknown,
        "departure without a named observed site is not substitution");

    ++cases;ResponseHistory gap;
    (void)gap.classify(rival(10),false,false);
    check(gap.classify(rival(12,4,20,{3,3}),false,false)==unknown,"owner-round gap cannot establish a visible approach");
    check(gap.classify(rival(13,4,20,{3,3}),false,false)==unknown,"post-gap second same-tile sample still lacks a third observation");
    check(gap.classify(rival(14,4,20,{3,3}),false,false)==wait,"post-gap waiting requires three new consecutive observations");
    ++cases;ResponseHistory changed;
    (void)changed.classify(rival(10),false,false);
    check(changed.classify(rival(11,4,21,{3,3}),false,false)==unknown,"changed rival identity cannot inherit movement");
    check(changed.classify(rival(12,4,21,{3,3}),false,false)==unknown,"changed rival cannot inherit stationary observations");
    check(changed.classify(rival(13,4,21,{3,3}),false,false)==wait,"new rival gets its own three-sample wait boundary");

    for(int defect=0;defect<4;++defect) {
        ++cases;ResponseHistory missing;
        (void)missing.classify(rival(10),false,false);(void)missing.classify(rival(11),false,false);
        auto invalid=rival(12);
        if(defect==0) invalid.primary=-1;
        if(defect==1) invalid.id=-1;
        if(defect==2) invalid.round=-1;
        check(missing.classify(invalid,true,true,defect==3)==unknown,
            "missing route/identity/round or extra opposition overrides even a supplied contest flag");
        check(missing.classify(rival(13),false,false)==unknown,"invalid observation clears old waiting continuity");
        check(missing.classify(rival(14),false,false)==unknown,"second fresh post-reset sample remains unknown");
        check(missing.classify(rival(15),false,false)==wait,"third fresh post-reset sample can establish waiting");
    }
}
void response_deadline_matrix() {
    World w;w.self.bag=5;w.capture_turns=4;w.tribute_multiplier=.1;w.tribute_base=0;w.tribute_minimum=1;
    w.windows={14,13,20};w.response=3;
    Group g;g.id=9;g.gems=5;g.gold=7;g.observed=g.route_known=g.safe_solo=true;
    g.travel=2;g.clear_turns=2;g.to_bank=3;
    // Hand schedule: 2 travel +2 clear +4 hold +3 bank travel +1 channel +2
    // slack =14 to submit; +3 return +2 slack =19 to productive renewal.
    ++cases;const auto deadline=cycle_value(w,g);
    check(deadline.feasible&&deadline.turns==19&&deadline.arrival==19&&deadline.peak==10,
        "complete cycle retains hand-counted travel/channel/renewal costs");
    check(deadline.worst==0&&deadline.nominal==0&&deadline.unresolved==0,
        "one missed inherited deadline makes conservative minimum zero without claiming missing data");
    for(const auto& row:deadline.submitted)
        check(row==std::array<Gems,3>{10,0,10},"all four response rows retain each exact deadline boundary");

    ++cases;w.windows={14,14,14};const auto exact=cycle_value(w,g);
    check(exact.worst==10&&exact.nominal==10&&exact.unresolved==0,
        "submission at the exact inherited deadline counts the own whole bag once");
    for(const auto& row:exact.submitted) check(row==std::array<Gems,3>{10,10,10},"represented safe responses earn no extra benefit over own submission");

    ++cases;g.contested=true;const auto contested=cycle_value(w,g);
    check(contested.feasible&&contested.worst==0&&contested.nominal==0&&contested.unresolved==9,
        "contested contest/unknown continuations remain unresolved with no guaranteed future score");
    check(contested.submitted[0]==std::array<Gems,3>{0,0,0}&&contested.submitted[3]==std::array<Gems,3>{0,0,0},
        "unknown opposition and contest are never silently removed from minimum");
    check(contested.submitted[1]==std::array<Gems,3>{10,10,10}&&contested.submitted[2]==std::array<Gems,3>{10,10,10},
        "represented wait/substitute continuations retain actual own production only");
    w.response=1;check(cycle_value(w,g).nominal==10,"nominal uses current represented response while worst remains conservative");

    ++cases;w.extra_opposition=true;const auto extra=cycle_value(w,g);
    check(extra.feasible&&extra.worst==0&&extra.nominal==0&&extra.unresolved==15,
        "extra unrepresented opposition unresolved-flags all four response rows");
    for(const auto& row:extra.submitted) check(row==std::array<Gems,3>{0,0,0},"unrepresented opposition receives no favorable branch credit");

    ++cases;w.extra_opposition=false;g.contested=false;g.gold=1000000;w.enemy_score=999;w.own_gold=1000000;
    const auto no_denial=cycle_value(w,g);
    check(no_denial.worst==10&&no_denial.peak==10&&no_denial.submitted==exact.submitted,
        "gold or enemy score never converts into guaranteed own gems or imagined denial");
    const auto paid=cycle_value(w,g,1);
    check(paid.turns==20&&paid.worst==0&&paid.nominal==0,
        "an added sunk/deployment turn remains paid and misses the formerly exact deadline");

    ++cases;g.route_known=false;const auto unknown=cycle_value(w,g);
    check(!unknown.feasible&&unknown.worst==0,"missing actual source route has no invented future score");
    for(const auto& row:unknown.submitted) check(row==std::array<Gems,3>{0,0,0},"unavailable schedule leaves every response/deadline amount zero");
}
}
int main() {
    successful_intervals();start_rejections();continuity_rejections();cancellation_and_no_reset();
    campaign_admission();campaign_turns_and_expiry();
    response_history();response_deadline_matrix();
    std::cout<<"Plan9 live receipts: "<<cases<<" cases, "<<checks<<" checks, "<<failures<<" failures\n";
    return failures?1:0;
}
