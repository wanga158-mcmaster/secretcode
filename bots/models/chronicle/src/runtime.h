#pragma once
#include "chronicle.h"
#include "../../common/model-runtime.h"
#include <memory>

namespace plan4::runtime {
class Chronicle : public M0 {
    struct Scratch {
        std::array<chronicle::MemberObservation, 256> members;
        std::array<chronicle::GroupMembers, 256> groups;
    };
    std::unique_ptr<chronicle::State> state_;
    std::unique_ptr<Scratch> scratch_ = std::make_unique<Scratch>();
    ObservationClock snapshot_{};
    std::uint64_t revision_ = 0;
    bool source_current_ = false, batch_budget_ = false;
    int supported_paths_ = 0, influenced_paths_ = 0, unknown_paths_ = 0, quote_work_ = 0, samples_ = 0;
    struct Trace { int calls = 0; bool bound = false, changed = false, unknown = false; };
    void begin_batch(const Information& info) {
        supported_paths_=influenced_paths_=unknown_paths_=quote_work_=samples_=0; batch_budget_=false;
        source_current_=snapshot_.round==info.round && snapshot_.own.id==info.own.id && !state_->truncated();
        if (source_current_) for (int g=0;g<std::clamp(info.group_count,0,max_groups);++g) {
            if ((g%16)==0 && info.work_budget.can_refine && !info.work_budget.can_refine(info.work_budget.context)) { batch_budget_=true; break; }
            const auto history=state_->history(info.groups[g].id);
            samples_+=history.capture_receipt_round>=0 && history.earliest_ready_round>info.round;
            ++quote_work_;
        }
    }
    static int wait(int group,int now,int ordinary,int readiness,bool ready,const Information& info,const void* model,void* scratch) {
        const auto& self=*static_cast<const Chronicle*>(model); auto& trace=*static_cast<Trace*>(scratch); ++trace.calls;
        const int result=self.state_->supply_wait(group,now,ordinary,readiness,ready,info);
        if (group>=0 && group<std::clamp(info.group_count,0,max_groups) && !ready) {
            const auto& source=info.groups[group]; const auto& p=source.provenance;
            const bool exact=source.visible && source.lifecycle!=Lifecycle::unknown && p.observed_round==info.round &&
                p.status==Status::exact_now && (p.source==Source::personal_vision || p.source==Source::own_occupancy);
            if (!exact) {
                const auto history=self.state_->history(source.id);
                const bool bound=history.capture_receipt_round>=0 && history.capture_receipt_round<=info.round && history.earliest_ready_round>now;
                trace.bound|=bound; trace.unknown|=!bound;
            }
        }
        trace.changed|=result>readiness; return result;
    }
    void predict_current(const Information& info,const Job& job,const Menu& menu,Forecast& out) {
        if (!source_current_) { plan4::predict_into(info,job,menu,out); batch_budget_|=out.budget_exhausted; return; }
        Trace trace; ForecastHooks hooks; hooks.model=this; hooks.path=&trace; hooks.supply_wait=wait;
        plan4::predict_into(info,job,menu,out,hooks); quote_work_+=trace.calls; batch_budget_|=out.budget_exhausted;
        if (!out.budget_exhausted) {
            supported_paths_+=trace.bound; influenced_paths_+=trace.changed; unknown_paths_+=trace.unknown;
        }
    }
    struct CandidateOnly {
        Chronicle& self;
        void predict_into(const Information& info,const Job& job,const Menu& menu,Forecast& out) { self.predict_current(info,job,menu,out); }
    };
public:
    explicit Chronicle(bool enabled = true) : state_(std::make_unique<chronicle::State>(enabled)) {}
    const char* name() const { return "chronicle"; }
    void observe_lifecycles(const LifecycleFrame& observed) {
        state_->observe_lifecycle_frame(observed.clock.round, observed.clock.own.id,
            observed.groups, observed.count, observed.truncated);
        ++revision_; source_current_=false;
    }
    void observe_snapshot(const Information& info, MemberFrame observed = {}) {
        int count = 0, groups = 0;
        // The legal observer publishes a group-contiguous prefix. Overflow or
        // missing members prevents an exact vector; it never fills unseen HP.
        for (int n = 0; observed.samples && n < std::max(0, observed.count) && count < 256; ++n) {
            const auto& source = observed.samples[n];
            if (!groups || scratch_->groups[groups - 1].group_id != source.group_id) {
                auto& group = scratch_->groups[groups++];
                group = {}; group.group_id = source.group_id; group.member_count = source.declared_members;
                group.samples = &scratch_->members[count]; group.all_members_visible = !observed.truncated;
            }
            auto& target = scratch_->members[count++];
            target = {source.member_id, source.spawn, source.health, source.provenance};
            ++scratch_->groups[groups - 1].sample_count;
        }
        state_->observe(info, scratch_->groups.data(), groups);
        snapshot_={info.round,{info.own.id}}; ++revision_; source_current_=false;
        supported_paths_=influenced_paths_=unknown_paths_=quote_work_=samples_=0; batch_budget_=false;
    }
    bool capture_receipt(const Information&, const CapturedGroup& observed) {
        const bool accepted=state_->legal_action_receipt({observed.group_id, observed.round, observed.respawn_rounds,
            observed.provenance, observed.personally_witnessed});
        if (accepted) ++revision_;
        return accepted;
    }
    void predict_into(const Information& info, const Job& job, const Menu& menu, Forecast& out) {
        begin_batch(info); predict_current(info,job,menu,out);
    }
    void evaluate_into(const Information& info,const Menu& menu,Evaluation& out) {
        begin_batch(info); CandidateOnly candidates{*this}; evaluate_model(info,menu,candidates,out); batch_budget_|=out.budget_exhausted;
    }
    ModelEvidence model_evidence() const {
        ModelEvidence out; out.reported=true;
        // 1 current source missing/truncated; 2 budget; 3 no compatible
        // readiness-bound query. A health vector alone never proves influence.
        out.reason=!source_current_?1:batch_budget_?2:!supported_paths_?3:0;
        out.supported=out.reason==0;
        out.coupling_enabled=out.supported && state_->enabled() && influenced_paths_>0;
        out.samples=samples_; out.unknown=unknown_paths_; out.paths=supported_paths_;
        out.work=quote_work_; out.revision=revision_; return out;
    }
    const chronicle::State& state() const { return *state_; }
};
} // namespace plan4::runtime
