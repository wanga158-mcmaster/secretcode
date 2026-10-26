#pragma once
#include "model.h"
#include "../common/policy.h"

namespace plan3_signaller {
struct Motion {
    bool valid=false,aliveBefore=false,aliveAfter=false;
    int round=0,before=-1,after=-1;
};
struct Selection {State state=State::unavailable;plan3::JobIntent intent{};std::uint64_t token=0;Signal signal=Signal::none;unsigned omitted=0;};
struct Statistics {
    unsigned committed=0,completed=0,aborted=0,censored=0,exactSamples=0;
    unsigned targetwardOpenings=0,otherOpenings=0,motionUnknown=0,unwitnessedEndpoints=0;
    std::uint64_t exactConverted=0;
};
class Planner {
    struct Staged {std::uint64_t token;Key key;int round,origin;Signal signal;};
    struct Pending {Staged choice;bool motionAttempted=false,witnessed=false;};
    Geometry geometry_;
    Model model_;
    bool governingControl_=false;
    std::optional<Staged> staged_;
    std::optional<Pending> pending_;
    Statistics stats_;
public:
    explicit Planner(bool control=false,Model model={}):model_(model),governingControl_(control) {}
    void geometry(Geometry geometry) {geometry_=geometry;}
    const Statistics& statistics() const {return stats_;}
    bool pending() const {return pending_.has_value();}
    template<class Ready>
    Selection preview(bool control,const plan3::InformationState& info,const std::vector<plan3::JobForecast>& jobs,
                      const plan3::ControllerContext& context,Ready&& ready) {
        Selection selected;
        if(!ready()) {selected.state=State::cutoff;return selected;}
        if(pending_) {
            if(!context.activeJob || key(*context.activeJob)!=pending_->choice.key || !context.activeJobValid || context.sharedAbort) {
                selected.state=State::invalid;return selected;
            }
            selected.state=State::known;selected.signal=pending_->choice.signal;selected.token=pending_->choice.token;
            selected.intent={plan3::IntentStatus::preserve,context.activeJob,0,plan3::DecisionReason::options_commitment};return selected;
        }
        if(control==governingControl_)staged_.reset();
        if(stats_.committed>=4096)return selected;
        const auto mapped=map(info,jobs,geometry_,model_,ready);
        if(mapped.state!=State::known) {selected.state=mapped.state;return selected;}
        const auto result=solve(mapped.input,ready);selected.state=result.state;selected.omitted=mapped.omittedUnknown;
        const auto& choice=control?result.control:result.active;
        if(result.state!=State::known || !choice.present)return selected;
        if(!ready())return Selection{State::cutoff};
        selected.intent={plan3::IntentStatus::selected,job_id(choice.key),
            double(choice.value.numerator)/choice.value.denominator/model_.units,plan3::DecisionReason::objective,false,mapped.truncated};
        selected.signal=mapped.input.jobs[choice.row].signal;
        if(control==governingControl_) {
            selected.token=std::uint64_t(stats_.committed)+1;
            staged_=Staged{selected.token,choice.key,info.round,info.position.tile,selected.signal};
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
            pending_=Pending{*staged_};staged_.reset();++stats_.committed;return true;
        }
        if((event.kind!=plan3::PolicyEvent::Kind::completed && event.kind!=plan3::PolicyEvent::Kind::aborted) ||
           !pending_ || event.token!=pending_->choice.token || key(event.job)!=pending_->choice.key ||
           event.startedRound!=pending_->choice.round || event.round<event.startedRound ||
           (event.round==event.startedRound && event.exactConverted && *event.exactConverted>0))return false;
        if(event.kind==plan3::PolicyEvent::Kind::completed)++stats_.completed;else ++stats_.aborted;
        if(event.exactConverted) {++stats_.exactSamples;stats_.exactConverted+=*event.exactConverted;}else ++stats_.censored;
        if(pending_->choice.signal!=Signal::none && !pending_->witnessed)++stats_.unwitnessedEndpoints;
        pending_.reset();return true;
    }
    void motion(const Motion& motion) {
        if(!pending_ || pending_->choice.signal==Signal::none || pending_->motionAttempted)return;
        const auto area=std::int64_t(geometry_.width)*geometry_.height;
        if(!valid(geometry_) || !motion.valid || !motion.aliveBefore || !motion.aliveAfter || motion.round<pending_->choice.round ||
           motion.before!=pending_->choice.origin || motion.before<0 || motion.after<0 || motion.before>=area || motion.after>=area) {
            pending_->motionAttempted=true;++stats_.motionUnknown;return;
        }
        if(motion.before==motion.after)return;
        pending_->motionAttempted=true;
        const int target=pending_->choice.key[1];
        if(distance(motion.after,target,geometry_)<distance(motion.before,target,geometry_)) {
            pending_->witnessed=true;++stats_.targetwardOpenings;
        } else ++stats_.otherOpenings;
        // This proves only own same-round displacement. Opponent observation
        // and response remain assumptions regardless of this witness.
    }
};
} // namespace plan3_signaller
