#pragma once
#include "../hindsight-common/regret.h"

// Online downside-budget optimization on declared response/lifecycle forecasts.
// The common-column prior is uniform and unfitted. Values are outcome_value
// units, not empirical win payoffs or calibrated safety guarantees.
namespace plan3_underwriter {
using plan3_hindsight::State;
using plan3_hindsight::Reason;
using plan3_hindsight::Row;
using plan3_hindsight::Table;
using plan3_hindsight::Choice;
using plan3_hindsight::map_menu;
inline constexpr double default_delta=10;
struct Result {
    State state=State::unavailable;
    unsigned covered=0,columns=0;
    double referenceFloor=0,threshold=0;
    Choice constrained,unconstrained;
};
template<class Ready> Result evaluate(const Table& table,double delta,Ready&& ready) {
    const auto stop=[](State state){Result result;result.state=state;return result;};
    if(!ready())return stop(State::cutoff);
    if(!std::isfinite(delta) || delta<0 || delta>1e12)return stop(State::invalid);
    if(table.state!=State::known)return stop(table.state);
    if(table.rows.empty() || table.rows.size()>plan3::single_job_cap+plan3::double_job_cap+plan3::nonharvest_job_cap ||
       !table.covered || table.covered>15)return stop(State::invalid);
    Result result;result.covered=table.covered;
    for(unsigned w=0;w<4;++w)result.columns+=bool(table.covered&(1u<<w));
    // Validate the whole represented table before producing either arm.
    for(std::size_t i=0;i<table.rows.size();++i) {
        if(!ready())return stop(State::cutoff);
        const auto& row=table.rows[i];
        if(row.mask>15 || (row.mask&table.covered)!=table.covered)return stop(State::invalid);
        for(std::size_t j=0;j<i;++j)if(row.job==table.rows[j].job)return stop(State::invalid);
        double floor=std::numeric_limits<double>::infinity();
        for(unsigned w=0;w<4;++w) {
            if((row.mask&(1u<<w)) && !plan3_hindsight::nonnegative(row.value[w]))return stop(State::invalid);
            if(table.covered&(1u<<w))floor=std::min(floor,row.value[w]);
        }
        result.referenceFloor=std::max(result.referenceFloor,floor);
    }
    result.threshold=result.referenceFloor-delta; // No fabricated zero floor.
    for(const auto& row:table.rows) {
        if(!ready())return stop(State::cutoff);
        double mean=0,floor=std::numeric_limits<double>::infinity();
        for(unsigned w=0;w<4;++w)if(table.covered&(1u<<w)) {
            mean+=row.value[w]/result.columns;floor=std::min(floor,row.value[w]);
        }
        const auto adopt=[&](Choice& choice){
            if(!choice.present || mean>choice.value || (mean==choice.value && row.job<choice.job))choice={true,row.job,mean};
        };
        adopt(result.unconstrained);
        if(floor>=result.threshold)adopt(result.constrained);
    }
    if(!ready())return stop(State::cutoff);
    if(!result.constrained.present || !result.unconstrained.present)return stop(State::invalid);
    result.state=State::known;return result;
}
inline Result evaluate(const Table& table,double delta=default_delta){return evaluate(table,delta,[]{return true;});}
} // namespace plan3_underwriter
