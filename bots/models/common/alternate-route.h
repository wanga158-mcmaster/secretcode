#pragma once

// SDK-free, one-observation detours. Topology and personal occupancy are
// supplied separately; a complete route is only a proposal for future turns.
#include "pure.h"
#include <array>
#include <cstdint>
#include <span>

namespace plan4::detour {
inline constexpr unsigned max_nodes = 192;
inline constexpr int max_steps = 64;
inline constexpr int max_tiles = 16384;
inline constexpr unsigned max_goals = 256;

enum class Occupancy { unknown, free, occupied };
struct Frame {
    int actor = -1, round = -1, width = 0, height = 0;
    Point origin{}, obstruction{};
    std::span<const Point> goals{};
    bool (*walkable)(Point, const void*) = nullptr;
    const void* topology_context = nullptr;
    Occupancy (*occupancy)(Point, const void*) = nullptr;
    const void* occupancy_context = nullptr;
    WorkBudget work{};
};
struct Route {
    int actor = -1, round = -1;
    Point origin{}, obstruction{}, goal{};
    std::array<Point, max_steps> steps{};
    int count = 0;
    unsigned visits = 0;
    bool proven = false;
};

namespace detail {
inline bool coordinate(Point p) {
    return p.x >= 0 && p.y >= 0 && p.x < max_tiles && p.y < max_tiles;
}
inline bool adjacent(Point a, Point b) {
    // Callers establish the small nonnegative coordinate bounds first.
    const int dx = a.x - b.x, dy = a.y - b.y;
    return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1 && a != b;
}
inline bool allowed(WorkBudget work) {
    return !work.can_refine || work.can_refine(work.context);
}
inline bool bit(const std::array<unsigned char, max_tiles / 8>& bits, int index) {
    return (bits[unsigned(index) / 8] & (1u << (unsigned(index) % 8))) != 0;
}
inline void mark(std::array<unsigned char, max_tiles / 8>& bits, int index) {
    bits[unsigned(index) / 8] |= static_cast<unsigned char>(1u << (unsigned(index) % 8));
}
} // namespace detail

// Structural validation cannot re-establish current occupancy or movement
// readiness. The executor must freshly check both before every actual action.
inline bool valid(const Route& route) {
    if (!route.proven || route.actor < 0 || route.round < 0 || route.count < 1 ||
        route.count > max_steps || route.visits < unsigned(route.count) ||
        route.visits > max_nodes || !detail::coordinate(route.origin) ||
        !detail::coordinate(route.obstruction) || !detail::coordinate(route.goal) ||
        !detail::adjacent(route.origin, route.obstruction) ||
        route.steps[route.count - 1] != route.goal) return false;
    Point previous = route.origin;
    std::array<std::uint32_t, max_steps> keys;
    for (int i = 0; i < route.count; ++i) {
        const Point step = route.steps[i];
        if (!detail::coordinate(step) || !detail::adjacent(previous, step) ||
            step == route.origin || step == route.obstruction) return false;
        keys[i] = std::uint32_t(step.y) * max_tiles + std::uint32_t(step.x);
        previous = step;
    }
    const auto end = keys.begin() + route.count;
    std::sort(keys.begin(), end);
    return std::adjacent_find(keys.begin(), end) == end;
}

inline Route plan(const Frame& frame, unsigned max_visits) {
    Route result;
    result.actor = frame.actor;
    result.round = frame.round;
    result.origin = frame.origin;
    result.obstruction = frame.obstruction;
    if (frame.actor < 0 || frame.round < 0 || frame.width < 1 || frame.height < 1 ||
        frame.width > max_tiles || frame.height > max_tiles / frame.width ||
        frame.goals.empty() || frame.goals.size() > max_goals || !frame.walkable ||
        !frame.occupancy || !max_visits || !detail::allowed(frame.work)) return result;
    const auto inside = [&frame](Point p) {
        return p.x >= 0 && p.y >= 0 && p.x < frame.width && p.y < frame.height;
    };
    const auto key = [&frame](Point p) { return p.y * frame.width + p.x; };
    const auto point = [&frame](int index) { return Point{index % frame.width, index / frame.width}; };
    if (!inside(frame.origin) || !inside(frame.obstruction) ||
        !detail::adjacent(frame.origin, frame.obstruction) ||
        !frame.walkable(frame.origin, frame.topology_context) ||
        !frame.walkable(frame.obstruction, frame.topology_context) ||
        frame.occupancy(frame.obstruction, frame.occupancy_context) != Occupancy::occupied) return result;

    // Clear in bounded chunks rather than performing an unchecked map-sized
    // initialization. Only the bitmap bytes used by this map are ever read.
    std::array<unsigned char, max_tiles / 8> seen, targets;
    const int bytes = (frame.width * frame.height + 7) / 8;
    for (int start = 0; start < bytes; start += 32) {
        if (!detail::allowed(frame.work)) return result;
        for (int i = start; i < std::min(bytes, start + 32); ++i) seen[i] = targets[i] = 0;
    }
    for (std::size_t i = 0; i < frame.goals.size(); ++i) {
        if (i % 8 == 0 && !detail::allowed(frame.work)) return result;
        const Point goal = frame.goals[i];
        if (!inside(goal) || goal == frame.origin) return result;
        detail::mark(targets, key(goal));
    }

    struct Node { std::uint16_t tile; unsigned char parent, depth; };
    std::array<Node, max_nodes> queue;
    unsigned head = 0, tail = 1;
    queue[0] = {static_cast<std::uint16_t>(key(frame.origin)), 0, 0};
    detail::mark(seen, key(frame.origin));
    const unsigned quota = std::min(max_visits, max_nodes);
    while (head < tail && result.visits < quota) {
        if (!detail::allowed(frame.work)) return result;
        const unsigned parent = head++;
        const Node current = queue[parent];
        ++result.visits; // Includes work in searches which fail or exhaust their quota.
        if (current.depth >= max_steps) continue;
        const Point from = point(current.tile);
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const Point candidate{from.x + dx, from.y + dy};
            if (!inside(candidate) || candidate == frame.obstruction) continue;
            const int index = key(candidate);
            if (detail::bit(seen, index)) continue;
            detail::mark(seen, index);
            if (!frame.walkable(candidate, frame.topology_context) ||
                frame.occupancy(candidate, frame.occupancy_context) != Occupancy::free) continue;
            if (tail == max_nodes) return result;
            const unsigned found = tail++;
            queue[found] = {static_cast<std::uint16_t>(index), static_cast<unsigned char>(parent),
                            static_cast<unsigned char>(current.depth + 1)};
            if (!detail::bit(targets, index)) continue;
            const int count = queue[found].depth;
            unsigned cursor = found;
            for (int i = count - 1; i >= 0; --i) {
                if (i % 8 == 0 && !detail::allowed(frame.work)) return result;
                result.steps[i] = point(queue[cursor].tile);
                cursor = queue[cursor].parent;
            }
            result.goal = candidate;
            result.count = count;
            result.proven = true;
            return result;
        }
    }
    return result;
}

