#pragma once
#include "telemetry.h"
#include <array>
#include <optional>

namespace plan3_runtime {
// Historical diagnostics only: no sample is an input to eligibility or actions.
// Queue one fixed-size record at denial; format it on a fresh callback instead
// of spending a denied callback's cleanup reserve.
class PlanningGateTrace {
public:
    enum class Point {turn,prepared,shop,vision,observation,domain,menu,extended,paired,count};
    enum class Gate {vision_fuel,views,paths,domain_fuel,domain,choose_fuel,preparation,menu_fuel,pair,pair_ready,count};
private:
    struct Sample {
        unsigned round=0;
        Gate gate=Gate::vision_fuel;
        const char* reason="unknown";
        int index=-1;
        std::size_t groups=0;
        bool coherent=false;
        std::array<std::int64_t,static_cast<unsigned>(Point::count)> fuel{};
        Sample() {fuel.fill(-1);}
    } current_;
    std::optional<Sample> pending_;
    unsigned seen_=0;
    static const char* name(Gate gate) {
        constexpr const char* names[]={"vision_fuel","views","paths","domain_fuel","domain",
            "choose_fuel","preparation","menu_fuel","pair","pair_ready"};
        return names[static_cast<unsigned>(gate)];
    }
public:
    void begin(std::size_t groups) {
        current_.round=cambc::round_number();current_.groups=groups;
        current_.fuel.fill(-1);mark(Point::turn);
    }
    void mark(Point point) {current_.fuel[static_cast<unsigned>(point)]=static_cast<std::int64_t>(cambc::fuel_remaining());}
    void defer(Gate gate,const char* reason,int index=-1) {
        const unsigned bit=1u<<static_cast<unsigned>(gate);
        if(pending_ || (seen_&bit))return;
        current_.gate=gate;current_.reason=reason;current_.index=index;
        current_.coherent=current_.round==cambc::round_number();
        pending_=current_;seen_|=bit;
    }
    template<class Log> void flush(Log& log) {
        if(!pending_ || cambc::fuel_remaining()<65000)return;
        const auto sample=*pending_;pending_.reset();
        TelemetryFields fields;
        fields<<",\"observed_round\":"<<sample.round<<",\"gate\":\""<<name(sample.gate)
            <<"\",\"reason\":\""<<sample.reason<<"\",\"index\":"<<sample.index
            <<",\"groups\":"<<sample.groups<<",\"coherent\":"<<(sample.coherent?"true":"false")<<",\"fuel\":[";
        for(unsigned i=0;i<sample.fuel.size();++i) {if(i)fields<<',';fields<<sample.fuel[i];}
        fields<<']';log.emit("planning-gate",fields.str());
    }
};
} // namespace plan3_runtime
