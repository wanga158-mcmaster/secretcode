// Pure WASI test. The runner provides only mocked cambc query functions.
#include <cambc.h>
#include "../common/telemetry.h"
#include "../assignment-common/ownership.h"
#include <cstdlib>
#include <limits>

using plan3_runtime::Telemetry;
using plan3_runtime::TelemetryFields;
static unsigned checks=0;
static void require(bool value,int line) {
    ++checks;
    if(!value) {std::fprintf(stderr,"summary check failed at line%d\n",line);std::exit(1);}
}
#define REQUIRE(...) require(bool(__VA_ARGS__),__LINE__)

static std::string number(double value) {TelemetryFields f;f.summary_error(value);REQUIRE(f.valid());return f.str();}
static void numbers() {
    struct Case {double value;const char* text;};
    for(const auto& c:std::initializer_list<Case>{
        {0,"0"},{-0.0,"0"},{937.9999840259552,"938"},{755.9999842643738,"756"},
        {1.0004,"1"},{1.0006,"1.001"},{0.001,"0.001"},{0.01,"0.01"},{0.1,"0.1"},
        {0.9996,"1"},{0.0005,"0.001"},{-0.0004,"0"},{-0.0016,"-0.002"},
        {12.12,"12.12"},{-12.12,"-12.12"},{1e9,"1000000000"},{-1e9,"-1000000000"},
        {1e9-.0004,"1000000000"}}) REQUIRE(number(c.value)==c.text);
    for(double v:{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),
                  std::numeric_limits<double>::quiet_NaN(),std::nextafter(1e9,INFINITY),std::nextafter(-1e9,-INFINITY)})
        REQUIRE(number(v)=="null");
    // Exact integer thousandths across signs; independent integer spelling.
    for(int i=-2000;i<=2000;++i) {
        auto expected=std::to_string(std::abs(i)/1000);
        int tail=std::abs(i)%1000;
        if(tail) {
            char digits[4];std::snprintf(digits,sizeof(digits),"%03d",tail);
            std::string fractional=digits;
            while(fractional.back()=='0')fractional.pop_back();
            expected+='.'+fractional;
        }
        if(i<0)expected='-'+expected;
        REQUIRE(number(double(i)/1000)==expected);
    }
    TelemetryFields general;general<<937.9999840259552;
    REQUIRE(general.str()=="937.999984");
    REQUIRE(plan3_runtime::telemetry_number(937.9999840259552)==general.str());
    for(const auto& c:std::initializer_list<Case>{
        {0.0000004,"0"},{-0.0000004,"0"},{0.0000006,"0.000001"},{-0.0000006,"-0.000001"},
        {0.9999996,"1"},{1.0000006,"1.000001"},{-1.0000006,"-1.000001"},
        {0.000001,"0.000001"},{0.00001,"0.00001"},{0.0001,"0.0001"},{0.001,"0.001"},
        {0.01,"0.01"},{0.1,"0.1"},{1e9,"1000000000"},{-1e9,"-1000000000"}}) {
        TelemetryFields value;value<<c.value;REQUIRE(value.valid() && value.str()==c.text);
    }
    for(double v:{std::numeric_limits<double>::infinity(),-std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),
                  std::nextafter(1e9,INFINITY),std::nextafter(-1e9,-INFINITY)}) {
        TelemetryFields value;value<<v;REQUIRE(value.valid() && value.str()=="null");
    }
    for(int i=-2000;i<=2000;++i) {
        auto expected=std::to_string(std::abs(i)/1000000);
        const int tail=std::abs(i)%1000000;
        if(tail) {
            char digits[7];std::snprintf(digits,sizeof(digits),"%06d",tail);
            std::string fractional=digits;
            while(fractional.back()=='0')fractional.pop_back();
            expected+='.'+fractional;
        }
        if(i<0)expected='-'+expected;
        TelemetryFields value;value<<(double(i)/1000000);
        REQUIRE(value.valid() && value.str()==expected);
    }
    TelemetryFields full;full<<std::string(512,'x');REQUIRE(full.valid());
    full.summary_error(1);REQUIRE(!full.valid() && full.str().size()==513);
}

