#pragma once

// Static nominations are conditional. The selected-candidate collector and
// parent actuator must establish current path/occupancy and physical legality.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace mosaic::selected_nav {

enum class Status {
    need_compute, no_static_goal, arrived, candidate, local_candidates_exhausted,
    deferred, unavailable, ready
};

template<class Mask, class Position> struct RequestT {
    const Mask& static_blocked;
    const Mask& goals;
    Position origin;
    int speed{}, epoch{}, actor{};
    std::uint64_t revision{};
    bool complete{}, alive{}, can_move{};
};

// Refusals belong to this actor/origin/epoch, and can survive a fresh observation
// revision. They are exclusion evidence only, never terrain or occupancy facts.
template<class Position> struct FailedEndpointsT {
    std::span<const Position> tiles;
    int epoch{}, actor{};
    Position origin{};
};

struct Progress {
    Status status{Status::deferred};
    bool covered{};
    std::uint16_t attempted_depth{}, next_depth{1};
};

template<class Position> struct CandidateT {
    Status status{Status::deferred};
    int epoch{}, actor{};
    std::uint64_t revision{};
    Position origin{};
    std::optional<Position> destination;
    std::uint16_t origin_potential{0xffff}, destination_potential{0xffff};
    bool strictly_lower_potential{};
    bool conditional_only{true};
};

template<class Position> struct PotentialT {
    std::uint16_t value{};
    int epoch{}, actor{};
    std::uint64_t revision{};
    Position origin{}, point{};
    bool conditional_only{true};
};

// Expected geometry from a dated service binding, with no assertion that the
// actor is alive, movable, at that origin, or otherwise freshly observed.
template<class Mask, class Position> struct HypothesisT {
    const Mask& static_blocked;
    const Mask& goals;
    Position expected_origin;
    int expected_speed{}, epoch{}, actor{};
    std::uint64_t revision{};
    bool geometry_known{};
};

template<class Position> struct HypotheticalCandidateT {
    CandidateT<Position> proposal;
    static constexpr bool requires_fresh_capture = true;
    static constexpr bool actual_status_known = false;
};

struct Limits {
    // Search admission reuses the six saved BigWorld field costs, not a formal
    // bound. Masked merging and the new composition still need native health.
    std::uint64_t retain_fuel{18000}, setup_allowance{7000}, fixed_call_allowance{128};
    std::uint64_t nominate_fuel{21000};
    unsigned max_depth{256}, max_speed{4}, max_candidates{81};
};

