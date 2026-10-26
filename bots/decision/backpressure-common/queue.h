#pragma once
// SDK-free closed actor-local queue model. This never validates a production
// forecast's native/helper input domain; the versioned bridge must do that first.
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace plan3_backpressure {
inline constexpr unsigned maximum_jobs=28,maximum_groups=32;
using Wide=unsigned __int128;
using JobKey=std::array<int,5>; // kind,first,second,altar,equipment; numeric tuple order.
enum class State { known,unavailable,cutoff };
enum class Lifecycle { unknown,alive,reward,respawning };
enum class Reason { none,unsupported_kind,incomplete,source_domain,route,full_clear,recovery,
    unknown_dependency,stale_dependency,respawning,invalid_quantity,invalid_id,duplicate_dependency,
    invalid_time,horizon,attribution,score_domain,early_win,no_service,required_renew,
    invalid_snapshot,cutoff,post_queue_consistency };
struct GroupInput {
    int key=-1;
    std::int64_t gems=0;
    Lifecycle lifecycle=Lifecycle::unknown;
    int observedRound=-1;
    State state=State::known;
};
struct JobInput {
    JobKey key{3,-1,-1,-1,-1};
    std::int64_t nominalTau=0,renewalReturn=0,converted=0;
    State state=State::known;
    Reason unavailableReason=Reason::incomplete;
    bool complete=true,nominalKnown=true,routeKnown=true,fullClearAdmitted=true,recoveryAdmitted=true;
    bool sourceDomainValidated=false,censored=false,renewalRequired=false;
};
struct Input {
    int snapshotRound=0,observationRound=0,roundLimit=0,width=0,height=0;
    std::int64_t carriedGems=0,ownPermanentScore=0,winningGems=0;
    bool submitting=false,candidatePrefix=false;
    State state=State::known;
    unsigned groupCount=0,jobCount=0;
    std::array<GroupInput,maximum_groups> groups{};
    std::array<JobInput,maximum_jobs> jobs{};
};
struct Score { Wide numerator=0;unsigned denominator=1; };
struct Row {
    JobKey key{3,-1,-1,-1,-1};
    State state=State::unavailable;
    Reason reason=Reason::incomplete;
    bool eligible=false,requiredRenew=false;
    unsigned duration=0;
    std::uint64_t converted=0;
    std::array<std::uint64_t,4> service{};
    Score pressure{},rate{};
};
struct Choice {
    bool present=false;
    unsigned row=0;
    JobKey key{3,-1,-1,-1,-1};
    Score score{};
};
struct PairResult {
    State state=State::unavailable;
    Reason reason=Reason::invalid_snapshot;
    bool candidatePrefix=false;
    unsigned horizon=0,rowCount=0,eligibleCount=0,rejectedCount=0,omittedDependencyCount=0;
    std::array<std::uint64_t,4> queues{};
    std::vector<Row> rows;
    Choice active{},control{},requiredRenew{};
};
inline std::string decimal(Wide value) {
    char buffer[40];unsigned at=40;
    do {buffer[--at]=char('0'+value%10);value/=10;}while(value);
    return {buffer+at,buffer+40};
}
// Scores produced here have numerator<2^68 and denominator<=128. Cross-products
// are therefore exact in128bits; this is not a generic arbitrary-Wide comparator.
inline int compare(Score a,Score b) {
    const Wide left=a.numerator*b.denominator,right=b.numerator*a.denominator;
    return left>right?1:left<right?-1:0;
}
inline bool quantity(std::int64_t value) {return value>=0 && value<=std::numeric_limits<int>::max();}
inline bool tile(int value,const Input& input) {return value>=0 && value<input.width*input.height;}
inline int group_index(const Input& input,int key) {
    for(unsigned i=0;i<input.groupCount;++i)if(input.groups[i].key==key)return int(i);
    return -1;
}
inline Reason whole_job(const Input& input,const JobInput& job,unsigned horizon,Row& row) {
    row.key=job.key;
    if(job.state!=State::known)return job.state==State::unavailable?job.unavailableReason:Reason::incomplete;
    const int kind=job.key[0];
    if(kind<0 || kind>3)return Reason::unsupported_kind;
    if(!job.complete || !job.nominalKnown || job.censored)return Reason::incomplete;
    if(!job.sourceDomainValidated)return Reason::source_domain;
    if(!job.routeKnown)return Reason::route;
    if((kind==0 || kind==1) && !job.fullClearAdmitted)return Reason::full_clear;
    if(!job.recoveryAdmitted)return Reason::recovery;
    if(!quantity(job.nominalTau) || !quantity(job.renewalReturn) || (kind!=3 && !job.nominalTau))return Reason::invalid_time;
    const auto occupied=std::uint64_t(job.nominalTau)+std::uint64_t(job.renewalReturn);
    if(occupied>std::uint64_t(std::numeric_limits<int>::max()))return Reason::invalid_time;
    row.duration=unsigned(std::max<std::uint64_t>(1,occupied));
    if(job.nominalTau>horizon || row.duration>horizon)return Reason::horizon;
    if(!quantity(job.converted))return Reason::invalid_quantity;
    if(job.key[4]!=-1)return Reason::invalid_id;
    if(kind==3) {
        if(job.key[1]!=-1 || job.key[2]!=-1 || job.key[3]!=-1)return Reason::invalid_id;
        if(job.converted)return Reason::attribution;
        if(!job.renewalRequired)return Reason::no_service;
        row.requiredRenew=true;row.state=State::known;return Reason::required_renew;
    }
    if(!tile(job.key[3],input))return Reason::invalid_id;
    std::uint64_t expected=std::uint64_t(input.carriedGems);
    if(kind==2) {
        if(job.key[1]!=-1 || job.key[2]!=-1)return Reason::invalid_id;
    } else {
        if(!tile(job.key[1],input) || (kind==0 && job.key[2]!=-1) || (kind==1 && !tile(job.key[2],input)))return Reason::invalid_id;
        if(kind==1 && job.key[1]==job.key[2])return Reason::duplicate_dependency;
        for(unsigned leg=0;leg<unsigned(kind==1?2:1);++leg) {
            const int found=group_index(input,job.key[1+leg]);
            if(found<0)return Reason::unknown_dependency;
            const auto& group=input.groups[unsigned(found)];
            if(group.state!=State::known || group.lifecycle==Lifecycle::unknown)return Reason::unknown_dependency;
            if(group.observedRound!=input.snapshotRound)return Reason::stale_dependency;
            if(group.lifecycle==Lifecycle::respawning)return Reason::respawning;
            if(!quantity(group.gems))return Reason::invalid_quantity;
            if(group.lifecycle!=Lifecycle::alive && group.lifecycle!=Lifecycle::reward)return Reason::unknown_dependency;
            expected+=std::uint64_t(group.gems);
            if(expected>std::uint64_t(std::numeric_limits<int>::max()))return Reason::invalid_quantity;
            row.service[group.lifecycle==Lifecycle::alive?0:1]+=std::uint64_t(group.gems);
            if(kind==1 && leg==0 && expected>=std::uint64_t(std::max<std::int64_t>(0,input.winningGems-input.ownPermanentScore)))return Reason::early_win;
        }
    }
    if(expected+std::uint64_t(input.ownPermanentScore)>std::uint64_t(std::numeric_limits<int>::max()))return Reason::score_domain;
    if(expected!=std::uint64_t(job.converted))return Reason::attribution;
    if(!expected)return Reason::no_service;
    row.service[input.submitting?3:2]=std::uint64_t(input.carriedGems);
    row.converted=expected;row.eligible=true;row.state=State::known;return Reason::none;
}
template<class Ready> PairResult evaluate(const Input& input,Ready&& ready) {
    PairResult result;result.candidatePrefix=input.candidatePrefix;
    const auto stop=[&](Reason reason,State state) {
        result.reason=reason;result.state=state;result.active={};result.control={};result.requiredRenew={};
        result.rows.clear();result.rowCount=0;result.queues={};result.eligibleCount=result.rejectedCount=result.omittedDependencyCount=0;
        return std::move(result);
    };
    if(input.state==State::cutoff)return stop(Reason::cutoff,State::cutoff);
    if(input.state!=State::known || input.groupCount>maximum_groups || input.jobCount>maximum_jobs ||
       input.width<1 || input.width>100 || input.height<1 || input.height>100 ||
       input.snapshotRound<0 || input.roundLimit<input.snapshotRound || input.observationRound!=input.snapshotRound ||
       !quantity(input.carriedGems) || !quantity(input.ownPermanentScore) || !quantity(input.winningGems) || !input.winningGems)
        return stop(Reason::invalid_snapshot,State::unavailable);
    result.rows.reserve(input.jobCount);
    result.horizon=unsigned(std::min(128,input.roundLimit-input.snapshotRound));result.rowCount=input.jobCount;
    for(unsigned g=0;g<input.groupCount;++g) {
        if(!ready() || input.groups[g].state==State::cutoff)return stop(Reason::cutoff,State::cutoff);
        if(!tile(input.groups[g].key,input))return stop(Reason::invalid_snapshot,State::unavailable);
        for(unsigned prior=0;prior<g;++prior)if(input.groups[prior].key==input.groups[g].key)return stop(Reason::invalid_snapshot,State::unavailable);
    }
    std::array<bool,maximum_groups> represented{};
    std::array<int,maximum_groups> omittedKeys{};unsigned omittedCount=0,singles=0,doubles=0;
    for(unsigned j=0;j<input.jobCount;++j) {
        if(!ready() || input.jobs[j].state==State::cutoff)return stop(Reason::cutoff,State::cutoff);
        if((input.jobs[j].key[0]==0 && ++singles>16) || (input.jobs[j].key[0]==1 && ++doubles>8))
            return stop(Reason::invalid_snapshot,State::unavailable);
        for(unsigned prior=0;prior<j;++prior)if(input.jobs[prior].key==input.jobs[j].key)return stop(Reason::invalid_snapshot,State::unavailable);
        auto& row=result.rows.emplace_back();row.reason=whole_job(input,input.jobs[j],result.horizon,row);
        if(row.requiredRenew) {
            if(!result.requiredRenew.present || row.key<result.requiredRenew.key)result.requiredRenew={true,j,row.key,{}};
        } else if(row.eligible) {
            ++result.eligibleCount;
            if(row.key[0]==0 || row.key[0]==1)for(unsigned leg=0;leg<unsigned(row.key[0]==1?2:1);++leg)
                represented[unsigned(group_index(input,row.key[1+leg]))]=true;
        } else {
            ++result.rejectedCount;
            row.service={};row.duration=0;row.converted=0;
            if(row.key[0]==0 || row.key[0]==1)for(unsigned leg=0;leg<unsigned(row.key[0]==1?2:1);++leg) {
                const int key=row.key[1+leg];if(!tile(key,input))continue;
                bool found=false;for(unsigned i=0;i<omittedCount;++i)found|=omittedKeys[i]==key;
                if(!found) {
                    if(omittedCount>=maximum_groups)return stop(Reason::invalid_snapshot,State::unavailable);
                    omittedKeys[omittedCount++]=key;
                }
            }
        }
    }
    result.queues[input.submitting?3:2]=std::uint64_t(input.carriedGems);
    for(unsigned g=0;g<input.groupCount;++g) {
        if(!ready())return stop(Reason::cutoff,State::cutoff);
        if(represented[g])result.queues[input.groups[g].lifecycle==Lifecycle::alive?0:1]+=std::uint64_t(input.groups[g].gems);
    }
    for(unsigned i=0;i<omittedCount;++i) {
        const int g=group_index(input,omittedKeys[i]);
        if(g<0 || !represented[unsigned(g)])++result.omittedDependencyCount;
    }
    // All eligibility and stock is now frozen. A consistency defect invalidates
    // the whole pair; it never repairs one row or reconstructs another queue.
    for(unsigned j=0;j<input.jobCount;++j) {
        if(!ready())return stop(Reason::cutoff,State::cutoff);
        auto& row=result.rows[j];if(!row.eligible)continue;
        std::uint64_t sum=0;Wide numerator=0;
        for(unsigned stage=0;stage<4;++stage) {
            if(row.service[stage]>result.queues[stage])return stop(Reason::post_queue_consistency,State::unavailable);
            sum+=row.service[stage];numerator+=Wide(result.queues[stage])*row.service[stage];
        }
        if(sum!=row.converted)return stop(Reason::post_queue_consistency,State::unavailable);
        row.pressure={numerator,row.duration};row.rate={row.converted,row.duration};
    }
    // Fixed pressure then rate pass. Ready cuts never publish only one arm.
    for(unsigned arm=0;arm<2;++arm)for(unsigned j=0;j<input.jobCount;++j) {
        if(!ready())return stop(Reason::cutoff,State::cutoff);
        const auto& row=result.rows[j];if(!row.eligible)continue;
        auto& choice=arm?result.control:result.active;const auto score=arm?row.rate:row.pressure;
        if(!choice.present || compare(score,choice.score)>0 || (compare(score,choice.score)==0 && row.key<choice.key))
            choice={true,j,row.key,score};
    }
    result.state=State::known;result.reason=result.active.present?Reason::none:Reason::no_service;return result;
}
inline PairResult evaluate(const Input& input) {return evaluate(input,[] {return true;});}
} // namespace plan3_backpressure
