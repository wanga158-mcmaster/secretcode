#pragma once

// A route proposal consumes one immutable observation revision. Planning never
// refreshes world observations; a successful proposal is not an applied move.
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace mosaic::nav {

enum class Result {
    step_ready,
    arrived,
    moved,
    blocked,
    incomplete,
    revealed_blocker,
    refused = revealed_blocker,
    unavailable, // Dead or no ordinary movement allowance; not a physical obstruction.
    local_blocked // No fresh advancing landing; requests refinement, not infeasibility.
};

enum class DistanceKind { none, observed_graph, static_potential };

template<class Mask, class Position> struct InputT {
    const Mask& static_blocked;
    const Mask& dynamic_blocked;
    Position origin;
    int move_speed{}, epoch{}, actor{};
    std::uint64_t revision{};
    bool complete{}, alive{}, can_move{};
};

template<class Position> struct MovePlanT {
    Result result{Result::incomplete};
    std::optional<Position> destination;
    int expected_epoch{}, actor{};
    std::uint64_t revision{};
    Position expected_origin{};
    std::uint16_t route_distance{0xffff};
    // The origin's frozen-graph answer is complete, not the entire map search.
    bool search_complete{};
    DistanceKind distance_kind{DistanceKind::none};
};

// This packet must come from the same observation as InputT. In particular, its
// tiles already include the engine's full movement/path legality for this speed.
template<class Position> struct DestinationsT {
    std::span<const Position> tiles;
    int epoch{}, actor{};
    std::uint64_t revision{};
    Position origin{};
};

struct Limits {
    std::uint64_t entry_fuel{40000}, tail_fuel{22000}, action_fuel{15000};
    unsigned layer_cost_factor{8}, max_layers{64}, max_move_speed{4};
};

// Backend supplies only immutable mask/field operations and resource/epoch guards.
// The test backend exercises this same branch code against exhaustive small graphs.
template<class Backend, class Mask, class Position>
MovePlanT<Position> plan_core(Backend& backend, const InputT<Mask, Position>& input,
    const Mask& supplied_goals, const Mask* static_forbidden = nullptr,
    const Mask* dynamic_forbidden = nullptr, const Limits& limits = {})
{
    MovePlanT<Position> answer;
    answer.expected_epoch = input.epoch;
    answer.actor = input.actor;
    answer.revision = input.revision;
    answer.expected_origin = input.origin;
    const auto defer = [&]() {
        answer.result = Result::incomplete;
        answer.destination.reset();
        answer.route_distance = 0xffff;
        answer.search_complete = false;
        answer.distance_kind = DistanceKind::none;
        return answer;
    };
    const auto finish = [&](Result result, bool complete = false) {
        if (!backend.same_epoch(input.epoch)) return defer();
        answer.result = result;
        answer.search_complete = complete;
        if (answer.result != Result::step_ready) answer.destination.reset();
        return answer;
    };
    if (!input.complete || !backend.same_epoch(input.epoch)) return answer;
    if (backend.remaining() < limits.entry_fuel) return answer;
    if (!input.alive) return finish(Result::unavailable);
    auto goals = supplied_goals;
    if (static_forbidden) goals &= ~*static_forbidden;
    if (dynamic_forbidden) goals &= ~*dynamic_forbidden;
    if (goals.is_empty()) return finish(Result::blocked, true);
    if (goals.is_set(input.origin)) {
        answer.route_distance = 0;
        answer.distance_kind = DistanceKind::observed_graph;
        return finish(Result::arrived, true);
    }
    if (!input.can_move || input.move_speed <= 0) return finish(Result::unavailable);
    if (unsigned(input.move_speed) > limits.max_move_speed
        || backend.remaining() < limits.entry_fuel) return answer;

    auto blocked = input.static_blocked;
    blocked |= input.dynamic_blocked;
    if (static_forbidden) blocked |= *static_forbidden;
    if (dynamic_forbidden) blocked |= *dynamic_forbidden;
    // The current tile is a valid exit source even when newly forbidden. It was
    // removed from destinations above and is never made an artificial arrival.
    blocked.set(input.origin, false);
    goals &= ~blocked;
    if (goals.is_empty()) return finish(Result::blocked, true);
    const auto fuel = backend.remaining();
    if (fuel <= limits.tail_fuel || limits.layer_cost_factor == 0) return answer;
    const auto chunks = std::max<std::uint64_t>(1, backend.chunks(blocked));
    const auto layers = (fuel - limits.tail_fuel) / (limits.layer_cost_factor * chunks);
    if (layers <= 16 || limits.max_layers == 0) return answer;
    const auto max_distance = static_cast<std::uint16_t>(
        std::min<std::uint64_t>(limits.max_layers, layers - 16));

    typename Backend::Field field;
    Mask visited;
    backend.search(field, blocked, goals, input.origin, visited, max_distance);
    // A bounded miss never establishes that this task, group or route is impossible.
    if (!backend.same_epoch(input.epoch) || !visited.is_set(input.origin)
        || backend.remaining() < limits.action_fuel) return defer();
    answer.route_distance = field.get(input.origin);
    answer.distance_kind = DistanceKind::observed_graph;
    auto best = input.origin;
    auto best_distance = answer.route_distance;
    for (const auto tile : backend.reachable(blocked, input.origin, input.move_speed)) {
        if (backend.remaining() < limits.action_fuel) return defer();
        const auto candidate_distance = field.get(tile);
        if (candidate_distance < best_distance) {
            best = tile;
            best_distance = candidate_distance;
        }
    }
    if (!backend.same_epoch(input.epoch) || backend.remaining() < limits.action_fuel)
        return defer();
    if (best == input.origin) return finish(Result::blocked, true);
    answer.destination = best;
    return finish(Result::step_ready, true);
}

enum class PrepareResult { ready, reused, incomplete, no_static_goal };

struct CacheLimits {
    // Empirical admission from navigation-cost (BigWorld, SDK 3.0.5): six field
    // calls cost chunks*(16+layers)+53. These allowances are not a fuel proof.
    std::uint64_t retain_fuel{18000}, setup_allowance{7000}, fixed_call_allowance{128};
    std::uint64_t proposal_fuel{21000};
    unsigned max_layers{256}, max_destinations{81};
};

// A potential is a shortest distance in the exact static mask, never a current
// occupancy or path-safety fact. Epoch, actor, origin and dynamic masks are not
// cache keys; every proposal separately validates its fresh landing packet.
template<class Backend, class Mask, class Position> class PotentialCache {
public:
    Backend& backend() { return backend_; }

    PrepareResult prepare(const InputT<Mask, Position>& input,
        const Mask& supplied_goals, const CacheLimits& limits = {})
    {
        if (!input.complete || !backend_.same_epoch(input.epoch))
            return PrepareResult::incomplete;
        const auto entry_fuel = backend_.remaining();
        if (entry_fuel < limits.retain_fuel + limits.setup_allowance)
            return PrepareResult::incomplete;
        auto goals = supplied_goals;
        goals &= ~input.static_blocked;
        const bool same_key = static_key_ && *static_key_ == input.static_blocked
            && goal_key_ && *goal_key_ == goals;
        if (!same_key) {
            field_.reset();
            visited_.reset();
            static_key_ = input.static_blocked;
            goal_key_ = std::move(goals);
        }
        const auto finish = [&](PrepareResult result) {
            return backend_.same_epoch(input.epoch)
                && backend_.remaining() >= limits.retain_fuel
                ? result : PrepareResult::incomplete;
        };
        if (goal_key_->is_empty()) return finish(PrepareResult::no_static_goal);
        if (goal_key_->is_set(input.origin)
            || (field_ && visited_ && visited_->is_set(input.origin)))
            return finish(same_key ? PrepareResult::reused : PrepareResult::ready);

        const auto chunks = std::max<std::uint64_t>(1, backend_.chunks(*static_key_));
        const auto overhead = limits.retain_fuel + limits.setup_allowance
            + limits.fixed_call_allowance;
        if (entry_fuel <= overhead || limits.max_layers == 0)
            return PrepareResult::incomplete;
        const auto affordable = (entry_fuel - overhead) / chunks;
        if (affordable <= 16) return PrepareResult::incomplete;
        const auto cap = static_cast<std::uint16_t>(std::min<std::uint64_t>(
            0xfffe, std::min<std::uint64_t>(limits.max_layers, affordable - 16)));
        // Rebuild a bounded static field for a previously uncovered origin. A
        // partial field is retained, but an unvisited value is never a distance.
        field_.emplace();
        visited_.emplace();
        const auto field_estimate = chunks * (16 + std::uint64_t(cap))
            + limits.fixed_call_allowance;
        if (!backend_.same_epoch(input.epoch)
            || backend_.remaining() < limits.retain_fuel + field_estimate)
            return PrepareResult::incomplete;
        backend_.search(*field_, *static_key_, *goal_key_, input.origin, *visited_, cap);
        if (!visited_->is_set(input.origin)) return PrepareResult::incomplete;
        return finish(PrepareResult::ready);
    }

    MovePlanT<Position> propose(const InputT<Mask, Position>& input,
        const DestinationsT<Position>& landings, const CacheLimits& limits = {})
    {
        MovePlanT<Position> answer;
        answer.expected_epoch = input.epoch;
        answer.actor = input.actor;
        answer.revision = input.revision;
        answer.expected_origin = input.origin;
        const auto defer = [&]() {
            answer.result = Result::incomplete;
            answer.destination.reset();
            answer.route_distance = 0xffff;
            answer.search_complete = false;
            answer.distance_kind = DistanceKind::none;
            return answer;
        };
        const auto finish = [&](Result result) {
            if (!backend_.same_epoch(input.epoch)
                || backend_.remaining() < limits.retain_fuel) return defer();
            answer.result = result;
            if (result != Result::step_ready) answer.destination.reset();
            return answer;
        };
        if (!input.complete || !backend_.same_epoch(input.epoch)
            || backend_.remaining() < limits.proposal_fuel) return answer;
        if (landings.epoch != input.epoch || landings.actor != input.actor
            || landings.revision != input.revision || landings.origin != input.origin)
            return answer;
        if (!static_key_ || !goal_key_ || *static_key_ != input.static_blocked)
            return answer;
        if (!input.alive) return finish(Result::unavailable);
        if (goal_key_->is_empty()) return finish(Result::local_blocked);
        if (goal_key_->is_set(input.origin)) {
            answer.route_distance = 0;
            answer.search_complete = true;
            answer.distance_kind = DistanceKind::static_potential;
            return finish(Result::arrived);
        }
        if (!input.can_move || input.move_speed <= 0) return finish(Result::unavailable);
        if (!field_ || !visited_ || !visited_->is_set(input.origin)
            || landings.tiles.size() > limits.max_destinations) return answer;
        answer.route_distance = field_->get(input.origin);
        answer.search_complete = true;
        answer.distance_kind = DistanceKind::static_potential;
        auto best_distance = answer.route_distance;
        for (const auto tile : landings.tiles) {
            if (backend_.remaining() < limits.retain_fuel) return defer();
            if (tile == input.origin || static_key_->is_set(tile)
                || input.dynamic_blocked.is_set(tile) || !visited_->is_set(tile)) continue;
            const auto candidate_distance = field_->get(tile);
            if (candidate_distance < best_distance) {
                answer.destination = tile;
                best_distance = candidate_distance;
            }
        }
        return finish(answer.destination ? Result::step_ready : Result::local_blocked);
    }

private:
    Backend backend_;
    std::optional<Mask> static_key_, goal_key_, visited_;
    std::optional<typename Backend::Field> field_;
};

} // namespace mosaic::nav

