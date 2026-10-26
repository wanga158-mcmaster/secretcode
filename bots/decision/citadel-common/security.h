#pragma once
#include "../hindsight-common/regret.h"

// Assumed online class-mean security. Lifecycle masses are first averaged
// within each genuine response class; missing classes remain unknown. This
// is not an empirical win floor or Robust's minimum individual outcome.
namespace plan3_citadel {
using plan3_hindsight::State;
using plan3_hindsight::Reason;
using plan3_hindsight::Row;
using plan3_hindsight::Table;
using plan3_hindsight::Choice;
using plan3_hindsight::map_menu;
struct Result {
    State state=State::unavailable;
    unsigned covered=0,columns=0;
    Choice security,mean;
};
template<class Ready> Result evaluate(const Table& table,Ready&& ready) {
    const auto stop=[](State state){Result result;result.state=state;return result;};
    if(!ready())return stop(State::cutoff);
    if(table.state!=State::known)return stop(table.state);
    if(table.rows.empty() || table.rows.size()>plan3::single_job_cap+plan3::double_job_cap+plan3::nonharvest_job_cap ||
       !table.covered || table.covered>15)return stop(State::invalid);
    Result result;result.covered=table.covered;
    for(unsigned w=0;w<4;++w)result.columns+=bool(table.covered&(1u<<w));
    for(std::size_t i=0;i<table.rows.size();++i) {
        if(!ready())return stop(State::cutoff);
        const auto& row=table.rows[i];
        if(row.mask>15 || (row.mask&table.covered)!=table.covered)return stop(State::invalid);
        for(std::size_t j=0;j<i;++j)if(row.job==table.rows[j].job)return stop(State::invalid);
        double floor=std::numeric_limits<double>::infinity(),sum=0;
        for(unsigned w=0;w<4;++w) {
            if((row.mask&(1u<<w)) && !plan3_hindsight::nonnegative(row.value[w]))return stop(State::invalid);
            if(table.covered&(1u<<w)){floor=std::min(floor,row.value[w]);sum+=row.value[w];}
        }
        const auto adopt=[&](Choice& choice,double value){
            if(!choice.present || value>choice.value || (value==choice.value && row.job<choice.job))choice={true,row.job,value};
        };
        adopt(result.security,floor);adopt(result.mean,sum/result.columns);
    }
    if(!ready())return stop(State::cutoff);
    result.state=State::known;return result;
}
inline Result evaluate(const Table& table){return plan3_citadel::evaluate(table,[]{return true;});}
} // namespace plan3_citadel
