#pragma once
#include "model.h"
#include "../common/policy.h"

namespace plan3_commitment {
struct Selection {
    State state=State::unavailable;
    plan3::JobIntent intent{};
    std::uint64_t token=0;
    unsigned bestResponses=0,omitted=0;
};
struct Statistics {
    unsigned committed=0,completed=0,aborted=0,censored=0,exactSamples=0,switches=0;
    std::uint64_t elapsed=0,abortedElapsed=0,exactConverted=0,assumedSwitchUnits=0;
};
class Planner {
    struct Staged {std::uint64_t token;Key key;int round;std::int64_t cost;};
    struct Pending {std::uint64_t token;Key key;int round;};
    Model model_;
    bool governingControl_=false;
    std::optional<Key> previous_;
    std::optional<Staged> staged_;
    std::optional<Pending> pending_;
    Statistics stats_;
public:
    explicit Planner(bool control=false,Model model={}):model_(model),governingControl_(control) {}
    const Statistics& statistics() const {return stats_;}
    bool pending() const {return pending_.has_value();}
    template<class Ready>
    Selection preview(bool control,const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                      const plan3::ControllerContext& context,Ready&& ready) {
        Selection selected;
        if(!ready()) {selected.state=State::cutoff;return selected;}
        if(pending_) {
            if(!context.activeJob || key(*context.activeJob)!=pending_->key || !context.activeJobValid || context.sharedAbort) {
                selected.state=State::invalid;return selected;
            }
            selected.state=State::known;selected.token=pending_->token;
            selected.intent={plan3::IntentStatus::preserve,context.activeJob,0,plan3::DecisionReason::options_commitment};
            return selected;
        }
        if(control==governingControl_)staged_.reset();
        if(stats_.committed>=4096)return selected;
        const auto mapped=map(info,jobs,model_,previous_,ready);
        if(mapped.state!=State::known) {selected.state=mapped.state;return selected;}
        const auto result=solve(mapped.input,ready);
        selected.state=result.state;selected.omitted=mapped.omittedUnknown;
        const auto& choice=control?result.control:result.active;
        if(result.state!=State::known || !choice.present)return selected;
        if(!ready())return Selection{State::cutoff};
        selected.intent={plan3::IntentStatus::selected,job_id(choice.key),double(choice.value)/model_.units,
            plan3::DecisionReason::objective,false,mapped.truncated};
        selected.bestResponses=result.rows[choice.row].bestResponses;
        if(control==governingControl_) {
            selected.token=std::uint64_t(stats_.committed)+1;
            staged_=Staged{selected.token,choice.key,info.round,result.rows[choice.row].switchCost};
        }
        return selected;
    }
    Selection preview(bool control,const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                      const plan3::ControllerContext& context={}) {
        return preview(control,info,jobs,context,[] {return true;});
    }
    bool observe(const plan3::PolicyEvent& event) {
        if(!event.token)return true;
        if(event.kind==plan3::PolicyEvent::Kind::committed) {
            if(pending_ || !staged_ || event.token!=staged_->token || key(event.job)!=staged_->key ||
               event.startedRound!=staged_->round || event.round!=event.startedRound || event.exactConverted)return false;
            pending_=Pending{event.token,key(event.job),event.startedRound};previous_=key(event.job);
            ++stats_.committed;
            if(staged_->cost>0) {++stats_.switches;stats_.assumedSwitchUnits+=std::uint64_t(staged_->cost);}
            staged_.reset();return true;
        }
        if((event.kind!=plan3::PolicyEvent::Kind::completed && event.kind!=plan3::PolicyEvent::Kind::aborted) ||
           !pending_ || event.token!=pending_->token || key(event.job)!=pending_->key ||
           event.startedRound!=pending_->round || event.round<event.startedRound ||
           (event.round==event.startedRound && event.exactConverted && *event.exactConverted>0))return false;
        const auto elapsed=std::uint64_t(event.round-event.startedRound);stats_.elapsed+=elapsed;
        if(event.kind==plan3::PolicyEvent::Kind::completed)++stats_.completed;
        else {++stats_.aborted;stats_.abortedElapsed+=elapsed;}
        if(event.exactConverted) {++stats_.exactSamples;stats_.exactConverted+=*event.exactConverted;}
        else ++stats_.censored;
        pending_.reset();return true;
    }
};
} // namespace plan3_commitment
