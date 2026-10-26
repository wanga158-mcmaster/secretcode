#pragma once
#include "governor.h"
#include "../../common/model-runtime.h"

namespace plan4::runtime {
class Governor : public M0 {
    governor::Governor state_;
    std::uint64_t kit_ = 0;
    std::uint64_t revision_ = 0;
    bool has_kit_ = false, enabled_ = true, kit_consistent_ = true, source_current_ = false, batch_budget_ = false;
    ObservationClock snapshot_{};
    CombatKit combat_{};
    int completed_paths_ = 0, supported_paths_ = 0, influenced_paths_ = 0, quote_work_ = 0;
    unsigned unknown_phases_ = 0;
    struct Trace { unsigned warm = 0, cold = 0; int changes = 0, calls = 0; };
    static int quote(EventKind kind, int group, int nominal, int minimum, int now,
                     const Information& info, const void* model, void* path) {
        auto& self = *static_cast<const Governor*>(model); auto& trace = *static_cast<Trace*>(path);
        const int result = governor::Governor::phase_duration(kind,group,nominal,minimum,now,info,&self.state_,nullptr);
        governor::Phase5 phase;
        switch (kind) {
            case EventKind::travel: phase=governor::Phase5::travel; break;
            case EventKind::clear: phase=governor::Phase5::clear; break;
            case EventKind::capture: phase=governor::Phase5::capture; break;
            case EventKind::bank: phase=governor::Phase5::bank; break;
            case EventKind::heal: phase=governor::Phase5::restore; break;
            default: return result;
        }
        if (nominal>0) {
            ++trace.calls;
            (self.state_.entry(phase).completions>=3 ? trace.warm : trace.cold) |= governor::phase_mask(phase);
            trace.changes += result!=std::max(nominal,minimum);
        }
        return result;
    }
    void begin_batch(const Information& info) {
        completed_paths_=supported_paths_=influenced_paths_=quote_work_=0; unknown_phases_=0; batch_budget_=false;
        source_current_=snapshot_.round==info.round && snapshot_.own.id==info.own.id && has_kit_ && kit_consistent_ &&
            combat_.exact && combat_.clock.round==info.round && combat_.clock.own.id==info.own.id &&
            combat_.version==kit_ && combat_.stat_epoch==state_.stat_epoch();
        for (unsigned p=0;p<governor::phase_count;++p) {
            const auto& entry=state_.entry(static_cast<governor::Phase5>(p));
            if ((entry.completions || entry.pending) && entry.start.actor!=info.own.id) source_current_=false;
        }
    }
    void predict_current(const Information& info,const Job& job,const Menu& menu,Forecast& out) {
        if (!source_current_) { plan4::predict_into(info,job,menu,out); batch_budget_|=out.budget_exhausted; return; }
        Trace trace; ForecastHooks hooks; hooks.model=this; hooks.path=&trace; hooks.phase_duration=quote;
        plan4::predict_into(info,job,menu,out,hooks); quote_work_+=trace.calls;
        batch_budget_|=out.budget_exhausted;
        if (!out.budget_exhausted) {
            ++completed_paths_; supported_paths_+=trace.warm!=0;
            influenced_paths_+=trace.changes!=0; unknown_phases_|=trace.cold;
        }
    }
    struct CandidateOnly {
        Governor& self;
        void predict_into(const Information& info,const Job& job,const Menu& menu,Forecast& out) { self.predict_current(info,job,menu,out); }
    };
public:
    explicit Governor(bool enabled = true) : state_(enabled), enabled_(enabled) {}
    const char* name() const { return "governor"; }
    void observe_kit(std::uint64_t kit, std::uint32_t epoch = 0) {
        if (has_kit_ && kit!=kit_ && epoch==state_.stat_epoch()) kit_consistent_=false;
        else if (!has_kit_ || epoch!=state_.stat_epoch()) kit_consistent_=true;
        if (!has_kit_ || kit!=kit_ || epoch!=state_.stat_epoch()) ++revision_;
        if ((has_kit_ && kit != kit_) || epoch != state_.stat_epoch()) state_.on_stat_change(epoch, governor::all_phases);
        kit_ = kit; has_kit_ = true;
    }
    void observe_combat(const CombatKit& combat) { combat_=combat; }
    void observe_snapshot(const Information& info,MemberFrame = {}) { snapshot_={info.round,{info.own.id}}; begin_batch(info); }
    bool phase_started(const PhaseStart& source) {
        return state_.begin_phase({static_cast<governor::Phase5>(source.phase), source.actor, source.job_epoch,
            source.phase_sequence, source.start_round, source.nominal_duration, source.mode, source.stat_epoch, source.emergency});
    }
    void phase_completed(const PhaseReceipt& source) {
        const auto result=state_.observe_phase_receipt({static_cast<governor::Phase5>(source.phase), source.actor, source.job_epoch,
            source.phase_sequence, source.start_round, source.end_round, source.nominal_duration,
            source.requested_mode, source.executed_mode, source.stat_epoch, source.completed, source.endpoint_observed,
            source.emergency, static_cast<governor::Censor>(source.censor), source.legal_wait_rounds, source.waiting_bound, source.waiting_legal});
        if (result==governor::ReceiptResult::learned) ++revision_;
    }
    void predict_into(const Information& info, const Job& job, const Menu& menu, Forecast& out) {
        begin_batch(info); predict_current(info,job,menu,out);
    }
    void evaluate_into(const Information& info,const Menu& menu,Evaluation& out) {
        begin_batch(info); CandidateOnly candidates{*this}; evaluate_model(info,menu,candidates,out); batch_budget_|=out.budget_exhausted;
    }
    ModelEvidence model_evidence() const {
        ModelEvidence out; out.reported=true;
        // 1: missing current own/kit source; 2: warmup/no queried warm phase;
        // 3: budget failure. Cold phases never borrow another phase's support.
        out.reason=!source_current_?1:batch_budget_?3:!supported_paths_?2:0;
        out.supported=out.reason==0; out.coupling_enabled=source_current_ && enabled_ && !batch_budget_ && influenced_paths_>0;
        for (unsigned p=0;p<governor::phase_count;++p) {
            const auto count=state_.entry(static_cast<governor::Phase5>(p)).completions;
            out.samples+=int(std::min<std::uint32_t>(count,std::uint32_t(std::numeric_limits<int>::max()/governor::phase_count)));
            out.unknown+=(unknown_phases_&(1u<<p))!=0;
        }
        out.paths=supported_paths_; out.work=quote_work_; out.revision=revision_; return out;
    }
    const governor::Governor& state() const { return state_; }
};
} // namespace plan4::runtime
