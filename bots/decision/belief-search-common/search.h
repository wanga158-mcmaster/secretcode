#pragma once
// Finite assumed job-level POMDP. This exhausts depth2 observation branches;
// it is neither an engine simulator nor a calibrated Battlecode model.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

namespace plan3_belief_search {
inline constexpr unsigned maximum_actions=28,modes=4,maximum_outcomes=3,probability_total=4096;
inline constexpr unsigned maximum_observations=modes*maximum_outcomes;
using Key=std::array<int,5>;
using Belief=std::array<double,modes>;
enum class State {known,unknown,invalid,cutoff};
enum class Reason {none,snapshot,model,belief,no_actions,cutoff};
struct Observation {
    unsigned converted=0,elapsed=0;
    bool operator==(const Observation&) const =default;
    bool operator<(const Observation& b) const{return converted==b.converted?elapsed<b.elapsed:converted<b.converted;}
};
struct Outcome {unsigned weight=0,converted=0,elapsed=0;bool operator==(const Outcome&) const =default;};
struct Action {
    Key key{0,-1,-1,-1,-1};
    std::array<int,2> groups{-1,-1};
    unsigned recovery=0;
    std::array<unsigned,modes> counts{};
    std::array<std::array<Outcome,maximum_outcomes>,modes> outcomes{};
    bool operator==(const Action&) const =default;
};
struct Input {
    Belief belief{.25,.25,.25,.25};
    unsigned horizon=0,actionCount=0;
    std::array<Action,maximum_actions> actions{};
    bool operator==(const Input&) const =default;
};
inline bool valid_belief(const Belief& belief) {
    double sum=0;for(double b:belief){if(!std::isfinite(b) || b<0 || b>1)return false;sum+=b;}
    return std::abs(sum-1)<=1e-9;
}
inline Belief normalized(Belief belief) {
    double sum=0;for(auto b:belief)sum+=b;for(auto& b:belief)b/=sum;return belief;
}
inline bool valid_action(const Action& action) {
    if(action.recovery>8192 || action.groups[0]<-1 || action.groups[1]<-1 ||
       (action.groups[0]>=0 && action.groups[0]==action.groups[1]))return false;
    for(unsigned s=0;s<modes;++s) {
        if(!action.counts[s] || action.counts[s]>maximum_outcomes)return false;
        unsigned total=0;
        for(unsigned n=0;n<action.counts[s];++n) {
            const auto& outcome=action.outcomes[s][n];
            if(outcome.weight>probability_total || outcome.converted>1000000 || outcome.elapsed<1 || outcome.elapsed>8192)return false;
            total+=outcome.weight;
        }
        if(total!=probability_total)return false;
    }
    return true;
}
inline bool overlaps(const Action& a,const Action& b) {
    for(auto x:a.groups)for(auto y:b.groups)if(x>=0 && x==y)return true;return false;
}
struct Posterior {bool known=false;double probability=0;Belief belief{};};
inline Posterior bayes(const Belief& prior,const Action& action,Observation observed) {
    Posterior out;if(!valid_belief(prior) || !valid_action(action))return out;
    const auto belief=normalized(prior);
    for(unsigned s=0;s<modes;++s) {
        unsigned likelihood=0;
        for(unsigned n=0;n<action.counts[s];++n) {
            const auto& generated=action.outcomes[s][n];
            if(Observation{generated.converted,generated.elapsed}==observed)likelihood+=generated.weight;
        }
        out.belief[s]=belief[s]*double(likelihood)/probability_total;out.probability+=out.belief[s];
    }
    if(!(out.probability>0) || !std::isfinite(out.probability))return out;
    for(auto& p:out.belief)p/=out.probability;
    out.known=valid_belief(out.belief);return out;
}
struct Choice {bool present=false;unsigned action=0;Key key{0,-1,-1,-1,-1};double value=0;};
struct Row {Key key{0,-1,-1,-1,-1};double oneStage=0,twoStage=0;};
struct Result {
    State state=State::unknown;Reason reason=Reason::no_actions;
    std::vector<Row> rows;
    Choice active,control;
    unsigned rootActions=0,observationBranches=0,continuationChecks=0,cacheEntries=0;
};
namespace detail {
inline bool better(double value,const Key& key,const Choice& previous) {
    constexpr double tolerance=1e-9;
    return !previous.present || value>previous.value+tolerance || (std::abs(value-previous.value)<=tolerance && key<previous.key);
}
struct Continuation {
    unsigned horizon=0;
    std::array<Belief,maximum_actions> values{};
};
} // namespace detail
template<class Ready> Result evaluate(const Input& input,Ready&& ready) {
    const auto stop=[](State state,Reason reason){Result result;result.state=state;result.reason=reason;return result;};
    if(input.horizon>128 || input.actionCount>maximum_actions)return stop(State::invalid,Reason::snapshot);
    if(!valid_belief(input.belief))return stop(State::invalid,Reason::belief);
    const auto belief=normalized(input.belief);
    for(unsigned a=0;a<input.actionCount;++a) {
        if(!ready())return stop(State::cutoff,Reason::cutoff);
        if(!valid_action(input.actions[a]))return stop(State::invalid,Reason::model);
        for(unsigned b=0;b<a;++b)if(input.actions[a].key==input.actions[b].key)return stop(State::invalid,Reason::model);
    }
    if(!input.horizon || !input.actionCount)return stop(State::known,Reason::no_actions);
    std::vector<detail::Continuation> cache; // <=129 horizon entries, callback-owned.
    Result result;result.rows.reserve(input.actionCount);bool cutoff=false;
    const auto values=[&](unsigned horizon)->const detail::Continuation* {
        for(const auto& entry:cache)if(entry.horizon==horizon)return &entry;
        detail::Continuation entry;entry.horizon=horizon;
        for(unsigned a=0;a<input.actionCount;++a) {
            if(!ready()){cutoff=true;return nullptr;}
            const auto& action=input.actions[a];
            for(unsigned s=0;s<modes;++s)for(unsigned n=0;n<action.counts[s];++n) {
                const auto& generated=action.outcomes[s][n];
                // Reward is recognized only for the represented restored
                // complete job (endpoint tau plus recovery/return R) withinH.
                if(generated.elapsed+action.recovery<=horizon)
                    entry.values[a][s]+=double(generated.weight)*generated.converted/probability_total;
            }
        }
        cache.push_back(std::move(entry));return &cache.back();
    };
    for(unsigned a=0;a<input.actionCount;++a) {
        if(!ready())return stop(State::cutoff,Reason::cutoff);
        const auto* initial=values(input.horizon);if(!initial)return stop(State::cutoff,Reason::cutoff);
        const auto& action=input.actions[a];Row row;row.key=action.key;
        for(unsigned s=0;s<modes;++s)row.oneStage+=belief[s]*initial->values[a][s];
        row.twoStage=row.oneStage;++result.rootActions;
        std::array<Observation,maximum_observations> observations{};unsigned count=0;
        for(unsigned s=0;s<modes;++s)for(unsigned n=0;n<action.counts[s];++n) {
            const auto& generated=action.outcomes[s][n];const Observation observed{generated.converted,generated.elapsed};
            if(!generated.weight || generated.elapsed+action.recovery>=input.horizon)continue;
            if(std::find(observations.begin(),observations.begin()+count,observed)==observations.begin()+count)observations[count++]=observed;
        }
        std::sort(observations.begin(),observations.begin()+count);
        for(unsigned o=0;o<count;++o) {
            if(!ready())return stop(State::cutoff,Reason::cutoff);
            const auto observed=observations[o];const auto posterior=bayes(belief,action,observed);
            if(!posterior.known)continue; // An impossible model observation has zero mass.
            const unsigned remaining=input.horizon-observed.elapsed-action.recovery;
            const auto* continuation=values(remaining);if(!continuation)return stop(State::cutoff,Reason::cutoff);
            double best=0;++result.observationBranches;
            for(unsigned b=0;b<input.actionCount;++b) {
                if(!ready())return stop(State::cutoff,Reason::cutoff);
                ++result.continuationChecks;
                if(overlaps(action,input.actions[b]))continue;
                double expected=0;for(unsigned s=0;s<modes;++s)expected+=posterior.belief[s]*continuation->values[b][s];
                best=std::max(best,expected);
            }
            row.twoStage+=posterior.probability*best;
        }
        if(cutoff || !std::isfinite(row.oneStage) || !std::isfinite(row.twoStage))return stop(State::invalid,Reason::model);
        result.rows.push_back(row);
        if(detail::better(row.oneStage,row.key,result.control))result.control={true,a,row.key,row.oneStage};
        if(detail::better(row.twoStage,row.key,result.active))result.active={true,a,row.key,row.twoStage};
    }
    if(!ready())return stop(State::cutoff,Reason::cutoff);
    result.cacheEntries=unsigned(cache.size());result.state=State::known;result.reason=Reason::none;return result;
}
inline Result evaluate(const Input& input){return evaluate(input,[]{return true;});}
} // namespace plan3_belief_search
