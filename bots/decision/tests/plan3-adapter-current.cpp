#include "../adapter-common/feedback.h"
#include <cstdio>
#include <cstdlib>

// Test-only WASI exceptions abort; no native catch/unwind claim.
extern "C" void* __cxa_allocate_exception(std::size_t size) {return std::malloc(size);}
extern "C" void __cxa_throw(void*,void*,void(*)(void*)) {std::abort();}
using namespace plan3_adapter;
static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::abort();}} while(false)
static bool near(double a,double b) {return std::abs(a-b)<1e-12;}
static const JobKey job{0,7,-1,9,-1};
static Feedback feedback(std::uint64_t token,int start,int end,std::optional<std::uint32_t> gems,
                         Endpoint endpoint=Endpoint::completed) {
    Feedback value;value.token=token;value.job=job;value.started=start;value.ended=end;
    value.endpoint=endpoint;value.exactConverted=gems;return value;
}
static Decision only(Bandit& bandit,unsigned expert,std::uint64_t token,int start=10,bool fixed=false) {
    const auto decision=bandit.decide(1u<<expert,0,fixed);
    CHECK(decision.status==Status::known && decision.expert==int(expert));
    CHECK(bandit.commit(decision,token,job,start)==Status::known);return decision;
}
static plan3::JobForecast forecast(int group,int gems,int tau) {
    plan3::JobForecast value;value.id={plan3::JobKind::cycle,group,-1,9,-1};value.campGems=gems;
    plan3::Outcome outcome;outcome.tau=tau;outcome.deltaS=gems;outcome.conversions={{tau,gems}};
    outcome.terminal.health=outcome.terminal.maximumHealth=100;outcome.terminal.position.tile=8;
    value.outcomes.push_back(outcome);return value;
}
int main() {
    Bandit initial;
    const auto first=initial.decide(15,0);
    CHECK(first.status==Status::known && first.expert==0);
    for(auto probability:first.probabilities)CHECK(probability==.25);
    CHECK(initial.decide(15,0x40000000u).expert==1);
    CHECK(initial.decide(15,0x80000000u).expert==2);
    CHECK(initial.decide(15,0xc0000000u).expert==3);
    CHECK(initial.decide(15,0xffffffffu).expert==3);
    CHECK(initial.decide(0,0).status==Status::unavailable);
    CHECK(initial.decide(16,0).status==Status::invalid);
    const auto sleeping=initial.decide(10,0);
    CHECK(sleeping.expert==1 && sleeping.probabilities[1]==.5 && sleeping.probabilities[3]==.5);
    CHECK(sleeping.probabilities[0]==0 && sleeping.probabilities[2]==0);
    CHECK(initial.decide(2,0,true).status==Status::unavailable);
    CHECK(initial.decide(15,0xffffffffu,true).expert==0);
    CHECK(initial.episodes()==0);for(const auto& stats:initial.statistics())CHECK(stats.committed==0);

    // Only a committed chosen expert can receive a delayed observation.
    CHECK(initial.commit(first,1,job,10)==Status::known);
    CHECK(initial.pending()->expert==0 && initial.pending()->probability==.25);
    CHECK(initial.decide(15,0).status==Status::pending);
    CHECK(initial.finish(feedback(2,10,15,320)).status==Status::invalid);
    auto wrong=feedback(1,10,15,320);wrong.job[1]=8;
    CHECK(initial.finish(wrong).status==Status::invalid);
    wrong=feedback(1,11,15,320);CHECK(initial.finish(wrong).status==Status::invalid);
    wrong=feedback(1,10,10,320);CHECK(initial.finish(wrong).status==Status::invalid);
    wrong=feedback(1,10,15,320);wrong.convertedLowerBound=321;
    CHECK(initial.finish(wrong).status==Status::invalid);
    CHECK(initial.pending().has_value() && initial.statistics()[0].rewardSamples==0);
    const auto exact=initial.finish(feedback(1,10,15,320));
    CHECK(exact.status==Status::known && exact.rewardObserved && exact.reward==1);
    CHECK(!exact.costObserved && exact.loss==0 && !initial.pending());
    CHECK(initial.statistics()[0].completed==1 && initial.statistics()[0].rewardSamples==1);
    CHECK(initial.statistics()[0].converted==320 && initial.statistics()[0].elapsed==5);
    for(unsigned i=1;i<4;++i)CHECK(initial.statistics()[i].committed==0 && initial.log_weights()[i]==0);
    CHECK(initial.finish(feedback(1,10,15,320)).status==Status::invalid);
    CHECK(initial.commit(first,2,job,20)==Status::invalid); // stale preview, no replay.
    auto forged=initial.decide(15,0);forged.probabilities[0]=.5;
    CHECK(initial.commit(forged,2,job,20)==Status::invalid);
    forged=initial.decide(15,0);forged.expert=3;
    CHECK(initial.commit(forged,2,job,20)==Status::invalid);

    // Unknown reward is not a zero reward. A retained lower bound is neither
    // exact conversion nor a substitute for the missing reward component.
    only(initial,0,2,20);
    auto censored=feedback(2,20,30,std::nullopt);censored.convertedLowerBound=10;
    const auto before=initial.log_weights();
    auto update=initial.finish(censored);
    CHECK(update.status==Status::known && !update.rewardObserved && !update.costObserved);
    CHECK(update.loss==0 && initial.log_weights()==before);
    CHECK(initial.statistics()[0].censored==1 && initial.statistics()[0].rewardSamples==1);
    only(initial,0,3,30);
    update=initial.finish(feedback(3,30,35,0));
    CHECK(update.rewardObserved && update.reward==0 && update.loss>0);
    CHECK(initial.log_weights()[0]<before[0]); // Genuine observed zero is different.

    // Switching/aborting are known costs even when conversion is unavailable;
    // cost-only updates never increment the converted-reward sample count.
    only(initial,1,4,40);
    update=initial.finish(feedback(4,40,45,std::nullopt,Endpoint::aborted));
    CHECK(!update.rewardObserved && update.costObserved && near(update.cost,.15));
    CHECK(near(update.loss,.15/1.15));
    CHECK(initial.statistics()[1].aborted==1 && initial.statistics()[1].censored==1);
    CHECK(initial.statistics()[1].rewardSamples==0 && initial.statistics()[1].converted==0);
    CHECK(initial.statistics()[1].costSamples==1 && near(initial.statistics()[1].knownCost,.15));
    CHECK(initial.statistics()[2].committed==0 && initial.statistics()[3].committed==0);

    // Direct scalar recurrence, independent of the class state/update code.
    // Compare all pairwise log-weight ratios after known and censored outcomes.
    Bandit sequence;std::array<double,4> oracle{};int previous=-1;
    for(unsigned episode=0;episode<128;++episode) {
        const auto decision=sequence.decide(15,ticket(2,episode,episode));
        CHECK(decision.status==Status::known);
        const auto expert=unsigned(decision.expert);
        CHECK(sequence.commit(decision,episode+1,job,int(episode*20))==Status::known);
        const auto gems=episode%5==0?std::optional<std::uint32_t>{}:std::optional<std::uint32_t>{unsigned(episode%9)*64};
        const bool aborted=episode%7==0;
        const auto kind=aborted?Endpoint::aborted:Endpoint::completed;
        const double reward=gems?std::min(1.0,double(*gems)/(10*64)):0;
        const double cost=(previous>=0 && previous!=decision.expert?.05:0)+(aborted?.10:0);
        const double loss=((gems?1-reward:0)+cost)/1.15;
        oracle[expert]-=.03125*loss/decision.probabilities[expert];
        update=sequence.finish(feedback(episode+1,int(episode*20),int(episode*20+10),gems,kind));
        CHECK(update.status==Status::known && near(update.loss,loss));
        for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)
            CHECK(near(sequence.log_weights()[a]-sequence.log_weights()[b],oracle[a]-oracle[b]));
        const auto after=sequence.decide(15,0);double sum=0;
        for(double p:after.probabilities) {CHECK(p>=.125/4-1e-12);sum+=p;}
        CHECK(near(sum,1));previous=decision.expert;
    }

    Config invalid;invalid.learningRate=std::numeric_limits<double>::quiet_NaN();
    CHECK(Bandit(invalid).decide(15,0).status==Status::invalid);
    invalid={};invalid.exploration=0;CHECK(!valid(invalid));
    invalid={};invalid.switchCost=-1;CHECK(!valid(invalid));
    invalid={};invalid.maximumEpisodes=0;CHECK(!valid(invalid));
    Config limit;limit.maximumEpisodes=1;Bandit limited(limit);
    only(limited,0,std::numeric_limits<std::uint64_t>::max(),0);
    CHECK(limited.finish(feedback(std::numeric_limits<std::uint64_t>::max(),0,4097,1)).status==Status::invalid);
    CHECK(limited.finish(feedback(std::numeric_limits<std::uint64_t>::max(),0,4096,1000000)).status==Status::known);
    CHECK(limited.decide(15,0).status==Status::capacity);
    Bandit fixed;const auto fixedBefore=fixed.log_weights();only(fixed,0,1,10,true);
    CHECK(fixed.finish(feedback(1,10,20,0)).rewardObserved);
    CHECK(fixed.log_weights()==fixedBefore && fixed.statistics()[0].rewardSamples==1);

    // Preview is pure and every denied work checkpoint has no usable choice.
    unsigned calls=0;Bandit fresh;fresh.decide(15,0,false,[&]{++calls;return true;});
    for(unsigned cut=0;cut<calls;++cut) {
        unsigned seen=0;const auto denied=fresh.decide(15,0,false,[&]{return seen++<cut;});
        CHECK(denied.status==Status::cutoff && denied.expert==-1);
        for(auto p:denied.probabilities)CHECK(p==0);
        CHECK(fresh.episodes()==0 && !fresh.pending());
    }
    plan3::InformationState info;info.round=1;info.roundLimit=200;info.winningGems=1000;
    info.health=info.maximumHealth=100;info.position.tile=7;
    std::vector<plan3::JobForecast> jobs{forecast(1,10,5),forecast(2,100,100)};
    const auto mapped=experts(info,jobs);
    CHECK(mapped.status==Status::known && mapped.available==15);
    CHECK(mapped.intents[0].job==jobs[0].id && mapped.intents[1].job==jobs[1].id);
    for(unsigned i=0;i<4;++i) {
        const auto direct=plan3::select(expert_controllers[i],false,info,jobs);
        CHECK(mapped.intents[i].job==direct.job && mapped.intents[i].objective==direct.objective);
    }
    CHECK(fresh.log_weights()==fixedBefore); // Forecasts did not provide feedback.
    for(unsigned cut=0;cut<8;++cut) {
        unsigned seen=0;const auto denied=experts(info,jobs,{},[&]{return seen++<cut;});
        CHECK(denied.status==Status::cutoff && denied.available==0);
    }
    CHECK(key(jobs[0].id)==JobKey({0,1,-1,9,-1}));

    // Real selector bridge: both previews share the same table, only the
    // governing preview stages an episode, actual commit binds its exact ID.
    Bridge bridge;const auto governing=bridge.preview(false,info,jobs);
    CHECK(governing.status==Status::known && governing.token==1 && governing.intent.job);
    const auto companion=bridge.preview(true,info,jobs);
    CHECK(companion.status==Status::known && companion.token==0);
    CHECK(bridge.bandit().episodes()==0 && !bridge.bandit().pending());
    CHECK(bridge.committed(0,*governing.intent.job,info.round)==Status::unavailable);
    CHECK(bridge.committed(1,forecast(99,10,5).id,info.round)==Status::invalid);
    CHECK(bridge.committed(1,*governing.intent.job,info.round+1)==Status::invalid);
    CHECK(bridge.committed(1,*governing.intent.job,info.round)==Status::known);
    plan3::ControllerContext active;active.activeJob=governing.intent.job;
    const auto held=bridge.preview(false,info,jobs,active);
    CHECK(held.status==Status::pending && held.intent.status==plan3::IntentStatus::preserve && held.token==1);
    CHECK(bridge.preview(false,info,jobs).status==Status::invalid); // No fabricated missing endpoint.
    auto observed=feedback(1,info.round,info.round+4,std::nullopt);observed.job=key(*governing.intent.job);
    CHECK(bridge.endpoint(observed).status==Status::known);
    CHECK(bridge.bandit().episodes()==1 && bridge.bandit().statistics()[unsigned(governing.expert)].censored==1);
    CHECK(bridge.endpoint(observed).status==Status::invalid);
    info.round+=5;const auto next=bridge.preview(false,info,jobs);
    CHECK(next.token==2 && bridge.committed(2,*next.intent.job,info.round)==Status::known);
    observed=feedback(2,info.round,info.round,std::nullopt);observed.job=key(*next.intent.job);
    CHECK(bridge.endpoint(observed).status==Status::known); // Instant censored endpoint closes without fake zero.
    Bridge control(true);const auto actualControl=control.preview(true,info,jobs);
    CHECK(actualControl.expert==0 && actualControl.token==1);
    const auto counterfactual=control.preview(false,info,jobs);
    CHECK(counterfactual.token==0 && control.committed(1,*actualControl.intent.job,info.round)==Status::known);
    observed=feedback(1,info.round,info.round,0);observed.job=key(*actualControl.intent.job);
    CHECK(control.endpoint(observed).status==Status::known);
    CHECK(control.bandit().log_weights()==fixedBefore && control.bandit().statistics()[0].rewardSamples==1);

    // Actual shared event DTO projection; false conversion is never exact0.
    Bridge events;const auto proposed=events.preview(false,info,jobs);
    plan3::PolicyEvent event;event.token=proposed.token;event.job=*proposed.intent.job;
    event.startedRound=event.round=info.round;
    CHECK(observe(events,event).status==Status::known);
    event.kind=plan3::PolicyEvent::Kind::completed;event.round+=10;event.conversion=false;
    CHECK(observe(events,event).status==Status::known);
    const auto chosen=unsigned(proposed.expert);
    CHECK(events.bandit().statistics()[chosen].censored==1 && events.bandit().statistics()[chosen].rewardSamples==0);
    CHECK(observe(events,event).status==Status::invalid);
    info.round+=11;const auto proposed2=events.preview(false,info,jobs);
    event={};event.token=proposed2.token;event.job=*proposed2.intent.job;event.startedRound=event.round=info.round;
    CHECK(observe(events,event).status==Status::known);
    event.kind=plan3::PolicyEvent::Kind::completed;event.round+=10;event.conversion=true;event.exactConverted=24;
    const auto credited=observe(events,event);
    CHECK(credited.status==Status::known && credited.rewardObserved && near(credited.reward,24.0/640));
    CHECK(events.bandit().statistics()[unsigned(proposed2.expert)].rewardSamples==1);
    event.token=0;CHECK(observe(events,event).status==Status::unavailable);
    std::printf("adapter current PASS %u checks; chosen-only delayed feedback, no regret/native claim\n",checks);
}
