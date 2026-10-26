#pragma once
// Diagnostic-only scalar breadcrumbs. The caller supplies existing observations
// and the writer; this header imports no SDK, allocates nothing, and changes no
// Controller layout. References belong only in the parent's constexpr OFF gate.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace residual::probe {
int write(const char*,std::size_t) noexcept; // Parent's single unformatted transport.
using U32=std::uint32_t;using U64=std::uint64_t;
inline constexpr U32 none=std::numeric_limits<U32>::max(),magic=0x54545234u;
inline constexpr U64 diagnostic_reserve=16000;
inline constexpr unsigned selected_role=1,stale_status=2,budget_status=14;
enum class Failure : unsigned {None,Budget,Stale,Explicit};
struct FirstFailure {
    bool set{};U32 epoch{none},expected{none},actual{none},site{none},status{none};Failure kind{};
};
struct Record {
    U32 role{none},expected{none},actual{none};
    U32 before_site{none},world_stage{none},before_status{none},returned_site{none},returned_status{none};
    U32 floor{none},fuel{none};FirstFailure first;
    U32 packets{},last_emission_epoch{none};bool overflow{};
};
inline Record& record() noexcept {static constinit Record value;return value;}
inline U32 role() noexcept {return record().role;}
inline bool select(unsigned value) noexcept {
    if(value!=selected_role)return false;record().role=value;return true;
}
inline void reset() noexcept {record()=Record{};}
inline U32 scalar(U64 value) noexcept {
    if(value>=none){record().overflow=true;return none;}return static_cast<U32>(value);
}
inline U32 epoch(int value) noexcept {return value<0?none:static_cast<U32>(value);}
inline void first_failure(U32 when,U32 site,Failure kind,U32 status,U32 expected,U32 actual) noexcept {
    auto& r=record();
    // Routine cold deferrals must not occupy the first-failure record. The
    // FIRST stale transition supersedes any prior non-Stale record;
    // once a Stale is recorded, every field of that first Stale stays frozen.
    if(kind==Failure::Budget&&(when==none||when<8))return;
    if(r.first.set&&!(kind==Failure::Stale&&r.first.kind!=Failure::Stale))return;
    r.first={true,when,expected,actual,site,status,kind};
}
// Record regardless of actor selection so the World identity can select the
// existing breadcrumbs later. This does not grant permission to emit another role.
inline void before(int expected,unsigned site,unsigned world_stage,unsigned read_status) noexcept {
    auto& r=record();r.expected=epoch(expected);r.before_site=scalar(site);
    r.world_stage=scalar(world_stage);r.before_status=scalar(read_status);
}
// Numeric enum values are NOT a type discriminator. In particular routes'
// status2 means Unsupported. Only the caller's comparison against its actual
// enum's Stale member may supply typed_stale=true. Clock mismatch remains an
// independent authoritative transition even if a callee returns NeedCompute.
inline void returned(int expected,unsigned site,unsigned status,bool typed_stale=false) noexcept {
    auto& r=record();r.expected=epoch(expected);r.returned_site=scalar(site);r.returned_status=scalar(status);
    if(typed_stale)first_failure(r.actual==none?r.expected:r.actual,r.returned_site,
        Failure::Stale,r.returned_status,r.expected,r.actual);
}
inline void clock(int expected,int actual) noexcept {
    auto& r=record();r.expected=epoch(expected);r.actual=epoch(actual);
    if(r.expected!=none&&r.actual!=none&&r.expected!=r.actual)
        first_failure(r.actual,r.before_site,Failure::Stale,stale_status,r.expected,r.actual);
}
// These are the parent's EXISTING single fuel read and exact original floor.
// Equality is a refusal because production's admission is fuel > floor.
inline void budget(int expected,U64 floor,U64 fuel) noexcept {
    auto& r=record();r.expected=epoch(expected);r.floor=scalar(floor);r.fuel=scalar(fuel);
    if(fuel<=floor)first_failure(r.actual==none?r.expected:r.actual,r.before_site,
        Failure::Budget,budget_status,r.expected,r.actual);
}
inline void mark_failure(int expected,unsigned site,unsigned kind,unsigned status) noexcept {
    auto& r=record();r.expected=epoch(expected);
    Failure value=Failure::Explicit;
    if(kind==static_cast<unsigned>(Failure::Stale))value=Failure::Stale;
    else if(kind==static_cast<unsigned>(Failure::Budget))value=Failure::Budget;
    else if(kind!=static_cast<unsigned>(Failure::Explicit))r.overflow=true;
    first_failure(r.actual==none?r.expected:r.actual,scalar(site),value,scalar(status),r.expected,r.actual);
}
using Words=std::array<U32,8>;
using Line=std::array<char,65>;
inline U32 packed(U32 value,U32 unknown) noexcept {
    if(value==none)return unknown;
    if(value>=unknown){record().overflow=true;return unknown;}return value;
}
// Packet fields (low bits first):
// 0 magic; 1 last expected; 2 last actual;
// 3 beforeSite8/stage4/beforeStatus4/returnedSite8/returnedStatus4/role3/overflow1;
// 4 firstFailureEpoch16/site8/(kind2+packetOrdinal2)/status4;
// 5 firstFailureExpected16/actual16; 6 floor; 7 fuel.
// A packed all-ones field is UNKNOWN, never a truncated value. Full-width
// floor/fuel reserve UINT32_MAX likewise. All game epochs fit the 16-bit scope;
// an out-of-scope epoch becomes unknown plus the explicit overflow flag.
inline Words snapshot() noexcept {
    auto& r=record();Words out{};out[0]=magic;out[1]=r.expected;out[2]=r.actual;
    const U32 before_site=packed(r.before_site,255),stage=packed(r.world_stage,15),bs=packed(r.before_status,15);
    const U32 returned_site=packed(r.returned_site,255),rs=packed(r.returned_status,15),selected=packed(r.role,7);
    const U32 when=packed(r.first.epoch,65535),site=packed(r.first.site,255),status=packed(r.first.status,15);
    const U32 expected=packed(r.first.expected,65535),actual=packed(r.first.actual,65535);
    out[3]=before_site|(stage<<8)|(bs<<12)|(returned_site<<16)|(rs<<24)|(selected<<28)|
        (static_cast<U32>(r.overflow)<<31);
    const U32 ordinal=r.packets<3?r.packets+1:3;
    out[4]=when|(site<<16)|((static_cast<U32>(r.first.kind)|(ordinal<<2))<<24)|(status<<28);
    out[5]=expected|(actual<<16);out[6]=r.floor;out[7]=r.fuel;return out;
}
inline Line encode(const Words& words) noexcept {
    Line out{};constexpr char hex[]="0123456789abcdef";
    for(std::size_t i=0;i<words.size();++i)for(unsigned j=0;j<8;++j)
        out[i*8+j]=hex[(words[i]>>(28-4*j))&15u];
    out[64]='\n';return out;
}
// actual_epoch and fuel must come from the parent's existing FRESH sample.
// No policy/floor is changed. Early startup entries do not spend packet slots;
// only after the first real Stale at most three different eligible epochs emit.
// Routine Budget/Explicit records NEVER consume transport slots.
// The callback performs exactly one write, with no printf or extra metrology.
// If it throws, normal production turn-boundary exception handling remains in
// charge and no successful-transport count is invented here.
template<class Writer>
bool offer(int actual_epoch,U64 original_floor,U64 fresh_fuel,Writer&& writer) {
    auto& r=record();const auto when=epoch(actual_epoch);
    if(r.role!=selected_role||!r.first.set||r.first.kind!=Failure::Stale||actual_epoch<8||r.packets>=3||r.last_emission_epoch==when)return false;
    if(original_floor>std::numeric_limits<U64>::max()-diagnostic_reserve){r.overflow=true;return false;}
    if(fresh_fuel<original_floor+diagnostic_reserve)return false;
    if(r.actual!=none&&r.actual!=when)return false;
    // Keep the exact admitting sample, then freeze ALL bytes before the write.
    r.floor=scalar(original_floor);r.fuel=scalar(fresh_fuel);
    const auto words=snapshot();const auto line=encode(words);
    writer(line.data(),line.size());++r.packets;r.last_emission_epoch=when;return true;
}
}
