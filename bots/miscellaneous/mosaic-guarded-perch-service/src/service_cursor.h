#pragma once

// Semantic nomination progress only. A Selection is a raw hypothesis, never an
// observation, occupancy claim, legal action or reusable action certificate.
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace mosaic::service_cursor {

struct Point { int x{}, y{}; bool operator==(const Point&) const = default; };
struct Geometry {
    int type{}, move_speed{}, basic_range{}, splash_radius{};
    bool operator==(const Geometry&) const = default;
};
struct Key {
    int actor{-1};
    Point origin;
    Geometry geometry;
    int mode{}, group_id{-1};
    std::uint64_t group_cycle{}, goal_identity{}, body_generation{}, stat_generation{};
    // Generations identify actual semantic changes, never an observation nonce,
    // epoch, HP, current readiness or a refreshed occupancy snapshot.
    bool operator==(const Key&) const = default;
};

enum class Kind { move, basic_camp };
struct Raw {
    Kind kind{Kind::move};
    Point destination;
    int camp_index{-1};
    static Raw move(Point p) { return {Kind::move, p, -1}; }
    static Raw basic(int index) { return {Kind::basic_camp, {}, index}; }
    bool operator==(const Raw& other) const {
        return kind == other.kind && (kind == Kind::move
            ? destination == other.destination : camp_index == other.camp_index);
    }
    bool operator<(const Raw& other) const {
        if (kind != other.kind) return kind < other.kind;
        if (kind == Kind::basic_camp) return camp_index < other.camp_index;
        if (destination.x != other.destination.x) return destination.x < other.destination.x;
        return destination.y < other.destination.y;
    }
};
struct Catalogue { std::span<const Raw> order; bool complete{}; };
struct Selection {
    std::uint64_t generation{};
    unsigned index{};
    Raw raw;
};
// Exhausted refers only to this finite semantic catalogue, never reachability.
enum class Status { unknown, ready, deferred, exhausted, closed };
enum class Problem { none, unbound, incomplete_catalogue, catalogue_mismatch,
    capacity, duplicate, invalid_raw, generation_overflow, stale_selection,
    key_mismatch, packet_required };
enum class DeferredBy { none, partial_capture, admission, next_packet, fresh_reobservation,
    temporal_state };
enum class ClosedBy { none, applied, uncertain_effect, error };
enum class Outcome {
    // Complete classified no-certificate outcomes consume this nominee. A
    // partial capture is NOT the assessed_incomplete case.
    complete_rejected, complete_unsupported, assessed_incomplete,
    partial_capture, admission_deferred, applied, sdk_refused, uncertain_effect, error,
    // Complete current body permission/stun can change without geometric or
    // family change. It retains this nominee until another fresh packet.
    temporal_deferred
};
enum class RebaseReason { observed_release, physical_phase_committed,
    explicit_family_rebase, family_exhaustion };
struct View {
    Status status{Status::unknown};
    std::optional<Selection> selection;
    Problem problem{Problem::unbound};
    DeferredBy deferred_by{DeferredBy::none};
    ClosedBy closed_by{ClosedBy::none};
};
struct Update { bool accepted{}, advanced{}; View view; };

class Cursor {
public:
    static constexpr unsigned capacity = 81;

    View bind(const Key& key, Catalogue catalogue) {
        if (key_ && *key_ == key) {
            // A refreshed catalogue may confirm the same order, never replace
            // it using new occupancy/readiness or silently restart this phase.
            if (!same_catalogue(catalogue)) {
                issue_ = Problem::catalogue_mismatch; return peek();
            }
            issue_ = Problem::none;
            return peek();
        }
        return start(key, catalogue);
    }

    View acknowledge_fresh_packet(const Key& actual_key) {
        // The parent has captured and checked actual semantic identity. This
        // acknowledgement resumes raw service progress, never action authority.
        if (!key_ || !(*key_ == actual_key)) {
            auto view = peek(); view.status = Status::unknown;
            view.problem = Problem::key_mismatch; view.selection.reset(); return view;
        }
        if (issue_ == Problem::none && status_ == Status::deferred) settle();
        return peek();
    }

    View rebase(const Key& key, Catalogue catalogue, RebaseReason) {
        // Calling this is an explicit parent phase event, including observed
        // release. Exhaustion itself never calls this or resets the cursor.
        return start(key, catalogue);
    }

    View peek() const {
        View view;
        view.status = issue_ == Problem::none ? status_ : Status::unknown;
        view.problem = issue_;
        view.deferred_by = deferred_by_; view.closed_by = closed_by_;
        if (issue_ == Problem::none && (status_ == Status::ready || status_ == Status::deferred)
            && key_ && index_ < size_) view.selection = Selection{generation_, index_, order_[index_]};
        if (issue_ == Problem::none && status_ == Status::unknown)
            view.problem = Problem::incomplete_catalogue;
        return view;
    }

    Update report(const Selection& selection, Outcome outcome) {
        const auto current = peek();
        if (!current.selection || selection.generation != generation_
            || selection.index != index_ || !(selection.raw == order_[index_])) {
            auto rejected = current; rejected.problem = Problem::stale_selection;
            return {false, false, rejected};
        }
        if (outcome == Outcome::partial_capture || outcome == Outcome::admission_deferred
            || outcome == Outcome::temporal_deferred) {
            status_ = Status::deferred;
            deferred_by_ = outcome == Outcome::temporal_deferred ? DeferredBy::temporal_state
                : (outcome == Outcome::partial_capture ? DeferredBy::partial_capture : DeferredBy::admission);
            return {true, false, peek()};
        }
        if (status_ == Status::deferred && (outcome == Outcome::complete_rejected
            || outcome == Outcome::complete_unsupported || outcome == Outcome::assessed_incomplete)) {
            auto blocked = current; blocked.problem = Problem::packet_required;
            return {false, false, blocked};
        }
        ++index_; // The current semantic nominee is consumed exactly once.
        if (outcome == Outcome::applied || outcome == Outcome::uncertain_effect || outcome == Outcome::error) {
            status_ = Status::closed; deferred_by_ = DeferredBy::none;
            closed_by_ = outcome == Outcome::applied ? ClosedBy::applied
                : (outcome == Outcome::uncertain_effect ? ClosedBy::uncertain_effect : ClosedBy::error);
        } else {
            settle();
            if (status_ == Status::ready) {
                status_ = Status::deferred;
                deferred_by_ = outcome == Outcome::sdk_refused
                    ? DeferredBy::fresh_reobservation : DeferredBy::next_packet;
            }
        }
        return {true, true, peek()};
    }

private:
    bool same_catalogue(Catalogue catalogue) const {
        return catalogue.order.size() == size_ && catalogue.complete == complete_
            && std::equal(catalogue.order.begin(), catalogue.order.end(), order_.begin());
    }
    static Problem validate(Catalogue catalogue) {
        if (catalogue.order.size() > capacity) return Problem::capacity;
        std::array<Raw, capacity> sorted;
        unsigned size = 0;
        for (const auto raw : catalogue.order) {
            if ((raw.kind != Kind::move && raw.kind != Kind::basic_camp)
                || (raw.kind == Kind::basic_camp && raw.camp_index < 0)) return Problem::invalid_raw;
            sorted[size++] = raw;
        }
        std::sort(sorted.begin(), sorted.begin()+size);
        return std::adjacent_find(sorted.begin(), sorted.begin()+size) == sorted.begin()+size
            ? Problem::none : Problem::duplicate;
    }
    View start(const Key& key, Catalogue catalogue) {
        const auto problem = validate(catalogue);
        if (problem != Problem::none) { issue_ = problem; return peek(); }
        if (generation_ == std::numeric_limits<std::uint64_t>::max()) {
            issue_ = Problem::generation_overflow; return peek();
        }
        ++generation_; key_ = key; size_ = unsigned(catalogue.order.size()); index_ = 0;
        complete_ = catalogue.complete;
        std::copy(catalogue.order.begin(), catalogue.order.end(), order_.begin());
        issue_ = Problem::none; deferred_by_ = DeferredBy::none; closed_by_ = ClosedBy::none;
        settle(); return peek();
    }
    void settle() {
        deferred_by_ = DeferredBy::none;
        status_ = index_ < size_ ? Status::ready : (complete_ ? Status::exhausted : Status::unknown);
    }
    std::optional<Key> key_;
    std::array<Raw, capacity> order_;
    std::uint64_t generation_{};
    unsigned size_{}, index_{};
    bool complete_{};
    Status status_{Status::unknown};
    Problem issue_{Problem::unbound};
    DeferredBy deferred_by_{DeferredBy::none};
    ClosedBy closed_by_{ClosedBy::none};
};
} // namespace mosaic::service_cursor
