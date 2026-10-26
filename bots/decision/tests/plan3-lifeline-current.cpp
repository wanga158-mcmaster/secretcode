// SDK-free analytical model tests. No observations, engine or native claims.
#include "../lifeline-common/viability.h"
#include <cstdio>
#include <cstdlib>

// Isolated WASI fixture only: unexpected allocation exceptions abort. This
// does not test native exception unwinding, turn-boundary catches or fuel.
extern "C" void* __cxa_allocate_exception(std::size_t size) {return std::malloc(size);}
extern "C" void __cxa_throw(void*,void*,void(*)(void*)) {std::abort();}

static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}} while(false)
using namespace plan3;
using namespace plan3_lifeline;
static constexpr Assumptions model{true,true};
static InformationState snapshot(int bag=10) {
    InformationState s;s.round=20;s.roundLimit=120;s.health=s.maximumHealth=100;
    s.carriedGems=bag;s.position.tile=7;s.winningGems=1000;return s;
}
static Outcome endpoint(int tau,int gems,double health=80,int bag=0) {
    Outcome o;o.tau=tau;o.deltaS=gems;o.healthLoss=100-health;
    o.terminal.health=health;o.terminal.maximumHealth=100;o.terminal.bag=bag;
    o.terminal.position.tile=8;o.terminal.preparationTurns=8;
    if(gems)o.conversions.push_back({tau,gems});
    return o;
}
static JobForecast cycle(int group,int gems,int tau=5) {
    JobForecast j;j.id={JobKind::cycle,group,-1,9,-1};j.campGems=gems;
    j.outcomes.push_back(endpoint(tau,gems));return j;
}
static JobForecast bank() {
    JobForecast j;j.id={JobKind::bank,-1,-1,9,-1};j.outcomes.push_back(endpoint(3,10,100));return j;
}
static Outcome fatal(int tau=65) {
    auto o=endpoint(tau,0,100);o.response=ResponseWorld::interception;
    o.healthLoss=100;o.terminal.position.tile=1;return o;
}
static void blank(const Result& r,Status status) {
    CHECK(r.status==status);CHECK(!r.active.present);CHECK(!r.control.present);
    CHECK(r.states.empty());CHECK(r.actions.empty());
}
int main() {
    const auto initial=snapshot();
    auto high=cycle(2,100);high.outcomes.push_back(fatal());
    const std::vector<JobForecast> fork{high,bank()};
    const auto result=evaluate(initial,fork,model);
    CHECK(result.status==Status::known);CHECK(result.states[0].reachable);
    CHECK(result.active.present && result.active.job==bank().id);
    CHECK(result.control.present && result.control.job==high.id);
    CHECK(result.active.score.converted==10 && result.active.score.occupied==3);
    CHECK(result.actions.size()==2 && !result.actions[0].admitted && result.actions[1].admitted);
    CHECK(result.actions[0].successorCount==2);
    const auto& death=result.states[result.actions[0].successors[1]];
    CHECK(death.health==100);CHECK(!death.safe && !death.target && !death.reachable);
    CHECK(result.states[0].rank==1);

    // Same high reward without a represented fatal continuation remains viable.
    high.outcomes.pop_back();auto quiet=evaluate(initial,{high,bank()},model);
    CHECK(quiet.active.job==high.id && quiet.control.job==high.id);
    auto early=high;early.outcomes[0].tau=101;early.outcomes[0].conversions[0].completedAt=101;
    auto late=evaluate(initial,{early,bank()},model);
    CHECK(late.status==Status::known && late.active.job==bank().id);
    CHECK(!late.actions[0].reactiveAllowed); // Common nominal deadline applies to both arms.
    auto disturbed=high;auto delayed=endpoint(101,100);delayed.response=ResponseWorld::conversion_denial;
    disturbed.outcomes.push_back(delayed);late=evaluate(initial,{disturbed,bank()},model);
    CHECK(late.active.job==bank().id && late.control.job==high.id);
    CHECK(!late.states[late.actions[0].successors[1]].safe);

    // Gold capture is not banking. A concrete two-hop recall/heal continuation
    // is needed, fully charged even when the retained potential says zero.
    auto gold=cycle(3,0);gold.campGold=5;gold.rewardOnlyEndpoint=true;
    auto empty=snapshot(0);empty.roundLimit=33;
    auto recovery=evaluate(empty,{gold},model);
    CHECK(recovery.active.present);CHECK(!recovery.states[2].target);
    CHECK(recovery.states[2].location==Location::field);
    CHECK(recovery.states[2].remaining==8 && recovery.states[2].recoveryTurns==8);
    CHECK(recovery.states[2].rank==1 && recovery.states[0].rank==2);
    empty.roundLimit=32;recovery=evaluate(empty,{gold},model);
    CHECK(recovery.status==Status::known && !recovery.active.present && recovery.control.present);
    CHECK(recovery.states[2].reason==Reason::no_certified_recovery);
    gold.outcomes[0].terminal.preparationTurns.reset();recovery=evaluate(snapshot(0),{gold},model);
    CHECK(!recovery.active.present && !recovery.states[2].target);
    gold.outcomes[0].terminal.preparationTurns=0;empty.roundLimit=25;
    recovery=evaluate(empty,{gold},model);CHECK(!recovery.active.present);
    empty.roundLimit=26;recovery=evaluate(empty,{gold},model);
    CHECK(recovery.active.present && recovery.states[2].recoveryTurns==1);

    JobForecast renew;renew.id={JobKind::renew,-1,-1,-1,-1};
    renew.outcomes.push_back(endpoint(8,0,100,10));renew.outcomes[0].terminal.position.tile=999;
    auto healed=evaluate(initial,{renew},model);
    CHECK(healed.active.present && healed.active.job==renew.id);
    CHECK(healed.states[2].target && healed.states[2].bag==10 && healed.states[2].credited==0);
    CHECK(healed.states[2].location==Location::spawn && healed.states[2].tile==-1);
    auto free_bank=renew;free_bank.outcomes[0].deltaS=10;free_bank.outcomes[0].conversions={{8,10}};
    blank(evaluate(initial,{free_bank},model),Status::invalid);
    auto lost_bag=renew;lost_bag.outcomes[0].terminal.bag=0;
    blank(evaluate(initial,{lost_bag},model),Status::invalid);

    // Health and time are conservative endpoint abstractions, not live facts.
    auto damaged=bank();damaged.outcomes[0].terminal.health=.9;
    auto low=evaluate(initial,{damaged},model);
    CHECK(low.status==Status::known && !low.active.present && low.control.present);
    CHECK(low.states[2].health==0 && !low.states[2].safe);
    // An explicit full-job minimum can certify survival through intermediate
    // recovery even when total damage exceeds the starting health.
    auto multi=cycle(4,20);multi.outcomes[0].healthLoss=150;multi.outcomes[0].minimumHealth=20;
    auto phases=evaluate(snapshot(0),{multi},model);
    CHECK(phases.status==Status::known && phases.active.present && phases.control.present);
    CHECK(phases.states[2].safe && phases.states[2].health==80);
    multi.outcomes[0].minimumHealth.reset();
    phases=evaluate(snapshot(0),{multi},model);
    CHECK(!phases.active.present && phases.control.present);
    CHECK(phases.states[2].reason==Reason::survival_unknown);
    multi.outcomes[0].minimumHealth=0;multi.outcomes[0].terminal.health=100;
    phases=evaluate(snapshot(0),{multi},model);
    CHECK(!phases.active.present && !phases.states[2].safe && phases.states[2].health==100);
    multi.outcomes[0].minimumHealth=20;multi.outcomes[0].terminal.health=0;
    phases=evaluate(snapshot(0),{multi},model);
    CHECK(!phases.active.present && !phases.states[2].safe); // Phase survival cannot repair a dead endpoint.
    multi.outcomes[0].terminal.health=80;
    for(const auto invalid:{-1.0,101.0,std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        multi.outcomes[0].minimumHealth=invalid;
        blank(evaluate(snapshot(0),{multi},model),Status::invalid);
    }
    multi.outcomes[0].minimumHealth=20;
    phases=evaluate(initial,{multi},model);CHECK(phases.active.present); // Credited gems preserve the bag.
    multi.outcomes[0].deltaS=0;multi.outcomes[0].conversions.clear();multi.outcomes[0].terminal.bag=9;
    phases=evaluate(initial,{multi},model);
    // Retained bag also makes the endpoint position unknown; neither failure
    // can be bypassed by a positive phase minimum.
    CHECK(!phases.active.present && phases.control.present && !phases.states[2].safe);
    CHECK(phases.states[2].bag<initial.carriedGems && phases.states[2].reason==Reason::position_unknown);
    auto unknown=high;unknown.complete=false;blank(evaluate(initial,{unknown,bank()},model),Status::unavailable);
    blank(evaluate(initial,fork,{}),Status::unavailable);
    blank(evaluate(initial,fork,{true,false}),Status::unavailable);
    auto wrong=initial;wrong.health=std::numeric_limits<double>::quiet_NaN();
    blank(evaluate(wrong,fork,model),Status::invalid);
    wrong=initial;wrong.position.tile=-1;blank(evaluate(wrong,fork,model),Status::invalid);
    auto malformed=bank();malformed.outcomes[0].conversions.clear();
    blank(evaluate(initial,{malformed},model),Status::invalid);
    malformed=bank();malformed.outcomes[0].conversions[0].completedAt=0;
    blank(evaluate(initial,{malformed},model),Status::invalid);
    malformed=bank();malformed.outcomes[0].conversions[0].completedAt=4;
    blank(evaluate(initial,{malformed},model),Status::invalid);
    malformed=bank();malformed.outcomes[0].tau=0;
    blank(evaluate(initial,{malformed},model),Status::invalid);
    malformed=bank();malformed.projectedRenewalAndReturnTime=-1;
    blank(evaluate(initial,{malformed},model),Status::invalid);
    malformed=bank();malformed.outcomes[0].terminal.maximumHealth=101;
    blank(evaluate(initial,{malformed},model),Status::invalid);
    malformed=bank();malformed.id.kind=JobKind::observe;
    blank(evaluate(initial,{malformed},model),Status::unavailable);

    // Independent Boolean support oracle: nominal production is always safe;
    // each of three actions has two additional safe/fatal disturbances. All
    // 64 supports are checked without reusing the backward operator.
    for(unsigned support=0;support<64;++support) {
        std::vector<JobForecast> jobs;
        int best=-1;
        for(unsigned action=0;action<3;++action) {
            auto j=cycle(int(action)+1,int(action+1)*10,int(action)*2+3);
            for(unsigned disturbance=0;disturbance<2;++disturbance) {
                auto o=(support&(1u<<(2*action+disturbance)))?j.outcomes.front():fatal();
                o.response=static_cast<ResponseWorld>(disturbance+1);j.outcomes.push_back(o);
            }
            if((support&(3u<<(2*action)))==(3u<<(2*action)))best=int(action);
            jobs.push_back(j);
        }
        const auto r=evaluate(snapshot(0),jobs,model);
        CHECK(r.status==Status::known && r.control.present && r.control.job==jobs[2].id);
        CHECK(r.active.present==(best>=0));
        if(best>=0)CHECK(r.active.job==jobs[unsigned(best)].id);
        for(unsigned a=0;a<3;++a)CHECK(r.actions[a].admitted==((support&(3u<<(2*a)))==(3u<<(2*a))));
    }

    // Full supported canonical menu/world bounds, numeric ties, order
    // independence and stable-first duplicates; no eight-action truncation.
    std::vector<JobForecast> full;
    for(unsigned j=0;j<27;++j) {
        auto job=cycle(int(j)+1,10,3);
        if(j>=16 && j<24)job.id.kind=JobKind::double_cycle;
        if(j>=24 && j<26) {job=bank();job.id.altarTile=int(j);}
        if(j==26)job=renew;
        const auto o=job.outcomes[0];job.outcomes.clear();
        for(unsigned w=0;w<plan3::outcome_cap;++w) {
            auto world=o;world.response=static_cast<ResponseWorld>(w%4);world.lifecycle=int(w/4);
            job.outcomes.push_back(world);
        }
        full.push_back(job);
    }
    const auto whole=evaluate(initial,full,model);
    CHECK(whole.status==Status::known && whole.actions.size()==27 && whole.states.size()==326);
    CHECK(whole.active.job==full[0].id && whole.control.job==full[0].id);
    CHECK(whole.states[0].rank==1);
    for(const auto& a:whole.actions)CHECK(a.admitted && a.successorCount==12);
    std::reverse(full.begin(),full.end());const auto reversed=evaluate(initial,full,model);
    CHECK(reversed.active.job==whole.active.job && reversed.control.job==whole.control.job);
    auto duplicate=std::vector<JobForecast>{bank(),bank()};duplicate[1].complete=false;
    CHECK(evaluate(initial,duplicate,model).actions.size()==1);
    full.push_back(full.front());CHECK(evaluate(initial,full,model).omitted==1);
    full.push_back(full.front());blank(evaluate(initial,full,model),Status::invalid);
    std::vector<JobForecast> capped;
    for(unsigned j=0;j<28;++j)capped.push_back(cycle(int(j),j==27?1000:1));
    const auto truncated=evaluate(snapshot(0),capped,model);
    CHECK(truncated.status==Status::known && truncated.actions.size()==16 && truncated.omitted==12);
    CHECK(truncated.active.job.firstGroup==0);
    malformed=bank();malformed.outcomes.resize(13,malformed.outcomes[0]);
    blank(evaluate(initial,{malformed},model),Status::invalid);
    auto huge=cycle(1,std::numeric_limits<int>::max(),1);
    huge.projectedRenewalAndReturnTime=std::numeric_limits<int>::max()-1;
    auto bounded=evaluate(snapshot(0),{huge},model);
    CHECK(bounded.status==Status::known && bounded.active.score.converted==unsigned(std::numeric_limits<int>::max()));
    CHECK(bounded.active.score.occupied==unsigned(std::numeric_limits<int>::max()));
    huge.projectedRenewalAndReturnTime=std::numeric_limits<int>::max();
    blank(evaluate(snapshot(0),{huge},model),Status::invalid);

    // Every interrupted work unit discards all choices and model publication.
    for(unsigned cut=0;cut<result.work;++cut) {
        unsigned calls=0;
        auto r=evaluate(initial,fork,model,[&] {return calls++<cut;});
        blank(r,Status::cutoff);CHECK(r.work==cut+1);
    }
    CHECK(evaluate(initial,fork,model).active.job==result.active.job);
    std::printf("lifeline current PASS %u checks; finite conditional model only\n",checks);
}