static void frames() {
    Telemetry t;t.turns=1472;t.decisions=35;t.completed=25;t.aborted=5;t.captures=21;
    t.cutoffs=146;t.duration_error=113;t.health_error=937.9999840259552;t.conversion_error=6;
    auto line=t.format_line(1495,"varg","summary",t.summary_fields().str());
    REQUIRE(line.valid() && !line.empty() && line.size()<=512);
    REQUIRE(line.str().find("\"abs_health_error\":938")!=std::string::npos);
    std::fwrite(line.view().data(),1,line.size(),stdout);
    t.duration_error=std::numeric_limits<double>::infinity();t.health_error=1e9;t.conversion_error=std::numeric_limits<double>::quiet_NaN();
    line=t.format_line(1496,"varg","summary",t.summary_fields().str());
    REQUIRE(line.valid() && !line.empty() && line.size()<=512);
    std::fwrite(line.view().data(),1,line.size(),stdout);
    Telemetry huge;huge.turns=huge.decisions=huge.completed=huge.aborted=huge.censored=UINT32_MAX;
    huge.captures=huge.opportunities=huge.activations=huge.disagreements=UINT32_MAX;
    huge.details_omitted=huge.converted=huge.collisions=UINT32_MAX;
    huge.cutoffs=huge.preparations=huge.two_trip_started=huge.two_trip_completed=huge.two_trip_aborted=UINT32_MAX;
    REQUIRE(huge.format_line(1,"varg","summary",huge.summary_fields().str()).empty());
    REQUIRE(huge.oversize_omitted==1); // Omit oversized JSON rather than truncate it.
    // Ordinary assignment policy formatting uses the unchanged general path.
    namespace a=plan3_assignment;
    for(auto policy:{a::Policy::matchmaker_exact,a::Policy::matchmaker_greedy,a::Policy::bargainer_product,a::Policy::bargainer_total}) {
        a::DraftPlan plan;plan.complete=true;plan.feasible=true;plan.groups={10,20};plan.matrixFingerprint=123;
        line=t.format_line(7,"tavi","policy",a::adoption_fields(plan,policy));
        REQUIRE(line.valid() && !line.empty() && line.size()<=512);
        std::fwrite(line.view().data(),1,line.size(),stdout);
    }
    line=t.format_line(8,"tavi","policy",a::adoption_fields({},a::Policy::bargainer_product));
    REQUIRE(line.valid() && !line.empty() && line.size()<=512);
    std::fwrite(line.view().data(),1,line.size(),stdout);
}

static void call_caps() {
    Telemetry t;
    summary_test_host::reset(29999);
    t.summary();
    REQUIRE(summary_test_host::fuel_calls==1 && summary_test_host::round_calls==0 && summary_test_host::self_calls==0);
    summary_test_host::reset(30000);
    t.summary();
    REQUIRE(summary_test_host::fuel_calls==1 && summary_test_host::round_calls==1 && summary_test_host::self_calls==1);
    Telemetry capped;
    for(unsigned i=0;i<95;++i)REQUIRE(!capped.format_line(i,"varg","summary","").empty());
    summary_test_host::reset(100000);
    capped.summary();
    REQUIRE(summary_test_host::fuel_calls==0 && summary_test_host::round_calls==0 && summary_test_host::self_calls==0);
    summary_test_host::reset(30000);
    capped.summary(true);
    REQUIRE(summary_test_host::fuel_calls==1 && summary_test_host::round_calls==1 && summary_test_host::self_calls==1);
    summary_test_host::reset(100000);
    capped.summary(true);
    REQUIRE(summary_test_host::fuel_calls==0 && summary_test_host::round_calls==0 && summary_test_host::self_calls==0);
}

