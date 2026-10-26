#pragma once
// Diagnostic only: supplied scalars, no SDK reads, allocation, printf or action
// authority. Parent calls every hook under its default-OFF constexpr flag and
// owns the existing epilogue fuel/epoch fence plus ONE actual stream write.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tt::assignment_reachability_probe {
using U32=std::uint32_t;using U64=std::uint64_t;
inline constexpr U32 none=std::numeric_limits<U32>::max(),audited_role=1,magic=0x54545231u;
inline constexpr U32 half_none=65535,half_max=65534;
enum class Milestone : U32 {WorldReady,MenuReady,BindingReady,ValuationReady,CashReady};
enum class Collector : U32 {Initial,Publication};
enum Flag : U32 {ScalarOverflow=1u,CounterSaturated=2u,PackedOverflow=4u,InvalidId=8u,ProtocolError=16u,
    EntryFirstLow=32u,RejectTerminal=64u,ActivationTerminal=128u,ReceiptTerminal=256u};
struct Rejection {U32 epoch{none},site{none},status{none},reason{none};};
struct Budget {U32 epoch{none},site{none},floor{none},fuel{none};};
struct Collection {U32 calls{},first_result{none},last_result{none},guard_epoch{none},guard_floor{none},guard_fuel{none};};
struct Record {
    U32 role{none},milestones{};std::array<U32,5> first_epoch{none,none,none,none,none};
    Rejection first_rejection;Budget entry_last,entry_first_low;std::array<Collection,2> collectors;
    U32 progressed{},fallbacks{},activations{},receipts{},command_kind{none},command_status{none};
    U32 transport_epoch{none},transport_cost{none},transport_flags{},flags{},terminal_epoch{none},terminal_kind{};
};
using Words=std::array<U32,32>;using Line=std::array<char,257>; // lower hex + LF, no NUL
namespace detail {
struct State {
    Record value;U32 frame_emitted{},terminal_emitted{},results_seen{},collector_active{none},collector_epoch{none};
    U32 pending_epoch{none},pending_terminal{none};
};
inline State& state(){static constinit State value{};return value;}
inline bool active(U32 actor){return actor==audited_role&&state().value.role==audited_role;}
inline U32 scalar(U64 value,U32& flags) {
    if(value>=none){flags|=ScalarOverflow;return none;}return static_cast<U32>(value);
}
inline U32 pair(U64 low,U64 high,U32& flags) {
    if(low>half_max||high>half_max){flags|=PackedOverflow;return none;}
    return static_cast<U32>(low)|(static_cast<U32>(high)<<16);
}
inline U32 maybe_half(U32 value,U32& flags) {
    if(value==none)return half_none;if(value>half_max){flags|=PackedOverflow;return half_none;}return value;
}
inline void increment(U32& value,U32& flags,U32 maximum=none) {
    if(value<maximum)++value;else flags|=CounterSaturated;
}
inline bool epoch(U64 value,U32& out) {
    if(value>=none){state().value.flags|=ScalarOverflow;return false;}out=static_cast<U32>(value);return true;
}
inline unsigned frame(U64 epoch){return epoch==512?1u:epoch==1024?2u:epoch==1536?4u:0u;}
inline void terminal(U32 epoch,U32 kind) {
    auto& r=state().value;if(r.terminal_epoch==none){r.terminal_epoch=epoch;r.terminal_kind=kind;}
}
}
inline Record& record(){return detail::state().value;}
inline U32 role(){return record().role;}
inline bool select(U32 actor) {
    if(actor!=audited_role)return false;
    auto& r=record();if(r.role==none)r.role=actor;return r.role==actor;
}
inline bool reset(U32 actor) {
    if(actor!=audited_role)return false;detail::state()=detail::State{};record().role=actor;return true;
}
inline void mark(U32 actor,U64 epoch,Milestone milestone) {
    if(!detail::active(actor))return;auto& r=record();const auto id=static_cast<U32>(milestone);U32 dated;
    if(id>=r.first_epoch.size()){r.flags|=InvalidId;return;}if(!detail::epoch(epoch,dated))return;
    r.milestones|=1u<<id;if(r.first_epoch[id]==none)r.first_epoch[id]=dated;
}
inline void collector_called(U32 actor,U64 epoch,Collector collector) {
    if(!detail::active(actor))return;auto& s=detail::state();const auto id=static_cast<U32>(collector);U32 dated;
    if(id>=s.value.collectors.size()){s.value.flags|=InvalidId;return;}if(!detail::epoch(epoch,dated))return;
    detail::increment(s.value.collectors[id].calls,s.value.flags);s.value.milestones|=1u<<(5+id);
    s.collector_active=id;s.collector_epoch=dated;
}
inline void guard(U32 actor,U64 epoch,U64 floor,U64 fuel) {
    if(!detail::active(actor))return;auto& s=detail::state();
    if(s.collector_active>=2||epoch!=s.collector_epoch)return;
    auto& c=s.value.collectors[s.collector_active];c.guard_epoch=s.collector_epoch;
    c.guard_floor=detail::scalar(floor,s.value.flags);c.guard_fuel=detail::scalar(fuel,s.value.flags);
}
inline void collector_result(U32 actor,U64 epoch,Collector collector,U64 status,U64 reason) {
    if(!detail::active(actor))return;auto& s=detail::state();const auto id=static_cast<U32>(collector);
    if(id>=2){s.value.flags|=InvalidId;return;}
    if(s.collector_active!=id||epoch!=s.collector_epoch){s.value.flags|=ProtocolError;return;}
    const auto result=detail::pair(status,reason,s.value.flags);auto& c=s.value.collectors[id];
    if(!(s.results_seen&(1u<<id))){c.first_result=result;s.results_seen|=1u<<id;}
    c.last_result=result;s.collector_active=s.collector_epoch=none;
}
inline void entry_budget(U32 actor,U64 epoch,U64 site,U64 floor,U64 fuel) {
    if(!detail::active(actor))return;auto& r=record();U32 dated;if(!detail::epoch(epoch,dated))return;
    r.entry_last={dated,detail::scalar(site,r.flags),detail::scalar(floor,r.flags),detail::scalar(fuel,r.flags)};
    // Numeric observation only, NOT a new admission decision. The existing
    // caller guard is strict fuel>floor; at/below selects its first refused slot.
    if(fuel<=floor&&r.entry_first_low.epoch==none)r.entry_first_low=r.entry_last;
}
inline void reject(U32 actor,U64 epoch,U64 site,U64 status,U64 reason,bool terminal=false) {
    if(!detail::active(actor))return;auto& r=record();U32 dated;if(!detail::epoch(epoch,dated))return;
    if(r.first_rejection.epoch==none)
        r.first_rejection={dated,detail::scalar(site,r.flags),detail::scalar(status,r.flags),detail::scalar(reason,r.flags)};
    if(terminal)detail::terminal(dated,RejectTerminal);
}
inline void progress(U32 actor,U64 epoch) {
    if(!detail::active(actor))return;U32 dated;if(!detail::epoch(epoch,dated))return;auto& r=record();detail::increment(r.progressed,r.flags,half_max);
}
inline void fallback(U32 actor,U64 epoch) {
    if(!detail::active(actor))return;U32 dated;if(!detail::epoch(epoch,dated))return;auto& r=record();detail::increment(r.fallbacks,r.flags,half_max);
}
inline void activate(U32 actor,U64 epoch) {
    if(!detail::active(actor))return;U32 dated;if(!detail::epoch(epoch,dated))return;auto& r=record();
    detail::increment(r.activations,r.flags,half_max);r.milestones|=1u<<7;detail::terminal(dated,ActivationTerminal);
}
inline void command_receipt(U32 actor,U64 epoch,U64 kind,U64 status) {
    if(!detail::active(actor))return;U32 dated;if(!detail::epoch(epoch,dated))return;auto& r=record();
    detail::increment(r.receipts,r.flags,half_max);r.milestones|=1u<<8;
    r.command_kind=detail::scalar(kind,r.flags);r.command_status=detail::scalar(status,r.flags);detail::terminal(dated,ReceiptTerminal);
}
inline bool should_emit(U32 actor,U64 epoch) {
    if(!detail::active(actor)||epoch>=none)return false;const auto& s=detail::state();
    return (detail::frame(epoch)&~s.frame_emitted)!=0||
        (!s.terminal_emitted&&s.value.terminal_epoch!=none&&epoch>=s.value.terminal_epoch);
}
// Packet words:
// 0magic,1epoch,2(role low8|flags<<8),3milestones,4..8first5epochs,
// 9rejectEpoch,10reject(site low16|status<<16),11rejectReason,
// 12entry(epoch low16|site<<16),13entryFloor,14entryFuel,
// 15initialCalls,16initialFirstResult,17initialLastResult,
// 18publicationCalls,19publicationFirstResult,20publicationLastResult,
// 21/22initialGuardFloor/Fuel,23/24publicationGuardFloor/Fuel,
// 25progress|fallback<<16,26activation|receipt<<16,27command(kind|status<<16),
// 28previousTransportCost,29previousTransport(epoch|flags<<16),
// 30firstTerminal(epoch|kind<<16),31guardEpochs(initial|publication<<16).
// Scalar none (UINT32_MAX) is RESERVED, not a valid epoch/scalar observation.
// Result words pack status low16/reason high16. Packed values max65534;
// unrepresentable pairs are none+PackedOverflow, NEVER truncated. Counter pairs
// saturate at65534 with CounterSaturated. Entry uses first fuel<=floor sample
// if present, otherwise last entry; this never replaces the first rejection.
inline Words snapshot(U32 actor,U64 epoch) {
    Words out{};if(!detail::active(actor)||epoch>=none)return out;
    auto& s=detail::state();const auto& r=s.value;U32 flags=r.flags;
    const auto& entry=r.entry_first_low.epoch!=none?r.entry_first_low:r.entry_last;
    if(r.entry_first_low.epoch!=none)flags|=EntryFirstLow;
    out[0]=magic;out[1]=static_cast<U32>(epoch);out[3]=r.milestones;
    for(std::size_t i=0;i<5;++i)out[4+i]=r.first_epoch[i];
    out[9]=r.first_rejection.epoch;out[10]=r.first_rejection.epoch==none?none:detail::pair(r.first_rejection.site,r.first_rejection.status,flags);out[11]=r.first_rejection.reason;
    out[12]=entry.epoch==none?none:detail::pair(entry.epoch,entry.site,flags);out[13]=entry.floor;out[14]=entry.fuel;
    out[15]=r.collectors[0].calls;out[16]=r.collectors[0].first_result;out[17]=r.collectors[0].last_result;
    out[18]=r.collectors[1].calls;out[19]=r.collectors[1].first_result;out[20]=r.collectors[1].last_result;
    out[21]=r.collectors[0].guard_floor;out[22]=r.collectors[0].guard_fuel;out[23]=r.collectors[1].guard_floor;out[24]=r.collectors[1].guard_fuel;
    out[25]=detail::pair(r.progressed,r.fallbacks,flags);out[26]=detail::pair(r.activations,r.receipts,flags);
    out[27]=r.command_kind==none?none:detail::pair(r.command_kind,r.command_status,flags);
    out[28]=r.transport_cost;out[29]=r.transport_epoch==none?none:detail::pair(r.transport_epoch,r.transport_flags,flags);
    out[30]=r.terminal_epoch==none?none:detail::pair(r.terminal_epoch,r.terminal_kind,flags);
    out[31]=detail::maybe_half(r.collectors[0].guard_epoch,flags)|(detail::maybe_half(r.collectors[1].guard_epoch,flags)<<16);
    out[2]=r.role|(flags<<8);s.pending_epoch=static_cast<U32>(epoch);s.pending_terminal=r.terminal_epoch;return out;
}
inline void note_transport(U32 actor,U64 epoch,U64 cost,U64 flags=0) {
    if(!detail::active(actor))return;auto& s=detail::state();U32 dated;if(!detail::epoch(epoch,dated))return;
    s.value.transport_epoch=dated;s.value.transport_cost=detail::scalar(cost,s.value.flags);s.value.transport_flags=detail::scalar(flags,s.value.flags);
    if(s.pending_epoch!=dated){s.value.flags|=ProtocolError;return;}
    s.frame_emitted|=detail::frame(epoch);
    if(s.pending_terminal!=none&&s.pending_terminal==s.value.terminal_epoch)s.terminal_emitted=1;
    s.pending_epoch=s.pending_terminal=none;
}
constexpr Line encode(const Words& words) {
    constexpr char digits[]="0123456789abcdef";Line line{};
    for(std::size_t w=0;w<words.size();++w)for(unsigned n=0;n<8;++n)line[w*8+n]=digits[(words[w]>>(28-4*n))&15u];
    line[256]='\n';return line;
}
}
