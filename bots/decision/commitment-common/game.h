#pragma once
// A finite assumed leader/follower game, not an empirical opponent payoff.
#include <array>
#include <cstdint>
#include <optional>

namespace plan3_commitment {
inline constexpr unsigned maximum_jobs=28,maximum_responses=4;
inline constexpr std::int64_t payoff_bound=1000000000000LL;
using Key=std::array<std::int32_t,5>;
enum class State {known,infeasible,unavailable,invalid,cutoff};
enum class Knowledge {unknown,known,unavailable};
struct Cell {Knowledge state=Knowledge::unknown;std::int64_t leader=0,follower=0;};
struct Row {Key key{};std::array<Cell,maximum_responses> cells{};};
struct Input {
    unsigned jobCount=0,responseCount=4;
    std::array<Row,maximum_jobs> jobs{};
    std::optional<Key> previous;
    std::int64_t switchCost=0;
};
struct Analysis {
    Key key{};
    bool feasible=false,switched=false;
    unsigned bestResponses=0,pessimisticResponse=0,minimaxResponse=0;
    std::int64_t followerBest=0,pessimistic=0,minimax=0,switchCost=0;
};
struct Choice {
    bool present=false;
    Key key{};
    unsigned row=0,response=0;
    std::int64_t value=0;
};
struct Result {
    State state=State::unavailable;
    unsigned rowCount=0,work=0;
    std::array<Analysis,maximum_jobs> rows{};
    Choice active,control;
};
inline bool valid_key(const Key& key) {
    if(key[0]<0 || key[0]>5)return false;
    for(unsigned i=1;i<key.size();++i)if(key[i]<-1)return false;
    return true;
}
template<class Ready>
Result solve(const Input& input,Ready&& ready) {
    Result result;
    const auto stop=[&](State state) {Result empty;empty.state=state;empty.work=result.work;return empty;};
    const auto step=[&] {++result.work;return ready();};
    if(!step())return stop(State::cutoff);
    if(input.jobCount>maximum_jobs || !input.responseCount || input.responseCount>maximum_responses ||
       input.switchCost<0 || input.switchCost>payoff_bound || (input.previous && !valid_key(*input.previous)))
        return stop(State::invalid);
    bool unknown=false;
    for(unsigned j=0;j<input.jobCount;++j) {
        if(!step())return stop(State::cutoff);
        if(!valid_key(input.jobs[j].key))return stop(State::invalid);
        for(unsigned p=0;p<j;++p) {
            if(!step())return stop(State::cutoff);
            if(input.jobs[p].key==input.jobs[j].key)return stop(State::invalid);
        }
        for(unsigned r=0;r<input.responseCount;++r) {
            if(!step())return stop(State::cutoff);
            const auto& cell=input.jobs[j].cells[r];
            if(static_cast<unsigned>(cell.state)>static_cast<unsigned>(Knowledge::unavailable) ||
               cell.leader < -payoff_bound || cell.leader>payoff_bound ||
               cell.follower < -payoff_bound || cell.follower>payoff_bound ||
               (cell.state!=Knowledge::known && (cell.leader || cell.follower)))return stop(State::invalid);
            unknown|=cell.state==Knowledge::unknown;
        }
    }
    if(unknown)return stop(State::unavailable);
    const auto nominate=[](Choice& choice,const Analysis& row,unsigned index,bool control) {
        const auto value=control?row.minimax:row.pessimistic;
        if(!choice.present || value>choice.value || (value==choice.value && row.key<choice.key))
            choice={true,row.key,index,control?row.minimaxResponse:row.pessimisticResponse,value};
    };
    for(unsigned j=0;j<input.jobCount;++j) {
        auto& row=result.rows[j];row.key=input.jobs[j].key;
        row.switched=input.previous && *input.previous!=row.key;
        row.switchCost=row.switched?input.switchCost:0;
        for(unsigned r=0;r<input.responseCount;++r) {
            if(!step())return stop(State::cutoff);
            const auto& cell=input.jobs[j].cells[r];if(cell.state!=Knowledge::known)continue;
            if(!row.feasible || cell.leader-row.switchCost<row.minimax) {
                row.minimax=cell.leader-row.switchCost;row.minimaxResponse=r;
            }
            if(!row.feasible || cell.follower>row.followerBest) {
                row.followerBest=cell.follower;row.bestResponses=1u<<r;
                row.pessimistic=cell.leader-row.switchCost;row.pessimisticResponse=r;
            } else if(cell.follower==row.followerBest) {
                row.bestResponses|=1u<<r;
                if(cell.leader-row.switchCost<row.pessimistic) {
                    row.pessimistic=cell.leader-row.switchCost;row.pessimisticResponse=r;
                }
            }
            row.feasible=true;
        }
        if(row.feasible) {nominate(result.active,row,j,false);nominate(result.control,row,j,true);}
    }
    result.rowCount=input.jobCount;result.state=result.active.present?State::known:State::infeasible;return result;
}
inline Result solve(const Input& input) {return solve(input,[] {return true;});}
} // namespace plan3_commitment