static void detail_caps() {
    summary_test_host::reset(100000);
    Telemetry ordinary;REQUIRE(ordinary.detail_available());
    for(unsigned i=0;i<32;++i) {
        REQUIRE(ordinary.detail_available());
        REQUIRE(!ordinary.format_line(i,"varg","endpoint","").empty());
    }
    REQUIRE(!ordinary.detail_available());
    REQUIRE(ordinary.format_line(32,"varg","prediction","").empty());
    REQUIRE(ordinary.details_omitted==1);
    Telemetry reserved;reserved.reserve_context_line();
    for(unsigned i=0;i<31;++i) {
        REQUIRE(reserved.detail_available());
        REQUIRE(!reserved.format_line(i,"varg","endpoint","").empty());
    }
    REQUIRE(!reserved.detail_available());
    REQUIRE(!reserved.format_line(32,"varg","horizon-context","",false,true).empty());
    REQUIRE(!reserved.detail_available());
    Telemetry lines;
    for(unsigned i=0;i<95;++i)REQUIRE(!lines.format_line(i,"varg","summary","").empty());
    REQUIRE(!lines.detail_available());
    REQUIRE(summary_test_host::fuel_calls==0 && summary_test_host::round_calls==0 && summary_test_host::self_calls==0);
}
static void compact_sortie() {
    std::array<std::int64_t,19> metrics{};metrics.fill(UINT32_MAX);
    Telemetry maximum;maximum.turns=maximum.decisions=maximum.completed=maximum.aborted=maximum.converted=maximum.cutoffs=UINT32_MAX;
    auto line=maximum.format_line(UINT32_MAX,"tavi","summary",maximum.sortie_summary_fields(metrics,true).str(),true);
    REQUIRE(line.valid()&&!line.empty()&&line.size()<=512);
    std::fwrite(line.view().data(),1,line.size(),stdout);
    metrics.fill(0);metrics[10]=metrics[11]=metrics[18]=-1;
    Telemetry empty;line=empty.format_line(0,"tavi","summary",empty.sortie_summary_fields(metrics).str());
    REQUIRE(line.valid()&&!line.empty()&&line.size()<=512);std::fwrite(line.view().data(),1,line.size(),stdout);
    summary_test_host::reset(29999);empty.summary_sortie(metrics);
    REQUIRE(summary_test_host::fuel_calls==1&&summary_test_host::round_calls==0&&summary_test_host::self_calls==0);
    summary_test_host::reset(30000);empty.summary_sortie(metrics);
    REQUIRE(summary_test_host::fuel_calls==1&&summary_test_host::round_calls==1&&summary_test_host::self_calls==1);
    Telemetry capped;
    for(unsigned i=0;i<95;++i)REQUIRE(!capped.format_line(i,"tavi","summary","").empty());
    summary_test_host::reset(100000);capped.summary_sortie(metrics);
    REQUIRE(summary_test_host::fuel_calls==0&&summary_test_host::round_calls==0&&summary_test_host::self_calls==0);
    summary_test_host::reset(30000);capped.summary_sortie(metrics,true);
    REQUIRE(summary_test_host::fuel_calls==1&&summary_test_host::round_calls==1&&summary_test_host::self_calls==1);
    summary_test_host::reset(100000);capped.summary_sortie(metrics,true);
    REQUIRE(summary_test_host::fuel_calls==0&&summary_test_host::round_calls==0&&summary_test_host::self_calls==0);
    Telemetry saturated;
    for(unsigned i=0;i<32;++i)REQUIRE(!saturated.format_line(i,"tavi","endpoint","").empty());
    REQUIRE(!saturated.detail_available());summary_test_host::reset(30000);saturated.summary_sortie(metrics);
    REQUIRE(summary_test_host::fuel_calls==1&&summary_test_host::round_calls==1&&summary_test_host::self_calls==1);
}

int main() {
    numbers();frames();call_caps();detail_caps();compact_sortie();
    std::printf("{\"status\":\"pass\",\"checks\":%u,\"nativeGames\":0,\"sdkRuntimeCalls\":0}\n",checks);
}
