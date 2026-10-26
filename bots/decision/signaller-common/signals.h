#pragma once
#include <array>
#include <cstdint>

namespace plan3_signaller {
inline constexpr unsigned maximum_jobs=28,maximum_mass=64;
inline constexpr std::int64_t maximum_value=1000000000000LL;
using Key=std::array<std::int32_t,5>;
using Weights=std::array<unsigned,4>;
enum class State {known,infeasible,unavailable,invalid,cutoff};
enum class Signal {none,home,contested,forward};
struct Row {
    Key key{};
    bool known=false;
    Signal signal=Signal::none;
    std::array<std::int64_t,4> ownOutput{};
    Weights response{};
    std::int64_t additionalCost=0;
};
struct Input {
    unsigned jobCount=0;
    std::array<Row,maximum_jobs> jobs{};
    Weights common{8,4,2,2};
};
struct Fraction {std::int64_t numerator=0;unsigned denominator=1;};
struct Choice {bool present=false;Key key{};unsigned row=0;Fraction value;};
struct Result {State state=State::unavailable;Choice active,control;unsigned work=0;};
inline bool valid_key(const Key& key) {
    if(key[0]<0 || key[0]>5)return false;
    for(unsigned i=1;i<5;++i)if(key[i]<-1)return false;
    return true;
}
inline unsigned mass(const Weights& weights) {
    unsigned result=0;
    for(auto weight:weights) {if(weight>maximum_mass)return 0;result+=weight;}
    return result && result<=maximum_mass?result:0;
}
inline bool better(const Fraction& a,const Fraction& b) {
    // |numerator|<=maximum_value*64, denominators<=64, so signed64 suffices.
    return a.numerator*std::int64_t(b.denominator)>b.numerator*std::int64_t(a.denominator);
}
inline Fraction expectation(const Row& row,const Weights& weights) {
    Fraction value;value.denominator=mass(weights);value.numerator=-row.additionalCost*value.denominator;
    for(unsigned r=0;r<4;++r)value.numerator+=row.ownOutput[r]*weights[r];
    return value;
}
template<class Ready>
Result solve(const Input& input,Ready&& ready) {
    Result result;
    const auto step=[&] {++result.work;return ready();};
    const auto stop=[&](State state) {Result empty;empty.state=state;empty.work=result.work;return empty;};
    if(!step())return stop(State::cutoff);
    if(input.jobCount>maximum_jobs || !mass(input.common))return stop(State::invalid);
    bool unknown=false;
    for(unsigned j=0;j<input.jobCount;++j) {
        if(!step())return stop(State::cutoff);
        const auto& row=input.jobs[j];
        if(!valid_key(row.key) || static_cast<unsigned>(row.signal)>3 || !mass(row.response) ||
           row.additionalCost<0 || row.additionalCost>maximum_value)return stop(State::invalid);
        for(unsigned p=0;p<j;++p) {
            if(!step())return stop(State::cutoff);
            if(input.jobs[p].key==row.key)return stop(State::invalid);
        }
        for(auto value:row.ownOutput) {
            if(!step())return stop(State::cutoff);
            if(value<0 || value>maximum_value)return stop(State::invalid);
        }
        unknown|=!row.known;
    }
    if(unknown)return stop(State::unavailable);
    const auto nominate=[](Choice& choice,const Row& row,unsigned index,Fraction value) {
        if(!choice.present || better(value,choice.value) ||
           (!better(choice.value,value) && row.key<choice.key))choice={true,row.key,index,value};
    };
    for(unsigned j=0;j<input.jobCount;++j) {
        if(!step())return stop(State::cutoff);
        nominate(result.active,input.jobs[j],j,expectation(input.jobs[j],input.jobs[j].response));
        if(!step())return stop(State::cutoff);
        nominate(result.control,input.jobs[j],j,expectation(input.jobs[j],input.common));
    }
    result.state=result.active.present?State::known:State::infeasible;return result;
}
inline Result solve(const Input& input) {return solve(input,[] {return true;});}
} // namespace plan3_signaller
