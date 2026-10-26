#pragma once
// Pinned SDK 3.0.5 / engine1.0.3 owning-record adapter. This merges projections
// of one host record, not distinct observations. Buffs, lifecycle/alive, items,
// gold/gems, champion cooldowns and target metadata remain separate SDK queries.
#include <cambc.h>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mosaic::native_observation {
static_assert(sizeof(cambc::detail::camp_state)==32);
static_assert(offsetof(cambc::detail::camp_state,health)==8);
static_assert(offsetof(cambc::detail::camp_state,cooldowns)==24);
static_assert(sizeof(cambc::detail::champion_info)==44);
static_assert(offsetof(cambc::detail::champion_info,health)==16);
static_assert(offsetof(cambc::detail::champion_info,move_speed)==32);
static_assert(offsetof(cambc::detail::champion_info,state)==40);

// Provenance only: callers fence epoch/actor/revision before and after collection
// and never reuse a record after a mutation. No implicit clock query or freshness
// claim is made by an owning record. A suspended input invalidates that contract.
// Equivalence concerns successful coherent packets and propagated native errors,
// not arbitrary partial-query traces or caller scratch after an exception.
struct Stamp {
    int epoch{-1},actor{-1};
    std::uint64_t revision{};
    bool matches(const Stamp& other) const {
        return epoch>=0&&actor>=0&&epoch==other.epoch&&actor==other.actor&&revision==other.revision;
    }
};
struct ChampionRecord {
    Stamp stamp;
    cambc::detail::champion_info value{};
    cambc::vec2 position() const{return {value.x,value.y};}
    float health() const{return value.health;}
    float max_health() const{return value.max_health;}
    float strength() const{return value.strength;}
    float focus() const{return value.focus;}
    int move_speed() const{return value.move_speed;}
    int attack_range() const{return value.attack_range;}
    cambc::champion_state state() const {
        return cambc::champion_state::from_bits_retain(static_cast<std::uint32_t>(value.state));
    }
    // Exactly the public unit::attack_tiles geometry from this captured record;
    // it is not an attack-target, visibility, occupancy or allowance predicate.
    void attack_tiles_into(cambc::bitboard& out) const {
        out.clear();out.draw_chebyshev(position(),attack_range());
    }
    cambc::bitboard attack_tiles() const {
        cambc::bitboard out{cambc::uninitialized};attack_tiles_into(out);return out;
    }
};
struct CampRecord {
    Stamp stamp;
    cambc::detail::camp_state value{};
    cambc::vec2 position() const{return {value.x,value.y};}
    float health() const{return value.health;}
    float max_health() const{return value.max_health;}
    float strength() const{return value.strength;}
    float focus() const{return value.focus;}
};
inline ChampionRecord read_champion(cambc::handle_champion_t handle,Stamp stamp) {
    const auto raw=static_cast<std::uint32_t>(handle);
    // This is the SDK's reserved design-only identity; preserve its explicit
    // live-query rejection rather than calling the host with that sentinel.
    if(raw==std::numeric_limits<std::uint32_t>::max())
        throw cambc::error{cambc::error_code::api_not_in_vision,"design champion has no live state"};
    ChampionRecord out{stamp,{}};
    cambc::detail::check(cambc::detail::sdk_champion(raw,&out.value));return out;
}
inline ChampionRecord read_champion(const cambc::champion& champion,Stamp stamp) {
    return read_champion(static_cast<cambc::handle_champion_t>(champion),stamp);
}
inline CampRecord read_camp(cambc::handle_camp_t handle,Stamp stamp) {
    CampRecord out{stamp,{}};
    cambc::detail::check(cambc::detail::sdk_camp(static_cast<std::uint32_t>(handle),&out.value));return out;
}
inline CampRecord read_camp(const cambc::camp& camp,Stamp stamp) {
    return read_camp(static_cast<cambc::handle_camp_t>(camp),stamp);
}
// The live tile lookup is exactly vision::camp_at's checked first import. The
// caller needs only its identity, so no immutable camp metadata is reconstructed.
// Native visibility/empty-tile/coordinate errors propagate unchanged; nothing is
// inferred from spawn coordinates, a remembered body, or a visibility predicate.
[[nodiscard]] inline cambc::handle_camp_t camp_handle_at(cambc::vec2 position) {
    std::uint32_t handle{};
    cambc::detail::check(cambc::detail::sdk_camp_at(position.x,position.y,&handle));
    return static_cast<cambc::handle_camp_t>(handle);
}

struct GroupVisibility {
    Stamp stamp;
    cambc::handle_camp_group_t group{std::numeric_limits<std::uint32_t>::max()};
    bool any_spawn_visible{};
};
struct GroupRecord {Stamp stamp;cambc::map::camp_group_state value;};
// The packed gate must come from the SAME current group/owner packet's exported
// visibility, with every spawn represented. It substitutes only the wrapper's
// sdk_vision + any-spawn test. It does not infer visibility from group state or
// enumerate a hidden group's lifecycle. Caller clock fences remain mandatory.
inline GroupRecord read_group(cambc::handle_camp_group_t handle,Stamp stamp,GroupVisibility gate) {
    if(!gate.stamp.matches(stamp)||gate.group!=handle||!gate.any_spawn_visible)
        throw cambc::error{cambc::error_code::api_not_in_vision,"camp group is not in vision"};
    std::uint32_t state{};
    cambc::detail::check(cambc::detail::sdk_camp_group_state(static_cast<std::uint32_t>(handle),&state));
    switch(state) {
    case 1:return {stamp,cambc::map::camp_group_alive{}};
    case 2:return {stamp,cambc::map::camp_group_reward_available{}};
    case 3: {
        std::int32_t timer{};
        cambc::detail::check(cambc::detail::sdk_camp_group_cooldown(static_cast<std::uint32_t>(handle),&timer));
        return {stamp,cambc::map::camp_group_respawning{timer}};
    }
    case 0:throw cambc::error{cambc::error_code::api_not_in_vision,"camp group state is unknown"};
    default:throw std::runtime_error("unknown engine camp group state");
    }
}
inline GroupRecord read_group(const cambc::map::camp_group& group,Stamp stamp,GroupVisibility gate) {
    return read_group(static_cast<cambc::handle_camp_group_t>(group),stamp,gate);
}
} // namespace mosaic::native_observation
