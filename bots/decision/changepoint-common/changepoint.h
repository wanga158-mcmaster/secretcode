#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace plan3_changepoint {
inline constexpr unsigned maximum_run=16,categories=3;
enum class Category : int {unknown=-1,score_increase=0,health_decrease=1,fresh_enemy=2};
struct Config {
    unsigned cap=16;
    double hazard=1.0/16,forgetting=1.0/16;
    std::array<double,3> alpha{1,1,1};
    // Explicit unfitted P(response world | literal event category) counts.
    std::array<std::array<unsigned,4>,3> projection{{{{6,2,1,1}},{{1,1,6,2}},{{1,3,4,2}}}};
};
inline bool same(const Config& a,const Config& b) {
    return a.cap==b.cap && a.hazard==b.hazard && a.forgetting==b.forgetting &&
        a.alpha==b.alpha && a.projection==b.projection;
}
inline bool valid(const Config& c) {
    if(!c.cap || c.cap>maximum_run || !std::isfinite(c.hazard) || c.hazard<0 || c.hazard>1 ||
       !std::isfinite(c.forgetting) || c.forgetting<0 || c.forgetting>1)return false;
    for(unsigned k=0;k<categories;++k) {
        if(!std::isfinite(c.alpha[k]) || c.alpha[k]<1.0/4096 || c.alpha[k]>4096)return false;
        unsigned sum=0;for(const auto count:c.projection[k]) {if(count>4096)return false;sum+=count;}
        if(!sum || sum>4096)return false;
    }
    return true;
}
struct Observation {int round=-1;Category category=Category::unknown;};
struct State {
    Config config;
    bool initialized=false;
    int lastRound=-1;
    std::uint32_t observations=0,skipped=0;
    std::array<double,maximum_run+1> posterior{};
    std::array<std::array<unsigned,categories>,maximum_run+1> counts{};
    std::array<double,categories> control{};
};
enum class Status {updated,skipped,duplicate,stale,cutoff,invalid};
inline bool valid(const State& s,const Config& c) {
    if(!valid(c) || (s.initialized && !same(s.config,c)))return false;
    if(!s.initialized)return true;
    double sum=0;
    for(unsigned r=0;r<=maximum_run;++r) {
        if(!std::isfinite(s.posterior[r]) || s.posterior[r]<0 || (r>c.cap && s.posterior[r]!=0))return false;
        unsigned count=0;for(const auto n:s.counts[r]) {if(n>maximum_run)return false;count+=n;}
        if(s.posterior[r]>0 && count!=r)return false;
        sum+=s.posterior[r];
    }
    for(unsigned k=0;k<categories;++k)if(!std::isfinite(s.control[k]) || s.control[k]<c.alpha[k])return false;
    return std::abs(sum-1)<1e-9;
}
// Adams--MacKay Algorithm1 (https://arxiv.org/abs/0710.3742): reset AFTER the
// current datum, so run0 has prior-only statistics. Finite geometric gap model:
// H=hazard below cap, H=1 at cap; no tail mass is silently discarded. Run length
// counts eligible evidence observations, not elapsed game rounds.
template<class Ready>
Status update(State& state,const Config& config,Observation observation,Ready&& ready) {
    if(!ready())return Status::cutoff;
    const int category=int(observation.category);
    if(!valid(state,config) || observation.round<0 || category< -1 || category>=int(categories))return Status::invalid;
    if(observation.round<state.lastRound)return Status::stale;
    if(observation.round==state.lastRound)return Status::duplicate;
    State next=state;
    if(!next.initialized) {
        next=State{};next.initialized=true;next.config=config;next.posterior[0]=1;next.control=config.alpha;
    }
    next.lastRound=observation.round;
    if(category<0) {
        if(next.skipped==std::numeric_limits<std::uint32_t>::max())return Status::invalid;
        ++next.skipped;if(!ready())return Status::cutoff;state=next;return Status::skipped;
    }
    if(next.observations==std::numeric_limits<std::uint32_t>::max())return Status::invalid;
    const auto previous=next;next.posterior={};next.counts={};
    const double prior=config.alpha[0]+config.alpha[1]+config.alpha[2];
    double evidence=0;
    for(unsigned r=0;r<=config.cap;++r) {
        if(!ready())return Status::cutoff;
        const double likelihood=(config.alpha[unsigned(category)]+previous.counts[r][unsigned(category)])/(prior+r);
        const double mass=previous.posterior[r]*likelihood;
        const double hazard=r==config.cap?1:config.hazard;
        next.posterior[0]+=mass*hazard;evidence+=mass;
        if(r<config.cap) {
            next.posterior[r+1]=mass*(1-hazard);next.counts[r+1]=previous.counts[r];
            ++next.counts[r+1][unsigned(category)];
        }
    }
    if(!std::isfinite(evidence) || evidence<=0)return Status::invalid;
    for(unsigned r=0;r<=config.cap;++r)next.posterior[r]/=evidence;
    for(unsigned k=0;k<categories;++k)
        next.control[k]=config.alpha[k]+(1-config.forgetting)*(previous.control[k]-config.alpha[k])+(k==unsigned(category)?1:0);
    ++next.observations;
    if(!valid(next,config))return Status::invalid;
    if(!ready())return Status::cutoff;
    state=next;return Status::updated;
}
inline Status update(State& state,const Config& config,Observation observation) {
    return update(state,config,observation,[] {return true;});
}
inline std::optional<std::array<double,4>> forecast_weights(const State& state,const Config& c,bool control) {
    if(!valid(state,c))return std::nullopt;
    const double prior=c.alpha[0]+c.alpha[1]+c.alpha[2];std::array<double,categories> predictive{};
    if(!state.initialized)for(unsigned k=0;k<categories;++k)predictive[k]=c.alpha[k]/prior;
    else if(control) {
        const double sum=state.control[0]+state.control[1]+state.control[2];
        for(unsigned k=0;k<categories;++k)predictive[k]=state.control[k]/sum;
    } else for(unsigned r=0;r<=c.cap;++r)for(unsigned k=0;k<categories;++k)
        predictive[k]+=state.posterior[r]*(c.alpha[k]+state.counts[r][k])/(prior+r);
    std::array<double,4> weights{};
    for(unsigned k=0;k<categories;++k) {
        unsigned sum=0;for(const auto n:c.projection[k])sum+=n;
        for(unsigned w=0;w<4;++w)weights[w]+=predictive[k]*c.projection[k][w]/sum;
    }
    double sum=0;for(const double value:weights) {if(!std::isfinite(value) || value<0)return std::nullopt;sum+=value;}
    if(!std::isfinite(sum) || sum<=0)return std::nullopt;
    for(auto& value:weights)value/=sum;return weights;
}
} // namespace plan3_changepoint
