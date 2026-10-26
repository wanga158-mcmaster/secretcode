#pragma once
// Diagnostic scalars only. No SDK import, logging, allocation or policy effect.
// The caller owns packet field selection, existing fuel reads and write fences.
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace tt::assignment_stage_probe {
inline constexpr std::uint32_t magic=0x54544333u;
inline constexpr std::uint32_t none=std::numeric_limits<std::uint32_t>::max();
using Words=std::array<std::uint32_t,12>;
using Line=std::array<char,97>; // 96 lower ASCII hex digits + LF; NO NUL byte.
enum Flag : std::uint32_t {
    StageFits=1u<<0,SiteFits=1u<<1,StatusFits=1u<<2,ReasonFits=1u<<3,
    GuardSeen=1u<<4,FloorFits=1u<<5,FuelFits=1u<<6,AttemptsSaturated=1u<<7
};
struct Scalar {std::uint32_t value{none};bool fits{};};
constexpr Scalar fit(std::uint64_t value) {
    return value<=none?Scalar{static_cast<std::uint32_t>(value),true}:Scalar{};
}
struct Record {
    std::uint32_t epoch{none},stage{none};
    std::uint32_t collector_epoch{none},collector_site{none},collector_context{};
    std::uint32_t collector_status{none},collector_reason{none};
    std::uint32_t guard_floor{none},guard_fuel{none},flags{},attempts{};
    // Supplied by the caller after its actual transport, never estimated here.
    std::uint32_t prior_transport_cost{none},prior_transport_flags{},prior_transport_epoch{none};
    friend bool operator==(const Record&,const Record&)=default;
};
inline Record state{};
inline Record& record(){return state;}
constexpr bool selected_epoch(std::uint64_t epoch) {
    return epoch==512||epoch==1024||epoch==1536;
}
namespace detail {
inline void epoch(Record& r,std::uint32_t value) {
    if(r.epoch==value)return;
    const auto cost=r.prior_transport_cost,flags=r.prior_transport_flags,previous=r.prior_transport_epoch;
    r=Record{};r.epoch=value;
    r.prior_transport_cost=cost;r.prior_transport_flags=flags;r.prior_transport_epoch=previous;
}
inline void set(std::uint64_t value,std::uint32_t& field,std::uint32_t flag,std::uint32_t& flags) {
    const auto converted=fit(value);field=converted.value;
    flags=(flags&~flag)|(converted.fits?flag:0u);
}
}
inline void begin_collect(std::uint64_t epoch,std::uint64_t site) {
    if(!selected_epoch(epoch))return;
    auto& r=record();detail::epoch(r,static_cast<std::uint32_t>(epoch));
    r.collector_epoch=r.epoch;r.collector_context=1;
    r.collector_status=r.collector_reason=r.guard_floor=r.guard_fuel=none;
    r.flags&=StageFits|AttemptsSaturated;
    detail::set(site,r.collector_site,SiteFits,r.flags);
    if(r.attempts<none)++r.attempts;else r.flags|=AttemptsSaturated;
}
inline void record_guard(std::uint64_t epoch,std::uint64_t floor,std::uint64_t fuel) {
    auto& r=record();
    if(!selected_epoch(epoch)||!r.collector_context||r.epoch!=epoch||r.collector_epoch!=epoch)return;
    r.flags|=GuardSeen;
    detail::set(floor,r.guard_floor,FloorFits,r.flags);
    detail::set(fuel,r.guard_fuel,FuelFits,r.flags);
}
inline void end_collect(std::uint64_t epoch,std::uint64_t status,std::uint64_t reason) {
    auto& r=record();
    if(!selected_epoch(epoch)||!r.collector_context||r.epoch!=epoch||r.collector_epoch!=epoch)return;
    detail::set(status,r.collector_status,StatusFits,r.flags);
    detail::set(reason,r.collector_reason,ReasonFits,r.flags);
    r.collector_context=0;
}
inline void mark(std::uint64_t epoch,std::uint64_t stage) {
    if(!selected_epoch(epoch))return;
    auto& r=record();detail::epoch(r,static_cast<std::uint32_t>(epoch));
    detail::set(stage,r.stage,StageFits,r.flags);
}
// Each u32 is encoded most-significant nibble first. All 32 bits, including a
// valid 0xffffffff value, survive. Fit flags disambiguate sentinel-valued fields.
// Magic placement/schema and any scalar packing remain explicit caller work.
constexpr Line encode(const Words& words) {
    constexpr char digits[]="0123456789abcdef";Line out{};
    for(std::size_t word=0;word<words.size();++word)
        for(unsigned nibble=0;nibble<8;++nibble)
            out[word*8+nibble]=digits[(words[word]>>(28-4*nibble))&15u];
    out[96]='\n';return out;
}
}
