#pragma once
#include "search.h"
#include "../common/policy.h"

namespace plan3_belief_search {
inline Key key(const plan3::JobId& id){return {int(id.kind),id.firstGroup,id.secondGroup,id.altarTile,id.equipmentBlock};}
inline plan3::JobId job_id(const Key& id){return {plan3::JobKind(id[0]),id[1],id[2],id[3],id[4]};}
struct Mapping {
    Input input;
    State state=State::unknown;
    unsigned canonicalCount=0,rejected=0,unknown=0;
    bool prefix=false;
};
inline bool probabilities(const std::array<double,maximum_outcomes>& weights,unsigned count,
                          std::array<unsigned,maximum_outcomes>& integers) {
    double sum=0;for(unsigned n=0;n<count;++n){if(!std::isfinite(weights[n]) || weights[n]<0 || weights[n]>1)return false;sum+=weights[n];}
    if(!(sum>0) || !std::isfinite(sum))return false;
    std::array<double,maximum_outcomes> remainders{};std::array<unsigned,maximum_outcomes> order{0,1,2};unsigned assigned=0;
    for(unsigned n=0;n<count;++n) {
        const double exact=weights[n]/sum*probability_total;integers[n]=unsigned(std::floor(exact));assigned+=integers[n];remainders[n]=exact-integers[n];
    }
    if(assigned>probability_total || probability_total-assigned>count)return false;
    std::sort(order.begin(),order.begin()+count,[&](unsigned a,unsigned b){return remainders[a]==remainders[b]?a<b:remainders[a]>remainders[b];});
    for(unsigned n=0;n<probability_total-assigned;++n)++integers[order[n]];
    return true;
}
template<class Ready> Mapping map_jobs(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
        const Belief& prior,bool completeMenu,Ready&& ready) {
    Mapping mapped;
    if(!completeMenu || !valid_belief(prior) || info.round<0 || info.roundLimit<info.round || info.carriedGems!=0)return mapped;
    mapped.input.horizon=unsigned(info.horizon());mapped.input.belief=normalized(prior);
    const auto menu=plan3::canonical_menu(jobs);mapped.canonicalCount=unsigned(menu.jobs.size());mapped.prefix=menu.truncated;
    for(const auto* job:menu.jobs) {
        if(!ready()){mapped.state=State::cutoff;mapped.input.actionCount=0;return mapped;}
        if(!job->complete || !job->routeKnown){++mapped.unknown;mapped.prefix=true;continue;}
        if(!plan3::admitted(info,*job,false)){++mapped.rejected;continue;}
        Action action;action.key=key(job->id);action.groups={job->id.firstGroup,job->id.secondGroup};
        if(job->projectedRenewalAndReturnTime<0 || job->projectedRenewalAndReturnTime>8192){++mapped.unknown;mapped.prefix=true;continue;}
        action.recovery=unsigned(job->projectedRenewalAndReturnTime);
        std::array<std::array<double,maximum_outcomes>,modes> weights{};
        std::array<unsigned,modes> lifecycleMask{};bool known=true;
        for(const auto& outcome:job->outcomes) {
            if(!ready()){mapped.state=State::cutoff;mapped.input.actionCount=0;return mapped;}
            const unsigned world=unsigned(outcome.response);
            if(world>=modes || outcome.lifecycle<0 || outcome.lifecycle>=int(maximum_outcomes) || outcome.provenance==plan3::Provenance::unknown ||
               outcome.deltaS<0 || outcome.deltaS>1000000 || outcome.tau<1 || outcome.tau>8192 ||
               !std::isfinite(outcome.conditionalLifecycleWeight) || outcome.conditionalLifecycleWeight<0 || outcome.conditionalLifecycleWeight>1 ||
               (outcome.deltaS==0 && !outcome.conversions.empty()) || (outcome.deltaS>0 &&
               (outcome.conversions.size()!=1 || outcome.conversions[0].gems!=outcome.deltaS || outcome.conversions[0].completedAt!=outcome.tau))) {known=false;break;}
            const unsigned bit=1u<<unsigned(outcome.lifecycle);if(lifecycleMask[world]&bit){known=false;break;}
            lifecycleMask[world]|=bit;++action.counts[world];const unsigned index=unsigned(outcome.lifecycle);
            action.outcomes[world][index]={0,unsigned(outcome.deltaS),unsigned(outcome.tau)};weights[world][index]=outcome.conditionalLifecycleWeight;
        }
        for(unsigned s=0;s<modes && known;++s) {
            if(!lifecycleMask[s] || lifecycleMask[s]!=lifecycleMask[0]){known=false;break;}
            std::array<unsigned,maximum_outcomes> counts{};std::array<double,maximum_outcomes> canonicalWeights{};
            std::array<Outcome,maximum_outcomes> canonicalOutcomes{};unsigned at=0;
            for(unsigned life=0;life<maximum_outcomes;++life)if(lifecycleMask[s]&(1u<<life)) {
                canonicalWeights[at]=weights[s][life];canonicalOutcomes[at++]=action.outcomes[s][life];
            }
            action.outcomes[s]=canonicalOutcomes;
            if(!probabilities(canonicalWeights,action.counts[s],counts)){known=false;break;}
            for(unsigned n=0;n<action.counts[s];++n)action.outcomes[s][n].weight=counts[n];
        }
        if(!known || !valid_action(action)){++mapped.unknown;mapped.prefix=true;continue;}
        mapped.input.actions[mapped.input.actionCount++]=std::move(action);
    }
    mapped.state=State::known;return mapped;
}
inline Mapping map_jobs(const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,const Belief& prior,bool completeMenu=true) {
    return map_jobs(info,jobs,prior,completeMenu,[]{return true;});
}
struct Ticket {
    std::uint64_t token=0;
    int round=-1;
    Action action;
    Belief prior{};
};
class Policy {
    std::optional<Belief> belief_;
    std::optional<Input> cached_;
    int cachedRound_=-1;
    Result result_;
    std::array<Ticket,2> tickets_;
    std::optional<Ticket> committed_;
    std::uint64_t nextToken_=1;
public:
    unsigned updates=0,modelGaps=0,censored=0,solves=0;
    unsigned canonicalCount=0,mappedCount=0,unknownCount=0;
    const std::optional<Belief>& belief() const{return belief_;}
    const Result& result() const{return result_;}
    template<class Ready,class StartReady> plan3::JobIntent choose(bool control,const plan3::InformationState& info,
            const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context,Ready&& ready,StartReady&& startReady) {
        const auto stopped=[&](State state,Reason reason=Reason::cutoff) {
            cached_.reset();result_={};result_.state=state;result_.reason=reason;
            return plan3::JobIntent{plan3::IntentStatus::yield,std::nullopt,0,plan3::DecisionReason::no_admitted_job};
        };
        if(!ready())return stopped(State::cutoff);
        const auto prior=belief_.value_or(context.belief.weights);
        auto mapped=map_jobs(info,jobs,prior,context.menuComplete,ready);
        canonicalCount=mapped.canonicalCount;mappedCount=mapped.input.actionCount;unknownCount=mapped.unknown;
        if(mapped.state!=State::known || !mapped.input.actionCount)return stopped(mapped.state,Reason::no_actions);
        if(!cached_ || cachedRound_!=info.round || !(*cached_==mapped.input)) {
            if(!startReady())return stopped(State::cutoff);
            auto result=evaluate(mapped.input,ready);++solves;
            if(!ready())return stopped(State::cutoff);
            if(result.state!=State::known || !result.active.present || !result.control.present)return stopped(result.state,result.reason);
            result_=std::move(result);cachedRound_=info.round;cached_=mapped.input;
        }
        if(!ready())return stopped(State::cutoff);
        const auto& selected=control?result_.control:result_.active;
        if(!selected.present || !nextToken_)return stopped(State::invalid);
        // Seed only with an actual whole, publication-ready choice. Subsequent
        // shared updates are diagnostic; endpoint Bayes owns this posterior.
        if(!belief_)belief_=mapped.input.belief;
        auto& ticket=tickets_[unsigned(control)];ticket={nextToken_++,info.round,mapped.input.actions[selected.action],*belief_};
        auto intent=plan3::JobIntent{plan3::IntentStatus::selected,job_id(selected.key),selected.value,plan3::DecisionReason::objective,false,mapped.prefix};
        intent.policyToken=ticket.token;return intent;
    }
    void observe(const plan3::PolicyEvent& event) {
        if(!event.token)return;
        if(event.kind==plan3::PolicyEvent::Kind::committed) {
            for(const auto& ticket:tickets_)if(ticket.token==event.token && ticket.round==event.startedRound &&
                ticket.round==event.round && ticket.action.key==key(event.job)){committed_=ticket;return;}
            ++censored;return;
        }
        if(!committed_ || committed_->token!=event.token || committed_->round!=event.startedRound ||
           committed_->action.key!=key(event.job)){++censored;return;}
        auto ticket=std::move(*committed_);committed_.reset();cached_.reset();
        if(event.kind!=plan3::PolicyEvent::Kind::completed || !event.conversion || !event.exactConverted ||
           event.round<=event.startedRound || event.round-event.startedRound>8192){++censored;return;}
        const auto posterior=bayes(ticket.prior,ticket.action,{*event.exactConverted,unsigned(event.round-event.startedRound)});
        if(!posterior.known){++modelGaps;return;} // Never silently reseed or invent a likelihood floor.
        belief_=posterior.belief;++updates;
    }
};
} // namespace plan3_belief_search
