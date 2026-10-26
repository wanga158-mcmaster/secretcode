#pragma once

#include <array>
#include <bit>
#include <cstdint>

namespace plan4 {

// Numeric snapshots only. No SDK object or planning-frame reference survives
// capture, and unused slots are never read.
struct PhaseSample {
    int actor, round;
    unsigned before, after;
    bool movement;
    std::array<std::uint64_t, 12> values;
};

struct DeferredPhases {
    std::array<PhaseSample, 6> slots;
    std::array<unsigned char, 6> order;
    int source_round = -1;
    unsigned pending_mask = 0, captured_mask = 0, count = 0;

    bool wants(unsigned kind, int now, unsigned seen) {
        if (kind >= slots.size() || now < 0) return false;
        if (source_round != now) {
            source_round = now;
            pending_mask = captured_mask = count = 0;
        }
        const unsigned bit = 1u << kind;
        return !(captured_mask & bit) && (!(seen & bit) || now % 32 == 0);
    }
    bool save(unsigned kind, const PhaseSample& sample) {
        if (kind >= slots.size() || sample.round != source_round || count >= order.size()) return false;
        const unsigned bit = 1u << kind;
        if (captured_mask & bit) return false;
        slots[kind] = sample;
        order[count++] = static_cast<unsigned char>(kind);
        pending_mask |= bit;
        captured_mask |= bit;
        return true;
    }
    void discard() {
        pending_mask = count = 0;
        // Keep the per-round cap even when low fuel or a stale clock omits rows.
    }
};

} // namespace plan4
