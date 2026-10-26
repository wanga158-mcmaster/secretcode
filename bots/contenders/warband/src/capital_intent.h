#pragma once
#include <cstdint>

// Eight-byte ranking intent only. The three-bit round stamp aliases every eight
// rounds: presence is never action, HP, position, life or current-work authority.
namespace wb::capital::intent {
inline constexpr unsigned actors = 5;
inline constexpr unsigned slot_bits = 11;
inline constexpr unsigned magic_bits = 9;
inline constexpr unsigned magic_shift = actors * slot_bits;
inline constexpr std::uint64_t magic = 0x16D;
inline constexpr std::uint64_t slot_mask = (std::uint64_t{1} << slot_bits) - 1;
inline constexpr std::uint64_t magic_mask = ((std::uint64_t{1} << magic_bits) - 1) << magic_shift;
inline constexpr std::uint64_t signature = magic << magic_shift;
static_assert(magic_shift + magic_bits == 64);
static_assert(sizeof(std::uint64_t) == 8);

struct Intent {
    bool present{};
    int group{-1};
    unsigned mode{};
};

// group -1 clears one actor's intent. Invalid input leaves prior untouched.
// A foreign payload cannot supply prior intents; valid encoding starts afresh.
inline constexpr std::uint64_t encode(std::uint64_t prior, unsigned ordinal,
                                      int group, unsigned mode, unsigned round) {
    if (ordinal >= actors || group < -1 || group > 62 || mode > 3) return prior;
    if ((prior & magic_mask) != signature) prior = signature;
    const unsigned shift = ordinal * slot_bits;
    const std::uint64_t slot = std::uint64_t(group + 1)
        | (std::uint64_t(mode) << 6) | (std::uint64_t(round & 7U) << 8);
    return (prior & ~(slot_mask << shift)) | (slot << shift);
}

inline constexpr Intent decode(std::uint64_t payload, unsigned ordinal, unsigned round) {
    if (ordinal >= actors || (payload & magic_mask) != signature) return {};
    const auto slot = (payload >> (ordinal * slot_bits)) & slot_mask;
    const unsigned code = unsigned(slot & 63U);
    const unsigned stamp = unsigned((slot >> 8) & 7U);
    if (!code || ((round - stamp) & 7U) > 1) return {};
    return {true, int(code) - 1, unsigned((slot >> 6) & 3U)};
}
} // namespace wb::capital::intent
