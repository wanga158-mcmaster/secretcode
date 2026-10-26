#pragma once
#include <array>
#include <cstdint>

namespace plan4::drafting {
// SDK-independent identities. The adapter maps SDK enumerators explicitly.
enum class Pick {
    ryth, vesper_vale, vendetta, cassi_and_issac, stellar, prism,
    marcella, willow, ren, varg, tavi, heart_of_ruin, count
};
enum class Profile {
    reference, foundry, governor, habitat, repairman, archipelago,
    timetable, stewards, bulkheads, chronicle, quotient, counterpart,
    kernel, capital, commons, metronome, covenant, count
};
inline constexpr int pick_count = 12;
inline constexpr int profile_count = 17;
inline constexpr std::uint16_t all_mask = (1u << pick_count) - 1u;

struct Input {
    int team_size = 0;
    int selected_count = 0;
    std::array<Pick, pick_count> selected{};
    std::uint16_t available_mask = all_mask;
    int actor_capacity = 5;
};
struct Decision {
    bool valid = false;
    bool has_pick = false;
    Pick pick = Pick::tavi;
};

namespace detail {
using Order = std::array<Pick, pick_count>;
inline constexpr Order fallback{
    Pick::tavi, Pick::varg, Pick::vendetta, Pick::vesper_vale,
    Pick::marcella, Pick::ryth, Pick::ren, Pick::prism, Pick::stellar,
    Pick::willow, Pick::cassi_and_issac, Pick::heart_of_ruin
};
constexpr std::uint16_t bit(Pick pick) {
    return std::uint16_t(1u << static_cast<unsigned>(pick));
}
constexpr Order complete(std::array<Pick, 5> prefix) {
    Order result{};
    std::uint16_t seen = 0;
    int next = 0;
    for (const auto pick : prefix) {
        result[next++] = pick;
        seen |= bit(pick);
    }
    for (const auto pick : fallback) {
        if ((seen & bit(pick)) == 0) result[next++] = pick;
    }
    return result;
}
// Provisional design-specific priorities, not measured optimal rosters.
inline constexpr std::array<Order, profile_count> orders{{
    complete({Pick::tavi, Pick::varg, Pick::vendetta, Pick::prism, Pick::marcella}),
    complete({Pick::cassi_and_issac, Pick::varg, Pick::ryth, Pick::willow, Pick::marcella}),
    complete({Pick::vesper_vale, Pick::tavi, Pick::vendetta, Pick::heart_of_ruin, Pick::prism}),
    complete({Pick::vendetta, Pick::vesper_vale, Pick::ren, Pick::prism, Pick::tavi}),
    complete({Pick::heart_of_ruin, Pick::ryth, Pick::varg, Pick::marcella, Pick::willow}),
    complete({Pick::vendetta, Pick::marcella, Pick::tavi, Pick::ren, Pick::vesper_vale}),
    complete({Pick::vendetta, Pick::varg, Pick::marcella, Pick::willow, Pick::stellar}),
    complete({Pick::marcella, Pick::vesper_vale, Pick::vendetta, Pick::tavi, Pick::prism}),
    complete({Pick::marcella, Pick::varg, Pick::heart_of_ruin, Pick::willow, Pick::tavi}),
    complete({Pick::prism, Pick::tavi, Pick::vesper_vale, Pick::stellar, Pick::willow}),
    complete({Pick::stellar, Pick::tavi, Pick::prism, Pick::cassi_and_issac, Pick::willow}),
    complete({Pick::ren, Pick::marcella, Pick::tavi, Pick::vesper_vale, Pick::heart_of_ruin}),
    complete({Pick::tavi, Pick::varg, Pick::vendetta, Pick::prism, Pick::marcella}),
    complete({Pick::tavi, Pick::vesper_vale, Pick::marcella, Pick::prism, Pick::vendetta}),
    complete({Pick::cassi_and_issac, Pick::vendetta, Pick::varg, Pick::prism, Pick::vesper_vale}),
    complete({Pick::vesper_vale, Pick::vendetta, Pick::tavi, Pick::marcella, Pick::ren}),
    complete({Pick::marcella, Pick::tavi, Pick::vesper_vale, Pick::heart_of_ruin, Pick::varg})
}};
inline constexpr std::array<const char*, profile_count> names{
    "reference", "foundry", "governor", "habitat", "repairman", "archipelago",
    "timetable", "stewards", "bulkheads", "chronicle", "quotient", "counterpart",
    "kernel", "capital", "commons", "metronome", "covenant"
};
constexpr bool valid_profile(Profile profile) {
    const int index = static_cast<int>(profile);
    return index >= 0 && index < profile_count;
}
constexpr int population(std::uint16_t mask) {
    int count = 0;
    for (int index = 0; index < pick_count; ++index)
        if ((mask & (1u << index)) != 0) ++count;
    return count;
}
} // namespace detail

constexpr const std::array<Pick, pick_count>& order(Profile profile) {
    return detail::orders[detail::valid_profile(profile) ? static_cast<int>(profile) : 0];
}
constexpr const char* profile_name(Profile profile) {
    return detail::valid_profile(profile) ? detail::names[static_cast<int>(profile)] : "unknown";
}

constexpr Decision choose(Profile profile, const Input& input) {
    if (!detail::valid_profile(profile) || input.team_size < 1 ||
        input.team_size > pick_count || input.selected_count < 0 ||
        input.selected_count > input.team_size || input.actor_capacity < 1 ||
        (input.available_mask & ~all_mask) != 0 ||
        input.team_size > detail::population(input.available_mask)) return {};

    std::uint16_t selected = 0;
    for (int index = 0; index < input.selected_count; ++index) {
        const auto pick = input.selected[index];
        const int value = static_cast<int>(pick);
        if (value < 0 || value >= pick_count) return {};
        const auto mask = detail::bit(pick);
        if ((selected & mask) != 0 || (input.available_mask & mask) == 0) return {};
        selected |= mask;
    }
    if (input.selected_count == input.team_size) return {true, false, Pick::tavi};

    // The duo uses one selection but two actors. Capacity is a preference: if
    // it is the only remaining legal selection, finishing the draft wins.
    const bool defer_duo = input.team_size + 1 > input.actor_capacity;
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto pick : order(profile)) {
            const auto mask = detail::bit(pick);
            if ((input.available_mask & mask) == 0 || (selected & mask) != 0) continue;
            if (pass == 0 && defer_duo && pick == Pick::cassi_and_issac) continue;
            return {true, true, pick};
        }
    }
    return {}; // Inconsistent input cannot reach this with a complete order.
}
} // namespace plan4::drafting
