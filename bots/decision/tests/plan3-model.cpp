// Synthetic analytical invariants only. These do not stand in for release
// match parity, personally observed timing, or a predictive-accuracy audit.
#include "../common/model.h"
#include "../common/model-capital.h"
#include <cstdio>
#include <cstdlib>
using namespace plan3;
static unsigned checks=0;
#define CHECK(e) do { ++checks; if (!(e)) { std::fprintf(stderr,"check %u failed at %d: %s\n",checks,__LINE__,#e); std::exit(1); } } while(false)
static bool close(double a,double b) { return std::abs(a-b)<1e-8; }
static InformationState information() {
    InformationState i; i.round=100; i.roundLimit=2000; i.winningGems=1000;
    i.health=i.maximumHealth=1000; i.teamSize=3; return i;
}
static JobForecast job(int group,int gems,int duration) {
    JobForecast j; j.id={JobKind::cycle,group,-1,100,-1};
    Outcome n; n.deltaS=gems; n.tau=duration;
    n.terminal.health=n.terminal.maximumHealth=1000;
    n.conversions.push_back({duration,gems}); j.outcomes.push_back(n);
    j.campGems=gems; j.travelPlusClear=duration; return j;
}
static JobForecast renew() { auto j=job(-1,0,8); j.id.kind=JobKind::renew; return j; }
static void timing_and_accounting() {
    CHECK(tribute_completion_turns(1,.1,0,1)==1);
    CHECK(tribute_completion_turns(10,.1,0,1)==1);
    CHECK(tribute_completion_turns(11,.1,0,1)==2);
    CHECK(tribute_completion_turns(21,.2,2,3)==7); // queried settings, not bag/10
    CHECK(tribute_completion_turns(1,0,0,0)==1); // even zero ticks resolves at next owner start
    CHECK(!tribute_completion_turns(0,.1,0,1));
    CHECK(!tribute_completion_turns(1,-1,0,1));
    CHECK(!Distance{}.reachable());
    CHECK((!Distance{DistanceStatus::unreachable,0}.reachable()));
    CHECK(!Distance::known(-1).reachable());
    CHECK(Distance::known(0).reachable());
    auto clear=estimate_clear(500,20,100,0,1,1,1000,1000);
    CHECK(clear.turns==5 && close(clear.healthLoss,140) && clear.admitted);
    CHECK(!estimate_clear(500,200,100,0,1,1,1000,1000).admitted);
    CHECK(!estimate_clear(0,0,0,0,1,1,200,1000).admitted); // common full-clear rule
    CHECK(estimate_clear(0,0,0,0,1,1,250,1000).admitted);
    CHECK(!estimate_clear(std::nan(""),1,1,1,1,1,1000,1000).admitted);
    RenewalLoop l{7,10,30,24,true};
    CHECK(loop_yield(l,9)==0);
    CHECK(loop_yield(l,10)==24);
    CHECK(loop_yield(l,39)==24);
    CHECK(loop_yield(l,40)==48);
    CHECK(loop_yield({7,10,0,24,true},100)==0);
    CHECK(loop_yield({7,10,30,24,false},100)==0);
    CHECK(loop_yield({7,0,30,24,true},0)==0); // no free score in a healthy empty bag
    CHECK(close(renewal_rate({l}),.8));
    PotentialState p; p.health=p.maximumHealth=1000; p.loops={l};
    CHECK(close(potential(p,9,1000),0));
    CHECK(close(potential(p,10,1000),24));
    p.bag=13; // Retained old bag is credited only after its actual modeled cashout.
    CHECK(close(potential(p,100,1000),0)); // unknown cashout is not free
    p.cashoutTurns=5;
    CHECK(close(potential(p,4,1000),0));
    CHECK(close(potential(p,5,1000),13));
    CHECK(close(potential(p,14,1000),13));
    CHECK(close(potential(p,15,1000),37));
    p.cashoutTurns=0; CHECK(close(potential(p,100,1000),0));
    p.cashoutTurns=5;
    p.health=500;
    CHECK(close(potential(p,100,1000),13)); // unknown preparation, no future production
    p.preparationTurns=20;
    CHECK(close(potential(p,34,1000),13));
    CHECK(close(potential(p,35,1000),37));
    CHECK(close(potential(p,35,20),20));
    CHECK(close(potential(p,100,0),0));
    auto i=information(); i.ownPermanentScore=900;
    auto j=job(1,24,10); j.outcomes[0].terminal=p;
    j.outcomes[0].terminal.bag=0; // converted bag must not be in endpoint inventory too
    CHECK(outcome_value(i,j.outcomes[0])<=100);
    j.outcomes[0].deltaS=120;
    CHECK(close(outcome_value(i,j.outcomes[0]),120)); // winning overshoot, zero continuation
    j.outcomes[0].tau=140; j.outcomes[0].conversions={{20,24},{140,30}};
    CHECK(close(outcome_value(i,j.outcomes[0]),24)); // explicitly timed conversions only
    j.outcomes[0].conversions.clear();
    CHECK(close(outcome_value(i,j.outcomes[0]),0));
}
static void objective_and_tie_tests() {
    auto i=information(); auto a=job(3,24,20),b=job(2,30,20);
    a.projectedRenewalAndReturnTime=0; b.projectedRenewalAndReturnTime=20;
    CHECK(select(Controller::ledger,false,i,{a,b}).job==a.id);
    CHECK(select(Controller::ledger,true,i,{a,b}).job==b.id);
    CHECK(select(Controller::atlas,true,i,{a,b}).job==b.id);
    a.outcomes[0].terminal.loops={{3,10,10,20,true}};
    CHECK(select(Controller::atlas,false,i,{a,b}).job==a.id);
    auto tie=job(1,24,20);
    CHECK(select(Controller::ledger,false,i,{a,tie}).job==tie.id); // canonical, input order immaterial
    tie.outcomes[0].deltaS=24; tie.outcomes[0].tau=20;
    CHECK(select(Controller::ledger,false,i,{tie,a}).job==tie.id);
    a.routeKnown=false;
    CHECK(select(Controller::atlas,false,i,{a,b}).job==b.id);
    a.routeKnown=true; a.fullClearAdmitted=false;
    CHECK(select(Controller::atlas,false,i,{a,b}).job==b.id);
    auto late=job(0,999,129);
    CHECK(select(Controller::atlas,false,i,{late,b}).job==b.id);
    i.round=1990; CHECK(!admitted(i,b,false));
    i=information();
    a=job(1,20,20); b=job(2,21,20);
    a.personalDeadlineEvidence=true; a.personalReleaseEvidence=true; a.releaseTime=0;
    a.opponentDeadline=21; a.supportedDenialGems=20;
    CHECK(select(Controller::tempo,false,i,{a,b}).job==a.id);
    CHECK(select(Controller::tempo,true,i,{a,b}).job==b.id);
    a.releaseTime=22; CHECK(close(tempo_value(a,false),nominal_rate(a)));
    a.releaseTime.reset(); a.opponentDeadline=20;
    CHECK(close(tempo_value(a,false),nominal_rate(a)));
    a.id.kind=JobKind::bank; a.opponentDeadline=21;
    CHECK(close(tempo_value(a,false),nominal_rate(a))); // old bag is not denied reward
    ControllerContext c; c.activeJob=b.id;
    CHECK(select(Controller::options,false,i,{a,b},c).status==IntentStatus::preserve);
    CHECK(select(Controller::options,true,i,{a,b},c).status==IntentStatus::selected);
    c.protectedChannelOrCapture=true;
    CHECK(select(Controller::atlas,false,i,{a,b},c).status==IntentStatus::preserve);
    c.sharedAbort=true;
    CHECK(select(Controller::options,false,i,{a,b},c).status==IntentStatus::selected);
    c={}; c.menuComplete=false; c.activeJob=b.id;
    CHECK(select(Controller::ledger,false,i,{a,b},c).status==IntentStatus::preserve);
    c.activeJob.reset();
    CHECK(select(Controller::ledger,false,i,{a,b},c).status==IntentStatus::yield);
    i.ownPermanentScore=980; i.carriedGems=24;
    auto winningBank=job(-1,24,3); winningBank.id.kind=JobKind::bank;
    auto oversized=job(5,224,80);
    c={}; c.newlyFeasibleWinningBank=true;
    CHECK(select(Controller::atlas,false,i,{winningBank,oversized},c).job==winningBank.id);
    c.activeJob=oversized.id; c.protectedChannelOrCapture=true;
    CHECK(select(Controller::options,false,i,{winningBank,oversized},c).status==IntentStatus::preserve);
    i=information();
    CHECK(select(Controller::ledger,false,i,{renew()}).job==renew().id);
    auto noOpRenewal=renew(); noOpRenewal.renewalRequired=false;
    CHECK(select(Controller::atlas,false,i,{noOpRenewal}).status==IntentStatus::yield);
    noOpRenewal.mandatorySafety=true;
    CHECK(select(Controller::atlas,false,i,{noOpRenewal}).job==noOpRenewal.id);
    auto gold=job(7,0,12); gold.campGold=35; gold.rewardOnlyEndpoint=true;
    gold.outcomes[0].conversions.clear();
    CHECK(select(Controller::atlas,false,i,{gold,renew()}).job==gold.id);
    CHECK(select(Controller::atlas,false,i,{gold,renew()}).reason==DecisionReason::gold_fallback);
    CHECK(select(Controller::insurance,true,i,{gold,renew()}).reason==DecisionReason::gold_fallback);
    auto gems=job(8,1,100);
    CHECK(select(Controller::atlas,false,i,{gold,gems,renew()}).reason!=DecisionReason::gold_fallback);
    auto emergency=renew(); emergency.mandatorySafety=true;
    CHECK(select(Controller::atlas,false,i,{gold,emergency}).job==emergency.id);
    std::vector<JobForecast> many;
    for (int k=30;k>=0;--k) many.push_back(job(k,24,20));
    auto menu=canonical_menu(many);
    CHECK(menu.jobs.size()==16 && menu.truncated && menu.jobs[0]->id.firstGroup==0);
    CHECK(menu.jobs.back()->id.firstGroup==15);
    CHECK(close(lower_tail_mean({0,100},{.1,.9}),80)); // fractional .4 of high outcome
    CHECK(close(lower_tail_mean({0,10,20,30},{.25,.25,.25,.25}),5));
    CHECK(close(lower_tail_mean({30,0,20,10},{.25,.25,.25,.25}),5));
    auto risk=job(1,100,10),safe=job(2,60,10);
    risk.outcomes[0].assumedWeight=.5;
    auto loss=risk.outcomes[0]; loss.deltaS=0; loss.assumedWeight=.5;
    risk.outcomes.push_back(loss);
    CHECK(select(Controller::insurance,false,i,{risk,safe}).job==safe.id);
    safe.outcomes[0].deltaS=40;
    CHECK(select(Controller::insurance,true,i,{risk,safe}).job==risk.id);
    CHECK(select(Controller::robust,false,i,{risk,safe}).job==safe.id);
    CHECK(select(Controller::robust,true,i,{risk,safe}).job==risk.id);
    safe.outcomes[0].deltaS=0;
    CHECK(select(Controller::robust,false,i,{risk,safe,renew()}).job==renew().id);
}
static void belief_and_information() {
    BeliefState s;
    BeliefEvidence e; e.newlyVisibleHostileLock=true; e.observedTributeTransition=true;
    e.enemyMovedTowardPreviouslyFixedActorLocationTwice=true;
    CHECK(classify_event(e)==BeliefEvent::hostile_lock_during_tribute);
    belief_update(s,1,classify_event(e));
    CHECK(close(s.weights[3],.45)); CHECK(close(s.weights[0],11.0/60));
    auto before=s.weights;
    belief_update(s,2,BeliefEvent::two_sighting_pursuit);
    CHECK(s.weights==before); // repeated queries in same four-round block cannot compound evidence
    belief_update(s,17,BeliefEvent::none);
    CHECK(close(s.weights[3],.35));
    before=s.weights; belief_update(s,17,BeliefEvent::none); CHECK(s.weights==before);
    belief_update(s,49,BeliefEvent::none); CHECK(close(s.weights[3],.275));
    belief_update(s,50,BeliefEvent::shared_camp_progress,true);
    CHECK((s.weights==std::array<double,4>({.25,.25,.25,.25})));
    ObservationTable table{{{100,0},{0,100}},{.5,.5},{0,1},128,"fixed-table"};
    CHECK(close(*observation_net_value(table,5,2),43));
    table.labels={0,0}; CHECK(close(*observation_net_value(table,5,2),-7));
    table.labels={0,1}; table.jobOutcomeValues[1]={};
    CHECK(!observation_net_value(table,5,2));
    auto i=information(); auto a=job(1,30,10),b=job(2,40,10);
    auto observe=job(-1,0,1); observe.id.kind=JobKind::observe;
    observe.observationNetValue=10; observe.observationSafe=true; observe.observationRevealsTarget=true;
    CHECK(select(Controller::scout,false,i,{a,b,observe}).job==observe.id);
    CHECK(select(Controller::scout,true,i,{a,b,observe}).job==b.id);
    ControllerContext c; c.ownerTurn=20; c.lastObservationTurn=5;
    CHECK(select(Controller::scout,false,i,{a,b,observe},c).job==b.id);
    c.lastObservationTurn=4;
    CHECK(select(Controller::scout,false,i,{a,b,observe},c).job==observe.id);
    observe.observationRevealsTarget=false;
    CHECK(select(Controller::scout,false,i,{a,b,observe}).job==b.id);
    auto contested=job(1,100,10),steady=job(2,45,10);
    contested.outcomes.clear();
    for (unsigned w=0;w<4;++w) {
        auto o=steady.outcomes[0]; o.response=static_cast<ResponseWorld>(w); o.deltaS=w==0?100:30;
        contested.outcomes.push_back(o);
    }
    c={}; c.belief.weights={.9,.033333333333333,.033333333333333,.033333333333334};
    CHECK(select(Controller::belief,false,i,{contested,steady},c).job==contested.id);
    CHECK(select(Controller::belief,true,i,{contested,steady},c).job==contested.id); // uniform mean47.5
    steady.outcomes[0].deltaS=50;
    CHECK(select(Controller::belief,true,i,{contested,steady},c).job==steady.id);
}
static void allocation_capital_and_council() {
    auto allocation=market_allocate({2,1},2,{{0,1,10,20,true},{1,1,20,20,true},
                                                {0,2,120,30,true},{1,2,20,30,true}});
    CHECK(allocation.complete && allocation.consideredGroups==std::vector<int>({1,2}));
    CHECK(allocation.ownerOrdinals==std::vector<int>({0,1}));
    CHECK(allocation.actorLoads==std::vector<int>({10,20}));
    auto incremental=market_allocation_start({2,1},2);
    const std::vector<AllocationEdge> incrementalEdges{{0,1,10,20,true},{1,1,20,20,true},
                                                       {0,2,120,30,true},{1,2,20,30,true}};
    MarketScan scan;
    CHECK(market_allocate_work(incremental,incrementalEdges,scan,1)==MarketWorkStatus::progress);
    CHECK(!incremental.complete && scan.nextEdge==1 && incremental.ownerOrdinals[0]==-1);
    unsigned scans=1;
    while (!incremental.complete && scans++<30)
        CHECK(market_allocate_work(incremental,incrementalEdges,scan,1)!=MarketWorkStatus::invalid);
    CHECK(incremental.complete && incremental.ownerOrdinals==allocation.ownerOrdinals);
    CHECK(incremental.actorLoads==allocation.actorLoads);
    CHECK(!market_allocate({1},2,{{0,1,10,20,true}},false).complete);
    CHECK(!market_allocate({1},2,{{0,1,10,20,true,false}}).complete);
    auto ties=market_allocate({1},2,{{1,1,10,20,true},{0,1,10,20,true}});
    CHECK(ties.ownerOrdinals[0]==0);
    auto goldOnly=market_allocate({1},2,{{0,1,10,0,true}});
    CHECK(goldOnly.ownerOrdinals[0]==-1);
    auto i=information(); auto owned=job(1,10,10),unowned=job(2,20,10); owned.owned=true;
    CHECK(select(Controller::market,false,i,{owned,unowned}).job==owned.id);
    CHECK(select(Controller::market,true,i,{owned,unowned}).job==unowned.id);
    ControllerContext c; c.turnsWithoutOwnedJob=8; c.allowOneUnownedJob=true;
    CHECK(select(Controller::market,false,i,{unowned},c).job==unowned.id);
    c.turnsWithoutOwnedJob=7;
    CHECK(select(Controller::market,false,i,{unowned,renew()},c).job==renew().id);
    auto investment=job(-1,0,20); investment.id.kind=JobKind::invest;
    investment.investment={true,true,{10,30,100},{20,25,80},false};
    auto economic=job(1,50,10);
    CHECK(select(Controller::treasury,false,i,{investment,economic}).job==investment.id);
    CHECK(select(Controller::treasury,true,i,{investment,economic}).job==economic.id);
    investment.investment.output[1]=20;
    CHECK(select(Controller::treasury,false,i,{investment,economic}).job==economic.id);
    InvestmentForecast f{true,true,{20,40,80},{20,40,80},false};
    InvestmentForecast g{true,true,{10,50,100},{20,40,80},false};
    auto chosen=best_investment_schedule({{InvestmentBlock::defer,f},
                                        {InvestmentBlock::power_focus_shard,g}});
    CHECK(chosen && chosen->block==InvestmentBlock::power_focus_shard);
    chosen=best_investment_schedule({{InvestmentBlock::defer,f},
                                    {InvestmentBlock::power_focus_shard,g}},true);
    CHECK(chosen && chosen->block==InvestmentBlock::defer);
    auto proposal1=job(1,20,10),proposal2=job(2,40,20);
    proposal1.proposerMask=1; proposal2.proposerMask=4;
    proposal2.outcomes[0].terminal.loops={{2,1,10,20,true}};
    CHECK(select(Controller::council,false,i,{proposal1,proposal2}).job==proposal2.id);
    CHECK(select(Controller::council,true,i,{proposal1,proposal2}).job==proposal1.id);
    proposal1.proposerMask=proposal2.proposerMask=0;
    CHECK(council_proposals(i,{proposal1,proposal2}).size()==1); // shared best-rate nominee only
    CHECK(select(Controller::council,false,i,{proposal1,proposal2}).job.has_value());
}
static void capital_accounting() {
    CapitalTiming t;
    t.horizon=128; t.scoreCapacity=1000;
    t.oldBag=10; t.cashoutAt=4; t.initialGold=30; t.blockCost=100;
    t.passiveGoldPerTurn=1; t.firstFundingCompletion=10; t.fundingPeriod=30;
    t.fundingGems=24; t.fundingGold=30; t.purchaseRecoveryReturn=5;
    t.postPurchaseLoops={{1,10,20,40,true}};
    auto result=capital_output(t);
    CHECK(result.feasible && result.fundingCycles==2 && result.readyAt==49);
    CHECK(close(result.remainingGold,39)); // 30cash+49passive+60earned-100spent
    CHECK(result.fundingConversions.size()==3); // old bag once, two new capture conversions
    CHECK(result.fundingConversions[0].completedAt==4 && result.fundingConversions[0].gems==10);
    CHECK(result.fundingConversions[1].completedAt==14 && result.fundingConversions[1].gems==24);
    CHECK(result.fundingConversions[2].completedAt==44 && result.fundingConversions[2].gems==24);
    CHECK(close(result.checkpoints[0],34)); // purchase after checkpoint grants no early output
    CHECK(close(result.checkpoints[1],98)); // one complete post-purchase conversion at59
    CHECK(close(result.checkpoints[2],218)); // four post-purchase conversions59,79,99,119
    t.scoreCapacity=70; CHECK(close(capital_output(t).checkpoints[2],70));
    t.initialGold=100; t.oldBag=0;
    result=capital_output(t);
    CHECK(result.feasible && result.fundingCycles==0 && result.fundingConversions.empty());
    CHECK(result.readyAt==5);
    CHECK(close(result.remainingGold,5));
    t.initialGold=0; t.passiveGoldPerTurn=0; t.fundingGold=0;
    CHECK(!capital_output(t).feasible);
    t.fundingGold=30; t.firstFundingCompletion=100; t.fundingPeriod=100;
    CHECK(!capital_output(t).feasible);
    t.initialGold=-1; CHECK(!capital_output(t).feasible);
}
static void horizon_tests() {
    auto i=information(); auto a=job(1,20,10),b=job(2,50,50);
    auto c=ControllerContext{};
    CHECK(select(Controller::horizon,false,i,{a,b},c).reason==DecisionReason::warmup_ledger);
    for (int k=0;k<4;++k) c.scoreHistory.push_back({k*32,(k+1)*32,32,16,16,true});
    CHECK(horizon_ready(c.scoreHistory));
    CHECK(!horizon_ready(c.scoreHistory,100));
    i.round=200;
    auto h=horizon_utility(i,a,c.scoreHistory); CHECK(h && !h->assumedShare);
    c.scoreHistory[0].personalCoverageComplete=false;
    h=horizon_utility(i,a,c.scoreHistory); CHECK(h && h->assumedShare);
    c.scoreHistory[2].endRound=95; CHECK(!horizon_ready(c.scoreHistory));
    // At a deadline, the chosen actor replaces only its own estimated share.
    i.round=1900; i.roundLimit=2000; i.ownPermanentScore=900; i.opponentPermanentScore=910;
    c.scoreHistory.clear();
    for (int k=0;k<4;++k) c.scoreHistory.push_back({k*32,(k+1)*32,32,32,8,true});
    a=job(1,30,10); b=job(2,70,90);
    auto ha=horizon_utility(i,a,c.scoreHistory),hb=horizon_utility(i,b,c.scoreHistory);
    CHECK(ha && hb && ha->winPayoff>hb->winPayoff);
    CHECK(select(Controller::horizon,false,i,{a,b},c).job==a.id);
    // Exact terminal gems remain unresolved because enemy lifetime gold/kills
    // are unknown; lower payoff, never a fabricated win from current lead.
    i.ownPermanentScore=i.opponentPermanentScore=0; i.winningGems=100000;
    for (auto& block:c.scoreHistory) { block.ownIncrement=0; block.opponentIncrement=0; }
    a=job(1,0,5); h=horizon_utility(i,a,c.scoreHistory);
    CHECK(h && h->winPayoff==-1 && h->terminalMargin==0);
}
int main() {
    timing_and_accounting(); objective_and_tie_tests(); belief_and_information();
    allocation_capital_and_council(); horizon_tests(); capital_accounting();
    std::printf("Plan 3: %u synthetic model/accounting/controller checks passed (no engine games).\n",checks);
    return 0;
}
