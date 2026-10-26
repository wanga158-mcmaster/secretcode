#pragma once
#include "../common/policy.h"
#include <array>

// Assumed online job-level Savage regret, not measured campaign regret.
namespace plan3_hindsight {
inline constexpr unsigned worlds=4;
enum class State { known, unavailable, cutoff, invalid };
enum class Reason { none, snapshot, no_jobs, outcome, no_common_world, cutoff };
struct Row {plan3::JobId job;std::array<double,worlds> value{};unsigned mask=0;};
struct Table {
    State state=State::unavailable;Reason reason=Reason::no_jobs;
    std::vector<Row> rows;unsigned covered=0,represented=0,canonicalCount=0;bool truncated=false;
};
struct Choice {bool present=false;plan3::JobId job;double value=0;};
struct Result {State state=State::unavailable;unsigned covered=0;std::array<double,worlds> oracle{};Choice regret,security;};
inline bool nonnegative(double n){return std::isfinite(n) && n>=0 && n<=1e12;}
inline bool supported_outcome(const plan3::InformationState& info,const plan3::Outcome& o,double& value) {
    if(o.tau<0 || o.deltaS<0 || !nonnegative(o.healthLoss) || !nonnegative(o.terminal.health) ||
       !nonnegative(o.terminal.maximumHealth) || o.terminal.bag<0 || o.conversions.size()>8 ||
       (o.minimumHealth && (!nonnegative(*o.minimumHealth) || *o.minimumHealth>info.maximumHealth)))return false;
    std::int64_t gems=0;int previous=-1;
    for(const auto& c:o.conversions) {
        if(c.completedAt<previous || c.completedAt<0 || c.completedAt>o.tau || c.gems<0)return false;
        previous=c.completedAt;gems+=c.gems;if(gems>INT32_MAX)return false;
    }
    if(gems!=o.deltaS)return false;
    value=plan3::outcome_value(info,o);return nonnegative(value);
}
template<class Ready>
Table map_menu(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,Ready&& ready) {
    Table table;
    const auto stop=[&](State s,Reason r){table.state=s;table.reason=r;table.rows.clear();table.covered=table.represented=0;return std::move(table);};
    if(!ready())return stop(State::cutoff,Reason::cutoff);
    if(info.round<0 || info.roundLimit<=info.round || !nonnegative(info.health) || info.health<=0 ||
       !nonnegative(info.maximumHealth) || info.maximumHealth<info.health || info.carriedGems<0)
        return stop(State::invalid,Reason::snapshot);
    const auto menu=plan3::canonical_menu(jobs);table.canonicalCount=unsigned(menu.jobs.size());table.truncated=menu.truncated;
    unsigned intersection=(1u<<worlds)-1;
    for(const auto* job:menu.jobs) {
        if(!ready())return stop(State::cutoff,Reason::cutoff);
        if(!plan3::admitted(info,*job,false))continue;
        Row row;row.job=job->id;std::array<double,worlds> weights{};std::array<unsigned,worlds> lifecycles{};
        for(const auto& outcome:job->outcomes) {
            if(!ready())return stop(State::cutoff,Reason::cutoff);
            const auto w=unsigned(outcome.response);
            if(w>=worlds || outcome.lifecycle<0 || outcome.lifecycle>2 ||
               !std::isfinite(outcome.conditionalLifecycleWeight) || outcome.conditionalLifecycleWeight<0 ||
               outcome.conditionalLifecycleWeight>1)return stop(State::unavailable,Reason::outcome);
            const auto bit=1u<<unsigned(outcome.lifecycle);
            if(lifecycles[w]&bit)return stop(State::unavailable,Reason::outcome);
            lifecycles[w]|=bit;
            double value=0;if(!supported_outcome(info,outcome,value))return stop(State::unavailable,Reason::outcome);
            weights[w]+=outcome.conditionalLifecycleWeight;row.value[w]+=value*outcome.conditionalLifecycleWeight;
        }
        for(unsigned w=0;w<worlds;++w)if(lifecycles[w]) {
            if(std::abs(weights[w]-1)>1e-9 || !nonnegative(row.value[w]))return stop(State::unavailable,Reason::outcome);
            // Conditional lifecycle means are job-local assumptions, not
            // jointly observed or learned counterfactual states.
            row.value[w]/=weights[w];row.mask|=1u<<w;
        }
        intersection&=row.mask;table.represented|=row.mask;table.rows.push_back(row);
    }
    if(table.rows.empty())return stop(State::unavailable,Reason::no_jobs);
    if(!intersection)return stop(State::unavailable,Reason::no_common_world);
    if(!ready())return stop(State::cutoff,Reason::cutoff);
    table.covered=intersection;table.state=State::known;table.reason=Reason::none;return table;
}
template<class Ready> Result evaluate(const Table& table,Ready&& ready) {
    Result result;
    if(table.state!=State::known || table.rows.empty() || table.rows.size()>plan3::single_job_cap+plan3::double_job_cap+plan3::nonharvest_job_cap || !table.covered || table.covered>15)return result;
    result.covered=table.covered;
    const auto stop=[](State state){Result stopped;stopped.state=state;return stopped;};
    for(const auto& row:table.rows) {
        if(!ready())return stop(State::cutoff);
        if((row.mask&table.covered)!=table.covered)return stop(State::invalid);
        for(unsigned w=0;w<worlds;++w)if(table.covered&(1u<<w)) {
            if(!nonnegative(row.value[w]))return stop(State::invalid);
            result.oracle[w]=std::max(result.oracle[w],row.value[w]);
        }
    }
    for(const auto& row:table.rows) {
        if(!ready())return stop(State::cutoff);
        double regret=0,security=std::numeric_limits<double>::infinity();
        for(unsigned w=0;w<worlds;++w)if(table.covered&(1u<<w)) {
            regret=std::max(regret,result.oracle[w]-row.value[w]);security=std::min(security,row.value[w]);
        }
        if(!result.regret.present || regret<result.regret.value || (regret==result.regret.value && row.job<result.regret.job))result.regret={true,row.job,regret};
        if(!result.security.present || security>result.security.value || (security==result.security.value && row.job<result.security.job))result.security={true,row.job,security};
    }
    if(!ready())return stop(State::cutoff);
    result.state=State::known;return result;
}
inline Result evaluate(const Table& table){return evaluate(table,[]{return true;});}
} // namespace plan3_hindsight