#ifndef MOSAIC_NAV_CORE_ONLY
#include <cambc.h>

namespace mosaic::nav {
using Input = InputT<cambc::bitboard, cambc::vec2>;
using MovePlan = MovePlanT<cambc::vec2>;

struct NativeBackend {
    using Field = cambc::distance_field;
    bool same_epoch(int epoch) const { return int(cambc::round_number()) == epoch; }
    std::uint64_t remaining() const { return cambc::fuel_remaining(); }
    std::uint64_t chunks(const cambc::bitboard& mask) const {
        return std::uint64_t((mask.width() + 7) / 8) * ((mask.height() + 7) / 8);
    }
    void search(Field& field, const cambc::bitboard& blocked,
        const cambc::bitboard& goals, cambc::vec2 origin, cambc::bitboard& visited,
        std::uint16_t max_distance) const {
        cambc::bitboard destination;
        destination.set(origin, true);
        field.compute_to(blocked, goals, destination, visited, max_distance);
    }
    auto reachable(const cambc::bitboard& blocked, cambc::vec2 origin, int speed) const {
        return blocked.reachable_tiles(origin, static_cast<std::uint16_t>(speed)).positions();
    }
};

using Destinations = DestinationsT<cambc::vec2>;
using RouteCache = PotentialCache<NativeBackend, cambc::bitboard, cambc::vec2>;

inline MovePlan plan(const Input& input, const cambc::bitboard& goals,
    const cambc::bitboard* static_forbidden = nullptr,
    const cambc::bitboard* dynamic_forbidden = nullptr, const Limits& limits = {})
{
    NativeBackend backend;
    return plan_core(backend, input, goals, static_forbidden, dynamic_forbidden, limits);
}

inline int distance(cambc::vec2 a, cambc::vec2 b)
{
    return static_cast<int>(a.chebyshev_distance(b));
}

// Legacy probe convenience only. The ordinary actor kernel consumes plan() and
// owns the one fresh action dispatch plus revision invalidation itself.
inline Result navigate(cambc::bitboard goals, const cambc::bitboard* forbidden = nullptr)
{
    const int epoch = int(cambc::round_number());
    if (cambc::fuel_remaining() < 40000) return Result::incomplete;
    auto& me = cambc::self();
    const bool alive = me.is_alive();
    if (!alive) return int(cambc::round_number()) == epoch
        ? Result::unavailable : Result::incomplete;
    const auto origin = me.position();
    const int actor = int(static_cast<std::uint32_t>(static_cast<cambc::handle_champion_t>(me)));
    const int speed = me.base_stats().base_move_speed;
    const bool can_move = me.can_move();
    auto terrain = cambc::map::walls();
    terrain |= cambc::map::holes();
    terrain |= cambc::map::altars();
    terrain |= cambc::map::spawn_region(cambc::team::enemy);
    auto dynamic = cambc::vision::champion_tiles();
    dynamic |= cambc::vision::camp_tiles();
    dynamic |= cambc::vision::teleport_tiles();
    const Input input{terrain, dynamic, origin, speed, epoch, actor, 0,
        int(cambc::round_number()) == epoch, alive, can_move};
    const auto proposal = plan(input, std::move(goals), nullptr, forbidden);
    if (proposal.result != Result::step_ready) return proposal.result;
    if (int(cambc::round_number()) != epoch || cambc::fuel_remaining() < 15000
        || !me.is_alive() || !me.can_move() || me.position() != origin
        || int(cambc::round_number()) != epoch) return Result::incomplete;
    return me.move_absolute(*proposal.destination) ? Result::moved : Result::revealed_blocker;
}

inline bool travel(cambc::bitboard goals, const cambc::bitboard* forbidden = nullptr)
{
    return navigate(std::move(goals), forbidden) == Result::moved;
}

inline bool travel_to(cambc::vec2 goal, int radius = 0,
    const cambc::bitboard* forbidden = nullptr)
{
    if (!cambc::map::is_in_bounds(goal)) return false;
    cambc::bitboard goals;
    goals.draw_chebyshev(goal, static_cast<float>(std::max(0, radius)));
    return travel(std::move(goals), forbidden);
}
} // namespace mosaic::nav
#endif
