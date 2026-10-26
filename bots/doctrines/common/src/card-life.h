#pragma once
#include <array>
#include <cstdint>
#include <limits>

namespace p5::diagnostics {
enum class CardLifeEvent { purchase = 1, proof_wait, production, payout,
    bank_intent, renewal, renewal_arrival, terminal };
struct CardLifeRecord {
    CardLifeEvent event = CardLifeEvent::purchase;
    int project = 1, actor = -1, round = -1;
    int job = -1, group = -1, renewal = -1, altar = -1, family = -1, renewal_entry = -1;
    int from_stage = -1, stage = -1, phase = -1, reason = -1, action = -1, outcome = -1;
    int flags = 0, position = -1, observed_group = -1, budget = -1, gold = -1, bag = -1;
    std::uint64_t purchase = 0, capture = 0, bank = 0;
};
inline unsigned card_life_increment(unsigned value) noexcept {
    return value == std::numeric_limits<unsigned>::max() ? value : value + 1;
}

// One actual purchased project, eight lifetime milestones, terminal slot kept
// even after earlier records are drained. No SDK, allocation or formatting.
class CardLifeQueue {
    std::array<CardLifeRecord, 8> records_{};
    unsigned count_ = 0, head_ = 0, seen_ = 0, dropped_ = 0;
    bool armed_ = false, terminal_ = false;
    bool same_project(const CardLifeRecord& value) const noexcept {
        const auto& first = records_[0];
        return value.project == first.project && value.actor == first.actor &&
            value.job == first.job && value.group == first.group && value.renewal == first.renewal &&
            value.altar == first.altar && value.family == first.family && value.renewal_entry == first.renewal_entry;
    }
public:
    bool arm(const CardLifeRecord& value) noexcept {
        if (armed_ || value.event != CardLifeEvent::purchase || value.project <= 0 || value.actor < 0 || value.round < 0)
            return false;
        records_[0] = value; count_ = 1; seen_ = 1u << unsigned(CardLifeEvent::purchase); armed_ = true;
        return true;
    }
    bool push(const CardLifeRecord& value) noexcept {
        if (!armed_ || terminal_) return false;
        const unsigned event = unsigned(value.event);
        if (event < 1 || event > unsigned(CardLifeEvent::terminal) || !same_project(value) ||
            value.round < records_[0].round) {
            dropped_ = card_life_increment(dropped_); return false;
        }
        if (seen_ & (1u << event)) return false;
        const bool terminal = value.event == CardLifeEvent::terminal;
        if (count_ >= records_.size() - (terminal ? 0u : 1u)) {
            dropped_ = card_life_increment(dropped_); return false;
        }
        records_[count_++] = value; seen_ |= 1u << event; terminal_ = terminal;
        return true;
    }
    bool armed() const noexcept { return armed_; }
    bool pending() const noexcept { return head_ < count_; }
    const CardLifeRecord* front() const noexcept { return pending() ? &records_[head_] : nullptr; }
    void consume() noexcept { if (pending()) ++head_; }
    unsigned count() const noexcept { return count_; }
    unsigned dropped() const noexcept { return dropped_; }
};
} // namespace p5::diagnostics
