#pragma once
#ifndef PLAN3_CONTINUATION_JOURNAL
#define PLAN3_CONTINUATION_JOURNAL 0
#endif
#ifndef PLAN3_CONTINUATION_EMIT
#define PLAN3_CONTINUATION_EMIT 0
#endif

#if PLAN3_CONTINUATION_JOURNAL
#include "telemetry.h"

namespace plan3_runtime {
// A data-only switch keeps the capture/formatting code in both experimental
// frames. The default-OFF build contains neither this state nor its callers.
// Both frames keep a nonzero initialized word in the same data class. The
// final low bit alone selects emission; compiled code/data parity is checked.
inline volatile unsigned continuation_emit_word=2u|PLAN3_CONTINUATION_EMIT;

class ContinuationJournal {
public:
    static constexpr std::array<unsigned,3> sample_rounds{134,141,143};
    struct Row {
        bool seen=false,execute_reached=false,target_scanned=false;
        unsigned round=0,fuel=0,epoch=0,observed_round=0;
        int checkpoint=0,kind=-2,first=-2,second=-2,altar=-2,equipment=-2;
        int stage=-2,lifecycle=-2,move=-2;
        unsigned target=~0u;
    };
private:
    std::array<Row,3> rows_{};
    Row* current_=nullptr;
    unsigned last_round_=0,next_=0;
    bool selected_=false;
    void state(int kind,int first,int second,int altar,int equipment,int stage,unsigned epoch) {
        if(!current_)return;
        current_->kind=kind;current_->first=first;current_->second=second;
        current_->altar=altar;current_->equipment=equipment;current_->stage=stage;current_->epoch=epoch;
    }
public:
    void identity(bool marcella) { selected_=marcella; }
    void begin(unsigned now,int kind,int first,int second,int altar,int equipment,int stage,unsigned epoch) {
        current_=nullptr;
        if(!selected_)return;
        last_round_=now;
        const unsigned slot=now==134?0:now==141?1:now==143?2:3;
        if(slot==rows_.size())return;
        current_=&rows_[slot];
        if(!current_->seen){current_->seen=true;current_->round=now;}
        state(kind,first,second,altar,equipment,stage,epoch);
    }
    void boundary(unsigned fuel,int kind,int first,int second,int altar,int equipment,int stage,unsigned epoch) {
        if(!current_)return;
        current_->checkpoint=1;current_->fuel=fuel;
        state(kind,first,second,altar,equipment,stage,epoch);
    }
    void execute() { if(current_)current_->execute_reached=true; }
    void observation(unsigned epoch,unsigned observed_round,int lifecycle) {
        if(!current_)return;
        current_->epoch=epoch;current_->observed_round=observed_round;current_->lifecycle=lifecycle;
    }
    void target(bool found,unsigned handle) {
        if(!current_)return;
        current_->target_scanned=true;current_->target=found?handle:~0u;
    }
    void move(int value) { if(current_)current_->move=value; }
    const auto& rows() const { return rows_; }
    unsigned emitted() const { return next_; }
    void flush(unsigned already_read_tail_fuel) {
        // Formatting and output happen after the callback's original gameplay
        // and telemetry. This diagnostic reserve never changes a game guard.
        if(!selected_ || last_round_<=143 || next_==rows_.size() || already_read_tail_fuel<75000)return;
        const auto& r=rows_[next_];
        TelemetryFields f;
        f<<"P3CJ {\"round\":"<<sample_rounds[next_]<<",\"seen\":"<<unsigned(r.seen)
         <<",\"checkpoint\":"<<r.checkpoint<<",\"fuel\":"<<r.fuel
         <<",\"job\":["<<r.kind<<','<<r.first<<','<<r.second<<','<<r.altar<<','<<r.equipment
         <<"],\"stage\":"<<r.stage<<",\"epoch\":"<<r.epoch<<",\"observed_round\":"<<r.observed_round
         <<",\"lifecycle\":"<<r.lifecycle<<",\"execute\":"<<unsigned(r.execute_reached)
         <<",\"target_scanned\":"<<unsigned(r.target_scanned)<<",\"target\":"<<r.target<<",\"move\":"<<r.move<<"}\n";
        if(f.valid() && (continuation_emit_word&1u)) {
            std::fwrite(f.view().data(),1,f.size(),stdout);std::fflush(stdout);
        }
        ++next_;
    }
};
} // namespace plan3_runtime
#endif
