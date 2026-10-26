#include "../belief-search-common/adapter.h"
#include <cstdio>
#include <cstdlib>
#include <memory>

extern "C" void* __cxa_allocate_exception(std::size_t size){return std::malloc(size);}
extern "C" void __cxa_throw(void*,void*,void(*)(void*)){std::abort();}
namespace b=plan3_belief_search;
static unsigned checks=0,scenarios=0;
static void require(bool condition,int line){++checks;if(!condition){std::fprintf(stderr,"belief search check failed at%d\n",line);std::exit(1);}}
#define REQUIRE(...) require(bool(__VA_ARGS__),__LINE__)
static bool equal(double a,double c){return std::abs(a-c)<1e-10;}
static b::Input informative() {
    b::Input in;in.horizon=3;in.belief={.5,.5,0,0};in.actionCount=3;
    for(unsigned a=0;a<3;++a) {
        auto& action=in.actions[a];action.key={0,int(10+10*a),-1,100,-1};action.groups={int(10+10*a),-1};
        for(unsigned s=0;s<4;++s) {
            action.counts[s]=1;const unsigned world=s%2;
            action.outcomes[s][0]={4096,a==0?world+1:(a==1?(world==0?6u:0u):(world==1?6u:0u)),a==0?1u:2u};
        }
    }
    return in;
}
static void informative_fork() {
    ++scenarios;const auto input=informative();const auto result=b::evaluate(input);
    REQUIRE(result.state==b::State::known && result.rows.size()==3);
    REQUIRE(result.active.action==0 && result.control.action==1);
    REQUIRE(equal(result.rows[0].oneStage,1.5) && equal(result.rows[0].twoStage,7.5));
    REQUIRE(equal(result.rows[1].oneStage,3) && equal(result.rows[1].twoStage,4.5));
    REQUIRE(equal(result.rows[2].oneStage,3) && equal(result.rows[2].twoStage,4.5));
    std::printf("BELIEF_MODEL {\"prior\":[0.5,0.5,0,0],\"horizon\":3,\"active\":%u,\"control\":%u,\"rows\":[[%g,%g],[%g,%g],[%g,%g]]}\n",
        result.active.action,result.control.action,result.rows[0].oneStage,result.rows[0].twoStage,
        result.rows[1].oneStage,result.rows[1].twoStage,result.rows[2].oneStage,result.rows[2].twoStage);
    const auto observed=b::bayes(input.belief,input.actions[0],{1,1});
    REQUIRE(observed.known && equal(observed.probability,.5) && equal(observed.belief[0],1) && observed.belief[1]==0);
    REQUIRE(!b::bayes(input.belief,input.actions[0],{99,1}).known);
    auto reversed=input;std::reverse(reversed.actions.begin(),reversed.actions.begin()+3);
    const auto same=b::evaluate(reversed);REQUIRE(same.active.key==result.active.key && same.control.key==result.control.key);
}
static void bayesian_history() {
    ++scenarios;auto action=informative().actions[0];
    for(unsigned s=0;s<4;++s) {
        action.counts[s]=2;action.outcomes[s][0]={s%2?1024u:3072u,1,1};action.outcomes[s][1]={s%2?3072u:1024u,2,1};
    }
    const auto first=b::bayes({.5,.5,0,0},action,{1,1});
    REQUIRE(first.known && equal(first.probability,.5) && equal(first.belief[0],.75) && equal(first.belief[1],.25));
    const auto second=b::bayes(first.belief,action,{1,1});
    REQUIRE(second.known && equal(second.probability,.625) && equal(second.belief[0],.9) && equal(second.belief[1],.1));
    REQUIRE(!b::bayes(second.belief,action,{1,2}).known); // Time belongs to the observable class.
}
static void horizon_and_groups() {
    ++scenarios;auto input=informative();input.actions[0].recovery=1;
    const auto charged=b::evaluate(input);REQUIRE(charged.active.action==1 && equal(charged.rows[0].twoStage,1.5));
    input=informative();input.actions[0].groups={10,11};input.actions[1].groups={20,11};
    const auto noReuse=b::evaluate(input);REQUIRE(equal(noReuse.rows[0].twoStage,4.5));
    input=informative();input.horizon=1;const auto shortH=b::evaluate(input);
    REQUIRE(shortH.active.action==0 && shortH.control.action==0 && equal(shortH.active.value,1.5));
    input.horizon=0;const auto end=b::evaluate(input);REQUIRE(!end.active.present && end.rows.empty() && end.reason==b::Reason::no_actions);
}
static void every_cut(const b::Input& input) {
    unsigned needed=0;REQUIRE(b::evaluate(input,[&]{++needed;return true;}).state==b::State::known);
    for(unsigned cut=0;cut<needed;++cut) {
        ++scenarios;unsigned calls=0;const auto result=b::evaluate(input,[&]{return calls++<cut;});
        REQUIRE(result.state==b::State::cutoff && result.rows.empty() && !result.active.present && !result.control.present);
        REQUIRE(!result.rootActions && !result.observationBranches && !result.continuationChecks && !result.cacheEntries);
    }
}
static void full_menu() {
    ++scenarios;auto input=std::make_unique<b::Input>();input->horizon=3;input->actionCount=28;
    for(unsigned a=0;a<28;++a) {
        auto& action=input->actions[a];action.key={0,int(a),-1,100,-1};action.groups={int(a),-1};
        for(unsigned s=0;s<4;++s){action.counts[s]=1;action.outcomes[s][0]={4096,a+1,1};}
    }
    const auto result=b::evaluate(*input);REQUIRE(result.rows.size()==28 && result.control.action==27 && equal(result.active.value,55));
    REQUIRE(result.observationBranches<=336 && result.continuationChecks<=9408 && result.cacheEntries<=129);
    every_cut(*input);
}
static void invalid_inputs() {
    ++scenarios;auto input=informative();input.belief[0]=-.1;REQUIRE(b::evaluate(input).state==b::State::invalid);
    input=informative();input.actions[0].counts[3]=0;REQUIRE(b::evaluate(input).state==b::State::invalid);
    input=informative();input.actions[0].outcomes[0][0].weight=4095;REQUIRE(b::evaluate(input).state==b::State::invalid);
    input=informative();input.actions[1].key=input.actions[0].key;REQUIRE(b::evaluate(input).state==b::State::invalid);
    input=informative();input.actions[0].outcomes[0][0].elapsed=0;REQUIRE(b::evaluate(input).state==b::State::invalid);
    input=informative();input.horizon=129;REQUIRE(b::evaluate(input).state==b::State::invalid);
}
static std::vector<plan3::JobForecast> forecasts() {
    const auto model=informative();std::vector<plan3::JobForecast> jobs;
    for(unsigned a=0;a<3;++a) {
        plan3::JobForecast job;job.id=b::job_id(model.actions[a].key);
        for(unsigned s=0;s<4;++s) {
            const auto& generated=model.actions[a].outcomes[s][0];plan3::Outcome outcome;
            outcome.response=plan3::ResponseWorld(s);outcome.lifecycle=0;outcome.conditionalLifecycleWeight=1;
            outcome.deltaS=int(generated.converted);outcome.tau=int(generated.elapsed);
            if(outcome.deltaS)outcome.conversions.push_back({outcome.tau,outcome.deltaS});
            job.outcomes.push_back(outcome);
        }
        jobs.push_back(std::move(job));
    }
    return jobs;
}
static void mapping_and_feedback() {
    ++scenarios;auto jobs=forecasts();plan3::InformationState info;
    info.round=20;info.roundLimit=23;info.health=info.maximumHealth=100;info.winningGems=1000;
    plan3::ControllerContext context;context.belief.weights={.5,.5,0,0};
    const auto mapped=b::map_jobs(info,jobs,context.belief.weights);
    REQUIRE(mapped.state==b::State::known && mapped.input.actionCount==3 && mapped.unknown==0);
    auto policy=std::make_unique<b::Policy>();
    const auto active=policy->choose(false,info,jobs,context,[]{return true;},[]{return true;});
    const auto control=policy->choose(true,info,jobs,context,[]{return true;},[]{return false;});
    REQUIRE(active.job==jobs[0].id && control.job==jobs[1].id && active.policyToken && control.policyToken!=active.policyToken && policy->solves==1);
    policy->observe({plan3::PolicyEvent::Kind::committed,active.policyToken,*active.job,20,20,std::nullopt,false});
    policy->observe({plan3::PolicyEvent::Kind::completed,active.policyToken,*active.job,20,21,1,true});
    REQUIRE(policy->updates==1 && policy->belief() && equal((*policy->belief())[0],1));
    policy->observe({plan3::PolicyEvent::Kind::completed,active.policyToken,*active.job,20,21,1,true});
    REQUIRE(policy->updates==1); // Same evidence cannot update twice.
    info.round=21;context.belief.weights={0,1,0,0};
    const auto next=policy->choose(false,info,jobs,context,[]{return true;},[]{return true;});
    REQUIRE(next.job==jobs[1].id && equal((*policy->belief())[0],1)); // Shared diagnostics do not silently reseed.
    policy->observe({plan3::PolicyEvent::Kind::committed,next.policyToken,*next.job,21,21,std::nullopt,false});
    policy->observe({plan3::PolicyEvent::Kind::completed,next.policyToken,*next.job,21,22,999,true});
    REQUIRE(policy->modelGaps==1 && policy->updates==1 && equal((*policy->belief())[0],1));
    const auto again=policy->choose(false,info,jobs,context,[]{return true;},[]{return true;});
    policy->observe({plan3::PolicyEvent::Kind::committed,again.policyToken,*again.job,21,21,std::nullopt,false});
    const unsigned censored=policy->censored;
    policy->observe({plan3::PolicyEvent::Kind::aborted,again.policyToken,*again.job,21,22,std::nullopt,false});
    REQUIRE(policy->censored==censored+1 && policy->updates==1 && equal((*policy->belief())[0],1));
    const auto ambiguous=policy->choose(false,info,jobs,context,[]{return true;},[]{return true;});
    policy->observe({plan3::PolicyEvent::Kind::committed,ambiguous.policyToken,*ambiguous.job,21,21,std::nullopt,false});
    const unsigned beforeAmbiguous=policy->censored;
    policy->observe({plan3::PolicyEvent::Kind::completed,ambiguous.policyToken,*ambiguous.job,21,23,std::nullopt,true});
    REQUIRE(policy->censored==beforeAmbiguous+1 && policy->updates==1 && equal((*policy->belief())[0],1));
    const auto matched=policy->choose(false,info,jobs,context,[]{return true;},[]{return true;});
    policy->observe({plan3::PolicyEvent::Kind::committed,matched.policyToken,*matched.job,21,21,std::nullopt,false});
    policy->observe({plan3::PolicyEvent::Kind::completed,matched.policyToken+1,*matched.job,21,23,6,true});
    REQUIRE(policy->updates==1);
    policy->observe({plan3::PolicyEvent::Kind::completed,matched.policyToken,*matched.job,21,23,6,true});
    REQUIRE(policy->updates==2 && equal((*policy->belief())[0],1)); // Only the matching live episode consumed evidence.
    auto missing=jobs;missing[0].outcomes.pop_back();const auto incomplete=b::map_jobs(info,missing,context.belief.weights);
    REQUIRE(incomplete.unknown==1 && incomplete.input.actionCount==2 && incomplete.prefix);
    info.carriedGems=1;REQUIRE(b::map_jobs(info,jobs,context.belief.weights).state==b::State::unknown);info.carriedGems=0;
    REQUIRE(b::map_jobs(info,jobs,context.belief.weights,false).state==b::State::unknown);
    std::array<unsigned,3> rounded{};REQUIRE(b::probabilities({1.0/3,1.0/3,1.0/3},3,rounded) && rounded==std::array<unsigned,3>({1366,1365,1365}));
    info.round=20;context.belief.weights={.5,.5,0,0};unsigned required=0;auto measured=std::make_unique<b::Policy>();
    REQUIRE(measured->choose(false,info,jobs,context,[&]{++required;return true;},[]{return true;}).job.has_value());
    for(unsigned cut=0;cut<required;++cut) {
        ++scenarios;unsigned calls=0;auto stopped=std::make_unique<b::Policy>();
        const auto choice=stopped->choose(false,info,jobs,context,[&]{return calls++<cut;},[]{return true;});
        REQUIRE(choice.status==plan3::IntentStatus::yield && !choice.job && !stopped->belief() && stopped->result().rows.empty());
    }
    auto denied=std::make_unique<b::Policy>();
    REQUIRE(!denied->choose(false,info,jobs,context,[]{return true;},[]{return false;}).job && !denied->belief() && denied->solves==0);
}
int main() {
    informative_fork();bayesian_history();horizon_and_groups();every_cut(informative());full_menu();invalid_inputs();mapping_and_feedback();
    std::printf("{\"status\":\"pass\",\"scenarios\":%u,\"checks\":%u,\"nativeGames\":0}\n",scenarios,checks);
}
