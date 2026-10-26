#pragma once
#include "habitat.h"
#include "../../common/model-runtime.h"
#include <memory>

namespace plan4::runtime {
class Habitat : public M0 {
    std::unique_ptr<habitat::State> state_ = std::make_unique<habitat::State>();
    bool enabled_;
    std::uint64_t combat_version_ = 0;
    ObservationClock snapshot_{};
    bool source_current_ = false, batch_budget_ = false;
    int supported_paths_ = 0, influenced_paths_ = 0, unknown_paths_ = 0, quote_work_ = 0, samples_ = 0;
    struct Trace { habitat::Path path; int changes = 0; };
    void begin_batch(const Information& info) {
        supported_paths_=influenced_paths_=unknown_paths_=quote_work_=samples_=0; batch_budget_=false;
        source_current_=snapshot_.round==info.round && snapshot_.own.id==info.own.id &&
            state_->initialized && state_->combat_version==combat_version_;
        if (source_current_ && state_->supported)
            for (int p=0;p<state_->altar_count;++p) samples_+=habitat::pressure(*state_,p,info.round).observed_consumers;
    }
    static int wait(int group,int now,int ordinary,int readiness,bool ready,const Information& info,const void* model,void* scratch) {
        const auto& self=*static_cast<const Habitat*>(model); auto& trace=*static_cast<Trace*>(scratch);
        // Compare the SAME pure renewal law with n=0; this never updates the
        // live path or supplies an invented consumer observation.
        auto control=trace.path; control.coupling_enabled=false;
        const int baseline=habitat::supply_wait(group,now,ordinary,readiness,ready,info,self.state_.get(),&control);
        const int result=habitat::supply_wait(group,now,ordinary,readiness,ready,info,self.state_.get(),&trace.path);
        trace.changes+=std::max(readiness,result)!=std::max(readiness,baseline); return result;
    }
    static void captured(int group,int now,const Information& info,const void* model,void* scratch) {
        const auto& self=*static_cast<const Habitat*>(model); auto& trace=*static_cast<Trace*>(scratch);
        habitat::captured(group,now,info,self.state_.get(),&trace.path);
    }
    void predict_current(const Information& info,const Job& job,const Menu& menu,Forecast& out) {
        if (!source_current_) { plan4::predict_into(info,job,menu,out); batch_budget_|=out.budget_exhausted; return; }
        Trace trace; trace.path.coupling_enabled=enabled_;
        ForecastHooks hooks; hooks.model=this; hooks.path=&trace; hooks.supply_wait=wait; hooks.captured=captured;
        plan4::predict_into(info,job,menu,out,hooks); quote_work_+=trace.path.supply_queries; batch_budget_|=out.budget_exhausted;
        if (!out.budget_exhausted) {
            supported_paths_+=trace.path.supply_queries>0 && !trace.path.unsupported_capacity;
            unknown_paths_+=trace.path.unknown_pressure || trace.path.unsupported_capacity;
            influenced_paths_+=trace.changes>0;
        }
    }
    struct CandidateOnly {
        Habitat& self;
        void predict_into(const Information& info,const Job& job,const Menu& menu,Forecast& out) { self.predict_current(info,job,menu,out); }
    };
public:
    explicit Habitat(bool enabled = true) : enabled_(enabled) {}
    const char* name() const { return "habitat"; }
    void observe_kit(std::uint64_t version, std::uint32_t = 0) { combat_version_ = version; }
    void observe_snapshot(const Information& info, MemberFrame = {}) {
        habitat::prepare_state(info, *state_, combat_version_);
        snapshot_={info.round,{info.own.id}}; source_current_=false;
        supported_paths_=influenced_paths_=unknown_paths_=quote_work_=samples_=0; batch_budget_=false;
    }
    void observe_producers(const ProducerFrame& observed) {
        habitat::ObservationFrame frame;
        frame.round = observed.clock.round; frame.count = std::min(observed.count, 4);
        frame.complete = observed.complete; frame.truncated = observed.truncated || observed.count > 4;
        frame.full_patch_visibility = observed.full_patch_visibility;
        for (int n = 0; n < frame.count; ++n) {
            const auto& source = observed.samples[n]; auto& target = frame.consumers[n];
            target.actor_id = source.actor_id; target.group_id = source.group_id;
            target.position = source.position; target.provenance = source.provenance;
            target.personally_visible = source.personally_visible; target.producing = source.producing;
            target.ambiguous = source.ambiguous;
            target.evidence = static_cast<habitat::ProductionEvidence>(source.evidence);
        }
        habitat::observe(*state_, observed.clock, frame);
    }
    void predict_into(const Information& info, const Job& job, const Menu& menu, Forecast& out) {
        begin_batch(info); predict_current(info,job,menu,out);
    }
    void evaluate_into(const Information& info,const Menu& menu,Evaluation& out) {
        begin_batch(info); CandidateOnly candidates{*this}; evaluate_model(info,menu,candidates,out); batch_budget_|=out.budget_exhausted;
    }
    ModelEvidence model_evidence() const {
        ModelEvidence out; out.reported=true;
        // 1 stale source; 2 unresolved public capacity; 3 budget; 4 no
        // acquisition query. Unknown pressure remains distinct from n=0.
        out.reason=!source_current_?1:!state_->supported?2:batch_budget_?3:!supported_paths_?4:0;
        out.supported=out.reason==0;
        out.coupling_enabled=out.supported && enabled_ && influenced_paths_>0;
        out.samples=samples_; out.unknown=unknown_paths_; out.paths=supported_paths_;
        out.work=quote_work_; out.revision=state_->capacity_revision; return out;
    }
    const habitat::State& state() const { return *state_; }
};
} // namespace plan4::runtime
