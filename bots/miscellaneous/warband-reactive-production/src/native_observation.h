#pragma once

#ifndef WB_NATIVE_OBSERVATION_SDK_DOUBLE
#include <cambc.h>
#endif
#include <cstdint>
#include <limits>
#include <optional>

namespace wb {

// Owning, visible combat records only. No hidden identity, wallet, camp awake
// state, or cross-input validity is inferred. Callers fence the input clock and
// discard/refetch these values after an action that can change their fields.
struct NativeCampObservation {
    const cambc::handle_camp_t handle;
    const cambc::detail::camp_state info;
    cambc::vec2 position() const { return {info.x, info.y}; }
    float health() const { return info.health; }
    float max_health() const { return info.max_health; }
    float strength() const { return info.strength; }
    float focus() const { return info.focus; }
};

struct NativeChampionObservation {
    const cambc::handle_champion_t handle;
    const cambc::detail::champion_info info;
    cambc::vec2 position() const { return {info.x, info.y}; }
    float health() const { return info.health; }
    float max_health() const { return info.max_health; }
    float strength() const { return info.strength; }
    float focus() const { return info.focus; }
    cambc::champion_state state() const {
        return cambc::champion_state::from_bits_retain(info.state);
    }
    int attack_range() const { return info.attack_range; }
    int move_speed() const { return info.move_speed; }
    // Captured visible life bit, not the SDK's globally readable is_alive().
    bool is_alive() const { return state().contains(cambc::champion_state::alive); }
    bool can_attack() const { return state().contains(cambc::champion_state::can_attack); }
    bool can_cast() const { return state().contains(cambc::champion_state::can_cast); }
    bool can_move() const { return state().contains(cambc::champion_state::can_move); }
    bool is_recalling() const { return state().contains(cambc::champion_state::recalling); }
    bool is_submitting_gems() const { return state().contains(cambc::champion_state::submitting_gems); }
    bool is_teleporting() const { return state().contains(cambc::champion_state::teleporting); }
};

inline NativeCampObservation native_camp(const cambc::camp& camp) {
    const auto handle = static_cast<cambc::handle_camp_t>(camp);
    cambc::detail::camp_state info{};
    cambc::detail::check(cambc::detail::sdk_camp(static_cast<std::uint32_t>(handle), &info));
    return {handle, info};
}

inline NativeChampionObservation native_champion(const cambc::champion& champion) {
    const auto handle = static_cast<cambc::handle_champion_t>(champion);
    // Pinned SDK designs use this sentinel and reject health() before an import.
    // Delegate the exceptional sentinel path so designs retain that exact error,
    // while an invalid physical sentinel retains the engine's actual error.
    if (static_cast<std::uint32_t>(handle) == std::numeric_limits<std::uint32_t>::max())
        (void)champion.health();
    cambc::detail::champion_info info{};
    cambc::detail::check(cambc::detail::sdk_champion(static_cast<std::uint32_t>(handle), &info));
    return {handle, info};
}

inline std::optional<NativeChampionObservation>
native_visible_champion(const cambc::champion& champion) {
    const auto handle = static_cast<cambc::handle_champion_t>(champion);
    // The SDK's design-only is_visible() is false without an engine lookup.
    if (static_cast<std::uint32_t>(handle) == std::numeric_limits<std::uint32_t>::max()
        && !champion.is_visible()) return std::nullopt;
    cambc::detail::champion_info info{};
    const auto code = cambc::detail::sdk_champion(static_cast<std::uint32_t>(handle), &info);
    if (code == static_cast<std::int32_t>(cambc::error_code::api_not_in_vision))
        return std::nullopt;
    cambc::detail::check(code); // In particular, api_not_found is not hidden.
    if (info.x < 0 || info.y < 0) return std::nullopt;
    return NativeChampionObservation{handle, info};
}

} // namespace wb
