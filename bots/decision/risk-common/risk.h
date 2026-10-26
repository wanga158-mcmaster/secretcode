#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

// Exact finite models, not a calibration or native probability guarantee.
// Array positions 0..worldCount-1 are the common canonical world order.
// Values use one caller-declared exact nonnegative unit across every job.
namespace plan3_risk {
inline constexpr unsigned maximum_worlds=4,maximum_jobs=28,maximum_weight=4096;
inline constexpr std::uint64_t maximum_value=1000000000000ULL;
static_assert(maximum_value<=std::numeric_limits<std::uint64_t>::max()/
              (std::uint64_t(maximum_weight)*maximum_weight),"Exact expectation bound");
using JobId=std::array<std::int32_t,5>;
// Numeric IDs are opaque here; SDK legality and job admission belong to callers.
struct Fraction32 {std::uint32_t numerator=0,denominator=1;};
struct Fraction {std::uint64_t numerator=0;std::uint32_t denominator=1;};
struct Outcome {
    std::uint64_t value=0;
    bool known=false,lossKnown=false,loss=false;
};
struct Job {
    JobId key{3,-1,-1,-1,-1};
    bool complete=false;
    unsigned outcomeCount=0;
    std::array<Outcome,maximum_worlds> outcomes{};
};
struct Input {
    unsigned worldCount=0,jobCount=0;
    // Counts are normalized by their positive sum, at most4096. Zero-weight
    // worlds still require known outcomes: the TV ball may put mass there.
    std::array<std::uint32_t,maximum_worlds> probabilityWeights{};
    Fraction32 radius{},budget{};
    std::array<Job,maximum_jobs> jobs{};
};
enum class State {known,unavailable,invalid};
enum class Reason {none,world_count,job_count,weights,radius,budget,job_id,
                   duplicate_job,outcome_count,value_bound,incomplete,unknown_outcome};
struct Row {
    JobId key{3,-1,-1,-1,-1};
    Fraction nominal{},worst{},minimum{},nominalLoss{},upperLoss{};
    // Probability numerators over Result.denominator; no rounding occurs.
    std::array<std::uint32_t,maximum_worlds> worstProbabilities{},upperLossProbabilities{};
    bool ruinAdmitted=false;
};
struct Choice {
    bool present=false;
    unsigned row=0;
    JobId key{3,-1,-1,-1,-1};
    Fraction score{};
};
struct Result {
    State state=State::invalid;
    Reason reason=Reason::none;
    unsigned rowCount=0;
    std::uint32_t denominator=1;
    std::array<Row,maximum_jobs> rows{};
    Choice ambiguity{},belief{},minimax{},ruin{};
    // known with no ruin choice is genuine finite-model infeasibility; its
    // loss budget is never relaxed. Belief is Ruin's unconstrained control.
};
namespace detail {
struct Validation {State state;Reason reason;std::uint32_t weight;};
inline bool probability(Fraction32 p) {
    return p.denominator>0 && p.denominator<=maximum_weight && p.numerator<=p.denominator;
}
inline Validation validate(const Input& input) {
    const auto invalid=[](Reason reason){return Validation{State::invalid,reason,0};};
    if(!input.worldCount || input.worldCount>maximum_worlds)return invalid(Reason::world_count);
    if(!input.jobCount || input.jobCount>maximum_jobs)return invalid(Reason::job_count);
    if(!probability(input.radius))return invalid(Reason::radius);
    if(!probability(input.budget))return invalid(Reason::budget);
    std::uint64_t weight=0;
    for(unsigned w=0;w<input.worldCount;++w) {
        if(input.probabilityWeights[w]>maximum_weight)return invalid(Reason::weights);
        weight+=input.probabilityWeights[w];
    }
    if(!weight || weight>maximum_weight)return invalid(Reason::weights);
    Reason unknown=Reason::none;
    for(unsigned j=0;j<input.jobCount;++j) {
        const auto& job=input.jobs[j];
        if(job.key[0]<0 || job.key[0]>5)return invalid(Reason::job_id);
        for(const auto part:job.key)if(part< -1)return invalid(Reason::job_id);
        for(unsigned prior=0;prior<j;++prior)
            if(input.jobs[prior].key==job.key)return invalid(Reason::duplicate_job);
        if(job.outcomeCount>input.worldCount)return invalid(Reason::outcome_count);
        if((!job.complete || job.outcomeCount<input.worldCount) && unknown==Reason::none)unknown=Reason::incomplete;
        for(unsigned w=0;w<job.outcomeCount;++w) {
            const auto& outcome=job.outcomes[w];
            if(outcome.value>maximum_value)return invalid(Reason::value_bound);
            if((!outcome.known || !outcome.lossKnown) && unknown==Reason::none)unknown=Reason::unknown_outcome;
        }
    }
    // Invalid represented data takes precedence over an earlier unknown row.
    return {unknown==Reason::none?State::known:State::unavailable,unknown,std::uint32_t(weight)};
}
struct Extreme {
    std::uint64_t numerator=0;
    std::array<std::uint32_t,maximum_worlds> probabilities{};
};
inline Extreme extreme(const Input& input,const std::array<std::uint64_t,maximum_worlds>& values,
                       std::uint32_t weight,bool maximize) {
    Extreme result;
    unsigned target=0;
    std::array<unsigned,maximum_worlds> donors{};
    for(unsigned w=0;w<input.worldCount;++w) {
        donors[w]=w;result.probabilities[w]=input.probabilityWeights[w]*input.radius.denominator;
        if(maximize?values[w]>values[target]:values[w]<values[target])target=w;
    }
    std::sort(donors.begin(),donors.begin()+input.worldCount,[&](unsigned a,unsigned b) {
        if(values[a]==values[b])return a<b;
        return maximize?values[a]<values[b]:values[a]>values[b];
    });
    std::uint32_t remaining=input.radius.numerator*weight;
    for(unsigned n=0;n<input.worldCount;++n) {
        const unsigned donor=donors[n];
        const bool improves=maximize?values[target]>values[donor]:values[donor]>values[target];
        if(!remaining || !improves)break;
        const auto moved=std::min(result.probabilities[donor],remaining);
        result.probabilities[donor]-=moved;result.probabilities[target]+=moved;remaining-=moved;
    }
    for(unsigned w=0;w<input.worldCount;++w)result.numerator+=std::uint64_t(result.probabilities[w])*values[w];
    return result;
}
inline void choose(Choice& choice,unsigned row,const JobId& key,Fraction score) {
    // Every score in one Result has exactly the same denominator.
    if(!choice.present || score.numerator>choice.score.numerator ||
       (score.numerator==choice.score.numerator && key<choice.key))choice={true,row,key,score};
}
} // namespace detail
inline Result evaluate(const Input& input) {
    const auto checked=detail::validate(input);
    Result result;result.state=checked.state;result.reason=checked.reason;
    if(checked.state!=State::known)return result; // No partial rows or choices.
    // D<=4096^2=2^24. The largest value numerator is D*10^12<2^64;
    // normalization bounds the SUM by that product, not four such products.
    const auto denominator=checked.weight*input.radius.denominator;
    result.denominator=denominator;result.rowCount=input.jobCount;
    for(unsigned j=0;j<input.jobCount;++j) {
        const auto& job=input.jobs[j];auto& row=result.rows[j];row.key=job.key;
        std::array<std::uint64_t,maximum_worlds> values{},loss{};
        std::uint64_t nominal=0,nominal_loss=0,minimum=maximum_value;
        for(unsigned w=0;w<input.worldCount;++w) {
            values[w]=job.outcomes[w].value;loss[w]=unsigned(job.outcomes[w].loss);
            minimum=std::min(minimum,values[w]);
            const auto probability=input.probabilityWeights[w]*input.radius.denominator;
            nominal+=std::uint64_t(probability)*values[w];nominal_loss+=std::uint64_t(probability)*loss[w];
        }
        const auto worst=detail::extreme(input,values,checked.weight,false);
        const auto upper=detail::extreme(input,loss,checked.weight,true);
        row.nominal={nominal,denominator};row.worst={worst.numerator,denominator};
        row.minimum={minimum*denominator,denominator};row.nominalLoss={nominal_loss,denominator};
        row.upperLoss={upper.numerator,denominator};
        row.worstProbabilities=worst.probabilities;row.upperLossProbabilities=upper.probabilities;
        // These cross products are at most2^24*4096=2^36.
        row.ruinAdmitted=upper.numerator*input.budget.denominator<=std::uint64_t(input.budget.numerator)*denominator;
        detail::choose(result.ambiguity,j,job.key,row.worst);
        detail::choose(result.belief,j,job.key,row.nominal);
        detail::choose(result.minimax,j,job.key,row.minimum);
        if(row.ruinAdmitted)detail::choose(result.ruin,j,job.key,row.nominal);
    }
    return result;
}
} // namespace plan3_risk
