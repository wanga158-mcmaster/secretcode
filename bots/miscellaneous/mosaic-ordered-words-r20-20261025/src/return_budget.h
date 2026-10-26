#pragma once
// Bounded CAMP-only return incumbent. The adapter supplies actual owner-frame
// authority, legal ordinary edges, and whole-horizon threat upper bounds. This
// is independent of work nomination, current sleep, and the local word hot set.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace mosaic::return_budget {
using Cost = std::uint64_t;
inline constexpr Cost cost_limit = std::numeric_limits<Cost>::max() / 128;
inline constexpr std::size_t max_threats = 500;
inline constexpr unsigned max_waits = 32;

struct Point {
    int x{}, y{};
    friend constexpr bool operator==(Point, Point) = default;
};
struct Threat { Point pos; int range{}; double strength{}; };
struct Damage { Cost amount{}; bool valid{true}; };

inline std::uint64_t distance(Point a, Point b) {
    const auto dx = std::int64_t(a.x) - std::int64_t(b.x);
    const auto dy = std::int64_t(a.y) - std::int64_t(b.y);
    return std::uint64_t(std::max(dx < 0 ? -dx : dx, dy < 0 ? -dy : dy));
}

// Scan the COMPLETE supplied catalog; never truncate to the word's eight hot
// targets. Each input Strength/range and multiplier must bound that camp over
// the entire quoted horizon. No asleep/dead/aggro inference is made here.
// Floor HP/shield and ceil each camp hit give integral conservative accounting.
// Retaining both double and float products also covers float32 multiplication
// rounding of the supplied native float values. Other defense/effect changes
// belong in the adapter's upper bound, not in work-memory state.
inline Damage raw_damage_at(Point p, std::span<const Threat> threats,
                            double taken_multiplier = 1.0) {
    if (threats.size() > max_threats || !std::isfinite(taken_multiplier) || taken_multiplier < 0)
        return {0, false};
    Cost total = 0;
    for (const auto& t : threats) {
        if (t.range < 0 || !std::isfinite(t.strength) || t.strength < 0) return {0, false};
        if (distance(p, t.pos) > std::uint64_t(t.range)) continue;
        const double exact = t.strength * taken_multiplier;
        const double native = double(float(t.strength) * float(taken_multiplier));
        const double upper = std::ceil(std::max(exact, native));
        if (!std::isfinite(upper) || upper < 0 || upper > double(cost_limit)) return {0, false};
        const auto hit = Cost(upper);
        if (hit > cost_limit - total) return {0, false};
        total += hit;
    }
    return {total, true};
}

enum class Reason {
    ok, unanchored, origin_mismatch, not_ordinary_step, invalid_damage,
    capacity, invalid_state, anchor_not_quiet
};

struct State {
    double health{};
    double shield{};
    // Number of NEXT camp phases the ACTUAL currently present shield covers.
    // At a ready owner input, native duration1 covers the next camp phase. A
    // sampled zero timer requires phase provenance supplied by the adapter.
    int shield_phases{};
    bool can_move_now{true};
    // Extra known waits, beyond the first wait caused by spent movement.
    // Stun/channel release time is adapter authority, not inferred from MAC.
    unsigned additional_forced_waits{};
    bool allow_one_refusal{true};
    // Default0: mandatory observe/quote/dispatch must fit its admitted fuel.
    // Positive values are supported conservatively without ANY shield credit.
    unsigned compute_waits{};
};

struct Quote {
    bool valid{}, safe{};
    Cost required_hp{}, worst_loss{};
    Point first_reverse_endpoint{};
    std::size_t steps{};
    Reason reason{Reason::unanchored};
    bool shield_credited{};
};

class Trace {
public:
    // Includes the quiet anchor: at most31 ordinary reverse edges.
    static constexpr std::size_t capacity = 32;
    bool anchored() const { return count_ != 0; }
    bool valid() const { return anchored(); }
    bool at(Point p) const { return anchored() && current() == p; }
    std::size_t size() const { return count_; }
    Point current() const { return count_ ? poses_[count_ - 1].point : Point{}; }
    // Caller indexes only [0,size()); these accessors expose fixed geometry
    // for a full-horizon threat reprice, never live WorkNomination authority.
    Point point(std::size_t i) const { return poses_[i].point; }
    Cost damage(std::size_t i) const { return poses_[i].damage; }
    std::uint64_t revision() const { return revision_; }
    void invalidate() { count_ = 0; ++revision_; }
    void clear() { invalidate(); }

    // Atomic geometry-preserving reprice. A newly threatened anchor, invalid
    // numeric bound or wrong-size input invalidates the WHOLE certificate;
    // no partly refreshed trace may look safe. Adapter may then reset at its
    // actual current pose only if fresh full-horizon evidence makes it quiet.
    Reason refresh_damage(std::span<const Damage> damage) {
        if (!anchored()) return Reason::unanchored;
        if (damage.size() != count_) { invalidate(); return Reason::invalid_damage; }
        for (const auto d : damage) if (!d.valid || d.amount > cost_limit) {
            invalidate(); return Reason::invalid_damage;
        }
        if (damage[0].amount != 0) { invalidate(); return Reason::anchor_not_quiet; }
        for (std::size_t i = 0; i < count_; ++i) poses_[i].damage = damage[i].amount;
        ++revision_; return Reason::ok;
    }

    // Called only after ACTUALLY reaching a quiet pose. A rejected reset leaves
    // the previous anchor intact. Unexpected displacement/death/arrival must
    // invalidate the old trace before a fresh actual anchor is established.
    bool reset_quiet(Point actual, Damage d) {
        if (!d.valid || d.amount != 0) return false;
        poses_[0] = {actual, 0}; count_ = 1; ++revision_; return true;
    }

