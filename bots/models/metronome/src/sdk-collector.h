#pragma once
#include "runtime.h"
#include "cambc.h"

namespace plan4::runtime {
// These two current-owner resource queries contain no opponent/gameplay state.
// They are called only after a real frozen profile passes admission. The
// default mains intentionally provide no such measurements or opportunity.
class SdkMetronome : public Metronome {
    static metronome_binding::Resources resources(const void*){
        return {int(cambc::round_number()),cambc::fuel_remaining(),true};
    }
public:
    explicit SdkMetronome(bool enabled=true):Metronome(enabled){observe_resources({resources,nullptr});}
};
} // namespace plan4::runtime
