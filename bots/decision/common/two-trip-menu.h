#pragma once
#include "two-trip-runtime.h"
#include "opening-bootstrap.h"
#include "candidate-prefix.h"

namespace plan3_runtime::two_trip {
struct Prefix {
    std::vector<std::pair<int,int>> groups; // distance, PersonalInformation group index
    bool truncated=false;
};
// Resume only immutable geometry preparation. Candidate actor/member inputs are
// read afresh by Builder before they enter the ordinary paired selector.
class Menu {
    Builder builder_;
    std::size_t cursor_=0;
    std::optional<std::size_t> pending_group_;
    std::unique_ptr<Candidate> candidate_;
    std::optional<plan3::JobId> owned_;
    Status prepared_status_=Status::unknown;
    int callback_round_=-1;
    Status status(Status value) {prepared_status_=value;return value;}
    bool same_callback(const plan3::InformationState& state) const {
        return state.round>=0 && state.round==callback_round_ &&
            cambc::round_number()==unsigned(state.round) && candidate_ &&
            candidate_->plan.preparedRound==unsigned(state.round);
    }
public:
    using Prefix=::plan3_runtime::two_trip::Prefix;
    void discard_candidate() {
        candidate_.reset();owned_.reset();prepared_status_=Status::unknown;callback_round_=-1;
    }
    static Prefix single_prefix(const PersonalInformation& personal,const StaticPaths& paths,cambc::vec2 position) {
        Prefix result;
        std::vector<candidate_prefix::Entry> available;
        for(std::size_t i=0;i<personal.groups.size();++i) {
            const int distance=paths.get(personal.groups[i].field,position);
            if(distance>=0)available.push_back({distance,int(i),personal.groups[i].key,personal.groups[i].gem_value>0});
        }
        result.truncated=available.size()>plan3::single_job_cap;
        // Proposal coverage is separate from each controller's objective.
        // A dense gold ring must not hide every score-bearing alternative.
        for(const auto& entry:candidate_prefix::select(available))
            result.groups.emplace_back(entry.distance,entry.index);
        return result;
    }
    // An unfinished optional extension cannot veto a usable whole ordinary
    // job. The ordinary paired selector still decides whether to choose it.
    static bool defer_selection(Status status,const plan3::InformationState& state,
                                const std::vector<plan3::JobForecast>& jobs) {
        if(status!=Status::pending && status!=Status::cutoff)return false;
        for(const auto& job:jobs) {
            if(!plan3::admitted(state,job,false))continue;
            if(job.nominal()->deltaS>0 || job.mandatorySafety ||
               opening_bootstrap::single_gold_eligible(state,job))return false;
        }
        return true;
    }
    template<class Ready>
    void advance_preparation(StaticPaths& paths,Ready&& ready) {
        builder_.advance_preparation(paths,ready);
    }
    template<class Ready>
    Status prepare_early(const PersonalInformation& personal,const std::optional<EquipmentStats>& stats,
                StaticPaths& paths,const plan3::InformationState& state,
                const Prefix& prefix,Ready&& ready) {
        // Only immutable geometry survives callbacks. No old live candidate or
        // ownership can enter a new choose invocation.
        discard_candidate();callback_round_=state.round;
        if(!stats || state.carriedGems || personal.groups.empty() || prefix.groups.empty() ||
           prefix.groups.size()>plan3::single_job_cap ||
           state.actorOrdinal!=static_cast<int>(cambc::champion_type::tavi))return status(Status::unsupported);
        bool gems=false;
        for(const auto& entry:prefix.groups) {
            if(entry.second<0 || std::size_t(entry.second)>=personal.groups.size())return status(Status::unsupported);
            gems|=personal.groups[std::size_t(entry.second)].gem_value>0;
        }
        if(!gems)return status(Status::unsupported);
        if(!ready())return status(Status::cutoff);
        if(state.round<0 || cambc::round_number()!=unsigned(state.round))return status(Status::cutoff);
        const auto visible=cambc::vision::visible_tiles();
        if(pending_group_) {
            const auto found=std::find_if(prefix.groups.begin(),prefix.groups.end(),[&](const auto& entry) {
                return std::size_t(entry.second)==*pending_group_;
            });
            if(found==prefix.groups.end()) {pending_group_.reset();builder_.reset_preparation();}
            else cursor_=std::size_t(found-prefix.groups.begin());
        }
        unsigned attempts=0;
        for(std::size_t scanned=0;scanned<prefix.groups.size();++scanned) {
            if(!ready())return status(Status::cutoff);
            cursor_%=prefix.groups.size();
            const auto index=std::size_t(prefix.groups[cursor_].second);
            const auto& group=personal.groups[index];
            if(group.gem_value<=0 || (visible&group.spawns).is_empty()) {
                ++cursor_;pending_group_.reset();builder_.reset_preparation();continue;
            }
            pending_group_=index;
            auto prepared=builder_.prepare(personal,index,*stats,paths,ready);
            ++attempts;
            if(prepared.status==Status::pending || prepared.status==Status::cutoff)return status(prepared.status);
            ++cursor_;pending_group_.reset();
            if(prepared.candidate && prepared.status==Status::ready &&
               plan3::admitted(state,prepared.candidate->job,false)) {
                candidate_=std::move(prepared.candidate);
            }
            builder_.reset_preparation();
            if(candidate_) {
                if(!same_callback(state)) {candidate_.reset();return status(Status::cutoff);}
                return status(Status::ready);
            }
            if(attempts>=2)return status(Status::unknown);
        }
        return status(Status::unknown);
    }
    Status merge(const plan3::InformationState& state,std::vector<plan3::JobForecast>& jobs) {
        if(!candidate_)return prepared_status_;
        // prepare_early -> ordinary forecasts -> merge is one uninterrupted,
        // QUERY-ONLY choose callback. No action/shop/turn end may occur between
        // these sections. Round drift invalidates all of choose at its caller;
        // first execution separately revalidates actual gear/member state.
        if(!same_callback(state)) {candidate_.reset();return status(Status::cutoff);}
        if(owned_)return prepared_status_;
        unsigned singles=0;
        plan3::JobForecast* replacement=nullptr;
        for(auto& job:jobs)if(job.id.kind==plan3::JobKind::cycle) {
            ++singles;
            if(job.id.firstGroup==candidate_->job.id.firstGroup && job.id.secondGroup<0) {
                if(replacement || plan3::admitted(state,job,false)) {
                    candidate_.reset();return status(Status::unsupported);
                }
                replacement=&job;
            }
        }
        // Indexer's unsupported physical projects retain their reserved slots.
        // A complete macro replaces that same slot, without another proposal.
        if(replacement)*replacement=candidate_->job;
        else {
            if(singles>=plan3::single_job_cap) {candidate_.reset();return status(Status::unsupported);}
            jobs.push_back(candidate_->job);
        }
        owned_=candidate_->job.id;
        return status(Status::ready);
    }
    const Candidate* prepared_candidate() const {return candidate_.get();}
    bool owns(const plan3::JobId& id) const {return owned_ && *owned_==id;}
    std::unique_ptr<Plan> take(const plan3::JobId& id) {
        if(!owns(id))return nullptr;
        // owns() deliberately retains stale published ownership until this
        // check, so the caller rejects a failed sidecar rather than committing
        // its JobForecast as a generic one-trip Cycle.
        owned_.reset();
        if(!candidate_ || callback_round_<0 || cambc::round_number()!=unsigned(callback_round_) ||
           candidate_->plan.preparedRound!=unsigned(callback_round_)) {
            candidate_.reset();status(Status::cutoff);return nullptr;
        }
        auto plan=std::make_unique<Plan>(std::move(candidate_->plan));candidate_.reset();return plan;
    }
};
} // namespace plan3_runtime::two_trip
