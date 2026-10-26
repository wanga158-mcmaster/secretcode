#pragma once
#include <cstdint>
#include <limits>

// Actor-private stock observations only. No SDK, forecasts, public rewards,
// bank ownership, or policy counters live here.
namespace p5::runoff::receipts {
struct Frame {
    int actor = -1, round = -1, bag = -1, gold = -1, passive_gold = -1;
    bool current = false, alive = false, submitting = false, supported = false;
    std::uint64_t capture = 0, bank = 0, purchase = 0, cash_attempt = 0;
    int capture_round = -1, capture_group = -1, capture_gems = -1, bank_round = -1;
    bool operator==(const Frame&) const = default;
};
struct Capture {
    bool exact = false;
    std::uint64_t receipt = 0;
    int round = -1, group = -1, gems = 0, gold = 0;
    bool operator==(const Capture&) const = default;
};
struct State {
    Frame end{}, entry{};
    Capture cached{};
    int actor = -1, latest_round = -1, blocked_round = -1;
    bool has_end = false, baseline_valid = false, has_entry = false;
    std::uint64_t capture_high = 0, bank_high = 0, purchase_high = 0, cash_high = 0;
};

// The caller invokes this after a stale/failed boundary or an uncertain action.
// Keep high-water marks: clearing a baseline cannot replay an earlier receipt.
inline void invalidate(State& s) noexcept {
    s.baseline_valid = false;
    s.cached = {};
    s.blocked_round = s.latest_round;
}
namespace detail {
inline bool shape(const Frame& f) noexcept {
    return f.current && f.actor >= 0 && f.round >= 0 && f.bag >= 0 &&
        f.gold >= 0 && f.passive_gold >= 0;
}
inline bool supported(const Frame& f) noexcept {
    return shape(f) && f.alive && f.supported && !f.submitting;
}
inline bool accept(const Frame& f, State& s) noexcept {
    if (!shape(f) || (s.actor >= 0 && f.actor != s.actor) || f.round < s.latest_round) {
        invalidate(s);
        return false;
    }
    if (s.actor < 0) s.actor = f.actor;
    s.latest_round = f.round;
    if (f.capture < s.capture_high || f.bank < s.bank_high ||
        f.purchase < s.purchase_high || f.cash_attempt < s.cash_high) {
        invalidate(s);
        return false;
    }
    s.capture_high = f.capture;
    s.bank_high = f.bank;
    s.purchase_high = f.purchase;
    s.cash_high = f.cash_attempt;
    return s.blocked_round != f.round;
}
} // namespace detail

// Call once at owner entry, before actions. Identical rereads return the same
// native serial. A changed owner-entry packet poisons that round; it cannot
// manufacture a second capture or restore a previously invalidated baseline.
inline Capture observe(const Frame& f, State& s) noexcept {
    if (s.has_entry && f.round == s.entry.round && f.round == s.latest_round) {
        if (f == s.entry && s.blocked_round != f.round) return s.cached;
        invalidate(s);
        return {};
    }
    const auto prior_capture_high = s.capture_high;
    if (!detail::accept(f, s)) return {};
    // A new entry cannot occur after this same owner's finalized end sample.
    if (s.has_end && f.round == s.end.round) {
        invalidate(s);
        return {};
    }
    s.entry = f;
    s.has_entry = true;
    s.cached = {};
    const auto& p = s.end;
    const bool consecutive = s.has_end && s.baseline_valid &&
        p.round < std::numeric_limits<int>::max() && f.round == p.round + 1;
    const bool clean = consecutive && detail::supported(p) && detail::supported(f) &&
        p.actor == f.actor && p.passive_gold == f.passive_gold &&
        p.bank == f.bank && p.bank_round == f.bank_round &&
        p.purchase == f.purchase && p.cash_attempt == f.cash_attempt;
    if (clean && p.capture != std::numeric_limits<std::uint64_t>::max() &&
        f.capture == p.capture + 1 && f.capture > prior_capture_high &&
        f.capture_round == f.round && f.capture_group >= 0 && f.capture_gems > 0) {
        const auto gems = std::int64_t(f.bag) - p.bag;
        const auto gold = std::int64_t(f.gold) - p.gold - f.passive_gold;
        if (gems == f.capture_gems && gold >= 0 && gold <= std::numeric_limits<int>::max())
            s.cached = {true, f.capture, f.round, f.capture_group, int(gems), int(gold)};
    }
    // Every new owner entry consumes its previous end basis. Only an explicit
    // fresh end sample can establish the next interval, even after no payout.
    s.baseline_valid = false;
    return s.cached;
}

// Call only after actual actions/Runtime finalization and a same-round check,
// using newly queried private stock. A supported frame means plain permitted
// inventory and no other gold-producing/PvP/autonomous action in this domain;
// cash_attempt increments BEFORE every possibly ambiguous cash action attempt.
// No exact capture is emitted here. Identical finalized rereads are idempotent;
// a different second final sample invalidates the round rather than rebasing it.
inline bool finalize(const Frame& f, State& s) noexcept {
    if (s.has_end && f.round == s.end.round && f.round == s.latest_round) {
        if (f == s.end && s.blocked_round != f.round) return s.baseline_valid;
        invalidate(s);
        return false;
    }
    if (!detail::accept(f, s)) return false;
    if (!detail::supported(f)) {
        invalidate(s);
        return false;
    }
    s.end = f;
    s.has_end = s.baseline_valid = true;
    return true;
}
} // namespace p5::runoff::receipts