// Backend: field/get, mask operations, search, merge, static reachable endpoints,
// resource and epoch guards. No champion, vision, camp or dynamic-world queries.
template<class Backend, class Mask, class Position> class ServiceT {
public:
    Backend& backend() { return backend_; }

    Progress prepare(const RequestT<Mask, Position>& request,
        unsigned admitted_depth, const Limits& limits = {})
    {
        Progress answer;
        if (!request.complete || !backend_.same_epoch(request.epoch)) return answer;
        const auto entry_fuel = backend_.remaining();
        if (entry_fuel < limits.retain_fuel + limits.setup_allowance) return answer;
        if (!request.alive) { answer.status = Status::unavailable; return answer; }
        auto normalized = request.goals;
        normalized &= ~request.static_blocked;
        if (!same_key(request.static_blocked, normalized)) {
            field_.reset(); visited_.reset(); pending_field_.reset(); pending_visited_.reset();
            failed_origin_.reset(); failed_depth_ = 0;
            static_key_ = request.static_blocked;
            goal_key_ = std::move(normalized);
        }
        if (failed_origin_ && *failed_origin_ == request.origin) {
            answer.attempted_depth = failed_depth_;
            answer.next_depth = static_cast<std::uint16_t>(failed_depth_ + 1u);
        }
        const auto finish = [&](Status status, bool covered = false) {
            if (!backend_.same_epoch(request.epoch)
                || backend_.remaining() < limits.retain_fuel) {
                answer.status = Status::deferred; answer.covered = false;
            } else { answer.status = status; answer.covered = covered; }
            return answer;
        };
        // If a completed native search returned late/low on fuel, preserve its
        // input and do this bounded merge only under a new admitted quiet read.
        if (pending_field_) {
            if (backend_.remaining() < limits.retain_fuel + limits.setup_allowance)
                return finish(Status::deferred);
            backend_.merge(*field_, *pending_field_, *pending_visited_);
            *visited_ |= *pending_visited_;
            pending_field_.reset(); pending_visited_.reset();
        }
        if (goal_key_->is_empty()) return finish(Status::no_static_goal);
        if (goal_key_->is_set(request.origin)) return finish(Status::arrived, true);
        if (field_ && visited_ && visited_->is_set(request.origin))
            return finish(Status::ready, true);
        if (!admitted_depth || !limits.max_depth) return finish(Status::need_compute);
        const auto chunks = std::max<std::uint64_t>(1, backend_.chunks(*static_key_));
        const auto overhead = limits.retain_fuel + limits.setup_allowance
            + limits.fixed_call_allowance;
        if (entry_fuel <= overhead) return finish(Status::need_compute);
        const auto affordable = (entry_fuel - overhead) / chunks;
        if (affordable <= 16) return finish(Status::need_compute);
        const auto cap = static_cast<std::uint16_t>(std::min<std::uint64_t>(0xfffe,
            std::min<std::uint64_t>(admitted_depth,
                std::min<std::uint64_t>(limits.max_depth, affordable - 16))));
        // Repeating an already completed failed radius at the same origin adds
        // no input. The parent must admit a larger radius or defer this task.
        if (failed_origin_ && *failed_origin_ == request.origin && cap <= failed_depth_)
            return finish(Status::need_compute);
        typename Backend::Field update;
        Mask newly_visited;
        const auto estimate = chunks * (16 + std::uint64_t(cap))
            + limits.fixed_call_allowance;
        if (!backend_.same_epoch(request.epoch)
            || backend_.remaining() < limits.retain_fuel + estimate)
            return finish(Status::need_compute);
        backend_.search(update, *static_key_, *goal_key_, request.origin, newly_visited, cap);
        const bool found = newly_visited.is_set(request.origin);
        answer.attempted_depth = cap;
        if (!found) {
            failed_origin_ = request.origin; failed_depth_ = cap;
            answer.next_depth = static_cast<std::uint16_t>(cap + 1u);
        }
        // Merge only proved values. Changed search destinations or smaller new
        // searches cannot erase previous visited cells under these exact keys.
        if (!field_) { field_.emplace(std::move(update)); visited_ = std::move(newly_visited); }
        else {
            if (!backend_.same_epoch(request.epoch)
                || backend_.remaining() < limits.retain_fuel + limits.setup_allowance) {
                pending_field_.emplace(std::move(update));
                pending_visited_ = std::move(newly_visited);
                return finish(Status::deferred);
            }
            backend_.merge(*field_, update, newly_visited); *visited_ |= newly_visited;
        }
        if (!found) return finish(Status::need_compute);
        return finish(Status::ready, true);
    }

    CandidateT<Position> nominate(const RequestT<Mask, Position>& request,
        const FailedEndpointsT<Position>& failures, const Limits& limits = {})
    {
        CandidateT<Position> answer;
        answer.epoch = request.epoch; answer.actor = request.actor;
        answer.revision = request.revision; answer.origin = request.origin;
        const auto defer = [&]() {
            answer.status = Status::deferred; answer.destination.reset();
            answer.origin_potential = answer.destination_potential = 0xffff;
            answer.strictly_lower_potential = false;
            return answer;
        };
        const auto finish = [&](Status status) {
            if (!backend_.same_epoch(request.epoch)
                || backend_.remaining() < limits.retain_fuel) return defer();
            answer.status = status;
            if (status != Status::candidate) {
                answer.destination.reset(); answer.strictly_lower_potential = false;
            }
            return answer;
        };
        if (!request.complete || !backend_.same_epoch(request.epoch)
            || backend_.remaining() < limits.nominate_fuel) return answer;
        if (failures.epoch != request.epoch || failures.actor != request.actor
            || failures.origin != request.origin || failures.tiles.size() > limits.max_candidates)
            return answer;
        auto normalized = request.goals;
        normalized &= ~request.static_blocked;
        // This request compares its own goals: a failed early prepare can never
        // silently nominate using an older task's mathematical field.
        if (!same_key(request.static_blocked, normalized)) return finish(Status::need_compute);
        if (!request.alive) return finish(Status::unavailable);
        if (goal_key_->is_empty()) return finish(Status::no_static_goal);
        if (goal_key_->is_set(request.origin)) {
            answer.origin_potential = answer.destination_potential = 0;
            return finish(Status::arrived);
        }
        if (!request.can_move || request.speed <= 0) return finish(Status::unavailable);
        if (unsigned(request.speed) > limits.max_speed) return finish(Status::deferred);
        if (!field_ || !visited_ || !visited_->is_set(request.origin))
            return finish(Status::need_compute);
        answer.origin_potential = field_->get(request.origin);
        auto best = answer.origin_potential;
        const auto endpoints = backend_.reachable(*static_key_, request.origin, request.speed);
        if (endpoints.size() > limits.max_candidates) return defer();
        for (const auto tile : endpoints) {
            if (backend_.remaining() < limits.retain_fuel) return defer();
            if (tile == request.origin || static_key_->is_set(tile) || !visited_->is_set(tile)
                || std::find(failures.tiles.begin(), failures.tiles.end(), tile) != failures.tiles.end())
                continue;
            const auto potential = field_->get(tile);
            if (potential < best) {
                best = potential; answer.destination = tile;
                answer.destination_potential = potential;
            }
        }
        answer.strictly_lower_potential = answer.destination.has_value();
        return finish(answer.destination ? Status::candidate : Status::local_candidates_exhausted);
    }

    // A represented static suffix length at point; origin still identifies the
    // actual fresh actor packet. A hypothetical spawn query never moves that
    // stamp, computes a field, or establishes current physical reachability.
    std::optional<PotentialT<Position>> potential(
        const RequestT<Mask, Position>& request, Position point, const Limits& limits = {}) const
    {
        if (!request.complete || !request.alive || !backend_.same_epoch(request.epoch)
            || backend_.remaining() < limits.nominate_fuel) return std::nullopt;
        auto normalized = request.goals;
        normalized &= ~request.static_blocked;
        if (!same_key(request.static_blocked, normalized)
            || !field_ || !visited_ || !visited_->is_set(point)) return std::nullopt;
        const auto value = field_->get(point);
        if (value == 0xffff || !backend_.same_epoch(request.epoch)
            || backend_.remaining() < limits.retain_fuel) return std::nullopt;
        return PotentialT<Position>{value, request.epoch, request.actor, request.revision,
            request.origin, point, true};
    }

    std::optional<PotentialT<Position>> potential(
        const RequestT<Mask, Position>& request, const Limits& limits = {}) const
    {
        return potential(request, request.origin, limits);
    }

    HypotheticalCandidateT<Position> nominate_hypothesis(
        const HypothesisT<Mask, Position>& hypothesis, const Limits& limits = {})
    {
        // These grants exist only inside the mathematical hypothesis. The
        // distinct output type cannot assert actual body or allowance status.
        const RequestT<Mask, Position> assumed{hypothesis.static_blocked, hypothesis.goals,
            hypothesis.expected_origin, hypothesis.expected_speed, hypothesis.epoch,
            hypothesis.actor, hypothesis.revision, hypothesis.geometry_known, true, true};
        // Dated refusals/occupancy cannot enter this API. Real within-observation
        // exclusions belong exclusively to the fresh nominate() entry point.
        const FailedEndpointsT<Position> empty{{}, hypothesis.epoch, hypothesis.actor,
            hypothesis.expected_origin};
        return {nominate(assumed, empty, limits)};
    }

private:
    bool same_key(const Mask& walls, const Mask& goals) const {
        return static_key_ && goal_key_ && *static_key_ == walls && *goal_key_ == goals;
    }
    Backend backend_;
    std::optional<Mask> static_key_, goal_key_, visited_;
    std::optional<typename Backend::Field> field_;
    std::optional<typename Backend::Field> pending_field_;
    std::optional<Mask> pending_visited_;
    std::optional<Position> failed_origin_;
    std::uint16_t failed_depth_{};
};
} // namespace mosaic::selected_nav

#ifndef MOSAIC_SELECTED_NAV_CORE_ONLY
#include "navigation.h"
namespace mosaic::selected_nav {
struct NativeBackend : nav::NativeBackend {
    void merge(Field& target, const Field& source, const cambc::bitboard& visited) const {
        target.copy_from(source, visited);
    }
};
using Request = RequestT<cambc::bitboard, cambc::vec2>;
using FailedEndpoints = FailedEndpointsT<cambc::vec2>;
using Candidate = CandidateT<cambc::vec2>;
using Potential = PotentialT<cambc::vec2>;
using Hypothesis = HypothesisT<cambc::bitboard, cambc::vec2>;
using HypotheticalCandidate = HypotheticalCandidateT<cambc::vec2>;
using Service = ServiceT<NativeBackend, cambc::bitboard, cambc::vec2>;
} // namespace mosaic::selected_nav
#endif
