#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <tuple>
#include <vector>

namespace plan3_runtime::candidate_prefix {
inline constexpr std::size_t maximum_candidates = 16;

// Caller joins each physical group to its canonical key and reward class.
struct Entry {
    int distance = -1, index = -1, key = -1;
    bool gem = false;
    friend bool operator==(const Entry&, const Entry&) = default;
};

inline bool less(const Entry& a, const Entry& b) {
    return std::tie(a.distance, a.key, a.index) < std::tie(b.distance, b.key, b.index);
}
inline bool same_group(const Entry& a, const Entry& b) {
    return a.index == b.index || a.key == b.key;
}

// O(input size * 16), two fixed buffers, and at most16 owned output entries.
// Keep the nearest groups within each class, then alternate GEM, GOLD. Empty
// classes donate their remaining places; this is candidate exposure, not ranking.
inline std::vector<Entry> select(std::span<const Entry> input) {
    std::array<std::array<Entry, maximum_candidates>, 2> nearest{};
    std::array<std::size_t, 2> count{};
    for (const auto& entry : input) {
        if (entry.distance < 0 || entry.index < 0 || entry.key < 0) continue;
        const auto slot = entry.gem ? 0u : 1u;
        auto& values = nearest[slot];
        auto& size = count[slot];
        bool skip = false;
        for (std::size_t i = 0; i < size; ++i) {
            if (!same_group(values[i], entry)) continue;
            if (!less(entry, values[i])) { skip = true; break; }
            for (auto j = i + 1; j < size; ++j) values[j - 1] = values[j];
            --size;
            break;
        }
        if (skip) continue;
        std::size_t position = 0;
        while (position < size && less(values[position], entry)) ++position;
        if (position == maximum_candidates) continue;
        for (auto j = std::min(size, maximum_candidates - 1); j > position; --j) values[j] = values[j - 1];
        values[position] = entry;
        size = std::min(size + 1, maximum_candidates);
    }
    std::vector<Entry> result;
    result.reserve(maximum_candidates);
    std::array<std::size_t, 2> cursor{};
    auto next = [&](unsigned slot) -> const Entry* {
        while (cursor[slot] < count[slot]) {
            const auto& candidate = nearest[slot][cursor[slot]];
            if (std::none_of(result.begin(), result.end(), [&](const auto& e) { return same_group(e, candidate); }))
                return &candidate;
            ++cursor[slot];
        }
        return nullptr;
    };
    unsigned wanted = 0;
    while (result.size() < maximum_candidates) {
        auto slot = wanted;
        const auto* candidate = next(slot);
        if (!candidate) { slot ^= 1u; candidate = next(slot); }
        if (!candidate) break;
        result.push_back(*candidate);
        ++cursor[slot];
        wanted ^= 1u;
    }
    return result;
}
} // namespace plan3_runtime::candidate_prefix
