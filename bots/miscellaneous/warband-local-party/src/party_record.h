#pragma once
// Three-byte identity and acknowledgements for one personally observed local pair.
// A record communicates a duty; it does not establish visibility or contribution.
#include <array>
#include <cstdint>

namespace mb::party {

struct LocalRecord {
    int group = -1;
    int leader = -1;
    int helper = -1;
    int epoch = -1;
    unsigned ack = 0;  // bit 0: leader, bit 1: helper

    bool valid() const {
        return group >= 0 && group <= 62 && leader >= 0 && leader < helper
            && helper <= 4 && epoch >= 0 && epoch <= 2047 && ack <= 3;
    }

    bool fresh(int now, int ttl = 64) const {
        // Absolute rounds avoid the aliasing of a wrapped timestamp. Subtract only
        // after the ordering test, so even negative/ancient caller rounds are safe.
        return valid() && ttl >= 0 && now >= epoch && now - epoch <= ttl;
    }

    bool contains(int actor) const {
        return valid() && (actor == leader || actor == helper);
    }

    bool acknowledged(int actor) const {
        if (!contains(actor)) return false;
        return (ack & (actor == leader ? 1u : 2u)) != 0;
    }

    void acknowledge(int actor) {
        if (contains(actor)) ack |= actor == leader ? 1u : 2u;
    }

    bool same_duty(const LocalRecord& other) const {
        return valid() && other.valid() && group == other.group && leader == other.leader
            && helper == other.helper && epoch == other.epoch;
    }

    std::array<std::uint8_t, 3> encode() const {
        if (!valid()) return {};
        const auto mask = (1u << leader) | (1u << helper);
        const auto word = static_cast<std::uint32_t>(group + 1) | (mask << 6)
            | (static_cast<std::uint32_t>(epoch) << 11) | (ack << 22);
        return {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8),
                static_cast<std::uint8_t>(word >> 16)};
    }

    static LocalRecord decode(std::array<std::uint8_t, 3> bytes) {
        const auto word = static_cast<std::uint32_t>(bytes[0])
            | (static_cast<std::uint32_t>(bytes[1]) << 8)
            | (static_cast<std::uint32_t>(bytes[2]) << 16);
        const auto group_code = word & 63u;
        if (group_code == 0) return {};
        const auto mask = (word >> 6) & 31u;
        LocalRecord out;
        int actors = 0;
        for (int actor = 0; actor < 5; ++actor) {
            if ((mask & (1u << actor)) == 0) continue;
            if (actors == 0) out.leader = actor;
            else if (actors == 1) out.helper = actor;
            ++actors;
        }
        if (actors != 2) return {};
        out.group = static_cast<int>(group_code) - 1;
        out.epoch = static_cast<int>((word >> 11) & 2047u);
        out.ack = (word >> 22) & 3u;
        return out.valid() ? out : LocalRecord{};
    }
};

} // namespace mb::party
