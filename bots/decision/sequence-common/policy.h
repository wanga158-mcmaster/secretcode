#pragma once
#include "policy-data.h"
#include "../common/policy.h"

namespace plan3_sequence {
enum class State {known,unavailable,cutoff,invalid};
enum class Signal : unsigned {personal_detection,public_score_rise,unavailable};
struct Candidate {plan3::JobId id;int converted=0,duration=1;double minimum=0;};
struct Mapping {
    State state=State::unavailable;
    std::array<std::optional<Candidate>,2> roles{};
    unsigned availability=0,omitted=0;
    bool truncated=false;
};
template<class Ready>
Mapping map_jobs(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,Ready&& ready) {
    Mapping result;
    if(info.round<0 || info.roundLimit<=info.round || info.ownPermanentScore<0 || info.winningGems<=0)return result;
    const auto menu=plan3::canonical_menu(jobs);result.truncated=menu.truncated;
    for(const auto* job:menu.jobs) {
        if(!ready())return Mapping{State::cutoff};
        if(!plan3::admitted(info,*job,true))continue;
        const auto& nominal=*job->nominal();
        if(nominal.deltaS<=0 || nominal.tau<=0)continue;
        bool known=true;double minimum=std::numeric_limits<double>::infinity();std::array<unsigned,4> represented{};
        for(const auto& outcome:job->outcomes) {
            if(!ready())return Mapping{State::cutoff};
            const unsigned response=unsigned(outcome.response);
            if(response>=4 || outcome.lifecycle<0 || outcome.lifecycle>2 || (represented[response]&(1u<<unsigned(outcome.lifecycle)))) {known=false;break;}
            represented[response]|=1u<<unsigned(outcome.lifecycle);
            if(outcome.deltaS<0 || outcome.tau<0 || std::int64_t(info.ownPermanentScore)+outcome.deltaS>INT32_MAX) {known=false;break;}
            const auto value=plan3::outcome_value(info,outcome);
            if(!std::isfinite(value) || value<0) {known=false;break;}
            minimum=std::min(minimum,value);
        }
        for(unsigned response=0;response<4;++response)known=known && represented[response] && represented[response]==represented[0];
        if(!known) {++result.omitted;result.truncated=true;continue;}
        Candidate candidate{job->id,nominal.deltaS,nominal.tau,minimum};
        auto& quick=result.roles[0];auto& resilient=result.roles[1];
        if(!quick || std::int64_t(candidate.converted)*quick->duration>std::int64_t(quick->converted)*candidate.duration ||
           (std::int64_t(candidate.converted)*quick->duration==std::int64_t(quick->converted)*candidate.duration && candidate.id<quick->id))quick=candidate;
        if(!resilient || plan3::strictly_better(candidate.minimum,resilient->minimum) ||
           (!plan3::strictly_better(resilient->minimum,candidate.minimum) && candidate.id<resilient->id))resilient=candidate;
    }
    if(!result.roles[0])return result;
    result.availability=1;
    if(result.roles[1] && !(result.roles[1]->id==result.roles[0]->id))result.availability|=2;
    else result.roles[1].reset();
    result.state=State::known;return result;
}
struct Baseline {int actor=-1,opponentScore=0;double health=0;};
inline Signal literal_signal(const plan3::InformationState& info,const Baseline& before) {
    // Info.champions is the shared Campaign's personal_.enemies view. Missing
    // or stale sightings are UNKNOWN, never an observed opponent role.
    if(info.actorOrdinal!=before.actor)return Signal::unavailable;
    if(std::isfinite(info.health) && std::isfinite(before.health) && info.health<before.health)return Signal::personal_detection;
    if(info.champions.size()<=5)for(const auto& enemy:info.champions)
        if(enemy.position.value && enemy.position.value->tile>=0 && enemy.position.observedRound==info.round &&
           enemy.position.provenance==plan3::Provenance::personally_observed)return Signal::personal_detection;
    if(info.opponentPermanentScore>before.opponentScore)return Signal::public_score_rise;
    return Signal::unavailable;
}
inline std::optional<unsigned> tickets(bool control,bool second,unsigned rootMask,unsigned firstAction,
                                       Signal signal,unsigned availability) {
    if(rootMask<1 || rootMask>3 || availability<1 || availability>3 || unsigned(signal)>2)return std::nullopt;
    if(!second) {
        if(rootMask!=availability)return std::nullopt;
        return policy_tickets[unsigned(control)][rootMask-1][0];
    }
    if(firstAction>1 || !(rootMask&(1u<<firstAction)))return std::nullopt;
    return policy_tickets[unsigned(control)][rootMask-1][1+firstAction*9+unsigned(signal)*3+availability-1];
}
// Prescribed source-visible finite deck, not fresh/independent randomness.
// Bit reversal visits every midpoint slot once per64 COMMITTED decisions.
inline unsigned ticket_slot(unsigned cursor) {
    unsigned result=0;for(unsigned i=0;i<6;++i)result|=((cursor>>i)&1u)<<(5-i);return result;
}
struct Selection {
    State state=State::unavailable;
    plan3::JobIntent intent{};
    unsigned availability=0,action=0,tickets0=0,slot=0,omitted=0;
    Signal signal=Signal::unavailable;
    bool second=false;
};
struct Statistics {unsigned committed=0,completed=0,firstCompleted=0,episodes=0,censored=0;};
class Planner {
    struct Staged {
        std::uint64_t token=0;plan3::JobId job;int round=0;
        bool second=false;unsigned rootMask=0,action=0;Baseline baseline;
    };
    bool governingControl_=false,waitingSecond_=false;
    unsigned rootMask_=0,firstAction_=0;
    Baseline baseline_;
    std::optional<Staged> staged_,pending_;
    Statistics statistics_;
public:
    explicit Planner(bool control=false):governingControl_(control) {}
    const Statistics& statistics() const {return statistics_;}
    bool waiting_second() const {return waitingSecond_;}
    template<class Ready>
    Selection preview(bool control,const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                      const plan3::ControllerContext& context,Ready&& ready) {
        if(!ready())return Selection{State::cutoff};
        if(control==governingControl_)staged_.reset();
        if(pending_) {
            if(!context.activeJob || !(*context.activeJob==pending_->job) || !context.activeJobValid || context.sharedAbort)
                return Selection{State::invalid};
            Selection result;result.state=State::known;
            result.intent={plan3::IntentStatus::preserve,pending_->job,0,plan3::DecisionReason::protected_obligation};return result;
        }
        if(statistics_.committed>=4096 || info.opponentPermanentScore<0 || !std::isfinite(info.health) || info.health<=0)return {};
        const auto mapped=map_jobs(info,jobs,ready);if(mapped.state!=State::known)return Selection{mapped.state};
        Selection result;result.second=waitingSecond_;result.availability=mapped.availability;result.omitted=mapped.omitted;
        result.signal=waitingSecond_?literal_signal(info,baseline_):Signal::unavailable;
        const unsigned root=waitingSecond_?rootMask_:mapped.availability;
        const auto count=tickets(control,waitingSecond_,root,firstAction_,result.signal,mapped.availability);
        if(!count || *count>64)return Selection{State::invalid};
        result.tickets0=*count;result.slot=ticket_slot(statistics_.committed);result.action=result.slot<*count?0:1;
        if(!(mapped.availability&(1u<<result.action)) || !mapped.roles[result.action])return Selection{State::invalid};
        if(!ready())return Selection{State::cutoff};
        result.state=State::known;
        result.intent={plan3::IntentStatus::selected,mapped.roles[result.action]->id,
            double(result.action?64-*count:*count)/64,plan3::DecisionReason::objective,false,mapped.truncated};
        if(control==governingControl_) {
            result.intent.policyToken=std::uint64_t(statistics_.committed)+1;
            staged_=Staged{result.intent.policyToken,*result.intent.job,info.round,waitingSecond_,root,result.action,
                {info.actorOrdinal,info.opponentPermanentScore,info.health}};
        }
        return result;
    }
    Selection preview(bool control,const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                      const plan3::ControllerContext& context={}) {
        return preview(control,info,jobs,context,[] {return true;});
    }
    bool observe(const plan3::PolicyEvent& event) {
        if(!event.token)return true;
        if(event.kind==plan3::PolicyEvent::Kind::committed) {
            if(pending_ || !staged_ || event.token!=staged_->token || !(event.job==staged_->job) ||
               event.startedRound!=staged_->round || event.round!=event.startedRound || event.exactConverted || event.conversion)return false;
            pending_=staged_;staged_.reset();++statistics_.committed;
            if(!pending_->second) {
                rootMask_=pending_->rootMask;firstAction_=pending_->action;baseline_=pending_->baseline;
            }
            return true;
        }
        if((event.kind!=plan3::PolicyEvent::Kind::completed && event.kind!=plan3::PolicyEvent::Kind::aborted) ||
           !pending_ || event.token!=pending_->token || !(event.job==pending_->job) || event.startedRound!=pending_->round ||
           event.round<event.startedRound)return false;
        const bool exact=event.kind==plan3::PolicyEvent::Kind::completed && event.conversion &&
            event.exactConverted && *event.exactConverted>0 && event.round>event.startedRound;
        if(exact) {
            ++statistics_.completed;
            if(!pending_->second) {waitingSecond_=true;++statistics_.firstCompleted;}
            else {waitingSecond_=false;++statistics_.episodes;}
        } else {waitingSecond_=false;++statistics_.censored;}
        pending_.reset();return true;
    }
};
} // namespace plan3_sequence