class Cursor {
    Route route_{};
    std::uint64_t epoch_ = 0;
    int index_ = 0, last_round_ = -1;
    bool active_ = false, pending_ = false;
public:
    void reset() {
        active_ = pending_ = false;
        index_ = 0;
        last_round_ = -1;
        epoch_ = 0;
    }
    bool begin(std::uint64_t epoch, const Route& route) {
        reset();
        if (!valid(route)) return false;
        route_ = route;
        epoch_ = epoch;
        last_round_ = route.round;
        active_ = true;
        return true;
    }
    bool next(std::uint64_t epoch, int actor, int round, Point origin,
              std::span<const Point> goals, Point& next, int& remaining) {
        remaining = 0;
        if (!active_ || epoch != epoch_ || actor != route_.actor || round < last_round_ ||
            goals.empty() || goals.size() > max_goals ||
            origin != (index_ ? route_.steps[index_ - 1] : route_.origin)) {
            reset();
            return false;
        }
        bool goal_present = false;
        for (Point goal : goals) if (goal == route_.goal) { goal_present = true; break; }
        if (!goal_present) { reset(); return false; }
        last_round_ = round;
        next = route_.steps[index_];
        remaining = route_.count - index_;
        pending_ = true;
        return true;
    }
    void commit(bool success, Point actual) {
        if (!active_ || !pending_ || !success || actual != route_.steps[index_]) { reset(); return; }
        pending_ = false;
        if (++index_ == route_.count) reset();
    }
    bool active() const { return active_; }
};
} // namespace plan4::detour
