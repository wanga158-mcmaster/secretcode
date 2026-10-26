#include "../changepoint-common/observations.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace cp=plan3_changepoint;
unsigned checks=0,sequences=0;
void require(bool value,const char* text,int line) {
    ++checks;if(!value) {std::fprintf(stderr,"changepoint:%d: %s\n",line,text);std::exit(1);}
}
#define CHECK(value) require(bool(value),#value,__LINE__)
bool near(long double a,long double b) {return std::abs(a-b)<1e-11L;}
bool same_state(const cp::State& a,const cp::State& b) {
    return cp::same(a.config,b.config) && a.initialized==b.initialized && a.lastRound==b.lastRound &&
        a.observations==b.observations && a.skipped==b.skipped && a.posterior==b.posterior && a.counts==b.counts && a.control==b.control;
}
void oracle(const std::vector<unsigned>& sequence,const cp::Config& config) {
    cp::State state;
    for(unsigned n=0;n<sequence.size();++n)
        CHECK(cp::update(state,config,{int(n),static_cast<cp::Category>(sequence[n])})==cp::Status::updated);
    CHECK(cp::valid(state,config));++sequences;
    // Independent enumeration of EVERY boundary string, integrating each
    // segment's categorical parameter under the declared Dirichlet prior.
    std::array<long double,17> posterior{};std::array<long double,3> predictive{};long double evidence=0;
    const long double prior=config.alpha[0]+config.alpha[1]+config.alpha[2];
    for(unsigned boundaries=0;boundaries<(1u<<sequence.size());++boundaries) {
        unsigned run=0;std::array<unsigned,3> counts{};long double weight=1;
        for(unsigned n=0;n<sequence.size();++n) {
            const unsigned symbol=sequence[n];const long double hazard=run==config.cap?1:config.hazard;
            weight*=((long double)config.alpha[symbol]+counts[symbol])/(prior+run);
            if(boundaries&(1u<<n)) {weight*=hazard;run=0;counts={};}
            else {weight*=1-hazard;++run;++counts[symbol];}
            if(!weight)break;
        }
        if(!weight)continue;
        CHECK(run<=config.cap);posterior[run]+=weight;evidence+=weight;
        for(unsigned k=0;k<3;++k)predictive[k]+=weight*(config.alpha[k]+counts[k])/(prior+run);
    }
    CHECK(evidence>0);
    for(unsigned r=0;r<=16;++r)CHECK(near(state.posterior[r],posterior[r]/evidence));
    const auto weights=cp::forecast_weights(state,config,false);CHECK(weights.has_value());
    for(unsigned w=0;w<4;++w) {
        long double expected=0;
        for(unsigned k=0;k<3;++k) {
            unsigned sum=0;for(const auto count:config.projection[k])sum+=count;
            expected+=predictive[k]/evidence*config.projection[k][w]/sum;
        }
        CHECK(near((*weights)[w],expected));
    }
    std::array<long double,3> control{config.alpha[0],config.alpha[1],config.alpha[2]};
    for(const unsigned symbol:sequence)for(unsigned k=0;k<3;++k)
        control[k]=config.alpha[k]+(1-config.forgetting)*(control[k]-config.alpha[k])+(k==symbol);
    for(unsigned k=0;k<3;++k)CHECK(near(state.control[k],control[k]));
}
void kernels() {
    for(const unsigned cap:{1u,3u,16u}) {
        cp::Config config;config.cap=cap;
        unsigned count=1;
        for(unsigned length=1;length<=6;++length) {
            count*=3;
            for(unsigned encoded=0;encoded<count;++encoded) {
                auto value=encoded;std::vector<unsigned> sequence(length);
                for(auto& symbol:sequence) {symbol=value%3;value/=3;}oracle(sequence,config);
            }
        }
    }
    cp::Config config;config.cap=2;config.hazard=0;config.forgetting=0;oracle({0,0,1,2,2,1},config);
    cp::State state;for(int r=0;r<3;++r)CHECK(cp::update(state,config,{r,cp::Category::score_increase})==cp::Status::updated);
    CHECK(state.posterior[0]==1); // Forced finite-gap reset, not discarded tail.
    config.hazard=config.forgetting=1;oracle({2,1,0,0,2,1},config);
    config=cp::Config{};config.alpha={0.25,2,5};oracle({1,1,2,0,2},config);
    state=cp::State{};unsigned calls=0;
    CHECK(cp::update(state,config,{0,cp::Category::fresh_enemy},[&] {++calls;return true;})==cp::Status::updated);
    for(unsigned cut=0;cut<calls;++cut) {
        cp::State untouched;const auto before=untouched;unsigned at=0;
        CHECK(cp::update(untouched,config,{0,cp::Category::fresh_enemy},[&] {return at++!=cut;})==cp::Status::cutoff);
        CHECK(same_state(untouched,before));
    }
    const auto before=state;
    CHECK(cp::update(state,config,{0,cp::Category::score_increase})==cp::Status::duplicate && same_state(state,before));
    CHECK(cp::update(state,config,{-1,cp::Category::unknown})==cp::Status::invalid && same_state(state,before));
    auto bad=config;bad.hazard=std::numeric_limits<double>::quiet_NaN();
    CHECK(cp::update(state,bad,{1,cp::Category::score_increase})==cp::Status::invalid && same_state(state,before));
    bad=config;bad.cap=17;CHECK(!cp::valid(bad));bad=config;bad.alpha[0]=0;CHECK(!cp::valid(bad));
    bad=config;bad.projection[0]={0,0,0,0};CHECK(!cp::valid(bad));
    CHECK(cp::update(state,config,{1,cp::Category::unknown})==cp::Status::skipped);
    CHECK(state.posterior==before.posterior && state.counts==before.counts && state.observations==before.observations);
    CHECK(cp::update(state,config,{0,cp::Category::score_increase})==cp::Status::stale);
}
plan3::InformationState snapshot(int round) {
    plan3::InformationState info;info.round=round;info.roundLimit=300;info.winningGems=1000;
    info.health=info.maximumHealth=100;info.actorOrdinal=11;return info;
}
void mapping() {
    cp::Config config;cp::State state;cp::Previous previous;auto info=snapshot(20);
    info.opponentPermanentScore=10;
    CHECK(cp::observe(state,previous,config,info)==cp::Status::skipped && state.observations==0);
    info.round=21;info.opponentPermanentScore=12;info.health=90;
    CHECK(cp::map_observation(info,previous).observation.category==cp::Category::score_increase);
    CHECK(cp::observe(state,previous,config,info)==cp::Status::updated && state.observations==1);
    const auto before=state;const auto old=previous;
    CHECK(cp::observe(state,previous,config,info)==cp::Status::duplicate && same_state(state,before));
    CHECK(previous.round==old.round && previous.health==old.health);
    info.round=22;info.health=80;CHECK(cp::map_observation(info,previous).observation.category==cp::Category::health_decrease);
    info.round=24;CHECK(cp::map_observation(info,previous).observation.category==cp::Category::unknown);
    plan3::ChampionObservation enemy;enemy.position.value=plan3::Position{50};
    enemy.position.provenance=plan3::Provenance::personally_observed;enemy.position.observedRound=23;info.champions={enemy};
    CHECK(cp::map_observation(info,previous).observation.category==cp::Category::unknown);
    info.champions[0].position.observedRound=24;
    CHECK(cp::map_observation(info,previous).observation.category==cp::Category::fresh_enemy);
    info.champions[0].position.provenance=plan3::Provenance::assumed;
    CHECK(cp::map_observation(info,previous).observation.category==cp::Category::unknown);
    unsigned calls=0;auto copy=state;auto record=previous;
    CHECK(cp::observe(copy,record,config,info,[&] {++calls;return true;})==cp::Status::skipped);
    for(unsigned cut=0;cut<calls;++cut) {
        auto current=state;auto prior=previous;unsigned at=0;
        CHECK(cp::observe(current,prior,config,info,[&] {return at++!=cut;})==cp::Status::cutoff);
        CHECK(same_state(current,state) && prior.round==previous.round && prior.health==previous.health);
    }
    info.actorOrdinal=8;CHECK(!cp::map_observation(info,previous).available);
}
void governing() {
    cp::State state;cp::Config config;CHECK(cp::update(state,config,{0,cp::Category::score_increase})==cp::Status::updated);
    auto info=snapshot(20);std::vector<plan3::JobForecast> jobs(2);
    for(unsigned j=0;j<2;++j) {
        jobs[j].id={plan3::JobKind::cycle,int(j+1),-1,7,-1};
        for(unsigned world=0;world<4;++world) {
            plan3::Outcome outcome;outcome.response=static_cast<plan3::ResponseWorld>(world);outcome.tau=1;
            outcome.deltaS=j==0?(world==0?200:0):69;outcome.conversions={{1,outcome.deltaS}};
            outcome.terminal.health=outcome.terminal.maximumHealth=100;jobs[j].outcomes.push_back(outcome);
        }
    }
    plan3::ControllerContext active,control;
    active.belief.weights=*cp::forecast_weights(state,config,false);control.belief.weights=*cp::forecast_weights(state,config,true);
    const auto a=plan3::select(plan3::Controller::belief,false,info,jobs,active);
    const auto b=plan3::select(plan3::Controller::belief,false,info,jobs,control);
    CHECK(a.job && b.job && a.job->firstGroup==2 && b.job->firstGroup==1);
}
int main() {
    kernels();mapping();governing();
    std::printf("CHANGEPOINT_RESULT {\"checks\":%u,\"enumeratedSequences\":%u,\"nativeCalls\":0}\n",checks,sequences);
}
