#include "../common/policy.h"
#include <cstdio>
#include <cstdlib>
using namespace plan3;
static int checks=0;
static void check(bool value) {++checks;if(!value)std::abort();}
struct Policy {JobIntent result;int calls=0;bool arm=false;};
static JobIntent choose(void* raw,bool control,const InformationState&,
    const std::vector<JobForecast>&,const ControllerContext&) {
    auto& policy=*static_cast<Policy*>(raw);++policy.calls;policy.arm=control;return policy.result;
}
static JobForecast job(int key) {
    JobForecast j;j.id={JobKind::cycle,key,-1,5,-1};
    Outcome o;o.tau=5;o.deltaS=10;o.terminal.health=o.terminal.maximumHealth=100;
    j.outcomes.push_back(o);return j;
}
int main() {
    InformationState info;info.roundLimit=200;info.winningGems=100;
    info.health=info.maximumHealth=100;
    std::vector<JobForecast> jobs{job(1),job(2)};ControllerContext context;
    Policy policy{{IntentStatus::selected,jobs[1].id,42,DecisionReason::objective}};
    auto run=[&](bool control=false){return select_policy(choose,&policy,control,info,jobs,context);};
    check(run().job==jobs[1].id);check(policy.calls==1);check(run(true).objective==42);check(policy.arm);
    policy.result.job=job(99).id;check(run().status==IntentStatus::yield);
    policy.result.job=jobs[1].id;jobs[1].routeKnown=false;check(run().status==IntentStatus::yield);
    jobs[1].routeKnown=true;policy.result.objective=std::numeric_limits<double>::infinity();
    check(run().status==IntentStatus::yield);policy.result.objective=42;
    policy.result.status=IntentStatus::preserve;check(run().status==IntentStatus::yield);
    context.activeJob=jobs[1].id;check(run().status==IntentStatus::preserve);
    context.sharedAbort=true;check(run().status==IntentStatus::yield);context.sharedAbort=false;
    context.menuComplete=false;const int oldCalls=policy.calls;
    check(run().reason==DecisionReason::incomplete_menu);check(policy.calls==oldCalls);
    context.menuComplete=true;context.protectedChannelOrCapture=true;
    check(run().reason==DecisionReason::protected_obligation);check(policy.calls==oldCalls);
    context={};policy.result.status=IntentStatus::selected;
    auto bank=job(3);bank.id.kind=JobKind::bank;bank.outcomes[0].deltaS=100;
    jobs.push_back(bank);info.carriedGems=100;context.newlyFeasibleWinningBank=true;
    check(run().reason==DecisionReason::immediate_winning_conversion);check(policy.calls==oldCalls);
    info.carriedGems=0;context={};jobs.pop_back();
    auto recovery=job(4);recovery.id.kind=JobKind::renew;recovery.mandatorySafety=true;
    jobs.push_back(recovery);check(run().reason==DecisionReason::mandatory_recovery);
    check(policy.calls==oldCalls);jobs.pop_back();
    for(auto& candidate:jobs) {
        candidate.rewardOnlyEndpoint=true;candidate.campGold=10;
        candidate.outcomes[0].deltaS=0;
    }
    check(run().reason==DecisionReason::gold_fallback);check(policy.calls==oldCalls);
    auto purchase=job(5);purchase.id.kind=JobKind::invest;purchase.outcomes[0].deltaS=0;
    purchase.investment.available=purchase.investment.exactEquipmentReconstruction=true;
    purchase.investment.output={10,20,30};jobs.push_back(purchase);
    policy.result={IntentStatus::selected,purchase.id,10,DecisionReason::objective,false,false,77};
    const auto invested=select_policy(choose,&policy,false,info,jobs,context,Controller::treasury);
    check(invested.job==purchase.id);check(invested.policyToken==77);
    check(run().reason==DecisionReason::gold_fallback);
    jobs={job(1),job(2)};
    policy.result.status=IntentStatus::yield;check(!run().job);check(run().objective==0);
    check(run().policyToken==0);
    jobs.clear();for(int key=0;key<17;++key)jobs.push_back(job(key));
    policy.result={IntentStatus::selected,jobs.back().id,1,DecisionReason::objective};
    const auto truncated=run();check(truncated.status==IntentStatus::yield);check(truncated.candidateTruncated);
    check(select_policy(nullptr,nullptr,false,info,jobs,{}).status==IntentStatus::selected);
    std::printf("policy guards: %d checks passed\n",checks);
}
