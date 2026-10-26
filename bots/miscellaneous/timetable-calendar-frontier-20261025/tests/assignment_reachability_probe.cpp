#include "../src/assignment_reachability_probe.h"
#include <cstdio>
#include <cstdint>
#include <initializer_list>
#include <type_traits>

namespace {
namespace p=tt::assignment_reachability_probe;
int checks{},failures{};
void check(bool ok,const char* why){++checks;if(!ok){++failures;std::printf("FAIL reachability-probe %s\n",why);}}
p::U32 low(p::U32 word){return word&65535u;}p::U32 high(p::U32 word){return word>>16;}
p::U32 decode(const p::Line& line,std::size_t word) {
    p::U32 value{};for(unsigned i=0;i<8;++i) {
        const char c=line[word*8+i];check((c>='0'&&c<='9')||(c>='a'&&c<='f'),"hex bytes are lowercase ASCII digits");
        const auto digit=c<='9'?static_cast<unsigned>(c-'0'):static_cast<unsigned>(c-'a'+10);value=(value<<4)|digit;
    }return value;
}
void selection_and_context() {
    check(p::role()==p::none,"no role is fabricated before the caller's already-read trait selects Marcella");
    p::mark(1,10,p::Milestone::WorldReady);p::progress(1,10);p::reject(1,10,1,1,1,true);
    check(p::role()==p::none&&p::record().milestones==0&&p::record().progressed==0&&!p::should_emit(1,512),"preselection events and output are ignored");
    check(!p::select(2)&&!p::reset(2)&&p::role()==p::none,"other roles cannot select or reset the audited actor");
    check(p::select(1)&&p::select(1)&&p::role()==1,"selection/corroboration is idempotent and uses no SDK read");
    p::mark(1,100,p::Milestone::WorldReady);p::mark(1,101,p::Milestone::WorldReady);
    p::mark(2,101,p::Milestone::CashReady);p::progress(2,101);p::activate(2,101);
    check(p::record().first_epoch[0]==100&&p::record().milestones==1&&p::record().progressed==0&&p::record().terminal_epoch==p::none,
        "first milestones are immutable and other actor hooks cannot contaminate them");
    p::guard(1,100,999,888);check(p::record().collectors[0].guard_epoch==p::none,"a noncollector budget cannot overwrite collector guard context");
    p::collector_called(1,100,p::Collector::Initial);p::guard(1,101,9,8);
    check(p::record().collectors[0].guard_epoch==p::none,"wrong-epoch guards are ignored");
    p::guard(1,100,98000,81000);p::collector_result(1,100,p::Collector::Initial,1,0);
    p::guard(1,100,1,2);check(p::record().collectors[0].guard_floor==98000,"result closes collector context before other optional guards");
    p::collector_called(1,101,p::Collector::Initial);p::guard(1,101,97000,90000);p::collector_result(1,101,p::Collector::Initial,0,0);
    p::collector_called(1,200,p::Collector::Publication);p::guard(1,200,78000,61000);p::collector_result(1,200,p::Collector::Publication,1,2);
    const auto& r=p::record();
    check(r.collectors[0].calls==2&&low(r.collectors[0].first_result)==1&&r.collectors[0].last_result==0,
        "initial collector retains cumulative calls plus first and last actual-return status/reason");
    check(r.collectors[1].calls==1&&low(r.collectors[1].first_result)==1&&high(r.collectors[1].first_result)==2,
        "publication collector has independent result storage");
    p::entry_budget(1,90,1,98000,81000);p::entry_budget(1,201,2,78000,100000);
    p::reject(1,100,5,1,0);p::reject(1,201,20,3,7);
    p::progress(1,201);p::progress(1,202);for(unsigned i=0;i<3;++i)p::fallback(1,202);
    p::activate(1,300);p::command_receipt(1,301,7,1);
    const auto words=p::snapshot(1,512);
    check(words[0]==p::magic&&words[1]==512&&(words[2]&255)==1,"packet binds format/epoch/selected actor");
    check(words[4]==100&&words[9]==100&&low(words[10])==5&&high(words[10])==1&&words[11]==0,
        "outer-budget observations never replace the first shared rejection");
    check(low(words[12])==90&&high(words[12])==1&&words[13]==98000&&words[14]==81000&&((words[2]>>8)&p::EntryFirstLow),
        "outer entry preserves the first strict-low-fuel sample instead of the later successful boundary");
    check(words[21]==97000&&words[22]==90000&&words[23]==78000&&words[24]==61000&&low(words[31])==101&&high(words[31])==200,
        "initial/publication guard pairs remain separate and dated");
    check(low(words[25])==2&&high(words[25])==3&&low(words[26])==1&&high(words[26])==1&&low(words[27])==7&&high(words[27])==1,
        "boundary-invocation counters and supplied command receipt fields are exact");
}
void transport_and_bounded_frames() {
    p::reset(1);check(!p::should_emit(1,100),"ordinary frames do not create unbounded logging");
    p::reject(1,50,1,1,0);check(!p::should_emit(1,50),"a rejection does not infer terminal gameplay meaning by default");
    p::reject(1,60,2,3,4,true);check(!p::should_emit(1,59)&&p::should_emit(1,60),"explicit first terminal can emit once after its actual supplied epoch");
    auto words=p::snapshot(1,60);check(words[9]==50&&low(words[30])==60&&high(words[30])==p::RejectTerminal,
        "terminal nomination does not retag the earlier first rejection");
    const auto line=p::encode(words);unsigned writes{};
    const auto writer=[&](const char* bytes,std::size_t size){++writes;check(size==257&&bytes[256]=='\n',"parent writer receives one exact fixed-size line without NUL");};
    writer(line.data(),line.size());p::note_transport(1,60,777,2);
    check(writes==1&&!p::should_emit(1,60)&&!p::should_emit(1,61),"one captured terminal is acknowledged after exactly one caller write");
    unsigned frames{};
    for(const p::U32 epoch:{512u,1024u,1536u}) {
        check(p::should_emit(1,epoch),"each declared late frame is independently eligible once");
        const auto packet=p::snapshot(1,epoch);if(!frames)check(packet[28]==777&&low(packet[29])==60&&high(packet[29])==2,
            "packet captures PREVIOUS transport cost/flags/date before the current output");
        p::note_transport(1,epoch,1000+frames,0);++frames;
        check(!p::should_emit(1,epoch),"acknowledged late frame cannot emit twice");
    }
    check(frames==3&&!p::should_emit(1,2000)&&!p::should_emit(2,512)&&!p::should_emit(1,(p::U64{1}<<32)+512),
        "only three late frames plus one first terminal are permitted and wide epochs do not alias");
    check(!p::should_emit(1,p::none),"reserved unknown epoch cannot be emitted as a valid current frame");
    p::reset(1);const auto before=p::snapshot(1,512);const auto captured=p::encode(before);
    p::activate(1,513);p::note_transport(1,512,100,0);
    check(before[30]==p::none&&decode(captured,30)==p::none&&p::should_emit(1,513),
        "state changes after capture cannot change packet bytes or acknowledge an uncaptured terminal");
    p::note_transport(1,700,1,0);check(p::record().flags&p::ProtocolError,"transport without the matching captured epoch is explicitly invalid");
    p::reset(1);p::entry_budget(1,12,1,98000,98000);p::entry_budget(1,13,2,78000,100000);
    check(low(p::snapshot(1,512)[12])==12,"equal-to-floor preserves the first refusal of the existing strict-greater budget guard");
}
void limits_and_exact_hex() {
    p::reset(1);p::mark(1,p::U64{p::none}+1,p::Milestone::WorldReady);
    check(p::record().first_epoch[0]==p::none&&(p::record().flags&p::ScalarOverflow),"wide epoch cannot silently truncate a milestone date");
    p::mark(1,1,static_cast<p::Milestone>(99));p::collector_called(1,1,static_cast<p::Collector>(99));
    check(p::record().flags&p::InvalidId,"invalid enum indexes cannot reach array/shift operations");
    p::collector_called(1,2,p::Collector::Initial);p::guard(1,2,p::U64{p::none}+1,50);p::collector_result(1,2,p::Collector::Initial,65535,0);
    p::collector_called(1,3,p::Collector::Initial);p::collector_result(1,3,p::Collector::Initial,0,0);
    check(p::record().collectors[0].first_result==p::none&&p::record().collectors[0].last_result==0&&
        (p::record().flags&p::PackedOverflow)&&p::record().collectors[0].guard_floor==p::none,
        "unrepresentable first result remains unknown with a flag, not overwritten/truncated by a later success");
    p::record().progressed=p::record().fallbacks=p::record().activations=p::record().receipts=p::half_max;
    p::progress(1,4);p::fallback(1,4);p::activate(1,4);p::command_receipt(1,4,1,1);
    check(p::record().progressed==p::half_max&&p::record().receipts==p::half_max&&(p::record().flags&p::CounterSaturated),
        "paired counters saturate explicitly instead of wrapping");
    p::record().collectors[0].calls=p::none;p::collector_called(1,5,p::Collector::Initial);
    check(p::record().collectors[0].calls==p::none&&(p::record().flags&p::CounterSaturated),"full-width collector-call counter also saturates");
    p::Words raw{};for(std::size_t i=0;i<raw.size();++i)raw[i]=static_cast<p::U32>(0x89abcdefu+i*0x1234567u);
    raw[0]=p::magic;raw[1]=p::none;raw[31]=0x80000000u;const auto line=p::encode(raw);
    static_assert(p::Words{}.size()==32&&p::Line{}.size()==257&&std::is_trivially_copyable_v<p::Record>);
    check(line[256]=='\n',"manual transport terminates with exactly one LF");
    for(std::size_t i=0;i<raw.size();++i)check(decode(line,i)==raw[i],"all32 bits of every raw packet word survive encoding");
    check(p::reset(1)&&p::record().flags==0&&p::record().first_rejection.epoch==p::none&&p::record().collectors[0].calls==0,
        "explicit audited-role reset clears fixed storage and emission/collector context");
}
}
int main() {
    selection_and_context();transport_and_bounded_frames();limits_and_exact_hex();
    std::printf("assignment reachability probe (pure, SDK UNEXECUTED): %d checks, %d failures\n",checks,failures);return failures?1:0;
}
