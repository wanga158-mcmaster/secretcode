#pragma once
// Contact coalition protocol, independent of the SDK. Use only the final two shared bytes,
// after checking has_room(); champion camp-intent bytes stay untouched. Unlike a short modulo
// timestamp, the full 11-bit round cannot revive a stale record during a 2,000-round game.
#include <array>
#include <cstddef>
#include <cstdint>

namespace mb::coalition {

using Bytes = std::array<std::uint8_t, 2>;
inline constexpr int max_enemy_index = 7;
inline constexpr int max_encoded_round = 2047;

inline constexpr bool has_room(std::size_t capacity, std::size_t champion_slots) {
    return capacity >= champion_slots && capacity - champion_slots >= 2;
}

struct FocusRecord {
    int enemy = -1;
    int round = -1;

    constexpr bool valid() const {
        return enemy >= 0 && enemy <= max_enemy_index && round >= 0 && round <= max_encoded_round;
    }
    constexpr int age(int now) const {
        return valid() && now >= round ? now - round : -1;
    }
    constexpr bool fresh(int now) const {
        const int elapsed = age(now);
        return elapsed >= 0 && elapsed <= 1;
    }
    constexpr Bytes encode() const {
        if (!valid()) return {};
        // bits 0..2 enemy, 3..13 absolute round, 14 reserved zero, 15 valid.
        const auto word = static_cast<std::uint16_t>(0x8000u | static_cast<unsigned>(enemy)
            | (static_cast<unsigned>(round) << 3));
        return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8)};
    }
    static constexpr FocusRecord decode(Bytes bytes) {
        const auto word = static_cast<std::uint16_t>(bytes[0] | (static_cast<unsigned>(bytes[1]) << 8));
        if ((word & 0xc000u) != 0x8000u) return {};
        return {static_cast<int>(word & 7u), static_cast<int>((word >> 3) & 0x7ffu)};
    }
};

// This is an invitation to contribute from our current position, never authority to chase or
// to override a losing local fight. The caller supplies fresh, personal action prerequisites.
struct LocalOpportunity {
    int enemy = -1;
    bool personally_visible = false;
    bool alive = false;
    bool can_contribute = false;
    bool in_current_reach = false;
    bool locally_safe = false;
};

inline constexpr bool current_basic_eligible(bool personally_visible, bool alive, bool can_attack,
                                           int distance, int range, bool safe_fight) {
    return personally_visible && alive && can_attack && safe_fight && distance >= 0 && range >= 0
        && distance <= range;
}

inline constexpr bool can_adopt(const FocusRecord& record, int now, const LocalOpportunity& local) {
    return record.fresh(now) && record.enemy == local.enemy && local.personally_visible && local.alive
        && local.can_contribute && local.in_current_reach && local.locally_safe;
}

} // namespace mb::coalition
