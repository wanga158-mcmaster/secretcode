#pragma once
#include "capital-trace.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace plan3_runtime {
// One callback only. No queries: callers supply the original guard readings.
struct CapitalFuelReading { bool available=false; std::uint64_t fuel=0; };
struct CapitalDeferMeter {
    static constexpr std::uint64_t maximum=999999;
    bool attempted=false,endAvailable=false;
    std::uint64_t before=0,after=0;
    void begin(std::uint64_t fuel) { attempted=true;endAvailable=false;before=fuel;after=0; }
    void finish(CapitalFuelReading reading) { endAvailable=attempted && reading.available;after=reading.fuel; }
    bool valid() const {
        return attempted && endAvailable && after<=before && before-after<=maximum;
    }
    std::uint64_t cost() const { return valid()?before-after:0; } // validity is required before presentation
};

// C:<phase|->:<inclusive-defer-cost|?>:<attempted><end-available>:T/F/L/R/S
// Bounds: phase<=3, cost<=6 decimal digits, five one-character counts =>25B.
class CapitalMeterFragment {
    inline static constexpr std::string_view prefix=",\"provenance\":\"";
    std::array<char,48> bytes_{};
    std::size_t size_=0,valueEnd_=0;
    void append(std::string_view text) { for(char c:text)bytes_[size_++]=c; }
    void decimal(std::uint64_t value) {
        std::array<char,6> digits{};unsigned n=0;
        do {digits[n++]=char('0'+value%10);value/=10;}while(value);
        while(n)bytes_[size_++]=digits[--n];
    }
    void count(unsigned value,unsigned maximum) { bytes_[size_++]=value<=maximum?char('0'+value):'?'; }
public:
    static constexpr unsigned maximumValueBytes=26;
    CapitalMeterFragment(CapitalTrace trace,const CapitalDeferMeter& meter,
                         unsigned targets,unsigned forks,unsigned loops,unsigned terminals,unsigned schedules) {
        append(prefix);append("C:");
        const auto index=static_cast<unsigned>(trace.code);
        if(trace.code==CapitalUnknown::none)append("-");
        else if(index<capital_trace_descriptors.size()) {
            const auto label=capital_trace_descriptors[index].label;
            constexpr std::string_view oldPrefix="analytical-cap-";
            const auto phase=label.starts_with(oldPrefix)?label.substr(oldPrefix.size()):std::string_view{"?"};
            append(phase.size()<=3?phase:std::string_view{"?"});
        } else append("?");
        append(":");if(meter.valid())decimal(meter.cost());else append("?");append(":");
        bytes_[size_++]=meter.attempted?'1':'0';bytes_[size_++]=meter.endAvailable?'1':'0';append(":");
        count(targets,4);append("/");count(forks,4);append("/");count(loops,4);append("/");
        count(terminals,5);append("/");count(schedules,5);
        valueEnd_=size_;append("\"");bytes_[size_]='\0';
    }
    const char* fragment() const { return bytes_.data(); }
    std::string_view value() const { return {bytes_.data()+prefix.size(),valueEnd_-prefix.size()}; }
};
static_assert(15+CapitalMeterFragment::maximumValueBytes+2<=48);
} // namespace plan3_runtime
