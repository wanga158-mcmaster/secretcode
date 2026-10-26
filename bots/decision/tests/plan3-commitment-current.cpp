#include "../commitment-common/policy.h"
#include <cstdio>
#include <cstdlib>

extern "C" void* __cxa_allocate_exception(std::size_t size) {return std::malloc(size);}
extern "C" void __cxa_throw(void*,void*,void(*)(void*)) {std::abort();}
using namespace plan3_commitment;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}} while(false)
static Input game(unsigned rows=2,unsigned responses=2) {
    Input input;input.jobCount=rows;input.responseCount=responses;
    for(unsigned j=0;j<rows;++j) {
        input.jobs[j].key={0,int(j+1),-1,9,-1};
        for(unsigned r=0;r<responses;++r)input.jobs[j].cells[r]={Knowledge::known,0,0};
    }
    return input;
}
static void blank(const Result& result,State state) {
    CHECK(result.state==state && result.rowCount==0 && !result.active.present && !result.control.present);
}
static void oracle(const Input& input,const Result& result) {
    // Direct enumerate-all-best-responses definition, not the streaming solver.
    bool any=false;std::int64_t best=0,controlBest=0;Key chosen{},controlChosen{};
    for(unsigned j=0;j<input.jobCount;++j) {
        bool usable=false;std::int64_t pessimistic=0,minimum=0;unsigned mask=0;
        const auto penalty=input.previous && input.jobs[j].key!=*input.previous?input.switchCost:0;
        for(unsigned r=0;r<input.responseCount;++r) {
            const auto& cell=input.jobs[j].cells[r];if(cell.state!=Knowledge::known)continue;
            const auto net=cell.leader-penalty;
            if(!usable || net<minimum)minimum=net;
            usable=true;
            bool followerBest=true;
            for(unsigned q=0;q<input.responseCount;++q)
                if(input.jobs[j].cells[q].state==Knowledge::known && input.jobs[j].cells[q].follower>cell.follower)
                    followerBest=false;
            if(followerBest) {
                if(!mask || net<pessimistic)pessimistic=net;
                mask|=1u<<r;
            }
        }
        CHECK(result.rows[j].feasible==usable);
        if(!usable)continue;
        CHECK(result.rows[j].bestResponses==mask && result.rows[j].pessimistic==pessimistic && result.rows[j].minimax==minimum);
        if(!any || pessimistic>best || (pessimistic==best && input.jobs[j].key<chosen)) {
            best=pessimistic;chosen=input.jobs[j].key;
        }
        if(!any || minimum>controlBest || (minimum==controlBest && input.jobs[j].key<controlChosen)) {
            controlBest=minimum;controlChosen=input.jobs[j].key;
        }
        any=true;
    }
    CHECK(result.active.present==any && result.control.present==any);
    CHECK(result.state==(any?State::known:State::infeasible));
    if(any) {
        CHECK(result.active.key==chosen && result.active.value==best);
        CHECK(result.control.key==controlChosen && result.control.value==controlBest);
    }
}
static plan3::JobForecast forecast(int group,const std::array<int,4>& values) {
    plan3::JobForecast job;job.id={plan3::JobKind::cycle,group,-1,9,-1};job.campGems=values[0];
    for(unsigned r=0;r<4;++r) {
        plan3::Outcome outcome;outcome.response=static_cast<plan3::ResponseWorld>(r);outcome.tau=1;
        outcome.deltaS=values[r];if(values[r])outcome.conversions={{1,values[r]}};
        outcome.terminal.health=outcome.terminal.maximumHealth=100;outcome.terminal.position.tile=8;
        job.outcomes.push_back(outcome);
    }
    return job;
}
int main() {
    auto input=game();input.jobs[0].cells={Cell{Knowledge::known,10,0},Cell{Knowledge::known,7,-1}};
    input.jobs[1].cells={Cell{Knowledge::known,8,0},Cell{Knowledge::known,8,-1}};
    const auto fork=solve(input);oracle(input,fork);
    CHECK(fork.active.key==input.jobs[0].key && fork.control.key==input.jobs[1].key);
    // Follower indifference must be adverse to the leader, not an optimistic
    // tie or a selection of one convenient response.
    input.jobs[0].cells={Cell{Knowledge::known,100,10},Cell{Knowledge::known,0,10}};
    input.jobs[1].cells={Cell{Knowledge::known,50,10},Cell{Knowledge::known,40,0}};
    const auto tied=solve(input);oracle(input,tied);
    CHECK(tied.rows[0].bestResponses==3 && tied.rows[0].pessimisticResponse==1);
    CHECK(tied.active.key==input.jobs[1].key); // optimistic follower tie would choose row0/100.

    // All3^8 two-by-two integer payoff matrices, plus an explicit switch cost.
    for(unsigned code=0;code<6561;++code) {
        auto small=game();unsigned digits=code;
        for(unsigned j=0;j<2;++j)for(unsigned r=0;r<2;++r) {
            small.jobs[j].cells[r].leader=int(digits%3)-1;digits/=3;
        }
        for(unsigned j=0;j<2;++j)for(unsigned r=0;r<2;++r) {
            small.jobs[j].cells[r].follower=int(digits%3)-1;digits/=3;
        }
        if(code&1) {small.previous=small.jobs[1].key;small.switchCost=1;}
        oracle(small,solve(small));
    }
    for(unsigned mask=0;mask<16;++mask) {
        auto available=game();
        for(unsigned j=0;j<2;++j)for(unsigned r=0;r<2;++r)
            if(!(mask&(1u<<(j*2+r))))available.jobs[j].cells[r]={Knowledge::unavailable,0,0};
        oracle(available,solve(available));
    }
    auto full=game(28,4);full.previous=Key{0,99,-1,9,-1};full.switchCost=payoff_bound;
    for(auto& row:full.jobs)for(auto& cell:row.cells)cell={Knowledge::known,-payoff_bound,payoff_bound};
    const auto maximum=solve(full);oracle(full,maximum);
    CHECK(maximum.active.key==full.jobs[0].key && maximum.active.value==-2*payoff_bound);
    std::swap(full.jobs[0],full.jobs[27]);CHECK(solve(full).active.key==maximum.active.key);
    auto malformed=game();malformed.jobs[0].cells[0].state=Knowledge::unknown;
    blank(solve(malformed),State::unavailable);
    malformed.jobs[1].cells[1].leader=payoff_bound+1;blank(solve(malformed),State::invalid);
    malformed=game();malformed.jobs[0].cells[0]={Knowledge::unavailable,1,0};blank(solve(malformed),State::invalid);
    malformed=game();malformed.jobs[1].key=malformed.jobs[0].key;blank(solve(malformed),State::invalid);
    malformed=game();malformed.switchCost=-1;blank(solve(malformed),State::invalid);
    malformed=game();malformed.responseCount=0;blank(solve(malformed),State::invalid);
    malformed=game();malformed.jobCount=29;blank(solve(malformed),State::invalid);
    CHECK(solve(game(0)).state==State::infeasible);
    for(unsigned cut=0;cut<fork.work;++cut) {
        unsigned calls=0;auto sample=game();
        sample.jobs[0].cells={Cell{Knowledge::known,10,0},Cell{Knowledge::known,7,-1}};
        sample.jobs[1].cells={Cell{Knowledge::known,8,0},Cell{Knowledge::known,8,-1}};
        blank(solve(sample,[&]{return calls++<cut;}),State::cutoff);
    }

    plan3::InformationState info;info.round=10;info.roundLimit=210;info.health=info.maximumHealth=100;
    info.position.tile=7;info.winningGems=1000;
    std::vector<plan3::JobForecast> jobs{forecast(1,{10,9,7,9}),forecast(2,{8,8,8,8})};
    const auto mapped=map(info,jobs);CHECK(mapped.state==State::known && mapped.input.jobCount==2);
    const auto actual=solve(mapped.input);oracle(mapped.input,actual);
    CHECK(actual.active.key==key(jobs[0].id) && actual.control.key==key(jobs[1].id));
    CHECK(mapped.input.jobs[0].cells[0].leader==10240 && mapped.input.jobs[0].cells[2].follower==-1024);
    auto deny=jobs;deny[0]=forecast(1,{10,0,0,0});
    CHECK(solve(map(info,deny).input).active.key==key(jobs[1].id));
    CHECK(plan3::select(plan3::Controller::ledger,false,info,deny).job==jobs[0].id); // Not renamed Ledger.
    auto missing=jobs;missing[0].outcomes.pop_back();auto unknown=map(info,missing);
    CHECK(unknown.state==State::known && unknown.input.jobCount==1 && unknown.omittedUnknown==1);
    auto mismatched=jobs;mismatched[0].outcomes[1].conditionalLifecycleWeight=.5;
    CHECK(map(info,mismatched).omittedUnknown==1);
    auto duplicate=jobs;duplicate[0].outcomes.push_back(duplicate[0].outcomes[0]);
    CHECK(map(info,duplicate).omittedUnknown==1);
    auto numeric=jobs;numeric[0].outcomes[0].terminal.preparationTurns=INT32_MAX;
    CHECK(map(info,numeric).omittedUnknown==1);
    numeric=jobs;numeric[0].outcomes[0].conversions.clear();CHECK(map(info,numeric).omittedUnknown==1);
    unsigned mapCalls=0;map(info,jobs,{},std::nullopt,[&]{++mapCalls;return true;});
    for(unsigned cut=0;cut<mapCalls;++cut) {
        unsigned seen=0;const auto denied=map(info,jobs,{},std::nullopt,[&]{return seen++<cut;});
        CHECK(denied.state==State::cutoff && denied.input.jobCount==0);
    }

    Planner planner;const auto chosen=planner.preview(false,info,jobs);
    const auto companion=planner.preview(true,info,jobs);
    CHECK(chosen.state==State::known && chosen.intent.job==jobs[0].id && chosen.token==1);
    CHECK(companion.intent.job==jobs[1].id && companion.token==0);
    plan3::PolicyEvent event;event.job=*chosen.intent.job;event.token=chosen.token;event.startedRound=event.round=info.round;
    auto bad=event;bad.job=jobs[1].id;CHECK(!planner.observe(bad) && !planner.pending());
    CHECK(planner.observe(event) && planner.pending());
    plan3::ControllerContext context;context.activeJob=chosen.intent.job;
    auto changed=jobs;changed[1]=forecast(2,{1000,1000,1000,1000});
    const auto held=planner.preview(false,info,changed,context);
    CHECK(held.intent.status==plan3::IntentStatus::preserve && held.intent.job==chosen.intent.job);
    context.sharedAbort=true;CHECK(planner.preview(false,info,jobs,context).state==State::invalid);
    event.kind=plan3::PolicyEvent::Kind::aborted;event.round+=5;
    CHECK(planner.observe(event) && !planner.pending());
    CHECK(planner.statistics().abortedElapsed==5 && planner.statistics().censored==1 && planner.statistics().exactSamples==0);
    CHECK(!planner.observe(event));
    event.token=0;CHECK(planner.observe(event)); // Shared jobs never acquire an expert/commitment episode.
    info.round+=6;std::vector<plan3::JobForecast> nextJobs{jobs[1]};
    const auto next=planner.preview(false,info,nextJobs);
    event={};event.token=next.token;event.job=*next.intent.job;event.startedRound=event.round=info.round;
    CHECK(event.token==2 && planner.observe(event));
    CHECK(planner.statistics().switches==1 && planner.statistics().assumedSwitchUnits==1024);
    event.kind=plan3::PolicyEvent::Kind::completed;event.round+=4;event.conversion=true;event.exactConverted=8;
    CHECK(planner.observe(event));CHECK(planner.statistics().exactConverted==8 && planner.statistics().exactSamples==1);
    Planner control(true);const auto fixed=control.preview(true,info,jobs);
    CHECK(fixed.intent.job==jobs[1].id && fixed.token==1);
    control.preview(false,info,jobs);event={};event.token=1;event.job=*fixed.intent.job;event.startedRound=event.round=info.round;
    CHECK(control.observe(event));event.kind=plan3::PolicyEvent::Kind::completed;
    CHECK(control.observe(event) && control.statistics().censored==1); // Same-round missing reward stays unknown.
    std::printf("commitment current PASS %u checks;6561 exact games, assumed opponent proxy only\n",checks);
}
