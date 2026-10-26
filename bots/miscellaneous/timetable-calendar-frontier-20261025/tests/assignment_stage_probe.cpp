#include "../src/assignment_stage_probe.h"
#include <cstdio>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace {
namespace p=tt::assignment_stage_probe;
int checks{},failures{};
void check(bool ok){++checks;if(!ok)++failures;}
std::uint32_t decode(const p::Line& line,std::size_t word) {
    std::uint32_t value{};
    for(unsigned n=0;n<8;++n) {
        const char c=line[word*8+n];
        check((c>='0'&&c<='9')||(c>='a'&&c<='f'));
        const unsigned digit=c<='9'?static_cast<unsigned>(c-'0'):static_cast<unsigned>(c-'a'+10);
        value=(value<<4)|digit;
    }
    return value;
}
void selection_and_context() {
    p::record()={};
    check(p::selected_epoch(512)&&p::selected_epoch(1024)&&p::selected_epoch(1536));
    check(!p::selected_epoch(511)&&!p::selected_epoch(513)&&!p::selected_epoch(2000));
    check(!p::selected_epoch((std::uint64_t{1}<<32)+512));
    auto untouched=p::record();
    p::begin_collect(10,2);p::mark(10,3);p::record_guard(10,100,50);p::end_collect(10,4,5);
    check(p::record()==untouched);
    p::record().prior_transport_cost=2986;p::record().prior_transport_flags=7;p::record().prior_transport_epoch=256;
    p::mark(512,4);p::begin_collect(512,8);
    auto& r=p::record();
    check(r.epoch==512&&r.stage==4&&r.collector_site==8&&r.collector_context==1&&r.attempts==1);
    check(r.collector_epoch==512&&r.collector_status==p::none&&r.guard_floor==p::none);
    p::record_guard(512,78000,72077);
    check(r.guard_floor==78000&&r.guard_fuel==72077&&(r.flags&p::GuardSeen)&&
          (r.flags&p::FloorFits)&&(r.flags&p::FuelFits));
    auto active=r;p::record_guard(1024,1,2);p::end_collect(1024,1,2);
    check(r==active);
    p::end_collect(512,1,19);
    check(r.collector_context==0&&r.collector_status==1&&r.collector_reason==19&&
          (r.flags&p::StatusFits)&&(r.flags&p::ReasonFits));
    const auto ended=r;p::record_guard(512,1,2);p::end_collect(512,8,9);check(r==ended);
    p::mark(512,9);p::begin_collect(512,10);
    check(r.stage==9&&r.attempts==2&&r.collector_site==10&&r.guard_floor==p::none&&
          r.guard_fuel==p::none&&!(r.flags&p::GuardSeen)&&!(r.flags&p::StatusFits));
    check(r.prior_transport_cost==2986&&r.prior_transport_flags==7&&r.prior_transport_epoch==256);
    p::mark(1024,12);
    check(r.epoch==1024&&r.stage==12&&r.attempts==0&&r.collector_context==0&&r.collector_epoch==p::none);
    check(r.prior_transport_cost==2986&&r.prior_transport_epoch==256);
    const auto changed=r;p::record_guard(512,5,6);p::end_collect(512,7,8);check(r==changed);
}
void full_width_and_overflow() {
    const std::uint64_t wide=std::uint64_t{p::none}+1;
    check(p::fit(p::none).fits&&p::fit(p::none).value==p::none&&!p::fit(wide).fits&&p::fit(wide).value==p::none);
    p::record()={};p::mark(1536,p::none);p::begin_collect(1536,p::none);
    auto& r=p::record();p::record_guard(1536,p::none,p::none);p::end_collect(1536,p::none,p::none);
    const auto fits=p::StageFits|p::SiteFits|p::StatusFits|p::ReasonFits|p::FloorFits|p::FuelFits;
    check((r.flags&fits)==fits&&r.stage==p::none&&r.guard_fuel==p::none);
    p::mark(1536,wide);p::begin_collect(1536,wide);p::record_guard(1536,wide,std::numeric_limits<std::uint64_t>::max());
    p::end_collect(1536,wide,wide);
    check(!(r.flags&fits)&&(r.flags&p::GuardSeen)&&r.collector_site==p::none&&r.guard_floor==p::none);
    r.attempts=p::none;p::begin_collect(1536,1);
    check(r.attempts==p::none&&(r.flags&p::AttemptsSaturated));
    p::begin_collect(1536,2);check(r.attempts==p::none&&(r.flags&p::AttemptsSaturated));
    p::begin_collect(1024,3);check(r.attempts==1&&!(r.flags&p::AttemptsSaturated));
}
void transport() {
    static_assert(std::is_trivially_copyable_v<p::Record>);
    static_assert(p::Line{}.size()==97&&p::Words{}.size()==12);
    constexpr p::Words words{p::magic,0,1,0xffffffffu,0x80000000u,0x01234567u,
        0x89abcdefu,512,1024,1536,78000,72077};
    constexpr auto line=p::encode(words);
    static_assert(line[0]=='5'&&line[1]=='4'&&line[6]=='3'&&line[7]=='3'&&line[96]=='\n');
    check(line.size()==97&&line[96]=='\n');
    for(std::size_t n=0;n<words.size();++n)check(decode(line,n)==words[n]);
    for(std::uint32_t n=0;n<32;++n) {
        p::Words varying{};
        for(std::size_t w=0;w<varying.size();++w)varying[w]=(std::uint32_t{1}<<n)^static_cast<std::uint32_t>(w*0x1234567u);
        const auto encoded=p::encode(varying);
        for(std::size_t w=0;w<varying.size();++w)check(decode(encoded,w)==varying[w]);
    }
}
}
int main() {
    selection_and_context();full_width_and_overflow();transport();
    std::printf("assignment stage probe (pure): %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