    // PRE-dispatch admission: capacity failure never drops the old anchor.
    // This class admits one-tile ordinary edges only. A dash or speed>1 edge
    // needs a separately supplied executable suffix, not a fictitious inverse.
    Reason can_enter(Point from, Point to, Damage d) const {
        if (!anchored()) return Reason::unanchored;
        if (from != current()) return Reason::origin_mismatch;
        if (!d.valid || d.amount > cost_limit) return Reason::invalid_damage;
        if (distance(from, to) > 1) return Reason::not_ordinary_step;
        if (d.amount == 0) return Reason::ok;
        for (std::size_t i = 0; i < count_; ++i) if (poses_[i].point == to)
            return i == 0 ? Reason::anchor_not_quiet : Reason::ok;
        return count_ == capacity ? Reason::capacity : Reason::ok;
    }

    // Only bool-true ordinary effects may mutate the trace. A refusal keeps
    // its current endpoint/certificate; its actual extra camp phase must be
    // charged by the caller's finite refusal allowance, not reset as progress.
    Reason observe_step(Point from, Point to, Damage d, bool applied) {
        if (!anchored()) return Reason::unanchored;
        if (from != current()) return Reason::origin_mismatch;
        if (!applied) return Reason::ok;
        const auto reason = can_enter(from, to, d);
        if (reason != Reason::ok) return reason;
        if (d.amount == 0) { reset_quiet(to, d); return Reason::ok; }
        for (std::size_t i = 1; i < count_; ++i) if (poses_[i].point == to) {
            poses_[i].damage = d.amount; count_ = i + 1; ++revision_; return Reason::ok;
        }
        poses_[count_++] = {to, d.amount}; ++revision_; return Reason::ok;
    }

    // Preview the complete post-step return suffix without mutating actual
    // history. Set can_move_now=false for an outward ordinary move or a work
    // word that spends M, unless a separately modelled immediate dash exits.
    Quote quote_after_step(Point from, Point to, Damage d, const State& state) const {
        const auto reason = can_enter(from, to, d);
        if (reason != Reason::ok) return invalid(reason);
        Trace projected = *this;
        projected.observe_step(from, to, d, true);
        return projected.quote(state);
    }

    Quote quote(const State& state) const {
        if (!anchored()) return invalid(Reason::unanchored);
        if (!std::isfinite(state.health) || !std::isfinite(state.shield) ||
            state.health < 0 || state.shield < 0 || state.health > double(cost_limit) ||
            state.shield > double(cost_limit) || state.shield_phases < 0 ||
            state.additional_forced_waits > max_waits || state.compute_waits > max_waits)
            return invalid(Reason::invalid_state);
        const unsigned forced = state.additional_forced_waits + (state.can_move_now ? 0u : 1u);
        Cost total = Cost(forced) * poses_[count_ - 1].damage, max_origin = 0;
        for (std::size_t i = 1; i < count_; ++i) {
            total += poses_[i - 1].damage;
            max_origin = std::max(max_origin, poses_[i].damage);
        }
        Cost worst = total;
        if (state.compute_waits != 0) {
            // Arbitrary placement of compute waits also changes shield expiry.
            // Ignore shield altogether in this optional conservative model;
            // then every extra wait is bounded by maximum origin exposure.
            worst += Cost(state.compute_waits) * max_origin;
            if (state.allow_one_refusal && count_ > 1) worst += max_origin;
        } else {
            // O(L), no scenario copies: with nonnegative damage/no healing,
            // shield absorbs min(amount, sum of the first P covered phases).
            // Inserting a refusal before phase P replaces the last covered
            // baseline phase; insertion after P leaves shield coverage alone.
            const Cost shield = Cost(std::floor(state.shield));
            const auto phases = unsigned(state.shield_phases);
            const Cost covered = prefix_damage(phases, forced);
            const Cost before_last = phases ? prefix_damage(phases - 1, forced) : 0;
            worst -= std::min(shield, covered);
            if (state.allow_one_refusal) {
                std::size_t inserted_at = forced;
                for (std::size_t i = count_ - 1; i > 0; --i, ++inserted_at) {
                    const Cost hit = poses_[i].damage;
                    const Cost protected_damage = inserted_at < phases ? before_last + hit : covered;
                    worst = std::max(worst, total + hit - std::min(shield, protected_damage));
                }
            }
        }
        Quote q;
        q.valid = true; q.reason = Reason::ok; q.worst_loss = worst;
        q.required_hp = worst + 1; // strict positive HP with conservative floor
        q.safe = Cost(std::floor(state.health)) >= q.required_hp;
        q.steps = count_ - 1;
        q.first_reverse_endpoint = count_ > 1 ? poses_[count_ - 2].point : current();
        q.shield_credited = state.compute_waits == 0 && state.shield > 0 && state.shield_phases > 0;
        return q;
    }

private:
    struct Entry { Point point{}; Cost damage{}; };
    std::array<Entry, capacity> poses_{};
    std::size_t count_{};
    std::uint64_t revision_{};
    static Quote invalid(Reason reason) { Quote q; q.reason = reason; return q; }

    Cost prefix_damage(unsigned phases, unsigned forced) const {
        const unsigned held = std::min(phases, forced);
        Cost total = Cost(held) * poses_[count_ - 1].damage;
        phases -= held;
        for (std::size_t i = count_ - 1; i > 0 && phases > 0; --i, --phases)
            total += poses_[i - 1].damage;
        return total;
    }
};
} // namespace mosaic::return_budget
