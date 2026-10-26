#include "../signaller-common/policy.h"
#include <cstdio>
#include <cstdlib>

extern "C" void* __cxa_allocate_exception(std::size_t size) {return std::malloc(size);}
extern "C" void __cxa_throw(void*,void*,void(*)(void*)) {std::abort();}
using namespace plan3_signaller;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}} while(false)
static Input game() {
    Input input;input.jobCount=2;
    input.jobs[0].key={0,11,-1,9,-1};input.jobs[1].key={0,88,-1,9,-1};
    for(auto& row:input.jobs) {row.known=true;row.response=input.common;}
    return input;
}
static void oracle(const Input& input,const Result& result) {
    // Direct rational definition, including unequal ticket denominators.
    for(bool control:{false,true}) {
        bool any=false;std::int64_t best=0;unsigned bestDenom=1;Key selected{};
        for(unsigned j=0;j<input.jobCount;++j) {
            const auto& row=input.jobs[j];const auto& weights=control?input.common:row.response;
            unsigned denominator=0;std::int64_t numerator=0;
            for(unsigned r=0;r<4;++r) {denominator+=weights[r];numerator+=row.ownOutput[r]*weights[r];}
            numerator-=row.additionalCost*denominator;
            if(!any || numerator*bestDenom>best*denominator ||
               (numerator*bestDenom==best*denominator && row.key<selected)) {
                any=true;best=numerator;bestDenom=denominator;selected=row.key;
            }
        }
        const auto& choice=control?result.control:result.active;
        CHECK(choice.present==any);
        if(any)CHECK(choice.key==selected && choice.value.numerator*bestDenom==best*choice.value.denominator);
    }
}
static plan3::InformationState info() {
    plan3::InformationState info;info.round=10;info.roundLimit=210;info.health=info.maximumHealth=100;
    info.position.tile=10;info.winningGems=1000;
    for(int tile:{11,88}) {plan3::GroupObservation group;group.minimumSpawnTile=tile;info.groups.push_back(group);}
    return info;
}
static plan3::JobForecast forecast(int target,const std::array<int,4>& values) {
    plan3::JobForecast job;job.id={plan3::JobKind::cycle,target,-1,9,-1};job.campGems=values[0];
    for(unsigned r=0;r<4;++r) {
        plan3::Outcome outcome;outcome.response=static_cast<plan3::ResponseWorld>(r);outcome.tau=1;outcome.deltaS=values[r];
        if(values[r])outcome.conversions={{1,values[r]}};
        outcome.terminal.health=outcome.terminal.maximumHealth=100;outcome.terminal.position.tile=8;
        job.outcomes.push_back(outcome);
    }
    return job;
}
int main() {
    auto input=game();input.jobs[0].ownOutput={10,0,0,0};input.jobs[0].response={12,2,1,1};
    input.jobs[1].ownOutput={12,12,0,12};input.jobs[1].response={2,4,8,2};
    const auto fork=solve(input);oracle(input,fork);
    CHECK(fork.state==State::known && fork.active.key==input.jobs[0].key && fork.control.key==input.jobs[1].key);
    CHECK(fork.active.value.numerator==120 && fork.active.value.denominator==16);
    CHECK(fork.control.value.numerator==168);
    const std::array<Weights,3> weights{Weights{1,1,1,1},Weights{12,2,1,1},Weights{2,4,8,2}};
    for(unsigned mask=0;mask<256;++mask)for(unsigned a=0;a<3;++a)for(unsigned b=0;b<3;++b) {
        auto small=game();
        for(unsigned j=0;j<2;++j)for(unsigned r=0;r<4;++r)small.jobs[j].ownOutput[r]=(mask>>(j*4+r))&1;
        small.jobs[0].response=weights[a];small.jobs[1].response=weights[b];
        if(mask&1)small.jobs[0].additionalCost=1;
        oracle(small,solve(small));
    }
    auto same=input;for(auto& row:same.jobs)row.response=same.common;
    CHECK(solve(same).active.key==solve(same).control.key);
    auto ties=game();CHECK(solve(ties).active.key==ties.jobs[0].key);
    std::swap(ties.jobs[0],ties.jobs[1]);CHECK(solve(ties).active.key==Key({0,11,-1,9,-1}));
    Input full;full.jobCount=28;full.common={64,0,0,0};
    for(unsigned j=0;j<28;++j) {
        auto& row=full.jobs[j];row.known=true;row.key={0,int(j),-1,9,-1};row.response={1,0,0,0};
        row.ownOutput={maximum_value,maximum_value,maximum_value,maximum_value};row.additionalCost=maximum_value;
    }
    CHECK(solve(full).state==State::known);oracle(full,solve(full));
    auto malformed=game();malformed.jobs[0].known=false;
    CHECK(solve(malformed).state==State::unavailable && !solve(malformed).active.present);
    malformed.jobs[1].additionalCost=-1;CHECK(solve(malformed).state==State::invalid);
    malformed=game();malformed.jobs[0].response={0,0,0,0};CHECK(solve(malformed).state==State::invalid);
    malformed=game();malformed.common={65,0,0,0};CHECK(solve(malformed).state==State::invalid);
    malformed=game();malformed.jobs[1].key=malformed.jobs[0].key;CHECK(solve(malformed).state==State::invalid);
    malformed=game();malformed.jobs[0].ownOutput[0]=maximum_value+1;CHECK(solve(malformed).state==State::invalid);
    CHECK(solve(Input{}).state==State::infeasible);
    for(unsigned cut=0;cut<fork.work;++cut) {
        unsigned seen=0;const auto denied=solve(input,[&]{return seen++<cut;});
        CHECK(denied.state==State::cutoff && !denied.active.present && !denied.control.present);
    }
    const Geometry geometry{10,10,0,99};CHECK(valid(geometry));
    CHECK(classify(11,geometry,2)==Signal::home && classify(88,geometry,2)==Signal::forward);
    CHECK(classify(44,geometry,2)==Signal::contested);
    const Geometry reflected{10,10,99,0};
    CHECK(classify(88,reflected,2)==Signal::home && classify(11,reflected,2)==Signal::forward);
    auto snapshot=info();std::vector<plan3::JobForecast> jobs{forecast(11,{10,0,0,0}),forecast(88,{12,12,0,12})};
    const auto mapped=map(snapshot,jobs,geometry);CHECK(mapped.state==State::known && mapped.input.jobCount==2);
    CHECK(mapped.input.jobs[0].signal==Signal::home && mapped.input.jobs[1].signal==Signal::forward);
    CHECK(mapped.input.jobs[0].additionalCost==0 && mapped.input.jobs[1].additionalCost==0);
    const auto actual=solve(mapped.input);oracle(mapped.input,actual);
    CHECK(actual.active.key==key(jobs[0].id) && actual.control.key==key(jobs[1].id));
    Model ignored;ignored.assumeObserved=false;const auto noResponse=solve(map(snapshot,jobs,geometry,ignored).input);
    CHECK(noResponse.active.key==noResponse.control.key);
    auto unknown=snapshot;unknown.groups.pop_back();auto missing=map(unknown,jobs,geometry);
    CHECK(missing.omittedUnknown==1 && missing.input.jobCount==1);
    unknown=snapshot;unknown.groups.push_back(unknown.groups[0]);CHECK(map(unknown,jobs,geometry).omittedUnknown==1);
    auto incomplete=jobs;incomplete[0].outcomes.pop_back();CHECK(map(snapshot,incomplete,geometry).omittedUnknown==1);
    CHECK(map(snapshot,jobs,{}).state==State::invalid);
    auto bank=forecast(-1,{10,10,10,10});bank.id.kind=plan3::JobKind::bank;snapshot.carriedGems=10;
    Model nonmessage;nonmessage.reaction[0]={1,0,0,0};const auto banking=map(snapshot,{bank},geometry,nonmessage);
    CHECK(banking.input.jobCount==1 && banking.input.jobs[0].signal==Signal::none && banking.input.jobs[0].response==nonmessage.common);
    snapshot.carriedGems=0;
    unsigned calls=0;map(snapshot,jobs,geometry,{},[&]{++calls;return true;});
    for(unsigned cut=0;cut<calls;++cut) {
        unsigned seen=0;const auto denied=map(snapshot,jobs,geometry,{},[&]{return seen++<cut;});
        CHECK(denied.state==State::cutoff && denied.input.jobCount==0);
    }

    Planner planner;planner.geometry(geometry);
    const auto opening=planner.preview(false,snapshot,jobs);const auto companion=planner.preview(true,snapshot,jobs);
    CHECK(opening.intent.job==jobs[0].id && opening.token==1 && companion.intent.job==jobs[1].id && companion.token==0);
    plan3::PolicyEvent event;event.token=1;event.job=*opening.intent.job;event.startedRound=event.round=snapshot.round;
    auto wrong=event;wrong.job=jobs[1].id;CHECK(!planner.observe(wrong));
    CHECK(planner.observe(event));
    plan3::ControllerContext context;context.activeJob=opening.intent.job;
    CHECK(planner.preview(false,snapshot,jobs,context).intent.status==plan3::IntentStatus::preserve);
    planner.motion({true,true,true,snapshot.round,10,10});CHECK(planner.statistics().targetwardOpenings==0);
    planner.motion({true,true,true,snapshot.round+1,10,11});CHECK(planner.statistics().targetwardOpenings==1);
    planner.motion({true,true,true,snapshot.round+2,11,12});CHECK(planner.statistics().targetwardOpenings==1);
    event.kind=plan3::PolicyEvent::Kind::completed;event.round+=5;CHECK(planner.observe(event));
    CHECK(planner.statistics().unwitnessedEndpoints==0 && planner.statistics().censored==1 && planner.statistics().exactSamples==0);
    CHECK(!planner.observe(event));event.token=0;CHECK(planner.observe(event));
    // Unknown/changed-round/death samples never become an emission witness.
    for(unsigned variant=0;variant<4;++variant) {
        Planner bounded;bounded.geometry(geometry);const auto choice=bounded.preview(false,snapshot,jobs);
        event={};event.token=1;event.job=*choice.intent.job;event.startedRound=event.round=snapshot.round;
        CHECK(bounded.observe(event));
        Motion motion{true,true,true,snapshot.round,10,11};
        if(variant==0)motion.valid=false;
        if(variant==1)motion.round=snapshot.round-1;
        if(variant==2)motion.aliveAfter=false;
        if(variant==3)motion.before=0;
        bounded.motion(motion);bounded.motion({true,true,true,snapshot.round+1,10,11});
        CHECK(bounded.statistics().motionUnknown==1 && bounded.statistics().targetwardOpenings==0);
        event.kind=plan3::PolicyEvent::Kind::aborted;event.round+=3;CHECK(bounded.observe(event));
        CHECK(bounded.statistics().unwitnessedEndpoints==1);
    }
    Planner control(true);control.geometry(geometry);const auto controlOpening=control.preview(true,snapshot,jobs);
    control.preview(false,snapshot,jobs);event={};event.token=1;event.job=*controlOpening.intent.job;event.startedRound=event.round=snapshot.round;
    CHECK(control.observe(event));control.motion({true,true,true,snapshot.round,10,20});
    CHECK(control.statistics().otherOpenings==1 && control.statistics().targetwardOpenings==0);
    std::printf("signaller current PASS %u checks;2304 exact response games, visibility/reaction assumed\n",checks);
}
