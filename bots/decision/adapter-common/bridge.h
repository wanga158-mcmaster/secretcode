#pragma once
#include "experts.h"

namespace plan3_adapter {
struct Selection {
    Status status=Status::unavailable;
    plan3::JobIntent intent{};
    std::uint64_t token=0;
    int expert=-1;
};
class Bridge {
    struct Staged {Decision decision;std::uint64_t token;JobKey job;int round;};
    Bandit bandit_;
    bool governingControl_=false;
    std::optional<Staged> staged_;
public:
    explicit Bridge(bool control=false,Config config={}):bandit_(config),governingControl_(control) {}
    const Bandit& bandit() const {return bandit_;}
    template<class Ready>
    Selection preview(bool control,const plan3::InformationState& info,
                      const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context,
                      Ready&& ready) {
        Selection result;
        if(!ready()) {result.status=Status::cutoff;return result;}
        if(bandit_.pending()) {
            const auto& pending=*bandit_.pending();
            if(!context.activeJob || key(*context.activeJob)!=pending.job || !context.activeJobValid || context.sharedAbort) {
                result.status=Status::invalid;return result; // Missing endpoint event is not a reward observation.
            }
            result.status=Status::pending;result.expert=int(pending.expert);
            result.intent={plan3::IntentStatus::preserve,context.activeJob,0,plan3::DecisionReason::objective};
            result.token=pending.token;return result;
        }
        // A counterfactual preview cannot replace the actual arm's staged token.
        if(control==governingControl_)staged_.reset();
        if(info.round<0 || info.actorOrdinal<0) {result.status=Status::invalid;return result;}
        const auto menu=experts(info,jobs,context,ready);
        if(menu.status!=Status::known) {result.status=menu.status;return result;}
        const auto decision=bandit_.decide(menu.available,
            ticket(unsigned(info.actorOrdinal),unsigned(info.round),bandit_.episodes()),control,ready);
        result.status=decision.status;if(decision.status!=Status::known)return result;
        result.expert=decision.expert;result.intent=menu.intents[unsigned(decision.expert)];
        if(!ready())return Selection{Status::cutoff};
        if(control==governingControl_) {
            result.token=std::uint64_t(bandit_.episodes())+1;
            staged_=Staged{decision,result.token,key(*result.intent.job),info.round};
        }
        return result;
    }
    Selection preview(bool control,const plan3::InformationState& info,
                      const std::vector<plan3::JobForecast>& jobs,const plan3::ControllerContext& context={}) {
        return preview(control,info,jobs,context,[] {return true;});
    }
    Status committed(std::uint64_t token,const plan3::JobId& job,int started) {
        if(!token)return Status::unavailable; // Shared-priority jobs are not expert samples.
        if(!staged_ || staged_->token!=token || staged_->job!=key(job) || staged_->round!=started)
            return Status::invalid;
        const auto status=bandit_.commit(staged_->decision,token,staged_->job,started);
        if(status==Status::known)staged_.reset();
        return status;
    }
    Update endpoint(const Feedback& feedback) {return bandit_.finish(feedback);}
};
} // namespace plan3_adapter
