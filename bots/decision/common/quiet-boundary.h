#pragma once
#ifndef PLAN3_QUIET_BOUNDARY_PACKET
#define PLAN3_QUIET_BOUNDARY_PACKET 0
#endif

#if PLAN3_QUIET_BOUNDARY_PACKET
#include <array>
#include "telemetry.h"

namespace plan3_runtime {
// Diagnostic only: fixed private stamps, then one bounded row per later idle
// callback. No formatting, logging, ranking, or guard changes at the cutpoint.
class QuietBoundaryPacket {
    struct Row {
        std::array<unsigned,30> fuel{};
        int group=-1,stage=-1,move=-1;
        unsigned reason=0;
    };
    std::array<Row,16> rows_{};
    Row* current_=nullptr;
    unsigned next_=0;
    bool selected_=false;
public:
    void begin(unsigned round,cambc::champion_type actor) {
        current_=nullptr;
        if(round<131 || round>146 || actor!=cambc::champion_type::marcella)return;
        selected_=true;current_=&rows_[round-131];stamp(0);
    }
    void stamp(unsigned phase) {
        if(current_)current_->fuel[phase]=static_cast<unsigned>(cambc::fuel_remaining());
    }
    void state(int group,int stage) {
        if(current_){current_->group=group;current_->stage=stage;}
    }
    void reason(unsigned code) {
        if(current_)current_->reason=code;
    }
    void move(int status) {
        if(current_)current_->move=status;
    }
    void flush() {
        if(!selected_ || next_==rows_.size() || cambc::round_number()<=146 || cambc::fuel_remaining()<65000)return;
        const auto& row=rows_[next_];
        TelemetryFields fields;
        fields<<"P3QB {\"round\":"<<(131+next_)<<",\"fuel\":[";
        for(unsigned i=0;i<row.fuel.size();++i){if(i)fields<<',';fields<<row.fuel[i];}
        fields<<"],\"group\":"<<row.group<<",\"stage\":"<<row.stage
              <<",\"move\":"<<row.move<<",\"reason\":"<<row.reason<<"}\n";
        if(fields.valid()){
            std::fwrite(fields.view().data(),1,fields.size(),stdout);std::fflush(stdout);
        }
        ++next_;
    }
};
} // namespace plan3_runtime
#endif
